// Deterministic CPU input model for original and renderer-peer LLAgent navigation.
#include <cassert>
#include <cmath>
#include <algorithm>
#include <functional>
#include <optional>
#include <string>
#include <sstream>
#include <utility>
#include <cstdint>
using F32=float;using F64=double;using U32=uint32_t;using U64=uint64_t;using S32=int;
constexpr int VX=0,VY=1,VZ=2;
struct LLQuaternion{float yaw=0;};
struct LLVector3d;
struct LLVector3{
 float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{};explicit LLVector3(const LLVector3d&);
 void setVec(const LLVector3&v){*this=v;}float magVec()const{return std::sqrt(*this**this);}float normalize(){float n=magVec();if(n>0)for(float&v:mV)v/=n;return n;}
 friend LLVector3 operator-(LLVector3 a,const LLVector3&b){for(int i=0;i<3;++i)a.mV[i]-=b.mV[i];return a;}
 friend LLVector3 operator%(const LLVector3&a,const LLVector3&b){return {a.mV[1]*b.mV[2]-a.mV[2]*b.mV[1],a.mV[2]*b.mV[0]-a.mV[0]*b.mV[2],a.mV[0]*b.mV[1]-a.mV[1]*b.mV[0]};}
 friend float operator*(const LLVector3&a,const LLVector3&b){return a.mV[0]*b.mV[0]+a.mV[1]*b.mV[1]+a.mV[2]*b.mV[2];}
 friend LLVector3 operator*(LLVector3 a,const LLQuaternion&q){return {a.mV[0]*std::cos(q.yaw)-a.mV[1]*std::sin(q.yaw),a.mV[0]*std::sin(q.yaw)+a.mV[1]*std::cos(q.yaw),a.mV[2]};}
 static const LLVector3 x_axis;
};const LLVector3 LLVector3::x_axis{1,0,0};
struct LLVector3d{
 double mdV[3]{};LLVector3d()=default;LLVector3d(double x,double y,double z):mdV{x,y,z}{};
 LLVector3d&operator-=(const LLVector3d&b){for(int i=0;i<3;++i)mdV[i]-=b.mdV[i];return *this;}
 double magVec()const{return std::sqrt(mdV[0]*mdV[0]+mdV[1]*mdV[1]+mdV[2]*mdV[2]);}
 bool isExactlyZero()const{return mdV[0]==0&&mdV[1]==0&&mdV[2]==0;}
};LLVector3::LLVector3(const LLVector3d&v):mV{float(v.mdV[0]),float(v.mdV[1]),float(v.mdV[2])}{}
float dist_vec(LLVector3d a,const LLVector3d&b){a-=b;return float(a.magVec());}
float angle_between(LLVector3 a,LLVector3 b){a.normalize();b.normalize();return std::acos(std::clamp(a*b,-1.f,1.f));}
template<class T>T llmax(T a,T b){return std::max(a,b);}
struct LLUUID{int value=0;bool isNull()const{return value==0;}static const LLUUID null;};const LLUUID LLUUID::null{};
constexpr float F_PI=3.14159265358979323846f,AUTOPILOT_MAX_TIME_NO_PROGRESS_FLY=2,AUTOPILOT_MAX_TIME_NO_PROGRESS_WALK=1,AUTOPILOT_HEIGHT_ADJUST_DISTANCE=10,AUTOPILOT_MIN_TARGET_HEIGHT_OFF_GROUND=1;
constexpr U32 AGENT_CONTROL_YAW_POS=1,AGENT_CONTROL_YAW_NEG=2,AGENT_CONTROL_FAST_UP=4,AGENT_CONTROL_UP_POS=8,AGENT_CONTROL_UP_NEG=16,AGENT_CONTROL_FAST_AT=32,AGENT_CONTROL_AT_POS=64,AGENT_CONTROL_AT_NEG=128,AGENT_CONTROL_STOP=256;
struct LLTimer{static U64 getTotalTime(){return 260000;}};
#define LL_WARNS(...) std::ostringstream()
#define LL_INFOS(...) std::ostringstream()
#define LL_ENDL ""
namespace REFERENCE {
COMMON
REFERENCE_METHODS
}
namespace CURRENT {
COMMON
ADAPTER
CURRENT_METHODS
}
int main(){
 for(int caseNo=0;caseNo<1200;++caseNo){
  REFERENCE::gAgent=REFERENCE::LLAgent{};CURRENT::gAgent=CURRENT::LLAgent{};
  REFERENCE::calls=REFERENCE::Calls{};CURRENT::calls=CURRENT::Calls{};
  REFERENCE::valid=CURRENT::valid=caseNo%11!=0;REFERENCE::avatar.mInAir=CURRENT::avatar.mInAir=caseNo%3==0;
  const float yaw=float(caseNo%17)*.37f;const LLVector3d target(1+(caseNo%41)*2,2+(caseNo%29)*2,(caseNo%35)-10);
  REFERENCE::gAgent.position=CURRENT::gAgent.position={4,8,3};
  LLQuaternion rotation{yaw};const LLQuaternion*rot=caseNo%2?&rotation:nullptr;
  REFERENCE::gAgent.startAutoPilotGlobal(target,caseNo%3?"":"Sit",rot,nullptr,nullptr,caseNo%5?0.f:2.f,.03f,caseNo%4!=0);
  CURRENT::gAgent.startAutoPilotGlobal(target,caseNo%3?"":"Sit",rot,nullptr,nullptr,caseNo%5?0.f:2.f,.03f,caseNo%4!=0);
  auto compare=[&]{auto&a=REFERENCE::gAgent;auto&b=CURRENT::gAgent;
   assert(a.mAutoPilot==b.mAutoPilot&&a.flying==b.flying&&a.controls==b.controls);
   for(int i=0;i<3;++i)assert(a.mAutoPilotTargetGlobal.mdV[i]==b.mAutoPilotTargetGlobal.mdV[i]);
   assert(a.mAutoPilotStopDistance==b.mAutoPilotStopDistance&&a.mAutoPilotTargetDist==b.mAutoPilotTargetDist&&a.mAutoPilotNoProgressFrameCount==b.mAutoPilotNoProgressFrameCount);
  };compare();
  for(int tick=0;tick<4;++tick){float a=0,b=0;REFERENCE::gAgent.autoPilot(&a);CURRENT::gAgent.autoPilot(&b);assert(a==b);compare();}
  assert(REFERENCE::calls.valid==CURRENT::calls.valid&&REFERENCE::calls.height==CURRENT::calls.height&&REFERENCE::calls.pelvis==CURRENT::calls.pelvis&&REFERENCE::calls.air==CURRENT::calls.air&&REFERENCE::calls.position==CURRENT::calls.position);
 }
}
