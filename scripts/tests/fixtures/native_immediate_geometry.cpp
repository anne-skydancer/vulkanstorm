#include "vsimmediategeometry.h"
#include "vsimmediateprimitives.h"
#include "vsrenderprimitivegeometry.h"
#include "vsselectionlightgeometry.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <iostream>
using F32=float;using S32=int;constexpr float F_PI=3.14159265358979323846f,F_TWO_PI=6.28318530717958647692f;
struct LLVector3{float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}};
POLAR_REFERENCE
struct Capture{std::vector<std::pair<unsigned,std::vector<std::array<float,3>>>>batches;void begin(unsigned mode){batches.push_back({mode,{}});}void end(){}void vertex3f(float x,float y,float z){batches.back().second.push_back({x,y,z});}void vertex3fv(const float*p){vertex3f(p[0],p[1],p[2]);}}gGL;
struct LLRender{static constexpr unsigned TRIANGLE_FAN=2;};struct LLCone{void render(S32);};
CONE_REFERENCE
using GLfloat=float;
struct ReferenceMatrix{glm::mat4 mMatrix[2][1]{{glm::mat4(1)},{glm::mat4(1)}};unsigned mMatrixMode=0,mMatIdx[2]{},mMatHash[2]{};void flush(){}void rotatef(const GLfloat&,const GLfloat&,const GLfloat&,const GLfloat&);void translatef(const GLfloat&,const GLfloat&,const GLfloat&);void scalef(const GLfloat&,const GLfloat&,const GLfloat&);void multMatrix(const GLfloat*);};
MATRIX_REFERENCE
using GLubyte=unsigned char;template<class T>T llclamp(T value,T low,T high){return std::clamp(value,low,high);}
struct ReferenceVertexColor{glm::vec4 color;void color4f(const GLfloat&,const GLfloat&,const GLfloat&,const GLfloat&);void color4ub(GLubyte r,GLubyte g,GLubyte b,GLubyte a){color=glm::vec4(r,g,b,a)/255.f;}};
COLOR_REFERENCE
int main()
{
 for(unsigned lat=0;lat<=20;++lat)for(unsigned lon=0;lon<=30;++lon)
 {
  const auto expected=polar_to_cart(float(lat)/20,float(lon)/30);const auto actual=vs_sphere_point(float(lat)/20,float(lon)/30);
  assert(!std::memcmp(expected.mV,actual.data(),sizeof(expected.mV)));
 }
 for(int sides=0;sides<65;++sides)
 {
  gGL.batches.clear();LLCone{}.render(sides);Capture native;vs_render_cone_geometry(native,sides);assert(gGL.batches==native.batches);
 }
 Capture sphere;vs_render_sphere_geometry(sphere);assert(sphere.batches.size()==1&&sphere.batches[0].second.size()==3600);
 std::size_t used=0,limit=1<<20;
 struct Charge{std::size_t&used;std::size_t bytes;Charge(std::size_t&u,std::size_t b):used(u),bytes(b){used+=bytes;}~Charge(){used-=bytes;}};
 const auto reserve=[&](std::size_t bytes)->std::shared_ptr<const void>{if(bytes>limit-used)return {};return std::make_shared<Charge>(used,bytes);};
 std::vector<VSImmediateGeometry::Batch>batches;
 for(unsigned trial=0;trial<1000;++trial)
 {
  batches.clear();ReferenceMatrix reference;
  VSImmediateGeometry paint(reserve,[&](auto&& batch){batches.push_back(std::move(batch));return true;});
  const float x=float(int(trial%17)-8)*.7f,y=float(trial%13)*.3f,z=float(trial%11)-2;
  reference.translatef(x,y,z);paint.translatef(x,y,z);
  const float angle=float(trial)*1.7f,ax=x+.01f,ay=y+.1f,az=z+.3f;
  reference.rotatef(angle,ax,ay,az);paint.rotatef(angle,ax,ay,az);
  reference.scalef(x+.2f,y+.5f,z-.4f);paint.scalef(x+.2f,y+.5f,z-.4f);
  const auto extra=glm::rotate(glm::mat4(1),glm::radians(13.5f),glm::vec3(.2f,.4f,1));reference.multMatrix(glm::value_ptr(extra));paint.multMatrix(glm::value_ptr(extra));
  paint.begin(3);paint.vertex3f(1.3f,-2.5f,.7f);paint.end();paint.flush();assert(paint.good()&&batches.size()==1);
  const auto expected=reference.mMatrix[0][0]*glm::vec4(1.3f,-2.5f,.7f,1);assert(batches[0].vertices[0].position==glm::vec3(expected));
 }
 batches.clear();assert(used==0);
 for(unsigned trial=0;trial<1000;++trial)
 {
  batches.clear();ReferenceVertexColor reference;
  VSImmediateGeometry paint(reserve,[&](auto&& batch){batches.push_back(std::move(batch));return true;});
  const float r=float(trial)/500-.3f,g=float(trial%127)/126,b=-.1f,a=float(trial%19)/19;
  reference.color4f(r,g,b,a);paint.color4f(r,g,b,a);paint.begin(3);paint.vertex3f(0,0,0);paint.end();paint.flush();
  assert(paint.good()&&batches[0].vertices[0].color==reference.color);
 }
 batches.clear();assert(used==0);
 {
  VSImmediateGeometry paint(reserve,[&](auto&& b){batches.push_back(std::move(b));return true;});
  paint.translatef(4,5,6);paint.pushMatrix();paint.scalef(2,3,4);paint.color4f(.1f,.2f,.3f,.4f);
  {auto depth=paint.depth(true,false,0x0206);auto cull=paint.cull(true);paint.cullFace(0x0404);auto blend=paint.blend(true);
   paint.begin(VSImmediateGeometry::Triangles);paint.vertex3f(1,0,0);paint.vertex3f(0,1,0);paint.vertex3f(0,0,1);paint.end();}
  paint.popMatrix();paint.begin(VSImmediateGeometry::Points);paint.vertex3f(1,2,3);paint.end();paint.flush();
  assert(paint.good()&&batches.size()==2);
  assert(batches[0].vertices[0].position==glm::vec3(6,5,6)&&batches[0].vertices[1].position==glm::vec3(4,8,6));
  assert(batches[0].state.depthCompare==0x0206&&!batches[0].state.depthWrite&&batches[0].state.cullFront&&batches[0].state.blend);
  assert(batches[1].vertices[0].position==glm::vec3(5,7,9)&&batches[1].state.depthWrite&&batches[1].state.cullFront&&!batches[1].state.blend);
  const auto held=used;assert(held>0);batches.clear();assert(used<held);
  const float plane[4]{1,0,0,0};paint.loadIdentity();
  {auto clip=paint.clipPlane(0,plane);paint.begin(VSImmediateGeometry::Triangles);paint.color4f(1,0,0,1);paint.vertex3f(-1,0,0);paint.color4f(0,1,0,1);paint.vertex3f(1,0,0);paint.vertex3f(1,2,0);paint.end();}
  unsigned triangles=0;assert(vs_immediate_primitives(batches.back(),[&](const auto&a,const auto&b,const auto&c,unsigned kind){assert(kind==3&&a.position.x>=0&&b.position.x>=0&&c.position.x>=0);++triangles;return true;}));assert(triangles==2);
  batches.clear();paint.setLineWidth(2);paint.begin(VSImmediateGeometry::LineLoop);paint.vertex3f(0,0,0);paint.vertex3f(1,0,0);paint.vertex3f(0,1,0);paint.end();
  unsigned lines=0;assert(vs_immediate_primitives(batches.back(),[&](const auto&,const auto&,const auto&,unsigned kind){assert(kind==2);++lines;return true;}));assert(lines==3&&batches.back().state.linePixels==2);
  batches.clear();paint.begin(VSImmediateGeometry::TriangleStrip);for(unsigned i=0;i<5;++i)paint.vertex3f(float(i),0,0);paint.end();
  std::vector<std::array<float,3>>strip;vs_immediate_primitives(batches.back(),[&](const auto&a,const auto&b,const auto&c,unsigned){strip.push_back({a.position.x,b.position.x,c.position.x});return true;});assert((strip==std::vector<std::array<float,3>>{{0,1,2},{2,1,3},{2,3,4}}));
  batches.clear();paint.loadIdentity();paint.cullFace(0x0405);
  {auto depth=paint.depth(true,false);auto cull=paint.cull(true);auto blend=paint.blend(true);
   const float center[3]={1,2,3},color[4]={.2f,.3f,.4f,.5f};vs_selected_light_geometry(paint,center,2,color);}
  assert(batches.size()==2&&batches[0].vertices.size()==3600&&batches[1].vertices.size()==3600);
  assert(!batches[0].state.cullFront&&batches[1].state.cullFront&&batches[0].state.cull&&batches[1].state.cull);
  assert(batches[0].state.blend&&!batches[0].state.depthWrite&&batches[0].vertices[0].color==glm::vec4(51.f/255,76.f/255,102.f/255,127.f/255));
  for(unsigned i=0;i<3600;++i){assert(batches[0].vertices[i].position==batches[1].vertices[i].position);assert(batches[0].vertices[i].position==glm::vec3(sphere.batches[0].second[i][0]*2+1,sphere.batches[0].second[i][1]*2+2,sphere.batches[0].second[i][2]*2+3));}
 }
 assert(used>0);batches.clear();assert(used==0);
 {
  VSImmediateGeometry refused(reserve,[](auto&&){assert(false);return true;});limit=used;
  refused.begin(VSImmediateGeometry::Triangles);refused.vertex3f(0,0,0);refused.end();assert(!refused.good());
 }
 assert(used==0);
 {limit=1<<20;VSImmediateGeometry invalid(reserve,[](auto&&){return true;});invalid.setLineWidth(std::numeric_limits<float>::quiet_NaN());assert(!invalid.good());}
 assert(used==0);
}
