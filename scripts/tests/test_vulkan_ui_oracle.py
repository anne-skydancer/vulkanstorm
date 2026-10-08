"""Known pixel cases for the diagnostic CPU oracle, without a graphics device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class OracleTest(unittest.TestCase):
    def test_texture_orientation_clip_blend_and_triangle_edges(self):
        root=Path(__file__).resolve().parents[2]
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source=(root/'indra/newview/vsuipixeloracle.cpp').read_text().replace('#include "llviewerprecompiledheaders.h"','')
        definitions=r'''
#include "vsuirenderer.h"
#include <cassert>
struct VSUIRenderer::Texture { unsigned width,height;std::vector<std::uint8_t> pixels; };
const std::vector<std::uint8_t>& VSUIRenderer::imagePixels(const Image& image) { return image->pixels; }
std::array<unsigned,2> VSUIRenderer::imageExtent(const Image& image) { return {image->width,image->height}; }
'''
        cases=r'''
int main() {
    auto texture=std::make_shared<VSUIRenderer::Texture>();texture->width=texture->height=2;
    texture->pixels={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    VSUIRenderer::Packet packet;packet.image=texture;packet.bounds={0,0,4,4};packet.clip={0,0,4,4};
    auto pixels=vs_ui_expected_pixels(4,4,1,{packet});
    auto pixel=[&](unsigned x,unsigned y,unsigned c) { return pixels[(y*4+x)*4+c]; };
    assert(pixel(0,0,0)==255 && pixel(0,0,1)==0 && pixel(3,0,1)==255);
    assert(pixel(0,3,2)==255 && pixel(3,3,0)==255);
    packet.clip={1,1,3,3};pixels=vs_ui_expected_pixels(4,4,1,{packet});
    assert(pixel(0,0,0)==16 && pixel(1,1,0)==255 && pixel(3,3,0)==16);
    auto red=std::make_shared<VSUIRenderer::Texture>();red->width=red->height=1;red->pixels={255,0,0,128};
    packet.image=red;packet.clip={0,0,4,4};pixels=vs_ui_expected_pixels(4,4,1,{packet});
    for(unsigned y=0;y<4;++y) for(unsigned x=0;x<4;++x) {
        // Every pixel, including the diagonal shared edge, is painted once.
        assert(pixel(x,y,0)==136 && pixel(x,y,1)==16 && pixel(x,y,2)==24 && pixel(x,y,3)==255);
    }
    packet.blend=VSUIRenderer::Blend::AdditiveAlpha;pixels=vs_ui_expected_pixels(4,4,1,{packet});
    assert(pixel(0,0,0)==144 && pixel(0,0,1)==32 && pixel(0,0,2)==48);
    packet.blend=VSUIRenderer::Blend::Additive;pixels=vs_ui_expected_pixels(4,4,1,{packet});
    assert(pixel(0,0,0)==255 && pixel(0,0,1)==32 && pixel(0,0,2)==48);
    packet.blend=VSUIRenderer::Blend::StraightAlpha;
    packet.image=texture;packet.sampling=VSUIRenderer::Sampling::Linear;packet.bounds={0,0,1,1};
    pixels=vs_ui_expected_pixels(4,4,1,{packet});assert(pixel(0,0,0)==128 && pixel(0,0,1)==128 && pixel(0,0,2)==128);
    packet.image=red;packet.bounds={0,0,2,2};packet.sampling=VSUIRenderer::Sampling::Nearest;
    pixels=vs_ui_expected_pixels(4,4,2,{packet});
    assert(pixel(3,3,0)==136);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            file=Path(temp)/'ui_oracle.cpp';file.write_text(definitions+source+cases)
            exe=Path(temp)/'ui_oracle.exe'
            built=subprocess.run([compiler,'-std=c++20','-I'+str(root/'indra/newview'),str(file),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__': unittest.main()
