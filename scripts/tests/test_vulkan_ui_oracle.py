"""Known pixel cases for the diagnostic CPU oracle, without a graphics device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class OracleTest(unittest.TestCase):
    def test_native_clip_preserves_physical_pixel_margin_at_fractional_dpi(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/newview/vsuiresources.cpp').read_text(encoding='utf-8')
        start = source.index('void VSUIResources::screenClip(')
        opening = source.index('{', start)
        end, depth = opening + 1, 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        fixture = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <stdexcept>
struct LLRect {
    int mLeft,mTop,mRight,mBottom;
    bool isEmpty() const { return mRight<=mLeft || mTop<=mBottom; }
    int getWidth() const { return mRight-mLeft; }
    int getHeight() const { return mTop-mBottom; }
};
void check(bool condition,const char* message) { if(!condition) throw std::logic_error(message); }
struct VSUIResources {
    struct Impl { bool active=true;float logical_width=100,logical_height=100,dpi=1;std::array<float,4> clip{}; } impl;
    Impl* mImpl=&impl;
    void screenClip(const LLRect*);
};
''' + source[start:end] + r'''
int main() {
    VSUIResources resources;
    LLRect rect{3,13,10,4};
    for(float dpi : {1.f,1.25f,2.f}) {
        resources.impl.dpi=dpi;resources.screenClip(&rect);
        const auto clip=resources.impl.clip;
        // Known physical scissors: (3,4,8,10), (3,5,10,13), (6,8,15,19).
        const float x=dpi==1?3:dpi==1.25f?3:6;
        const float y=dpi==1?4:dpi==1.25f?5:8;
        const float width=dpi==1?8:dpi==1.25f?10:15;
        const float height=dpi==1?10:dpi==1.25f?13:19;
        assert(std::abs(clip[0]*dpi-x)<.001f);
        assert(std::abs((100-clip[3])*dpi-y)<.001f);
        assert(std::abs((clip[2]-clip[0])*dpi-width)<.001f);
        assert(std::abs((clip[3]-clip[1])*dpi-height)<.001f);
    }
    rect={2,3,2,0};resources.screenClip(&rect);
    assert((resources.impl.clip==std::array<float,4>{0,0,0,0}));
    resources.screenClip(nullptr);
    assert((resources.impl.clip==std::array<float,4>{0,0,100,100}));
    resources.impl.active=false;
    bool rejected=false;try { resources.screenClip(nullptr); } catch(const std::logic_error&) { rejected=true; }
    assert(rejected);
}
'''
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'native_clip.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'native_clip.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

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
    // A coverage edge just above a sample center rounds onto it at the
    // device's subpixel precision. Top edges include it; bottom edges exclude.
    packet.bounds={0,.500001f,1,1.5f};packet.clip={0,0,1,1};
    pixels=vs_ui_expected_pixels(4,4,1,{packet},8);assert(pixel(0,0,0)==136);
    pixels=vs_ui_expected_pixels(4,4,1,{packet});assert(pixel(0,0,0)==16);
    packet.bounds={0,-.5f,1,.500001f};
    pixels=vs_ui_expected_pixels(4,4,1,{packet},8);assert(pixel(0,0,0)==16);
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
