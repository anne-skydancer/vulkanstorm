#include <cassert>
#include <optional>
#include <vector>
#include <memory>
#include <map>
using U32=unsigned;constexpr U32 UPD_POSITION=1,UPD_ROTATION=2,UPD_SCALE=4;
struct Vec{int v=0;bool isExactlyZero()const{return v==0;}bool operator!=(Vec b)const{return v!=b.v;}};struct LLQuaternion{int v=0;bool operator!=(LLQuaternion b)const{return v!=b.v;}};
struct VSWorldScene{struct ObjectHandle{int id=1,generation=1;};struct LocalTransform{Vec position,scale;LLQuaternion rotation;};bool live=true,move=true,permanent=false;int writes=0;LocalTransform pose{{11},{33},{22}};std::optional<LocalTransform>objectLocalTransform(ObjectHandle){return pose;}bool previewLocalTransform(ObjectHandle,const LocalTransform&p,U32 fields){++writes;if(fields&1)pose.position=p.position;if(fields&2)pose.rotation=p.rotation;if(fields&4)pose.scale=p.scale;return true;}}world;VSWorldScene*scene(){return&world;}
struct VSEditObject{explicit VSEditObject(int){}explicit operator bool()const{return world.live;}bool permMove()const{return world.move;}bool isPermanentEnforced()const{return world.permanent;}};
struct VSObjectSelection{struct SavedTransform{Vec mLastPositionLocal,mLastScale;LLQuaternion mLastRotation;};struct Node{VSWorldScene::ObjectHandle object;std::shared_ptr<SavedTransform>transform;};struct Impl{bool retired=false;std::vector<Node>nodes;bool current(VSWorldScene::ObjectHandle h){return world.live&&h.generation==1;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();void overrideObjectUpdates();};
struct LLViewerObject{VSWorldScene::LocalTransform pose;bool permMove(){return world.move;}bool isPermanentEnforced(){return world.permanent;}void setPosition(Vec v){pose.position=v;}void setRotation(LLQuaternion q){pose.rotation=q;}void setScale(Vec v){pose.scale=v;}};
struct LLSelectNode{LLViewerObject*object;Vec mLastPositionLocal,mLastScale;LLQuaternion mLastRotation;LLViewerObject*getObject(){return object;}};
struct Original{ORIGINAL};
METHOD
int main(){for(unsigned bits=0;bits<1024;++bits){world=VSWorldScene{};world.live=bits&1;world.move=bits&2;world.permanent=bits&4;VSObjectSelection s;s.mImpl->retired=bits&8;auto saved=std::make_shared<VSObjectSelection::SavedTransform>();saved->mLastPositionLocal={bits&16?44:0};saved->mLastRotation={bits&32?55:0};saved->mLastScale={bits&64?66:0};s.mImpl->nodes.push_back({{1,bits&128?2:1},bits&256?nullptr:saved});LLViewerObject expected{world.pose};LLSelectNode ref{world.live?&expected:nullptr,saved->mLastPositionLocal,saved->mLastScale,saved->mLastRotation};if(!(bits&8)&&!(bits&128)&&!(bits&256))Original{}.apply(&ref);s.overrideObjectUpdates();assert(world.pose.position.v==expected.pose.position.v&&world.pose.rotation.v==expected.pose.rotation.v&&world.pose.scale.v==expected.pose.scale.v);const int writes=world.writes;s.overrideObjectUpdates();assert(world.writes==writes);}}
