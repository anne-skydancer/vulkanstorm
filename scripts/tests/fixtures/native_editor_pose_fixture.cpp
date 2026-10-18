#include <array>
#include <map>
#include <memory>
#include <optional>
#include <algorithm>
#include <cassert>
using U32=unsigned;using S32=int;using F32=float;constexpr int LL_PCODE_VOLUME=9,LL_PCODE_LEGACY_AVATAR=47;
struct LLQuaternion;
struct LLVector3{float x=0,y=0,z=0;LLVector3 operator+(LLVector3 b)const{return {x+b.x,y+b.y,z+b.z};}LLVector3 operator*(const LLQuaternion&)const;void set(const float*p){x=p[0];y=p[1];z=p[2];}float mV[3]{};bool operator==(LLVector3 b)const{return x==b.x&&y==b.y&&z==b.z;}};
struct LLQuaternion{float x=0,y=0,z=0,w=1;LLQuaternion operator*(LLQuaternion b)const{return {w*b.x+x*b.w+y*b.z-z*b.y,w*b.y-x*b.z+y*b.w+z*b.x,w*b.z+x*b.y-y*b.x+z*b.w,w*b.w-x*b.x-y*b.y-z*b.z};}void set(int value){x=float(value);}bool operator==(LLQuaternion b)const{return x==b.x&&y==b.y&&z==b.z&&w==b.w;}};
LLVector3 LLVector3::operator*(const LLQuaternion&q)const{LLQuaternion v{x,y,z,0},inverse{-q.x,-q.y,-q.z,q.w};auto rotated=inverse*v*q;return {rotated.x,rotated.y,rotated.z};}
using LLVector3d=LLVector3;

struct QRow{float value=1;QRow getLength3()const{return *this;}float getF32()const{return value;}};
struct Translation{float xyz[3]{1,2,3};const float*getF32ptr()const{return xyz;}};
struct LLMatrix4a{QRow mMatrix[3];int id=0;void loadu(const float*p){id=int(*p);}Translation getTranslation()const{return {};}int asMatrix4()const{return id;}};
void matMul(LLMatrix4a a,LLMatrix4a b,LLMatrix4a&c){c.id=a.id*100+b.id;}
struct RenderMatrix{float mMatrix[1][1]{{7}};};
struct Focus{RenderMatrix renderMatrix,assetToAgentMatrix;LLVector3 renderPosition{31,32,33};LLQuaternion renderRotation{.1f,.2f,.3f,.4f};bool current()const{return true;}};
struct GLTF{bool allowed=true;bool nodeMatrix(int n,LLMatrix4a&m){if(!allowed||n<0||n>2)return false;m.id=n+1;return true;}};
struct LLViewerObject{LLViewerObject*parent=nullptr;LLVector3 pos;LLQuaternion rot;mutable LLVector3 mPositionRegion;bool isRoot()const{return !parent;}const LLVector3&getPosition()const{return pos;}const LLQuaternion&getRotation()const{return rot;}LLViewerObject*getParent()const{return parent;}const LLVector3&getPositionRegion()const;const LLQuaternion getRotationRegion()const;};
REFERENCE_POSITION
REFERENCE_ROTATION
struct LLXform{ };
struct Motion{LLVector3 p;LLQuaternion q;const LLVector3&position()const{return p;}const LLQuaternion&rotation()const{return q;}};
struct VSWorldScene{struct ObjectHandle{int host=1;U32 local=1,generation=1;};struct LocalTransform{LLVector3 position,scale;LLQuaternion rotation;};struct EditorTransform{LLVector3 localPosition,scale;LLVector3d globalPosition;LLQuaternion rotation;};struct Impl{struct Object{U32 parent=0,parentGeneration=1,generation=1;bool parentRetired=false;LLVector3 scale{1,2,3};Motion motion;std::shared_ptr<GLTF>gltf;};int host=1,handle=0;std::map<U32,Object>objects;};std::unique_ptr<Impl>mImpl=std::make_unique<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;bool live=true,attachment=false;bool current(ObjectHandle t)const{return live&&mImpl->objects.count(t.local)&&mImpl->objects.at(t.local).generation==t.generation;}std::optional<LocalTransform>objectLocalTransform(ObjectHandle t)const{const auto&o=mImpl->objects.at(t.local);return LocalTransform{o.motion.p,o.scale,o.motion.q};}std::optional<int>objectAttachment(ObjectHandle)const{return attachment?std::optional{1}:std::nullopt;}std::optional<Focus>focusObject(ObjectHandle,const LLVector3d&)const{return Focus{};}std::optional<EditorTransform>editorTransform(const ObjectHandle&,S32,const LLVector3d&,bool renderedAttachment=true)const;};
LLVector3d from_region_handle(int h){return{float(h),0,0};}
METHOD
int main(){for(unsigned seed=0;seed<2048;++seed){VSWorldScene world;std::array<LLViewerObject,12>old;for(unsigned i=0;i<old.size();++i){auto&o=old[i];o.parent=i?&old[i-1]:nullptr;o.pos={float(i),float(seed%7),float(seed%11)};o.rot={float((seed+i)%5)*.1f,float(i%3)*.1f,.25f,.75f};auto&n=world.mImpl->objects[i+1];n.parent=i;n.motion.p=o.pos;n.motion.q=o.rot;}for(unsigned i=0;i<old.size();++i){const auto result=world.editorTransform({1,i+1,1},-1,{});assert(result&&result->localPosition==old[i].pos&&result->globalPosition==old[i].getPositionRegion()&&result->rotation==old[i].getRotationRegion());}world.mImpl->objects[1].gltf=std::make_shared<GLTF>();auto gltf=world.editorTransform({},1,{});assert(gltf&&gltf->rotation.x==207&&gltf->localPosition==LLVector3({1,2,3})&&gltf->globalPosition==LLVector3({1,2,3}));assert(!world.editorTransform({},7,{}));world.attachment=true;auto attached=world.editorTransform({},-1,{});assert(attached&&attached->globalPosition==LLVector3({31,32,33})&&attached->rotation==Focus{}.renderRotation);world.live=false;assert(!world.editorTransform({},-1,{}));}}
