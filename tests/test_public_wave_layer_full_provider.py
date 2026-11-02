# SPDX-License-Identifier: GPL-3.0-only
"""Public waveform client through real metadata resolution and real core worker.

USB, plain SQLite/key and mod identity use the ordinary core fixture. This is
host integration coverage, not native-loader or firmware acceptance.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]
CORE=pathlib.Path(os.environ.get('AZ_CORE_TEST_ROOT',ROOT.parent/'xdj-az-mods'))

class FullProvider(unittest.TestCase):
    def build_run(self,sanitizer=False):
        if sys.platform!='linux':self.skipTest('full disk provider requires Linux; run this test in the Linux SDK runtime')
        compiler=os.environ.get('HOST_CC') or ('clang' if sanitizer and shutil.which('clang') else 'cc')
        cc=shutil.which(compiler)
        if not cc:self.skipTest('host C compiler unavailable')
        unit=CORE/'tests/unit/sdktrack_test.c'
        if not unit.is_file():self.skipTest('core ordinary disk fixture unavailable')
        prefix,separator,_=unit.read_text().partition('int main(void) {')
        self.assertTrue(separator,'ordinary disk fixture main boundary')
        old='#define az_thread_create pthread_create'
        self.assertIn(old,prefix)
        prefix=prefix.replace(old,'#include <time.h>\nstatic int full_create(pthread_t *,const pthread_attr_t *,void *(*)(void *),void *);\nstatic int full_pause(const struct timespec *,struct timespec *);\n#define az_thread_create full_create\n#define nanosleep full_pause')
        prefix=prefix.replace('#undef pread','#undef nanosleep\n#undef pread',1)
        fixture=ROOT/'tests/fixtures/public-wave-layer-full-provider.c'
        probe=pathlib.Path(os.environ.get('AZ_PUBLIC_WAVE_ENGINE_PROBE',ROOT/'examples/wave-layer-probe/probe.c'))
        with tempfile.TemporaryDirectory(prefix='sdk-full-wave-') as temp:
            source=pathlib.Path(temp)/'full.c';binary=pathlib.Path(temp)/'full'
            source.write_text(prefix+'\n#include "'+str(fixture.resolve())+'"\n')
            link=['-Wl,-export_dynamic'] if sys.platform=='darwin' else ['-rdynamic','-ldl']
            flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if sanitizer else []
            argv=[cc,'-std=gnu11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-missing-field-initializers',*flags,
                  '-I'+str(CORE/'src/azmod'),'-I'+str(CORE/'tests/unit'),
                  '-DLIB_INC="'+str((CORE/'src/azmod/sdk_lib.c').resolve())+'"',
                  '-DPUBLIC_WAVE_PROBE_IMPL="'+str(probe.resolve())+'"',str(source),'-pthread','-lm','-lsqlite3',*link,'-o',str(binary)]
            built=subprocess.run(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            delays=[int(n) for n in os.environ.get('AZ_PUBLIC_FULL_DELAYS','0,2500').split(',')]
            for delay in delays:
                with self.subTest(delay_ms=delay):
                    result=subprocess.run([str(binary),str(delay)],stdout=subprocess.PIPE,stderr=subprocess.PIPE,universal_newlines=True,timeout=35)
                    self.assertIn('FULL_PROVIDER_RESULT',result.stdout,result.stdout+result.stderr)
                    self.assertIn('outstanding=0',result.stdout,result.stdout+result.stderr)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    print('metadata/worker delay_ms={} sanitizer={}: {}'.format(delay,sanitizer,next(line for line in result.stdout.splitlines() if line.startswith('FULL_PROVIDER_RESULT'))))
                    self.assertIn('FULL_PROVIDER_OK',result.stdout)
                    self.assertIn('opens=1 keys=1 queries=1 closes=1',result.stdout)
    def test_actual_metadata_and_worker(self):self.build_run()
    def test_actual_metadata_and_worker_asan_ubsan(self):self.build_run(True)

if __name__=='__main__':unittest.main()
