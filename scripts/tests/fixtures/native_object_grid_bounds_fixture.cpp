#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
using U32=unsigned;using S32=int;constexpr int GRID_MODE_WORLD=0,GRID_MODE_LOCAL=1,GRID_MODE_REF_OBJECT=2,SELECT_TYPE_WORLD=0,SELECT_TYPE_ATTACHMENT=1,SELECT_TYPE_HUD=2;constexpr float F32_MAX=1e30f;
struct LLQuaternion{int id=0;void loadIdentity(){id=0;}bool isFinite(){return true;}bool operator==(LLQuaternion b)const{return id==b.id;}};
struct LLVector3{float mV[3]{};static LLVector3 zero;LLVector3()=default;explicit LLVector3(const float*p){std::copy_n(p,3,mV);}LLVector3(float x,float y,float z){mV[0]=x;mV[1]=y;mV[2]=z;}LLVector3 operator+(LLVector3 b)const{return{mV[0]+b.mV[0],mV[1]+b.mV[1],mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 b)const{return{mV[0]-b.mV[0],mV[1]-b.mV[1],mV[2]-b.mV[2]};}LLVector3 operator*(float f)const{return{mV[0]*f,mV[1]*f,mV[2]*f};}void clearVec(){*this={};}void set(const float*p){std::copy_n(p,3,mV);}bool isFinite()const{return std::all_of(mV,mV+3,[](float f){return std::isfinite(f);});}bool operator==(LLVector3 b)const{return std::equal(mV,mV+3,b.mV);}};LLVector3 LLVector3::zero;using LLVector3d=LLVector3;
template<class T>T llmin(T a,T b){return std::min(a,b);}template<class T>T llmax(T a,T b){return std::max(a,b);}void llassert(bool b){assert(b);}
struct LLVector4a{LLVector3 v;explicit LLVector4a(float f=0):v(f,f,f){}void setAdd(LLVector4a a,LLVector4a b){v=a.v+b.v;}void setSub(LLVector4a a,LLVector4a b){v=a.v-b.v;}void mul(float f){v=v*f;}const float*getF32ptr(){return v.mV;}};
void update_min_max(LLVector4a&min,LLVector4a&max,LLVector4a lo){for(int i=0;i<3;++i){min.v.mV[i]=llmin(min.v.mV[i],lo.v.mV[i]);max.v.mV[i]=llmax(max.v.mV[i],lo.v.mV[i]);}}
struct LLBBox{LLVector3 center{4,5,6},extent{2,4,8};LLQuaternion rotation{7};LLVector3 getCenterAgent()const{return center;}LLVector3 getExtentLocal()const{return extent;}LLQuaternion getRotation()const{return rotation;}};
struct Settings{float resolution=3;float getF32(const char*)const{return resolution;}}gSavedSettings;

struct VSWorldRenderer{struct MeshData{std::array<std::array<float,3>,2>bounds;};using Mesh=std::shared_ptr<MeshData>;static auto bounds(const Mesh& mesh){return mesh->bounds;}};
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=1,generation=1;};struct Face{bool published=false;VSWorldRenderer::Mesh mesh;};struct Impl{struct Object{std::vector<Face>faces,admittedFaces;bool hud=false;U32 generation=1;};int host=1,handle=2;bool links=true;std::map<U32,Object>objects;bool linksCurrent(const Object&)const{return links;}bool hudObject(const Object&o)const{return o.hud;}};std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true;bool current(ObjectHandle t)const{if(!live)return false;if(t.host!=mImpl->host){const auto i=mNeighbours.find(t.host);return i!=mNeighbours.end()&&i->second->current(t);}const auto i=mImpl->objects.find(t.local);return i!=mImpl->objects.end()&&i->second.generation==t.generation;}std::optional<std::array<LLVector3,2>>publishedObjectBounds(const ObjectHandle&,const LLVector3d&)const;};
LLVector3d from_region_handle(int handle){return {float(handle)*256,0,0};}
METHOD
int main(){VSWorldScene scene;auto& object=scene.mImpl->objects[1];auto mesh=std::make_shared<VSWorldRenderer::MeshData>();mesh->bounds={{{1,2,3},{4,5,6}}};object.admittedFaces.push_back({true,mesh});auto desired=std::make_shared<VSWorldRenderer::MeshData>();desired->bounds={{{-100,-100,-100},{100,100,100}}};object.faces.push_back({false,desired});
 for(int i=0;i<256;++i){const LLVector3d origin(float(i),0,0);auto bounds=scene.publishedObjectBounds({},origin);assert(bounds&&(*bounds)[0]==LLVector3(513-i,2,3)&&(*bounds)[1]==LLVector3(516-i,5,6));object.hud=true;bounds=scene.publishedObjectBounds({},origin);assert(bounds&&(*bounds)[0]==LLVector3(1,2,3)&&(*bounds)[1]==LLVector3(4,5,6));object.hud=false;}
 assert(!scene.publishedObjectBounds({1,1,2},{}));scene.mImpl->links=false;assert(!scene.publishedObjectBounds({},{}));scene.mImpl->links=true;scene.live=false;assert(!scene.publishedObjectBounds({},{}));scene.live=true;
 scene.mNeighbours[3]=std::make_unique<VSWorldScene>();auto& neighbour=*scene.mNeighbours[3];neighbour.mImpl->host=3;neighbour.mImpl->handle=5;neighbour.mImpl->objects[1]=object;const auto bounds=scene.publishedObjectBounds({3,1,1},{});assert(bounds&&(*bounds)[0]==LLVector3(1281,2,3));
 object.admittedFaces.clear();object.faces[0].published=false;assert(!scene.publishedObjectBounds({},{}));object.faces[0].published=true;assert(scene.publishedObjectBounds({},{}));
}
