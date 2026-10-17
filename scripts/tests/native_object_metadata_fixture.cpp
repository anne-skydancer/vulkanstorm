#include "vsresourcebudget.h"
#include <cassert>
#include <map>
#include <set>
#include <vector>
#include <array>
#include <optional>
#include <string>
#include <iterator>
using U64=unsigned long long;using U32=unsigned;using U8=unsigned char;using LLHost=int;using LLUUID=int;
constexpr U8 LL_PCODE_LEGACY_AVATAR=47;
struct LLVector3 {float x=0,y=0,z=0;};
struct LLVector3d {double x=0,y=0,z=0;LLVector3d()=default;LLVector3d(LLVector3 v):x(v.x),y(v.y),z(v.z){};LLVector3d(double a,double b,double c):x(a),y(b),z(c){};LLVector3d operator+(const LLVector3d& v)const{return {x+v.x,y+v.y,z+v.z};}};
struct LLQuaternion{};
LLVector3d from_region_handle(U64 handle){return {double(handle),double(handle),0};}
struct VSWorldScene {
 struct ObjectHandle {int host=10,id=1;U32 local=1;U64 region=4,generation=8;int account=9;U64 accountGeneration=2;};
 struct ObjectSnapshot {ObjectHandle object;int owner=0;bool ownerKnown=false;std::string nameValues,mediaVersion;U32 updateFlags=0,rootUpdateFlags=0;U8 pcode=0;LLVector3d position;LLVector3 scale;bool attachment=false;};
 struct Budget {VSResourceBudget terrainBytes{100000};};
 struct Impl {
  struct Object {int id=1;U32 parent=0;U8 pcode=9;LLVector3 position{1,2,3},scale{2,3,4};int reportedOwner=0;bool reportedOwnerKnown=false;std::string nameValues="AttachItemID STRING RW SV asset",mediaVersion="media-version";U32 updateFlags=128;bool linked=true;};
  bool retired=false;int host=10;U64 handle=256;std::map<U32,Object> objects;std::shared_ptr<Budget> budgets=std::make_shared<Budget>();
  bool linksCurrent(const Object& o)const{return o.linked;}
  bool transform(U32 local,LLVector3& out,LLQuaternion&,std::set<U32>&)const{auto it=objects.find(local);if(it==objects.end()||!it->second.linked)return false;out=it->second.position;return true;}
 };
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>> mNeighbours;
 std::optional<ObjectHandle> object(int host,U32 local)const{if(host!=mImpl->host || !mImpl->objects.count(local))return {};ObjectHandle h;h.host=host;h.local=local;h.id=mImpl->objects.at(local).id;return h;}
 bool current(const ObjectHandle& h)const{if(mImpl->retired || h.account!=9 || h.accountGeneration!=2 || h.region!=4 || h.generation!=8)return false;if(h.host!=mImpl->host){auto peer=mNeighbours.find(h.host);return peer!=mNeighbours.end()&&peer->second->current(h);}auto i=mImpl->objects.find(h.local);return i!=mImpl->objects.end() && i->second.id==h.id && i->second.linked;}
 std::shared_ptr<const VSResourceBudget::Reservation> reserveSpatialBytes(std::size_t);
 std::optional<ObjectSnapshot> objectSnapshot(const ObjectHandle&)const;
 std::vector<ObjectSnapshot> objectSnapshots()const;
};
METHODS
int main(){
 VSWorldScene world;world.mImpl->objects[1]={};auto handle=*world.object(10,1);auto a=world.objectSnapshot(handle);assert(a);
 assert(a->updateFlags==128 && a->rootUpdateFlags==128 && !a->ownerKnown && a->owner==0);
 assert(a->position.x==257 && a->scale.z==4 && !a->attachment);
 world.mImpl->objects.at(1).updateFlags=256;world.mImpl->objects.at(1).nameValues="changed";
 assert(a->updateFlags==128 && a->nameValues!="changed");auto b=world.objectSnapshot(handle);assert(b->updateFlags==256 && b->nameValues=="changed");
 world.mImpl->objects[2]={};world.mImpl->objects.at(2).id=2;world.mImpl->objects.at(2).parent=1;
 auto child=*world.object(10,2);assert(world.objectSnapshot(child)->rootUpdateFlags==256);
 world.mImpl->objects[3]={};world.mImpl->objects.at(3).id=3;world.mImpl->objects.at(3).pcode=LL_PCODE_LEGACY_AVATAR;
 world.mImpl->objects.at(1).parent=3;assert(world.objectSnapshot(child)->attachment);
 auto stale=handle;stale.accountGeneration=1;assert(!world.objectSnapshot(stale));stale=handle;stale.id=100;assert(!world.objectSnapshot(stale));
 world.mImpl->objects.at(1).linked=false;assert(!world.objectSnapshot(handle));world.mImpl->objects.at(1).linked=true;
 auto neighbour=std::make_unique<VSWorldScene>();neighbour->mImpl->host=11;neighbour->mImpl->objects[4]={};neighbour->mImpl->objects.at(4).id=4;
 auto remote=*neighbour->object(11,4);world.mNeighbours[11]=std::move(neighbour);assert(world.objectSnapshot(remote));assert(world.objectSnapshots().size()==4);
 world.mNeighbours.clear();assert(!world.objectSnapshot(remote));world.mImpl->retired=true;assert(!world.objectSnapshot(handle)&&world.objectSnapshots().empty());assert(!world.reserveSpatialBytes(1024));
}
