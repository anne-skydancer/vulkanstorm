"""Actual Poser drawing kernels and physical skin geometry, without a window."""
from pathlib import Path
import os,sys,unittest
R=Path(os.environ.get('VS_POSER_ROOT',Path(__file__).resolve().parents[2]))
O=Path(os.environ['VS_POSER_PROPOSED']) if 'VS_POSER_PROPOSED' in os.environ else None
sys.path.insert(0,str(R/'scripts/tests'))
from test_native_material_selection import body,run
def source(path):
 p=O/Path(path).name if O else R/path
 return (p if p.exists() else R/path).read_text(encoding='utf-8')
class NativePhysicalImageTests(unittest.TestCase):
 def test_actual_raw_pixel_skin_background_preserves_gl_extents_at_fractional_dpi(self):
  text=source('indra/newview/vsuiresources.cpp')
  actual=body(text,'    void draw(const Asset&')
  color=text[text.index('float nativeVertexColor('):text.index('VSUIRenderer::Blend nativeBlend()')]
  header=(R/'indra/newview/vsuirenderer.h').as_posix()
  run(self,(r'''#include "HEADER"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <stdexcept>
using S32=int;using F32=float;using U8=unsigned char;
struct LLColor4{float mV[4]{1,1,1,1};};
struct LLRectf{float mLeft,mTop,mRight,mBottom;LLRectf(float l,float t,float r,float b):mLeft(l),mTop(t),mRight(r),mBottom(b){}float getWidth()const{return mRight-mLeft;}float getHeight()const{return mTop-mBottom;}float getCenterX()const{return(mLeft+mRight)/2;}float getCenterY()const{return(mBottom+mTop)/2;}void setCenterAndSize(float x,float y,float w,float h){mLeft=x-w/2;mRight=x+w/2;mBottom=y-h/2;mTop=y+h/2;}};
struct LLRender2D{static bool isNativeUI(){return true;}static std::array<float,2>nativeOrigin(){return{3,5};}};
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}template<class T>T llclamp(T v,T lo,T hi){return std::clamp(v,lo,hi);}COLOR
VSUIRenderer::Blend nativeBlend(){return VSUIRenderer::Blend::StraightAlpha;}
struct Canvas{struct Asset{unsigned width,height;VSUIRenderer::Image image;};bool active=true;float dpi=1,logical_height=1000;std::array<float,4>clip{};VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear;std::vector<VSUIRenderer::Packet>packets;
void draw(const Asset&a,S32 x,S32 y,S32 width,S32 height,const LLColor4&color,bool solid,const LLRectf&outer,const LLRectf&center,bool inner,bool snap_extent=true,bool physical=false) ACTUAL
};
int main(){Canvas c;Canvas::Asset a{8,8,{}};auto close=[](float a,float b){assert(std::abs(a-b)<.0002f);};
for(float dpi:{1.f,1.25f,1.5f,2.f}){c.dpi=dpi;c.logical_height=1000/dpi;c.packets.clear();c.draw(a,1,2,17,13,{},false,{0,1,1,0},{.25f,.75f,.75f,.25f},true,true,true);assert(c.packets.size()==9);
// Actual GL raw-pixel nine-slice boundaries: x=1,3,16,18; y=2,4,13,15.
float xs[]{1,3,16,18},ys[]{2,4,13,15};for(int row=0;row<3;++row)for(int col=0;col<3;++col){const auto&p=c.packets[row*3+col];close(p.bounds[0]*dpi,xs[col]);close(p.bounds[2]*dpi,xs[col+1]);close(p.bounds[1]*dpi,1000-ys[row+1]);close(p.bounds[3]*dpi,1000-ys[row]);}
c.packets.clear();c.draw(a,1,2,17,13,{},false,{0,1,1,0},{0,1,1,0},true,true,true);assert(c.packets.size()==1);close(c.packets[0].bounds[0]*dpi,1);close(c.packets[0].bounds[2]*dpi,18);}
// Existing logical skin draw retains parent origin and fractional-DPI snapping.
c.dpi=1.25f;c.logical_height=100;c.packets.clear();c.draw(a,1,2,17,13,{},false,{0,1,1,0},{.25f,.75f,.75f,.25f},true);close(c.packets[0].bounds[0],4);close(c.packets[0].bounds[2],6.4f);}
''').replace('HEADER',header).replace('COLOR',color).replace('ACTUAL',actual))
if __name__=='__main__':unittest.main()
