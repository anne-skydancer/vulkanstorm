#include <array>
#include <map>
#include <memory>
#include <optional>
#include <algorithm>
#include <cassert>
using U32=unsigned;constexpr int LL_PCODE_VOLUME=9,LL_PCODE_LEGACY_AVATAR=47;
struct LLQuaternion;
struct LLVector3{float x=0,y=0,z=0;LLVector3 operator+(LLVector3 b)const{return {x+b.x,y+b.y,z+b.z};}LLVector3 operator*(const LLQuaternion&)const;bool operator==(LLVector3 b)const{return x==b.x&&y==b.y&&z==b.z;}};
struct LLQuaternion{float x=0,y=0,z=0,w=1;LLQuaternion operator*(LLQuaternion b)const{return {w*b.x+x*b.w+y*b.z-z*b.y,w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w,w*b.w-x*b.x-y*b.y-z*b.z};}bool operator==(LLQuaternion b)const{return x==b.x&&y==b.y&&z==b.z&&w==b.w;}};
LLVector3 LLVector3::operator*(const LLQuaternion&q)const{LLQuaternion v{x,y,z,0},inverse{-q.x,-q.y,-q.z,q.w};auto rotated=inverse*v*q;return {rotated.x,rotated.y,rotated.z};}
class LLViewerObject;using LLXform=LLViewerObject;
class LLViewerObject{public:LLViewerObject*mParent=nullptr;int pcode=1;LLVector3 pos,scale;LLQuaternion rotation;virtual~LLViewerObject()=default;virtual bool isRootEdit()const{return !mParent;}bool isAvatar(){return pcode==47;}LLViewerObject*getParent()const{return mParent;}const LLVector3&getPosition()const{return pos;}const LLQuaternion&getRotation()const{return rotation;}const LLVector3 getPositionEdit()const;const LLQuaternion getRotationEdit()const;};
class LLVOVolume:public LLViewerObject{public:bool isRootEdit()const override;};
REFERENCE_POSITION
REFERENCE_ROTATION
REFERENCE_ROOT
struct Motion{LLVector3 p;LLQuaternion q;const LLVector3&position()const{return p;}const LLQuaternion&rotation()const{return q;}};
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=1,generation=1;};struct LocalTransform{LLVector3 position,scale;LLQuaternion rotation;};struct Impl{struct Object{U32 parent=0,parentGeneration=1,generation=1;int pcode=1;bool parentRetired=false;LLVector3 position,scale;LLQuaternion rotation;Motion motion;};int host=1;std::map<U32,Object>objects;};std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true;bool current(ObjectHandle t)const{return live&&mImpl->objects.count(t.local)&&mImpl->objects.at(t.local).generation==t.generation;}std::optional<bool>objectIsRootEdit(const ObjectHandle&)const;std::optional<LocalTransform>objectEditTransform(const ObjectHandle&)const;};
ROOT_QUERY
EDIT_QUERY
int main(){for(unsigned seed=0;seed<2048;++seed){VSWorldScene world;std::array<std::unique_ptr<LLViewerObject>,12>objects;for(unsigned i=0;i<objects.size();++i){int code=((seed>>(i%10))&1)?9:((seed+i)%3?47:1);objects[i]=code==9?std::unique_ptr<LLViewerObject>{new LLVOVolume}:std::make_unique<LLViewerObject>();auto&o=*objects[i];o.pcode=code;o.mParent=i?objects[i-1].get():nullptr;o.pos={float(i),float(seed%5),float((seed+i)%7)};o.scale={1,2,3};o.rotation={float((i+seed)%3)*.25f,float(i%2)*.25f,0,.75f};auto&native=world.mImpl->objects[i+1];native.parent=i;native.pcode=code;native.position={-99,-99,-99};native.scale=o.scale;native.rotation={0,0,0,1};native.motion.p=o.pos;native.motion.q=o.rotation;}for(unsigned i=0;i<objects.size();++i){auto q=world.objectIsRootEdit({1,i+1,1});assert(q&&*q==objects[i]->isRootEdit());auto t=world.objectEditTransform({1,i+1,1});assert(t&&t->position==objects[i]->getPositionEdit()&&t->rotation==objects[i]->getRotationEdit()&&t->scale==objects[i]->scale);}world.mImpl->objects[1].pcode=1;world.mImpl->objects[1].parent=12;for(auto&[id,o]:world.mImpl->objects)o.pcode=1;assert(!world.objectEditTransform({1,12,1}));world.mImpl->objects[12].parentRetired=true;assert(!world.objectEditTransform({1,12,1})&&!world.objectIsRootEdit({1,12,1}));world.live=false;assert(!world.objectEditTransform({1,1,1}));}}
