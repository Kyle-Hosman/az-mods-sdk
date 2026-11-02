"""Affected normal public cue-client behavior; no native/runtime claim."""
import os,pathlib,shutil,subprocess,tempfile,unittest
ROOT=pathlib.Path(__file__).resolve().parents[1]
class CueProbe(unittest.TestCase):
    def run_probe(self,sanitize=False,limit=False):
        cc=shutil.which('clang' if sanitize else 'cc')
        if not cc:self.skipTest('C compiler unavailable')
        include=pathlib.Path(os.environ.get('AZ_PUBLIC_PROBE_INCLUDE',ROOT/'include'))
        with tempfile.TemporaryDirectory() as t:
            binary=pathlib.Path(t)/'probe'
            flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if sanitize else []
            if limit:flags+=['-DPROBE_MAX_RECORDS=5u']
            subprocess.run([cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror',*flags,'-I'+str(include),str(ROOT/'tests/fixtures/public-cue-probe.c'),'-o',str(binary)],check=True,capture_output=True,text=True)
            for mode in range(14):
                with self.subTest(mode=mode):
                    result=subprocess.run([str(binary),str(mode)],capture_output=True,text=True,timeout=3)
                    self.assertEqual(result.returncode,0,result.stderr)
    def test_public_callbacks_revision_guards_and_init_failures(self):self.run_probe()
    def test_asan_ubsan(self):self.run_probe(sanitize=True)
    def test_shared_record_bound(self):self.run_probe(limit=True)
if __name__=='__main__':unittest.main()
