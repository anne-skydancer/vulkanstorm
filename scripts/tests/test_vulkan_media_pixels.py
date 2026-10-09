"""Execute native plugin copying against padded RGBA/BGRA and orientation fixtures."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class MediaPixels(unittest.TestCase):
    def test_formats_orientation_replacement_and_invalid_extents(self):
        compiler = shutil.which("clang++") or shutil.which("g++")
        self.assertIsNotNone(compiler)
        root = Path(__file__).resolve().parents[2]
        fixture = r"""
#include "vsmediapixels.h"
#include <cassert>
int main() {
    const std::uint8_t padded[]{10,20,30,40,50,60,70,80,99,99,99,99,
                               11,21,31,41,51,61,71,81,99,99,99,99};
    auto rgba=vs_media_pixels(padded,2,2,3,2,false,false);
    assert((rgba==std::vector<std::uint8_t>{11,21,31,41,51,61,71,81,10,20,30,40,50,60,70,80}));
    const auto preserved=rgba;
    auto bgra=vs_media_pixels(padded,2,2,3,2,true,true);
    assert((bgra==std::vector<std::uint8_t>{30,20,10,40,70,60,50,80,31,21,11,41,71,61,51,81}));
    auto opaque=vs_media_pixels(padded,2,2,3,2,true,true,true);
    assert(opaque[3]==255 && opaque[7]==255 && opaque[11]==255 && opaque[15]==255);
    assert(opaque[0]==30 && opaque[1]==20 && opaque[2]==10);
    assert(rgba==preserved); // Replacement cannot mutate an earlier publication.
    for(unsigned i=0;i<5;++i) {
        bool failed=false;
        try { vs_media_pixels(i==0?nullptr:padded,i==1?0:2,2,i==2?1:i==4?16385:3,i==3?1:2,false,false); }
        catch(const std::runtime_error&) { failed=true; }
        assert(failed);
    }
}
"""
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/"media_pixels.cpp"
            source.write_text(fixture,encoding="utf-8")
            exe=Path(temp)/"media_pixels.exe"
            build=subprocess.run([compiler,"-std=c++17","-Wall","-Wextra","-Werror","-I",str(root/"indra/newview"),str(source),"-o",str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=="__main__": unittest.main()
