# SPDX-License-Identifier: GPL-3.0-only
"""Host-only public-service contract tests; no native/provider acceptance claim."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CASES = ("good", "pending_once", "busy_once", "immediate_range", "reserved", "inverse", "provenance", "bounds", "ref",
         "clock_axis", "stride", "bytes", "availability", "null_payload", "count", "header",
         "pending", "layer_pending", "layer_request_busy", "request_busy", "resolve_busy", "no_track", "missing_service", "unsupported8", "clock", "clock_after_ticket", "start_failure", "error_ticket", "zero_ticket")


class PublicWaveLayerProbe(unittest.TestCase):
    def build_run(self, sanitizer=False):
        cc = shutil.which("clang" if sanitizer else "cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        with tempfile.TemporaryDirectory() as temp:
            binary = pathlib.Path(temp) / "fixture"
            flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] if sanitizer else []
            subprocess.run([cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", *flags,
                            "-I" + str(ROOT / "include"), str(ROOT / "tests/fixtures/public-wave-layer-probe.c"),
                            "-o", str(binary)], check=True, capture_output=True, text=True)
            for case in CASES:
                with self.subTest(case=case):
                    r = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertIn("PUBLIC_WAVE_FIXTURE_OK", r.stdout)
                    self.assertNotIn("NEVER_COPY_OR_LOG", r.stdout)

    def test_public_worker_contract(self):
        self.build_run()

    def test_asan_ubsan(self):
        self.build_run(True)

    def test_independent_controls(self):
        source = (ROOT / "examples/wave-layer-probe/probe.c").read_text()
        controls = [
            ("reserved", " || layered[5] != 0", ""),
            ("ref", "a->source_generation == b->source_generation", "1"),
            ("inverse", "return !memcmp(recovered, raw, 3);", "return 1;"),
            ("provenance", "v->info.provenance != (stride == 3 ? AZ_PROV_FILE : AZ_PROV_DERIVED)", "0"),
        ]
        with tempfile.TemporaryDirectory() as temporary:
            for case, before, after in controls:
                with self.subTest(control=case):
                    self.assertEqual(source.count(before), 1)
                    mutant = pathlib.Path(temporary) / (case + ".c")
                    binary = pathlib.Path(temporary) / case
                    mutant.write_text(source.replace(before, after))
                    subprocess.run(["cc", "-std=gnu11", "-O1", "-Wall", "-Wextra", "-Werror",
                                    "-I" + str(ROOT / "include"), '-DPUBLIC_WAVE_PROBE_IMPL="' + str(mutant) + '"',
                                    str(ROOT / "tests/fixtures/public-wave-layer-probe.c"), "-o", str(binary)],
                                   check=True, capture_output=True, text=True)
                    result = subprocess.run([str(binary), case], capture_output=True, text=True, timeout=10)
                    self.assertNotEqual(result.returncode, 0, result.stdout)
                    self.assertIn("assert", result.stderr.lower())


if __name__ == "__main__":
    unittest.main()
