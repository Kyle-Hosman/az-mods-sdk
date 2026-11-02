# SPDX-License-Identifier: GPL-3.0-only
import os
import hashlib
import pathlib
import re
import shutil
import struct
import subprocess
import tempfile
import unittest

ROOT=pathlib.Path(__file__).resolve().parents[1]

class SlipBinding(unittest.TestCase):
    def test_original_image_call_sites(self):
        firmware=pathlib.Path(os.environ.get("AZ_FIRMWARE_ROOT",str(ROOT.parent/"anchor/firmware")))
        if not (firmware/"EP147-1.30").exists():self.skipTest("original firmware inputs unavailable")
        header=(ROOT/"examples/slip-callback-probe/profiles.h").read_text()
        profiles=re.findall(r'\{"([^"]+)","([^"]+)",\{(0x[0-9a-f]+),(\d+),"([0-9a-f]+)"\},\{(0x[0-9a-f]+),(\d+),"([0-9a-f]+)"\},(0x[0-9a-f]+)\}',header)
        self.assertEqual(len(profiles),2)
        for version,tag,cb,cb_size,cb_hash,dispatch,d_size,d_hash,caller in profiles:
            with self.subTest(version=version):
                with (firmware/("EP147-"+version)).open("rb") as source:
                    eh=source.read(64);off=struct.unpack_from("<Q",eh,32)[0];stride,count=struct.unpack_from("<HH",eh,54)
                    source.seek(off);raw=source.read(stride*count)
                    segments=[struct.unpack_from("<IIQQQQQQ",raw,i*stride) for i in range(count)]
                    def read(rva,size):
                        for kind,flags,pos,start,_,length,_,_ in segments:
                            if kind==1 and flags&1 and start<=rva and rva+size<=start+length:
                                source.seek(pos+rva-start);return source.read(size)
                        self.fail("binding outside executable file bytes")
                    for rva,size,digest in [(int(cb,16),int(cb_size),cb_hash),(int(dispatch,16),int(d_size),d_hash)]:
                        self.assertEqual(hashlib.sha256(read(rva,size)).hexdigest(),digest)
                        self.assertNotEqual(hashlib.sha256(read(rva+4,size)).hexdigest(),digest)
                    self.assertEqual(struct.unpack("<I",read(int(caller,16)-4,4))[0],0xd63f0040) # BLR X2
                self.assertRegex(tag,r"^ep147-[0-9a-f]{16}$")

    def test_runtime_code_binding(self):
        compiler=os.environ.get("HOST_CC") or shutil.which("cc")
        if not compiler:self.skipTest("C compiler unavailable")
        with tempfile.TemporaryDirectory() as directory:
            binary=pathlib.Path(directory)/"binding"
            subprocess.run([compiler,"-std=gnu11","-O1","-Wall","-Wextra",
                            str(ROOT/"tests/fixtures/slip-binding-test.c"),"-o",str(binary)],check=True)
            subprocess.run([str(binary)],check=True,timeout=5)

if __name__=="__main__":unittest.main()
