# SPDX-License-Identifier: GPL-3.0-only
"""Actual engine/provider integration; native catalog and scheduling are fixtures.

Set AZ_CORE_TEST_ROOT when the core checkout is not next to the SDK checkout.
"""
import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
CORE = pathlib.Path(os.environ.get("AZ_CORE_TEST_ROOT", ROOT.parent / "xdj-az-mods"))
PROBE = pathlib.Path(os.environ.get("AZ_PUBLIC_WAVE_ENGINE_PROBE", ROOT / "examples/wave-layer-probe/probe.c"))
HEADERS = ("sdk_data_engine.h", "sdk_wave_provider.h", "sdk_waveform.h",
           "sdk_analysis.h", "hc_anlz.h", "azmod-mod.h")


class PublicWaveLayerEngine(unittest.TestCase):
    def build_run(self, sanitizer=False):
        core_headers = CORE / "src/azmod"
        if not all((core_headers / name).is_file() for name in HEADERS):
            self.skipTest("set AZ_CORE_TEST_ROOT to a core checkout for engine integration")
        self.assertEqual((ROOT / "include/azmod-mod.h").read_bytes(),
                         (core_headers / "azmod-mod.h").read_bytes(),
                         "SDK and core public headers must agree")
        cc = shutil.which("clang" if sanitizer else "cc")
        if not cc:
            self.skipTest("host C compiler unavailable")
        modes = os.environ.get("AZ_PUBLIC_WAVE_ENGINE_MODES", "immediate,delayed,permanent,malformed").split(",")
        self.assertTrue(set(modes) <= {"immediate", "delayed", "permanent", "malformed"})
        with tempfile.TemporaryDirectory(prefix="public-wave-engine-build-") as temp:
            binary = pathlib.Path(temp) / "fixture"
            flags = ["-fsanitize=address,undefined", "-fno-sanitize-recover=all"] if sanitizer else []
            argv = [cc, "-std=gnu11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", *flags,
                    "-I" + str(ROOT / "include"), "-I" + str(core_headers),
                    '-DPUBLIC_WAVE_PROBE_IMPL="' + str(PROBE.resolve()) + '"',
                    str(ROOT / "tests/fixtures/public-wave-layer-engine.c"), "-pthread", "-lm", "-o", str(binary)]
            built = subprocess.run(argv, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for mode in modes:
                with self.subTest(mode=mode):
                    r = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=15)
                    self.assertIn("PUBLIC_WAVE_ENGINE_BASELINE_OK", r.stdout, r.stdout + r.stderr)
                    self.assertIn("PUBLIC_WAVE_ENGINE_SERVICE_DONE", r.stdout, r.stdout + r.stderr)
                    self.assertIn("outstanding=0", r.stdout, r.stdout + r.stderr)
                    self.assertNotIn("FIXTURE_TITLE_NOT_FOR_LOGS", r.stdout)
                    self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
                    self.assertIn("PUBLIC_WAVE_ENGINE_FIXTURE_OK", r.stdout)

    def test_actual_engine_provider(self):
        self.build_run()

    def test_actual_engine_provider_asan_ubsan(self):
        self.build_run(True)


if __name__ == "__main__":
    unittest.main()
