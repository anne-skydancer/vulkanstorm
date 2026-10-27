#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <random>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <set>
#include <functional>
#include <optional>
#include <array>
#include <array>
using F32=float;using U32=unsigned;using S32=int;
constexpr int VX=0,VY=1,VZ=2;constexpr float F32_MAX=std::numeric_limits<float>::max();
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}float llabs(float v){return std::abs(v);}
struct LLQuaternion{float a=0;};LLQuaternion operator~(LLQuaternion q){return {-q.a};}
struct LLVector3{static const LLVector3 zero;LLVector3(const float*v):mV{v[0],v[1],v[2]}{};float mV[3]{};void scaleVec(LLVector3 b){for(int n=0;n<3;n++)mV[n]*=b.mV[n];}LLVector3 scaledVec(LLVector3 b)const{b.scaleVec(*this);return b;}LLVector3()=default;LLVector3(float a,float b,float c):mV{a,b,c}{};void setVec(LLVector3 v){*this=v;}void clamp(float a,float b){for(float&v:mV)v=std::clamp(v,a,b);}void abs(){for(float&v:mV)v=std::abs(v);}float normalize(){float n=std::sqrt(mV[0]*mV[0]+mV[1]*mV[1]+mV[2]*mV[2]);if(n>0)for(float&v:mV)v/=n;return n;}void rotVec(LLQuaternion q){float a=mV[0],b=mV[1];mV[0]=a*std::cos(q.a)-b*std::sin(q.a);mV[1]=a*std::sin(q.a)+b*std::cos(q.a);}LLVector3&operator-=(LLVector3 b){for(int n=0;n<3;n++)mV[n]-=b.mV[n];return *this;}};
LLVector3 operator+(LLVector3 a,LLVector3 b){return {a.mV[0]+b.mV[0],a.mV[1]+b.mV[1],a.mV[2]+b.mV[2]};}LLVector3 operator-(LLVector3 a,LLVector3 b){a-=b;return a;}LLVector3 operator*(float s,LLVector3 a){return {s*a.mV[0],s*a.mV[1],s*a.mV[2]};}float operator*(LLVector3 a,LLVector3 b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}LLVector3 operator/(LLVector3 a,float s){return (1/s)*a;}LLVector3 operator*(LLVector3 a,float s){return s*a;}using LLVector3d=LLVector3;
float dist_vec(LLVector3 a,LLVector3 b){auto v=a-b;return std::sqrt(v*v);}float clamp_rescale(float v,float a,float b,float c,float d){return c+std::clamp((v-a)/(b-a),0.f,1.f)*(d-c);}LLVector3 lerp(LLVector3 a,LLVector3 b,float t){return (1-t)*a+t*b;}
struct LLMatrix4{std::string steps;void initAll(LLVector3,LLQuaternion,LLVector3){};void initScale(LLVector3){steps+='s';}void rotate(LLQuaternion){steps+='r';}void translate(LLVector3){steps+='t';}LLVector3 axes[3];LLVector3 getFwdRow4()const{return axes[0];}LLVector3 getLeftRow4()const{return axes[1];}LLVector3 getUpRow4()const{return axes[2];}};
struct LLBBox{LLVector3 center,size;LLBBox()=default;LLBBox(LLVector3 p,LLQuaternion,LLVector3 a,LLVector3 b):center(p),size(b-a){};bool containsPointAgent(LLVector3 p)const{p-=center;for(int n=0;n<3;n++)if(std::abs(p.mV[n])>size.mV[n]/2)return false;return true;}};

const LLVector3 LLVector3::zero{};
FOCUS_FRAME
LLVector3 operator*(LLVector3 p,LLQuaternion q){p.rotVec(q);return p;}

namespace VSSurfaceCoordinates{template<class VolumePositionToAgent>
BOUND_KERNEL}
using U64=unsigned long long;
struct Extent{float v[3]{};const float*getF32ptr()const{return v;}};struct SourceFace{Extent mExtents[2];};struct SourceVolume{SourceFace face;int getNumVolumeFaces()const{return 1;}const SourceFace&getVolumeFace(int)const{return face;}};
constexpr int LL_PCODE_LEGACY_AVATAR=1,LL_PCODE_VOLUME=2;constexpr unsigned char LL_SCULPT_TYPE_MESH=5,LL_SCULPT_TYPE_MASK=7,LL_SCULPT_FLAG_INVERT=64;struct VolumeParams{unsigned char sculpt=0;unsigned char getSculptType()const{return sculpt;}};struct LLExtendedMeshParams{enum{ANIMATED_MESH_ENABLED_FLAG=1};};
LLVector3 from_region_handle(U64 handle){return {float(handle),float(handle*2),0};}
struct VSWorldScene{struct PickSurface{std::shared_ptr<SourceVolume>volume;int face=0;LLVector3 scale{2,3,4},position{7,8,9};LLQuaternion rotation{.3f};bool volumeGlobal=false;};struct Face{std::shared_ptr<PickSurface>pickSurface;};struct ObjectHandle{unsigned local,generation;int host;};struct Impl{struct Object{Object()=default;Object(unsigned value):id(value){};std::vector<Face>admittedFaces;unsigned id=0,parent=0,generation=1,parentGeneration=0;int pcode=LL_PCODE_VOLUME;unsigned extendedMeshFlags=0;VolumeParams params;std::shared_ptr<int>controlAvatar;LLVector3 scale{2,4,6};};struct Avatar{struct Character{LLVector3 getCharacterPosition(){return {3,4,5};}};Character&character(){static Character c;return c;}};bool hudObject(const Object&o)const{return o.id==42;}int host=1;U64 handle=100;bool retired=false;std::map<unsigned,Object>objects;std::map<unsigned,std::shared_ptr<Avatar>>avatars;bool linksCurrent(const Object&o)const{if(!o.parent)return true;auto p=objects.find(o.parent);return p!=objects.end()&&p->second.generation==o.parentGeneration;}bool transform(unsigned id,LLVector3&p,LLQuaternion&q,std::set<unsigned>&,bool predicted){p=predicted?LLVector3{10,20,30}:LLVector3{1,2,3};q={0};return objects.count(id);}};
std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();std::map<int,std::unique_ptr<VSWorldScene>>mNeighbours;
bool current(ObjectHandle token)const{if(token.host!=mImpl->host){auto r=mNeighbours.find(token.host);return r!=mNeighbours.end()&&r->second->current(token);}auto f=mImpl->objects.find(token.local);return !mImpl->retired&&f!=mImpl->objects.end()&&f->second.generation==token.generation;}
std::function<bool()>guard(ObjectHandle token)const{std::weak_ptr<Impl>w=mImpl;return [w,token]{auto p=w.lock();if(!p||p->retired)return false;auto f=p->objects.find(token.local);return f!=p->objects.end()&&f->second.generation==token.generation;};}
std::optional<VSCameraFocusObject>focusObject(const ObjectHandle&,const LLVector3d&)const;
std::optional<std::array<LLVector3,8>>mediaFaceBounds(const ObjectHandle&,U32,const LLVector3d&)const;
};
QUERY
MEDIA_QUERY
int main(){VSWorldScene world;world.mImpl->objects[7]={7};VSWorldScene::ObjectHandle token{7,1,1};LLVector3 agentOrigin{50,100,0};auto frame=world.focusObject(token,agentOrigin);assert(frame&&frame->current());assert(frame->position().mV[0]==60&&frame->position().mV[1]==120);assert(frame->bounds().center.mV[0]==51);assert(frame->scale().mV[2]==6);assert(frame->root()&&!frame->avatar());assert(!frame->isMesh&&frame->assetToAgentMatrix.steps=="srt");
world.mImpl->objects[7].params.sculpt=LL_SCULPT_TYPE_MESH|LL_SCULPT_FLAG_INVERT;assert(world.focusObject(token,agentOrigin)->isMesh);world.mImpl->objects[7].params.sculpt=0;
// Immutable predicted/official poses are distinct, and old frames retire on local ID reuse.
world.mImpl->objects[7].generation++;assert(!frame->current());assert(!world.focusObject(token,agentOrigin));token.generation++;world.mImpl->objects[8]={8};world.mImpl->objects[7].parent=8;world.mImpl->objects[7].parentGeneration=1;world.mImpl->objects[8].extendedMeshFlags=1;world.mImpl->objects[8].controlAvatar=std::make_shared<int>(1);frame=world.focusObject(token,agentOrigin);assert(frame&&frame->animated()&&frame->controlAvatar()&&!frame->root());world.mImpl->objects[8].generation++;assert(!frame->current());assert(!world.focusObject(token,agentOrigin));
world.mImpl->objects[7].parent=0;world.mImpl->objects[7].pcode=LL_PCODE_LEGACY_AVATAR;assert(!world.focusObject(token,agentOrigin));world.mImpl->avatars[7]=std::make_shared<VSWorldScene::Impl::Avatar>();frame=world.focusObject(token,agentOrigin);assert(frame&&frame->characterPosition().mV[0]==53&&frame->characterPosition().mV[1]==104);
// Neighbor avatar positions are converted using the owning simulator origin, never the main origin.
auto neighbor=std::make_unique<VSWorldScene>();neighbor->mImpl->host=2;neighbor->mImpl->handle=200;neighbor->mImpl->objects[9]={9};neighbor->mImpl->objects[9].pcode=LL_PCODE_LEGACY_AVATAR;neighbor->mImpl->avatars[9]=std::make_shared<VSWorldScene::Impl::Avatar>();world.mNeighbours[2]=std::move(neighbor);frame=world.focusObject({9,1,2},agentOrigin);assert(frame&&frame->position().mV[0]==160&&frame->characterPosition().mV[0]==153&&frame->characterPosition().mV[1]==304);world.mNeighbours[2]->mImpl->retired=true;assert(!frame->current());assert(!world.focusObject({9,1,2},agentOrigin));world.mNeighbours.clear();assert(!frame->current());
frame=world.focusObject(token,agentOrigin);assert(frame&&frame->current());world.mImpl->retired=true;assert(!frame->current());
world.mImpl->retired=false;auto&item=world.mImpl->objects[7];item.pcode=LL_PCODE_VOLUME;item.admittedFaces.resize(1);assert(!world.mediaFaceBounds(token,0,agentOrigin));auto raw=std::make_shared<VSWorldScene::PickSurface>();raw->volume=std::make_shared<SourceVolume>();raw->volume->face.mExtents[0]={{-1,-2,-3}};raw->volume->face.mExtents[1]={{1,2,3}};item.admittedFaces[0].pickSurface=raw;
auto bounds=world.mediaFaceBounds(token,0,agentOrigin);assert(bounds);auto expected=LLVector3{2,6,12}*raw->rotation+raw->position+LLVector3{50,100,0};for(int n=0;n<3;n++)assert((*bounds)[0].mV[n]==expected.mV[n]);raw->volumeGlobal=true;bounds=world.mediaFaceBounds(token,0,agentOrigin);expected=LLVector3{1,2,3}*raw->rotation+raw->position+LLVector3{50,100,0};for(int n=0;n<3;n++)assert((*bounds)[0].mV[n]==expected.mV[n]);assert(!world.mediaFaceBounds(token,1,agentOrigin));item.id=42;bounds=world.mediaFaceBounds(token,0,agentOrigin);expected=LLVector3{1,2,3}*raw->rotation+raw->position;for(int n=0;n<3;n++)assert((*bounds)[0].mV[n]==expected.mV[n]);world.mImpl->retired=true;assert(!world.mediaFaceBounds(token,0,agentOrigin));
}
