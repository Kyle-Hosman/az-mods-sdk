"""Use actual selected core provider code, never an optimistic caps supplier."""
import os,pathlib,re,shutil,subprocess,tempfile,unittest
SDK=pathlib.Path(__file__).resolve().parents[1]
class Provider(unittest.TestCase):
    def run_provider(self,sanitize=False):
        core=pathlib.Path(os.environ.get('AZ_CORE_TEST_ROOT',SDK.parent/'xdj-az-mods'))
        cc=shutil.which('clang' if sanitize else 'cc')
        if not cc:self.skipTest('C compiler unavailable')
        fixture=core/'tests/unit/sdkcueevents_test.c'
        if not fixture.is_file():self.skipTest('core cue boundary fixture unavailable')
        with tempfile.TemporaryDirectory() as temporary:
            out=pathlib.Path(temporary)
            deck=(core/'src/azmod/sdk_deck.c').read_text()
            (out/'deck.inc').write_text(deck[:deck.index('/* az_core_v1.deck_info */')]+'\n'+deck[deck.index('/* ---- on_deck_event'):])
            track=(core/'src/azmod/sdk_track.c').read_text();start=track.index('static int sdk_data_capabilities(')
            bridge=(core/'src/azmod/sdk_exit_bridge.c').read_text()
            # Keep the actual capability dependency; no optimistic mask supplier.
            prefix=bridge[:bridge.index('static int sdk_exit_arguments(')]
            (out/'caps.inc').write_text('#include "sdk_command_engine.h"\n'+prefix+'\n'+track[start:track.index('\n}',start)+2]+'\n')
            boundary=fixture.read_text()
            self.assertEqual(boundary.count('int main(void)'),1)
            (out/'provider-fixture.c').write_text(boundary.split('int main(void)',1)[0])
            engine=(core/'src/azmod/sdk_data_engine.h').read_text()
            limits=['-D'+re.search(r'^#define (ST_'+name+r'\s+[0-9]+)',engine,re.M).group(1).replace(' ','=') for name in ('PER_OWNER','SLOTS')]
            probe=pathlib.Path(os.environ.get('AZ_CUE_PROBE_SOURCE',SDK/'examples/public-state-probe/probe.c'))
            flags=['-fsanitize=address,undefined','-fno-sanitize-recover=all'] if sanitize else []
            argv=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable',*flags,*limits,
                  '-I'+str(SDK/'include'),'-I'+str(core/'src/azmod'),'-I'+str(core/'src'),'-DSDKCUEEVENTS_INC="'+str(out/'deck.inc')+'"',
                  '-DSDK_CUE_PROVIDER_FIXTURE="'+str(out/'provider-fixture.c')+'"','-DSDK_CUE_CAPABILITIES="'+str(out/'caps.inc')+'"',
                  '-DPUBLIC_PROBE_IMPL="'+str(probe)+'"',str(SDK/'tests/fixtures/public-cue-provider.c'),'-o',str(out/'test')]
            built=subprocess.run(argv,capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stderr)
            expected=os.environ.get('AZ_EXPECT_CUE_SUPPORT','1')
            for mode in range(4):
                with self.subTest(mode=mode):
                    result=subprocess.run([str(out/'test'),expected,str(mode)],capture_output=True,text=True,timeout=3)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_actual_provider_client_init_and_callbacks(self):self.run_provider()
    def test_asan_ubsan(self):self.run_provider(sanitize=True)
if __name__=='__main__':unittest.main()
