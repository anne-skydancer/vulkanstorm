#include <cassert>
#include <cmath>
#include <functional>
#include <optional>
#include <random>
using F32=float;using U32=unsigned;constexpr int VX=0,VY=1,VZ=2;constexpr float F_TWO_PI=6.2831853071795864769f;
struct LLVector3 {float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};LLVector3&operator+=(const LLVector3&r){for(int i=0;i<3;++i)mV[i]+=r.mV[i];return*this;}friend LLVector3 operator*(float f,LLVector3 a){for(auto&v:a.mV)v*=f;return a;}};
template<class T>struct Ptr {T*p=nullptr;bool isNull()const{return!p;}bool notNull()const{return p;}T*operator->()const{return p;}operator T*()const{return p;}Ptr&operator=(T*v){p=v;return*this;}};
struct Joint{LLVector3 pos;LLVector3 getWorldPosition(){return pos;}};struct LLViewerObject {Ptr<int>mDrawable;bool avatar=false;LLVector3 pos;bool isAvatar(){return avatar;}LLVector3 getRenderPosition(){return pos;}};struct LLVOAvatar:LLViewerObject{Joint*mWristLeftp;};
struct LLPartData{static constexpr U32 LL_PART_DEAD_MASK=128;};struct Agent{LLVector3 getPosAgentFromGlobal(LLVector3 v){return v;}}gAgent;
namespace gold {
struct LLViewerPartSource {Ptr<LLViewerObject>mSourceObjectp;LLVector3 mPosAgent;};struct LLViewerPart{float mLastUpdateTime,mMaxAge,mParameter;U32 mFlags=0;LLVector3 mPosAgent;LLViewerPartSource*mPartSourcep;};struct LLViewerPartSourceChat:LLViewerPartSource{static void updatePart(LLViewerPart&,float);};struct LLViewerPartSourceBeam:LLViewerPartSource{Ptr<LLViewerObject>mTargetObjectp;LLVector3 mLKGTargetPosGlobal;static void updatePart(LLViewerPart&,float);};
GOLD
}
namespace peer {
struct LLViewerPartSource {Ptr<LLViewerObject>mSourceObjectp;LLVector3 mPosAgent;std::function<std::optional<LLVector3>()>mNativePosition;bool sourcePosition(LLVector3&)const;};struct LLViewerPart{float mLastUpdateTime,mMaxAge,mParameter;U32 mFlags=0;LLVector3 mPosAgent;LLViewerPartSource*mPartSourcep;};struct LLViewerPartSourceChat:LLViewerPartSource{static void updatePart(LLViewerPart&,float);};struct LLViewerPartSourceBeam:LLViewerPartSource{Ptr<LLViewerObject>mTargetObjectp;LLVector3 mLKGTargetPosGlobal;std::function<std::optional<LLVector3>()>mNativeTarget;static void updatePart(LLViewerPart&,float);};
PEER
}
bool equal(LLVector3 a,LLVector3 b){for(int i=0;i<3;++i)if(a.mV[i]!=b.mV[i])return false;return true;}
int main(){std::mt19937 rng(7653);auto f=[&]{return float(int(rng()%20000)-10000)*.001f;};int draw=0;LLVOAvatar source;Joint wrist;source.mWristLeftp=&wrist;LLViewerObject target;source.mDrawable=&draw;target.mDrawable=&draw;
 for(unsigned i=0;i<20000;++i){source.avatar=i%2;source.pos={f(),f(),f()};wrist.pos={f(),f(),f()};target.pos={f(),f(),f()};float age=float(rng()%1000)*.001f,phase=f();
  gold::LLViewerPartSourceChat gc;peer::LLViewerPartSourceChat pc;gc.mSourceObjectp=&source;pc.mSourceObjectp=&source;gc.mPosAgent=pc.mPosAgent={f(),f(),f()};gold::LLViewerPart gp{age,1.1f,phase,0,{},&gc};peer::LLViewerPart pp{age,1.1f,phase,0,{},&pc};gold::LLViewerPartSourceChat::updatePart(gp,.016f);peer::LLViewerPartSourceChat::updatePart(pp,.016f);assert(equal(gp.mPosAgent,pp.mPosAgent));
  pc.mSourceObjectp=nullptr;pc.mNativePosition=[&]{return std::optional<LLVector3>(source.pos);};peer::LLViewerPartSourceChat::updatePart(pp,.016f);assert(equal(gp.mPosAgent,pp.mPosAgent));
  gold::LLViewerPartSourceBeam gb;peer::LLViewerPartSourceBeam pb;gb.mSourceObjectp=&source;pb.mSourceObjectp=&source;bool targetObject=i%3;gb.mTargetObjectp=targetObject?&target:nullptr;pb.mTargetObjectp=targetObject?&target:nullptr;gb.mLKGTargetPosGlobal=pb.mLKGTargetPosGlobal={f(),f(),f()};gp.mPartSourcep=&gb;pp.mPartSourcep=&pb;gold::LLViewerPartSourceBeam::updatePart(gp,.016f);peer::LLViewerPartSourceBeam::updatePart(pp,.016f);assert(equal(gp.mPosAgent,pp.mPosAgent));
  pb.mSourceObjectp=nullptr;pb.mTargetObjectp=nullptr;pb.mNativePosition=[&]{return std::optional<LLVector3>(source.avatar?wrist.pos:source.pos);};pb.mNativeTarget=[&]()->std::optional<LLVector3>{if(targetObject)return target.pos;return {};};peer::LLViewerPartSourceBeam::updatePart(pp,.016f);assert(equal(gp.mPosAgent,pp.mPosAgent));
  pb.mNativePosition=[]()->std::optional<LLVector3>{return {};};peer::LLViewerPartSourceBeam::updatePart(pp,.016f);assert(pp.mFlags==LLPartData::LL_PART_DEAD_MASK);
 }
}
