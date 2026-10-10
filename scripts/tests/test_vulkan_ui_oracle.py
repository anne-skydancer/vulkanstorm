"""Known pixel cases for the diagnostic CPU oracle, without a graphics device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class OracleTest(unittest.TestCase):
    def test_native_tint_matches_actual_gl_vertex_bytes(self):
        root = Path(__file__).resolve().parents[2]
        native_source = (root / 'indra/newview/vsuiresources.cpp').read_text()
        helper = native_source[native_source.index('float nativeVertexColor('):native_source.index('VSUIRenderer::Blend nativeBlend()')]
        gl_source = (root / 'indra/llrender/llrender.cpp').read_text()
        gl = gl_source[gl_source.index('void LLRender::color4f('):gl_source.index('void LLRender::color4fv(')]
        fixture = r'''#include <algorithm>
#include <cassert>
#include <cmath>
using U8=unsigned char;using GLubyte=unsigned char;using GLfloat=float;
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
struct LLRender { U8 bytes[4]{};void color4f(const GLfloat&,const GLfloat&,const GLfloat&,const GLfloat&);
 void color4ub(U8 r,U8 g,U8 b,U8 a){bytes[0]=r;bytes[1]=g;bytes[2]=b;bytes[3]=a;} };
''' + helper + gl + r'''
int main(){
 LLRender gl;
 for(int i=-1;i<=256;++i){
  for(float delta:{-0.000001f,0.f,0.000001f}){
   float v=i/255.f+delta;gl.color4f(v,v,v,v);
   for(U8 byte:gl.bytes)assert(nativeVertexColor(v)==byte/255.f);
  }
 }
 for(float opacity:{0.f,.1f,.25f,.5f,.7f,.9f,1.f}){
  gl.color4f(.267f,.953f,.5f,opacity);
  assert(nativeVertexColor(opacity)==gl.bytes[3]/255.f);
  // Independently known inactive-tab alpha blended onto a fixed background.
  float src=68/255.f,alpha=157/255.f*gl.bytes[3]/255.f,dst=24/255.f;
  float expected=src*alpha+dst*(1-alpha);
  float actual=src*(157/255.f*nativeVertexColor(opacity))+dst*(1-157/255.f*nativeVertexColor(opacity));
  assert(std::abs(expected-actual)<0.000001f);
 }
 assert(nativeVertexColor(.5f)==127/255.f);
}
'''
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'vertex_tint.cpp';path.write_text(fixture)
            exe=Path(directory)/'vertex_tint.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_native_widget_strokes_center_fractional_width_and_outline_insets(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/llrender/llrender2dutils.cpp').read_text(encoding='utf-8')
        def function(signature):
            start = source.index(signature)
            opening = source.index('{', start)
            end, depth = opening + 1, 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            return source[start:end]
        rectangle = function('void LLRender2D::nativeRectangle(')
        line = function('void gl_line_2d(S32 x1, S32 y1, S32 x2, S32 y2, const LLColor4')
        line = line[:line.index('    gGL.getTexUnit')] + '}\n'
        fixture = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <vector>
using S32=int;using F32=float;
template<class T>T llmin(T a,T b){return std::min(a,b);}
template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLColor4{float mV[4]{.2f,.4f,.6f,.5f};};
struct LLRectf{float l,t,r,b;LLRectf(float a,float c,float d,float e):l(a),t(c),r(d),b(e){}};
struct LLFontGL{static constexpr float sScaleX=1.25f,sScaleY=1.25f;};
std::vector<LLRectf> rectangles;
void native_rectangle(const LLRectf&r,const LLColor4&){rectangles.push_back(r);}
float native_line_width=2.25f;
struct LLRender2D{
 using Triangle=std::array<std::array<float,6>,3>;
 static inline std::vector<Triangle> triangles;
 static bool isNativeUI(){return true;}
 static std::array<float,2> nativeOrigin(){return {3,5};}
 static void nativeTriangle(const Triangle&t){triangles.push_back(t);}
 static void nativeRectangle(S32,S32,S32,S32,const LLColor4&,bool);
};
""" + rectangle + '\n' + line + r"""
int main(){
 auto close=[](float a,float b){assert(std::abs(a-b)<.0001f);};
 gl_line_2d(2,4,12,4,{});
 assert(LLRender2D::triangles.size()==2);
 close(LLRender2D::triangles[0][0][1],5.125f);
 close(LLRender2D::triangles[0][2][1],2.875f);
 LLRender2D::triangles.clear();
 gl_line_2d(2,4,2,14,{});
 close(LLRender2D::triangles[0][0][0],.875f);
 close(LLRender2D::triangles[0][2][0],3.125f);
 LLRender2D::triangles.clear();
 gl_line_2d(2,4,2,4,{});assert(LLRender2D::triangles.empty());
 LLRender2D::nativeRectangle(2,14,12,4,{},false);
 assert(rectangles.size()==4);
 // Requested width centered on GL's inset top13/right11 path, then
 // translated once and scaled to physical pixels. No ceil-to-three pixels.
 close(rectangles[0].l,(2-1.125f+3)*1.25f);
 close(rectangles[0].t,(13+1.125f+5)*1.25f);
 close(rectangles[0].r,(11+1.125f+3)*1.25f);
 close(rectangles[0].t-rectangles[0].b,2.25f*1.25f);
 close(rectangles[2].t,rectangles[0].b);
 close(rectangles[2].b,rectangles[1].t);
 close(rectangles[3].t,rectangles[0].b);
 close(rectangles[3].b,rectangles[1].t);
 rectangles.clear();
 LLRender2D::nativeRectangle(2,5,3,4,{},false);
 assert(rectangles.size()==1); // Collapsed interior still draws once.
}
"""
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'skin_strokes.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'skin_strokes.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_skin_nine_slice_inner_edges_match_gl_pixel_rounding(self):
        root = Path(__file__).resolve().parents[2]
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (root / 'indra/newview/vsuiresources.cpp').read_text(encoding='utf-8')
        color_helper = source[source.index('float nativeVertexColor('):source.index('VSUIRenderer::Blend nativeBlend()')]
        start = source.index('    void draw(const Asset&')
        opening = source.index('{', start)
        end, depth = opening + 1, 1
        while depth:
            depth += (source[end] == '{') - (source[end] == '}')
            end += 1
        fixture = r"""
#include "vsuirenderer.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
using S32=int; using F32=float;
struct LLColor4 { float mV[4]{1,1,1,1}; };
struct LLRectf {
    float mLeft,mTop,mRight,mBottom;
    LLRectf(float l,float t,float r,float b):mLeft(l),mTop(t),mRight(r),mBottom(b){}
    float getWidth()const{return mRight-mLeft;} float getHeight()const{return mTop-mBottom;}
    float getCenterX()const{return (mLeft+mRight)/2;} float getCenterY()const{return (mBottom+mTop)/2;}
    void setCenterAndSize(float x,float y,float w,float h){mLeft=x-w/2;mRight=x+w/2;mBottom=y-h/2;mTop=y+h/2;}
};
struct LLRender2D {
    static bool isNativeUI(){return true;}
    static std::array<float,2> nativeOrigin(){return {3,5};}
};
void check(bool condition,const char* text){if(!condition)throw std::runtime_error(text);}
VSUIRenderer::Blend nativeBlend(){return VSUIRenderer::Blend::StraightAlpha;}
using U8=unsigned char;
template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}
NATIVE_COLOR_HELPER
struct Canvas {
    struct Asset{unsigned width,height;VSUIRenderer::Image image;};
    bool active=true;float dpi=1.25f,logical_height=100;
    std::array<float,4> clip{0,0,100,100};
    VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear;
    std::vector<VSUIRenderer::Packet> packets;
""" + source[start:end] + r"""
};
int main(){
    Canvas c;Canvas::Asset asset{8,8,{}};
    // Independent known GL helper result at translated 125% DPI. Inner
    // boundaries snap; outer boundaries retain their fractional positions.
    c.draw(asset,1,2,17,13,{},false,{0,1,1,0},{.25f,.75f,.75f,.25f},true);
    assert(c.packets.size()==9);
    auto close=[](float a,float b){assert(std::abs(a-b)<.0001f);};
    close(c.packets[0].bounds[0],4);
    close(c.packets[0].bounds[2],6.4f);
    close(c.packets[1].bounds[0],6.4f);
    close(c.packets[1].bounds[2],19.2f);
    close(c.packets[2].bounds[2],21);
    close(c.packets[0].bounds[1],91.2f);
    close(c.packets[3].bounds[3],91.2f);
    close(c.packets[3].bounds[1],81.6f);
    close(c.packets[6].bounds[3],81.6f);
    // Both neighboring slices share exactly the same raster boundary.
    for(unsigned row=0;row<3;++row)for(unsigned col=0;col<2;++col)
        close(c.packets[row*3+col].bounds[2],c.packets[row*3+col+1].bounds[0]);
    // Plain image GL extent: round(17*1.25)/1.25=16.8;
    // round(13*1.25)/1.25=12.8. Origin is unchanged.
    c.packets.clear();
    c.draw(asset,1,2,17,13,{},false,{0,1,1,0},{0,1,1,0},true);
    assert(c.packets.size()==1);
    close(c.packets[0].bounds[0],4);
    close(c.packets[0].bounds[2],20.8f);
    close(c.packets[0].bounds[1],80.2f);
    close(c.packets[0].bounds[3],93);
    // Nonzero rotated calls explicitly retain the unrounded base extents.
    c.packets.clear();
    c.draw(asset,1,2,17,13,{},false,{0,1,1,0},{0,1,1,0},true,false);
    close(c.packets[0].bounds[2],21);
    close(c.packets[0].bounds[1],80);
}
"""
        fixture = fixture.replace('NATIVE_COLOR_HELPER', color_helper)
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'skin_nine_slice.cpp'
            path.write_text(fixture, encoding='utf-8')
            exe = Path(temp) / 'skin_nine_slice.exe'
            built = subprocess.run([compiler, '-std=c++17', '-I', str(root / 'indra/newview'), str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

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
    packet.blend=VSUIRenderer::Blend::Replace;pixels=vs_ui_expected_pixels(4,4,1,{packet});
    assert(pixel(0,0,0)==255 && pixel(0,0,1)==0 && pixel(0,0,2)==0 && pixel(0,0,3)==128);
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
