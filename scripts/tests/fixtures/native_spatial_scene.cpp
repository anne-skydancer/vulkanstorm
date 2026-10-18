#include "vsspatialindex.h"
#include <map>
#include <set>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
using U32=unsigned;using U64=unsigned long long;using S32=int;constexpr int LL_PCODE_VOLUME=9;
struct LLVector3{float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}explicit LLVector3(const float*p){std::copy_n(p,3,mV);}bool operator==(const LLVector3&o)const{return std::equal(mV,mV+3,o.mV);}LLVector3 operator-(const LLVector3&o)const{return {mV[0]-o.mV[0],mV[1]-o.mV[1],mV[2]-o.mV[2]};}};using LLVector3d=LLVector3;
struct LLQuaternion{int id=0;bool operator==(const LLQuaternion&q)const{return id==q.id;}};
struct LLMatrix4{LLVector3 scale{1,1,1},position;void initScale(const LLVector3&s){scale=s;}void rotate(const LLQuaternion&){}void translate(const LLVector3&p){position=p;}};
LLVector3 operator*(const LLVector3&p,const LLMatrix4&m){return {p.mV[0]*m.scale.mV[0]+m.position.mV[0],p.mV[1]*m.scale.mV[1]+m.position.mV[1],p.mV[2]*m.scale.mV[2]+m.position.mV[2]};}
struct V4{float v[3];const float*getF32ptr()const{return v;}};
struct Face{V4 storage[2]{{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};V4*mExtents=storage;int mNumVertices=8;};
struct LLVolume{Face face;int getNumVolumeFaces()const{return 1;}const Face&getVolumeFace(int)const{return face;}};
struct LLCamera{LLVector3 mAgentFrustum[8];void bounds(float left,float right){for(unsigned i=0;i<8;++i)mAgentFrustum[i]={i&1?right:left,i&2?10.f:-10.f,i&4?10.f:-10.f};}};
LLVector3d from_region_handle(int handle){return {float(handle)*256.f,0,0};}
struct Budget{VSResourceBudget actual{8*1024*1024};int failures=0;auto reserve(std::size_t bytes){if(failures>0){--failures;return std::shared_ptr<const VSResourceBudget::Reservation>();}return actual.reserve(bytes);}std::size_t used()const{return actual.used();}};
struct VSWorldScene{
 struct ObjectHandle{int host;U32 local;U64 generation;};
 struct SelectionCandidates{std::shared_ptr<const VSResourceBudget::Reservation>residency;std::vector<ObjectHandle>objects;};
 struct Budgets{Budget primitiveBytes;};
 struct Impl{
  struct Object{int pcode=LL_PCODE_VOLUME;bool renderable=true,links=true,flexible=false;U64 generation=1,revision=1,spatialEpoch=0,spatialRevision=0;LLVector3 position,scale{1,1,1},spatialPosition,spatialScale;LLQuaternion rotation,spatialRotation;std::shared_ptr<LLVolume>volume=std::make_shared<LLVolume>();const LLVolume*spatialVolume=nullptr;bool spatialFlexible=false;};
  std::map<U32,Object>objects;int host=1,handle=0;bool retired=false;U64 nextRevision=2;std::shared_ptr<Budgets>budgets=std::make_shared<Budgets>();
  std::unique_ptr<VSSpatialIndex>selectionIndex;bool selectionIndexAttempted=false;U64 selectionIndexEpoch=0,selectionIndexRevision=0,selectionRefusedRevision=0;std::size_t selectionRefusedBytes=0;
  bool linksCurrent(const Object&o)const{return o.links;}bool transform(U32 id,LLVector3&p,LLQuaternion&q,std::set<U32>&){auto i=objects.find(id);if(i==objects.end()||!i->second.links)return false;p=i->second.position;q=i->second.rotation;return true;}
  HELPERS
 };
 std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;
 std::optional<ObjectHandle>object(int host,U32 id)const{auto i=mImpl->objects.find(id);if(host!=mImpl->host||mImpl->retired||i==mImpl->objects.end()||!i->second.links)return {};return ObjectHandle{host,id,i->second.generation};}
 std::optional<SelectionCandidates>selectionCandidates(const LLCamera&,const LLVector3d&)const;
};
METHOD
int main(){VSWorldScene scene;scene.mImpl->objects[1].position={1,0,0};scene.mImpl->objects[2].position={1000,0,0};LLCamera camera;camera.bounds(-10,10);auto first=scene.selectionCandidates(camera,{});assert(first&&first->objects.size()==1&&first->objects[0].local==1&&first->objects[0].generation==1);assert(scene.mImpl->selectionIndex->valid());
 auto& moved=scene.mImpl->objects[1];moved.position={1000,0,0};++moved.revision;++moved.generation;++scene.mImpl->nextRevision;auto old=scene.selectionCandidates(camera,{});assert(old&&old->objects.empty());assert(first->objects[0].generation==1);
 camera.bounds(990,1010);auto next=scene.selectionCandidates(camera,{});assert(next&&next->objects.size()==2);auto at=std::find_if(next->objects.begin(),next->objects.end(),[](auto t){return t.local==1;});assert(at!=next->objects.end()&&at->generation==2);
 // A source intake change is visible before any new GPU generation exists.
 moved.scale={100,1,1};++moved.revision;++scene.mImpl->nextRevision;camera.bounds(949,951);auto wide=scene.selectionCandidates(camera,{});assert(wide&&wide->objects.size()==1&&wide->objects[0].local==1);
 // Retired/reused index keys never supply an old incarnation.
 scene.mImpl->selectionIndex->erase(1);scene.mImpl->objects.erase(1);++scene.mImpl->nextRevision;auto gone=scene.selectionCandidates(camera,{});assert(gone&&gone->objects.empty());
 // Acceleration refusal leaves every authoritative CPU source available.
 scene.mImpl->objects[3].position={20,0,0};++scene.mImpl->nextRevision;scene.mImpl->budgets->primitiveBytes.failures=1;camera.bounds(-10,30);auto fallback=scene.selectionCandidates(camera,{});assert(fallback&&fallback->objects.size()==2);assert(!scene.mImpl->selectionIndex->valid());
 auto neighbour=std::make_unique<VSWorldScene>();neighbour->mImpl->host=3;neighbour->mImpl->handle=2;neighbour->mImpl->objects[1].position={1,0,0};scene.mNeighbours[3]=std::move(neighbour);camera.bounds(500,530);auto across=scene.selectionCandidates(camera,{});assert(across);assert(std::any_of(across->objects.begin(),across->objects.end(),[](auto t){return t.host==3&&t.local==1;}));
 scene.mImpl->retired=true;assert(!scene.selectionCandidates(camera,{}));
}
