// Headless primitive sink comparison. LGPL-2.1.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>
using F32=float;using S32=int;using U32=std::uint32_t;
constexpr F32 F_TWO_PI=6.28318530717958647692f;
template<class T>T llmin(T a,T b){return std::min(a,b);}
struct LLColor4{float mV[4];};
using Vertex=std::array<F32,6>;
using Triangle=std::array<Vertex,3>;
struct LLRender2D
{
 inline static bool native=false;
 inline static LLColor4 color{{1,1,1,1}};
 inline static std::vector<Triangle> triangles;
 static bool isNativeUI(){return native;}
 static const LLColor4& nativeColor(){return color;}
 static void nativeTriangle(const Triangle& t){triangles.push_back(t);}
};
struct LLTexUnit{enum{TT_TEXTURE};void unbind(int){assert(!LLRender2D::native);}};
struct LLRender
{
 enum{TRIANGLES,TRIANGLE_FAN,TRIANGLE_STRIP,LINES,LINE_LOOP,LINE_STRIP};
 struct Stream{int mode;std::vector<Vertex> vertices;};
 std::vector<Stream> streams;
 std::array<float,2> offset{},saved{};
 LLTexUnit unit;
 LLTexUnit* getTexUnit(int){return &unit;}
 void pushUIMatrix(){saved=offset;}
 void popUIMatrix(){offset=saved;}
 void translateUI(float x,float y,float z){assert(z==0);offset[0]+=x;offset[1]+=y;}
 void color4fv(const float* c){for(int i=0;i<4;++i)LLRender2D::color.mV[i]=std::uint8_t(std::clamp(c[i],0.f,1.f)*255)/255.f;}
 void begin(int mode){assert(!LLRender2D::native);streams.push_back({mode,{}});}
 void vertex2f(float x,float y){auto c=LLRender2D::color;streams.back().vertices.push_back({x+offset[0],y+offset[1],c.mV[0],c.mV[1],c.mV[2],c.mV[3]});}
 void vertex2i(int x,int y){vertex2f(float(x),float(y));}
 void end(){}
}gGL;
float native_line_width=1.f;
#include "vsuiprimitive.h"
PRODUCTION_ADAPTER
PRODUCTION_WRAPPERS
#include "ui_primitives_reference.cpp"

// Independent reference topology expansion; production uses a constant-storage
// stream and must preserve every original fan/strip triangle and line segment.
std::vector<Triangle> expand(const std::vector<LLRender::Stream>& streams)
{
 std::vector<Triangle> out;
 auto line=[&](Vertex a,Vertex b){
  float dx=b[0]-a[0],dy=b[1]-a[1],len=std::hypot(dx,dy);if(!len)return;
  float nx=-dy*native_line_width*.5f/len,ny=dx*native_line_width*.5f/len;
  auto ap=a,am=a,bp=b,bm=b;
  ap[0]+=nx;ap[1]+=ny;am[0]-=nx;am[1]-=ny;
  bp[0]+=nx;bp[1]+=ny;bm[0]-=nx;bm[1]-=ny;
  out.push_back({ap,bp,am});out.push_back({am,bp,bm});
 };
 for(const auto& stream:streams){const auto& v=stream.vertices;
  switch(stream.mode){
  case LLRender::TRIANGLE_FAN:for(std::size_t i=2;i<v.size();++i)out.push_back({v[0],v[i-1],v[i]});break;
  case LLRender::TRIANGLE_STRIP:for(std::size_t i=2;i<v.size();++i)out.push_back(i%2?Triangle{v[i-1],v[i-2],v[i]}:Triangle{v[i-2],v[i-1],v[i]});break;
  case LLRender::TRIANGLES:for(std::size_t i=2;i<v.size();i+=3)out.push_back({v[i-2],v[i-1],v[i]});break;
  case LLRender::LINES:for(std::size_t i=1;i<v.size();i+=2)line(v[i-1],v[i]);break;
  case LLRender::LINE_STRIP:case LLRender::LINE_LOOP:
   for(std::size_t i=1;i<v.size();++i)line(v[i-1],v[i]);
   if(stream.mode==LLRender::LINE_LOOP&&v.size()>1)line(v.back(),v.front());break;
  default:assert(false);
  }
 }
 return out;
}
template<class ReferenceCall,class ProductionCall>
void compare(ReferenceCall reference,ProductionCall production)
{
 const float color[4]={.13f,.57f,.91f,.49f};
 LLRender2D::native=false;gGL.color4fv(color);gGL.streams.clear();reference();
 auto original=gGL.streams;auto endColor=LLRender2D::color;
 // The GL branch executes precisely the same original vertex sequence.
 gGL.color4fv(color);gGL.streams.clear();production();
 assert(gGL.streams.size()==original.size());
 for(std::size_t i=0;i<original.size();++i){assert(original[i].mode==gGL.streams[i].mode);assert(original[i].vertices==gGL.streams[i].vertices);}
 LLRender2D::native=true;gGL.color4fv(color);LLRender2D::triangles.clear();production();
 auto expected=expand(original);assert(expected.size()==LLRender2D::triangles.size());
 for(std::size_t i=0;i<expected.size();++i)for(int j=0;j<3;++j)for(int k=0;k<6;++k)
  assert(std::abs(expected[i][j][k]-LLRender2D::triangles[i][j][k])<=2e-5f);
 for(int i=0;i<4;++i)assert(endColor.mV[i]==LLRender2D::color.mV[i]);
 assert((gGL.offset==std::array<float,2>{}));
 LLRender2D::native=false;
}
int main()
{
 LLColor4 inner{{.7f,.2f,.1f,.3f}},outer{{.2f,.3f,.8f,.9f}};
 for(int steps:{3,4,15,60})for(float width:{.5f,1.f,2.75f}){
  native_line_width=width;
  for(bool filled:{false,true})for(float radius:{0.f,1.f,23.5f}){
   compare([&]{Reference::gl_circle_2d(12.5f,-7.25f,radius,steps,filled);},[&]{gl_circle_2d(12.5f,-7.25f,radius,steps,filled);});
   for(auto a:std::array<std::array<float,2>,3>{{{0.f,1.f},{5.f,.2f},{-2.f,2.f}}})
    compare([&]{Reference::gl_arc_2d(-3.f,2.5f,radius,steps,filled,a[0],a[1]);},[&]{gl_arc_2d(-3.f,2.5f,radius,steps,filled,a[0],a[1]);});
  }
  compare([&]{Reference::gl_washer_2d(10.f,5.f,steps,inner,outer);},[&]{gl_washer_2d(10.f,5.f,steps,inner,outer);});
  compare([&]{Reference::gl_washer_segment_2d(10.f,5.f,-.5f,2.f,steps,inner,outer);},[&]{gl_washer_segment_2d(10.f,5.f,-.5f,2.f,steps,inner,outer);});
 }
 compare([]{Reference::gl_corners_2d(-2,9,8,-4,20,.25f);},[]{gl_corners_2d(-2,9,8,-4,20,.25f);});
 compare([]{Reference::gl_rect_2d_simple(7,15);},[]{gl_rect_2d_simple(7,15);});
}
