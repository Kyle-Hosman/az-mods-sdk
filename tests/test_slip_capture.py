# SPDX-License-Identifier: GPL-3.0-only
"""Actual C producer with host original-frame double; no native/Slip semantics."""
import os
import pathlib
import platform
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PROBE = ROOT / "examples/slip-callback-probe"


class SlipCapture(unittest.TestCase):
    def run_fixture(self, modes, sanitizer=False, missing=False, actual_arm=False):
        cc, cxx = shutil.which("clang"), shutil.which("clang++")
        if not cc or not cxx:
            self.skipTest("host Clang C/C++ unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            obj = pathlib.Path(temporary) / "capture.o"
            binary = pathlib.Path(temporary) / "fixture"
            flags = ["-O1", "-g", "-Wall", "-Wextra", "-Werror", "-fexceptions", "-I" + str(PROBE), "-I" + str(ROOT / "include")]
            if sanitizer:
                flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
            producer = PROBE / "capture.c"
            absent = missing or not producer.exists()
            extra = ["-DSLIP_CAPTURE_FORWARD_ONLY"] if absent else []
            if not actual_arm:
                extra += ["-DSLIP_CAPTURE_HOST_BOUNDARY"]
            objects = []
            if not absent:
                subprocess.run([cc, "-std=gnu11", *flags, "-DSLIP_CAPTURE_TEST_CLOCK", "-c", str(producer), "-o", str(obj)], check=True, capture_output=True, text=True)
                objects.append(str(obj))
            if actual_arm:
                asm = pathlib.Path(temporary) / "entry.o"
                subprocess.run([cc, *flags, "-c", str(PROBE / "native/azmod/sdk_cache_entry.S"), "-o", str(asm)], check=True, capture_output=True, text=True)
                objects.append(str(asm))
            subprocess.run([cxx, "-std=c++11", *flags, *extra, "-pthread", str(ROOT / "tests/fixtures/slip-capture-test.cpp"), *objects, "-o", str(binary)], check=True, capture_output=True, text=True)
            for mode in modes:
                result = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                self.assertIn("SLIP_NATIVE_FORWARD_BASELINE_OK", result.stdout)
                if missing or os.environ.get("SLIP_CAPTURE_EXPECT_RED") == "1":
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("missing by-value Slip capture record after native forwarding", result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, mode + ": " + result.stdout + result.stderr)
                    self.assertIn("PASS slip capture " + mode, result.stdout)

    def test_by_value_record(self):
        self.run_fixture(["capture"])

    def test_guards_and_forwarding(self):
        self.run_fixture(["reject", "clock", "reentrant", "throws", "ring"])

    def test_missing_capture_control(self):
        self.run_fixture(["capture"], missing=True)

    def test_host_asan_ubsan(self):
        self.run_fixture(["capture", "reject", "clock", "reentrant", "throws", "ring"], sanitizer=True)

    @unittest.skipUnless(platform.system() == "Linux" and platform.machine() == "aarch64", "actual entry0 requires AArch64 Linux")
    def test_actual_arm_entry(self):
        self.run_fixture(["arm"], actual_arm=True)


if __name__ == "__main__":
    unittest.main()
