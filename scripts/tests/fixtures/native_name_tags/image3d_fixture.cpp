#include <array>
#include <vector>
#include <random>
#include <cassert>
#include <cmath>
#include <cstring>
using F32=float;
struct LLVector3{float mV[3];LLVector3(float x=0,float y=0,float z=0):mV{x,y,z}{}};
LLVector3 operator*(float v,const LLVector3& p){return {v*p.mV[0],v*p.mV[1],v*p.mV[2]};}
LLVector3 operator+(const LLVector3& a,const LLVector3& b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}
struct LLRectf{float mLeft,mTop,mRight,mBottom;};
struct Vertex{std::array<float,3> point;std::array<float,2> uv;};
struct Sink{
 std::vector<Vertex> vertices;std::array<float,2> uv;unsigned beginCount=0,endCount=0;
 void begin(int){++beginCount;vertices.clear();}void end(){++endCount;}
 void texCoord2f(float u,float v){uv={u,v};}
 void vertex3f(float x,float y,float z){vertices.push_back({{x,y,z},uv});}
 void vertex3fv(const float* p){vertex3f(p[0],p[1],p[2]);}
}gGL;
struct LLRender{enum{TRIANGLES};};
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_UI
REFERENCE
CURRENT
int main(){
 std::mt19937 random(0x9a144e);std::uniform_real_distribution<float> v(-10.f,10.f),uv(0.f,1.f);
 for(unsigned test=0;test<20000;++test){
  const LLRectf clip{uv(random),uv(random),uv(random),uv(random)},center{uv(random),uv(random),uv(random),uv(random)},draw{uv(random),uv(random),uv(random),uv(random)};
  const LLVector3 width(v(random),v(random),v(random)),height(v(random),v(random),v(random));
  gl_segmented_rect_3d_tex(clip,center,draw,width,height);Sink actual;
  vs_segmented_image_3d(actual,clip,center,draw,width,height);
  assert(gGL.vertices.size()==54&&actual.vertices.size()==54);
  for(unsigned i=0;i<54;++i){
   assert(gGL.vertices[i].uv==actual.vertices[i].uv);
   for(unsigned axis=0;axis<3;++axis)assert(gGL.vertices[i].point[axis]==actual.vertices[i].point[axis]);
  }
 }
 assert(gGL.beginCount==20000&&gGL.endCount==20000);
}
