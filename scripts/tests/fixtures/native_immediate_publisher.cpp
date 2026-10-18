#include "vsimmediategeometry.h"
#include "vsimmediateprimitives.h"
#include "vsresourcebudget.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>
constexpr unsigned U32_MAX=UINT32_MAX;
struct VSUIRenderer{struct Texture{int id;};using Image=std::shared_ptr<const Texture>;};
struct VSWorldContracts{enum{Position=1,Normal=2,UV0=4,Color=16};};
struct LLRender{enum{TRIANGLES=0,TRIANGLE_STRIP,TRIANGLE_FAN,POINTS,LINES,LINE_STRIP,LINE_LOOP};};
struct VSWorldRenderer
{
 VERTEX
 DEPTH
 struct Blend{unsigned source=7,destination=9,sourceAlpha=0,destinationAlpha=9,writeMask=15;};
 struct Geometry{std::vector<Vertex>vertices;std::vector<unsigned>indices;};using Mesh=std::shared_ptr<const Geometry>;
 enum class Topology{Triangles};
 struct Packet{Mesh mesh;std::array<float,4>color{1,1,1,1};bool constantColor=false;bool alpha=false,cullFront=false,fullbright=false,overlay=false,hud=false;std::optional<bool>cull,depthWrite;bool depthTest=true;DepthCompare depthCompare=DepthCompare::LessEqual;std::optional<Blend>blend;std::shared_ptr<const std::array<float,16>>projectionOverride;VSUIRenderer::Image image;float screenPointPixels=0,screenLinePixels=0;};
 bool refuse=false;unsigned uploads=0;std::function<void()>during;
 bool publishMesh(Mesh&mesh,const std::vector<Vertex>&v,const std::vector<unsigned>&i,Topology,unsigned){++uploads;if(during)std::exchange(during,{})();if(refuse)return false;mesh=std::make_shared<Geometry>(Geometry{v,i});return true;}
 static const auto&geometryVertices(const Mesh&m){return m->vertices;}static const auto&geometryIndices(const Mesh&m){return m->indices;}
};
HEADER
CPP
int main()
{
 VSResourceBudget budget(1<<20);VSWorldRenderer renderer;bool current=true;VSWorldPaint paint(renderer,[&](size_t bytes){return budget.reserve(bytes);},[&]{return current;});
 const auto triangle=[](VSImmediateGeometry&stream,float x){stream.begin(0);stream.vertex3f(x,0,0);stream.vertex3f(0,1,0);stream.vertex3f(0,0,1);stream.end();};
 auto& first=paint.begin();{auto blend=first.blend(true);auto depth=first.depth(true,false,0x0206);first.cullFace(0x0404);triangle(first,1);}
 std::vector<VSWorldRenderer::Packet>output;assert(paint.finish(output)&&output.size()==1);auto accepted=output[0].mesh;
 assert(output[0].depthCompare==VSWorldRenderer::DepthCompare::GreaterEqual&&!output[0].depthWrite.value()&&output[0].cullFront);
 assert(output[0].blend->source==7&&output[0].blend->destination==9&&output[0].blend->sourceAlpha==7&&output[0].blend->destinationAlpha==9);
 const auto uploads=renderer.uploads;triangle(paint.begin(),1);output.clear();assert(paint.finish(output)&&renderer.uploads==uploads&&output[0].mesh==accepted);
 renderer.refuse=true;triangle(paint.begin(),2);output.clear();assert(!paint.finish(output)&&output.empty());assert(VSWorldRenderer::geometryVertices(accepted)[0].x==1);
 renderer.refuse=false;auto& lines=paint.begin();lines.color4f(1,0,0,1);lines.setLineWidth(3);lines.begin(4);lines.vertex3f(0,0,0);lines.color4f(0,1,0,.5);lines.vertex3f(2,0,0);lines.end();assert(paint.finish(output)&&output.size()==1);
 auto vertices=VSWorldRenderer::geometryVertices(output[0].mesh);assert(vertices.size()==4&&vertices[0].r==1&&vertices[1].g==1&&vertices[0].nx==2&&vertices[1].a==127.f/255&&output[0].screenLinePixels==3);
 output.clear();auto& points=paint.begin();points.hud(true);points.matrixMode(1);points.scalef(2,3,1);points.matrixMode(0);points.setPointSize(5);points.begin(3);points.vertex3f(1,2,3);points.end();assert(paint.finish(output)&&output[0].hud&&output[0].projectionOverride&&(*output[0].projectionOverride)[0]==2&&output[0].screenPointPixels==5);
 output.clear();renderer.during=[&]{current=false;};triangle(paint.begin(),5);assert(!paint.finish(output)&&output.empty());current=true;
 // A later failure cannot append an earlier partial batch from the same tool.
 auto& multiple=paint.begin();triangle(multiple,1);renderer.refuse=true;triangle(multiple,3);assert(!paint.finish(output)&&output.empty());renderer.refuse=false;
 renderer.refuse=false;output.clear();
 const auto projection=glm::perspective(glm::radians(60.f),1.5f,.1f,512.f);
 const auto camera=glm::lookAt(glm::vec3(3,4,5),glm::vec3(0),glm::vec3(0,0,1));
 auto& uniform=paint.begin(camera,projection);uniform.debugShader();uniform.diffuseColor4f(-.5f,.2f,2.f,.75f);triangle(uniform,1);
 assert((paint.finish(output)&&output[0].constantColor&&output[0].color==std::array<float,4>({0,.2f,2,.75f})));
 const auto expectedProjection=projection*camera;assert(!std::memcmp(output[0].projectionOverride->data(),glm::value_ptr(expectedProjection),16*sizeof(float)));
 const auto baselineUploads=renderer.uploads;output.clear();
 const auto nextCamera=glm::lookAt(glm::vec3(4,5,6),glm::vec3(0),glm::vec3(0,0,1));
 auto& changed=paint.begin(nextCamera,projection);changed.debugShader();changed.diffuseColor4f(.3f,.6f,.7f,.9f);triangle(changed,1);
 assert(paint.finish(output)&&renderer.uploads==baselineUploads); // Camera/uniform change is not a geometry upload.
 auto projectionOwner=output[0].projectionOverride;output.clear();paint.retire();assert(budget.used()>0);projectionOwner.reset();assert(budget.used()==0);
}
