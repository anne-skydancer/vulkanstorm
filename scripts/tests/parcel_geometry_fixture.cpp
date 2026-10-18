#include "vsparcelgeometry.h"
#include <cassert>
#include <vector>
#include <limits>
using F32=float;using S32=int;using U8=unsigned char;
constexpr int VX=0,VY=1,WEST_MASK=1,SOUTH_MASK=2,EAST_MASK=4,NORTH_MASK=8;
template<class T>T llmax(T a,T b){return std::max(a,b);}int llfloor(float a){return int(std::floor(a));}
struct LLVector3{float mV[3];LLVector3(float x,float y,float z):mV{x,y,z}{}};
int receivedLimit=1000000;
float sample(int x,int y){return x>=receivedLimit?0.f:.02f*x*x+.03f*y*y+.1f*x*y;}
struct LLSurface{float mMetersPerGrid=1,mMetersPerEdge=256;unsigned mGridsPerEdge=257;float getZ(int x,int y)const{return sample(x,y);}float resolveHeightRegion(float,float)const;};
TERRAIN_REFERENCE

struct Land{float resolveHeightRegion(LLVector3 v){return .01f*v.mV[0]*v.mV[0]+.03f*v.mV[1];}};
struct LLViewerRegion{float width=256;Land land;LLVector3 origin{0,0,0};float getWidth(){return width;}Land& getLand(){return land;}LLVector3 getOriginAgent(){return origin;}};
struct GL {std::vector<VSParcelGeometry::Vertex> vertices;float u=0,v=0;bool textured=false;void vertex3f(float x,float y,float z){vertices.push_back({x,y,z,u,v,textured});}void texCoord2f(float x,float y){u=x;v=y;textured=true;}}gGL;
struct LLViewerParcelMgr{void renderOneSegment(float,float,float,float,float,U8,LLViewerRegion*,bool=false);};
SEGMENT_REFERENCE

int main(){
 {
  auto line=VSParcelGeometry::screenLine({0,-1,0,1},{0,1,0,1},2,100,200);
  assert(line);
  float minimum=1,maximum=-1;
  for(const auto& vertex:*line){minimum=std::min(minimum,vertex.x);maximum=std::max(maximum,vertex.x);assert(vertex.z==0);}
  assert(std::abs((maximum-minimum)*50.f-2.f)<1e-5f);
  assert(!VSParcelGeometry::screenLine({2,0,0,1},{2,1,0,1},2,100,200));
  assert(!VSParcelGeometry::screenLine({0,0,0,1},{0,0,0,1},2,100,200));
  assert(!VSParcelGeometry::screenLine({0,0,0,1},{1,0,0,1},2,0,200));
  assert(!VSParcelGeometry::screenLine({std::numeric_limits<float>::quiet_NaN(),0,0,1},{1,0,0,1},2,100,200));
  line=VSParcelGeometry::screenLine({0,-1,-2,1},{0,1,0,1},2,100,200);
  assert(line);
  for(const auto& vertex:*line)assert(vertex.z>=-1 && vertex.z<=0 && vertex.y>=0);
  line=VSParcelGeometry::screenLine({0,-1,0,2},{0,1,0,2},2,100,200);
  assert(line);
  minimum=1;maximum=-1;
  for(const auto& vertex:*line){minimum=std::min(minimum,vertex.x);maximum=std::max(maximum,vertex.x);}
  assert(std::abs((maximum-minimum)*50.f-2.f)<1e-5f);
 }
 LLViewerRegion region;LLViewerParcelMgr manager;
 for(float origin:{-4096.f,0.f,8192.f})for(float x:{0.f,4.f,252.f,256.f})for(float y:{0.f,32.f,255.99f,256.f})
 for(float h:{.666f,1.f,50.f,5000.f})for(bool absolute:{false,true})for(int d=0;d<4;++d){
  region.origin={origin,-origin,0};gGL=GL{};
  const int masks[]={WEST_MASK,SOUTH_MASK,EAST_MASK,NORTH_MASK};
  manager.renderOneSegment(x,y,x+4,y+4,h,masks[d],&region,absolute);
  auto vertices=VSParcelGeometry::segment(x,y,x+4,y+4,h,VSParcelGeometry::Direction(d),256,256,origin,-origin,absolute,[&](float px,float py){return region.land.resolveHeightRegion({px,py,0});});
  assert(gGL.vertices.size()==6);
  for(int i=0;i<6;++i){auto a=gGL.vertices[i],b=vertices[i];assert(a.x==b.x+origin && a.y==b.y-origin && a.z==b.z && a.textured==b.textured);if(a.textured)assert(a.u==b.u && a.v==b.v);}
 }
 LLSurface surface;
 // Execute the actual reference interpolator with an unreceived neighboring
 // patch: each unknown corner is zero, not the entire partly received cell.
 receivedLimit=16;
 for(float x:{15.f,15.1f,15.5f,15.9f,16.f})for(float y:{0.f,.1f,.5f,.9f,1.f})
   assert(surface.resolveHeightRegion(x,y)==VSParcelGeometry::terrainHeight(x,y,1,256,256,sample));
 receivedLimit=1000000;
 for(float spacing:{.5f,1.f,2.f}){
  surface.mMetersPerGrid=spacing;surface.mMetersPerEdge=256*spacing;
  for(float x:{-1.f,0.f,.1f,.5f,.9f,1.f,1.1f,17.8f,254.99f,255.f,255.99f,256.f,257.f,std::numeric_limits<float>::quiet_NaN()})
  for(float y:{-1.f,0.f,.1f,.5f,.9f,1.f,1.1f,17.8f,254.99f,255.f,255.99f,256.f,257.f})
    assert(surface.resolveHeightRegion(x*spacing,y*spacing)==VSParcelGeometry::terrainHeight(x*spacing,y*spacing,spacing,256,256,sample));
 }
}
