# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual public command client with synthetic SDK functions."""
import pathlib
import shutil
import subprocess
import tempfile
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[1]
class LoopCommandProbe(unittest.TestCase):
    def fixture(self, flags=(), compiler=None, name="loop-command-probe.c"):
        compiler = compiler or shutil.which("cc")
        if not compiler: self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as temporary:
            binary=pathlib.Path(temporary)/"probe"
            built=subprocess.run([compiler,"-std=gnu11","-O1","-g","-Wall","-Wextra","-Werror",*flags,
                "-I"+str(ROOT/"include"),str(ROOT/"tests/fixtures"/name),"-lm","-o",str(binary)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=10)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn("PASS ",run.stdout)
    def test_public_command_lifecycle(self): self.fixture()
    def test_explicit_batches(self): self.fixture(name="loop-command-plan.c")
    def test_batches_asan_ubsan(self):
        compiler=shutil.which("clang")
        if not compiler:self.skipTest("Clang unavailable")
        self.fixture(["-fsanitize=address,undefined","-fno-sanitize-recover=all"],compiler,name="loop-command-plan.c")
    def test_asan_ubsan(self):
        compiler=shutil.which("clang")
        if not compiler:self.skipTest("Clang unavailable")
        self.fixture(["-fsanitize=address,undefined","-fno-sanitize-recover=all"],compiler)
if __name__=="__main__":unittest.main()
