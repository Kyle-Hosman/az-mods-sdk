# SPDX-License-Identifier: GPL-3.0-only
"""The diagnostic must use the actual public SDK entry and receipt validity."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class PublicStateProbe(unittest.TestCase):
    def run_fixture(self, flags=(), compiler=None, source=None, failure=False):
        compiler = compiler or shutil.which("cc")
        if not compiler:
            self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            binary = pathlib.Path(temporary) / "probe"
            extra = []
            if source is not None:
                implementation = pathlib.Path(temporary) / "probe.c"
                implementation.write_text(source)
                extra = ['-DPUBLIC_PROBE_IMPL="' + str(implementation) + '"']
            subprocess.run([compiler, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
                            *flags, *extra, "-I" + str(ROOT / "include"),
                            str(ROOT / "tests/fixtures/public-state-probe.c"), "-o", str(binary)],
                           check=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
            result = subprocess.run([str(binary)], stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, timeout=5)
            if failure:
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("assert", result.stderr.lower())
            else:
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_public_entry_and_bounded_worker(self):
        self.run_fixture()

    def test_native_public_rows(self):
        self.run_fixture(["-DPROBE_NATIVE_TEST"])

    def test_loaded_collection_window(self):
        self.run_fixture(["-DPROBE_NATIVE_DELAY_TEST","-DPROBE_NATIVE_ONLY=1","-DPROBE_MAX_RECORDS=512u","-DPROBE_MAX_TICKS=750u","-DPROBE_CAPTURE_LINES=600u"])

    def test_native_asan_ubsan(self):
        compiler=shutil.which("clang")
        if not compiler:self.skipTest("Clang unavailable")
        self.run_fixture(["-DPROBE_NATIVE_TEST","-fsanitize=address,undefined","-fno-sanitize-recover=all"],compiler)

    def test_native_public_guards(self):
        original=(ROOT/"examples/public-state-probe/probe.c").read_text()
        variants=[("master-bit","(s->valid & AZ_LIVE_MASTER) &&","1 &&"),
                  ("native-load-bit","(s->valid & AZ_LIVE_NATIVE_LOAD) &&","1 &&"),
                  ("master-clock","compare_timestamps(v.master_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL","1"),
                  ("native-load-clock","compare_timestamps(v.load_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL","1"),
                  ("native-generation","s->native_load_generation && v.load_unit","1 && v.load_unit"),
                  ("version","s->version != AZ_DATA_VERSION) return v;","0) return v;"),
                  ("source-separation","v.native_load = s->native_load_generation;","v.native_load = s->load_generation;")]
        for name,old,new in variants:
            with self.subTest(guard=name):
                self.assertEqual(original.count(old),1)
                self.run_fixture(["-DPROBE_NATIVE_TEST"],source=original.replace(old,new),failure=True)

    def test_tempo_receipt_cadence(self):
        self.run_fixture(["-DPROBE_CADENCE_TEST", "-DPROBE_MAX_RECORDS=32u", "-DPROBE_MAX_TICKS=12u"])
        original = (ROOT / "examples/public-state-probe/probe.c").read_text()
        old = "a->tempo_valid != b->tempo_valid || a->tempo != b->tempo ||"
        self.assertEqual(original.count(old), 1)
        self.run_fixture(["-DPROBE_CADENCE_TEST", "-DPROBE_MAX_RECORDS=32u", "-DPROBE_MAX_TICKS=12u"],
                         source=original.replace(old, "a->tempo_rev != b->tempo_rev || a->tempo_ns != b->tempo_ns || " + old), failure=True)

    def test_tempo_format_limit(self):
        self.run_fixture(["-DPROBE_TEMPO_LINE_BYTES=32u", "-DPROBE_EXPECT_TEMPO_FORMAT_LIMIT"])

    def test_tempo_regression_guards(self):
        original = (ROOT / "examples/public-state-probe/probe.c").read_text()
        variants = [
            ("tempo-valid", "(s->valid & AZ_LIVE_TEMPO) &&", "1 &&"),
            ("slider-valid", "(s->valid & AZ_LIVE_TEMPO_SLIDER_LOCKED) &&", "1 &&"),
            ("finite", "isfinite(s->tempo) && s->tempo > 20.0 && s->tempo < 400.0", "1"),
            ("boolean", "(s->tempo_slider_locked == 0 || s->tempo_slider_locked == 1)", "1"),
            ("receipt-clock", "compare_timestamps(value.tempo_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL", "1"),
            ("slider-clock", "compare_timestamps(value.slider_ns, query_ns, 1000000000, 1000000000, 1) <= AZ_TIME_EQUAL", "1"),
            ("short-tail", "s->size >= offsetof(az_deck_snapshot, tempo_observed_ns) + sizeof s->tempo_observed_ns", "1"),
        ]
        for name, old, new in variants:
            with self.subTest(guard=name):
                self.assertEqual(original.count(old), 1)
                self.run_fixture(source=original.replace(old, new), failure=True)

    def test_asan_ubsan(self):
        compiler = shutil.which("clang")
        if not compiler:
            self.skipTest("Clang unavailable")
        self.run_fixture(["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"], compiler)

    def test_regression_guards(self):
        original = (ROOT / "examples/public-state-probe/probe.c").read_text()
        variants = [
            ("validity", "if (s->valid & AZ_LIVE_MASTER_TEMPO) value.mt = s->master_tempo;", "value.mt = s->master_tempo;", 1),
            ("deck", "probe_library->deck_snapshot(deck, &snapshot)", "probe_library->deck_snapshot(1, &snapshot)", 1),
            ("clock", "if (clock_gettime(CLOCK_MONOTONIC, &time) ||", "if ((clock_gettime(CLOCK_MONOTONIC, &time), 0) ||", 1),
            ("budget", "records < PROBE_MAX_RECORDS", "records < PROBE_MAX_RECORDS + 1", 5),
        ]
        for name, old, new, count in variants:
            with self.subTest(guard=name):
                self.assertEqual(original.count(old), count)
                self.run_fixture(source=original.replace(old, new), failure=True)


if __name__ == "__main__":
    unittest.main()
