#include <cassert>
#include <cmath>
#include <string>
#include <memory>
#define VS_NATIVE_VULKAN 1
#define _PREHASH_AgentBlock "AgentBlock"
#define _PREHASH_Hunter "Hunter"
#define _PREHASH_Prey "Prey"
#define _PREHASH_LocationBlock "LocationBlock"
#define _PREHASH_GlobalX "GlobalX"
#define _PREHASH_GlobalY "GlobalY"
using F64=double;
enum {VX,VY,VZ};
struct LLUUID {int value=0;bool isNull()const{return !value;}bool operator==(const LLUUID& o)const{return value==o.value;}bool operator!=(const LLUUID& o)const{return !(*this==o);}};
LLUUID gAgentID{1};
struct LLVector3d {double mdV[3]{};bool isFinite()const{return std::isfinite(mdV[0])&&std::isfinite(mdV[1])&&std::isfinite(mdV[2]);}};
struct LLMessageSystem {
 int sender=10;LLUUID hunter{1},prey{2};double x=256,y=512;
 void getUUIDFast(const char*,const char* k,LLUUID& out){out=std::string(k)=="Hunter"?hunter:prey;}
 void getF64Fast(const char*,const char* k,double& out){out=std::string(k)=="GlobalX"?x:y;}
 int getSender()const{return sender;}
};
struct VSNativeSession {
 enum class Phase {Login,Connected,Disconnected};Phase state=Phase::Connected;int peer=10;
 inline static std::shared_ptr<VSNativeSession> owner;
 static auto active(){return owner;}auto phase()const{return state;}int host()const{return peer;}
};
struct LLAvatarTracker {
 LLUUID tracked{2};int accepted=0;LLVector3d position;
 static LLAvatarTracker& instance(){static LLAvatarTracker v;return v;}
 LLUUID getAvatarID(){return tracked;}
 void agentFound(const LLUUID&,const LLVector3d& value){++accepted;position=value;}
 static void processAgentFound(LLMessageSystem*,void**);
};
void LLAvatarTracker::processAgentFound(LLMessageSystem* msg,void**) FOUND
int main(){
 VSNativeSession::owner=std::make_shared<VSNativeSession>();auto& tracker=LLAvatarTracker::instance();LLMessageSystem message;
 auto deliver=[&]{LLAvatarTracker::processAgentFound(&message,nullptr);};
 deliver();assert(tracker.accepted==1 && tracker.position.mdV[0]==256 && tracker.position.mdV[1]==512);
 message.hunter={3};deliver();assert(tracker.accepted==1);message.hunter=gAgentID;
 message.prey={3};deliver();assert(tracker.accepted==1);message.prey={2};
 tracker.tracked={4};deliver();assert(tracker.accepted==1);tracker.tracked={2};
 message.sender=11;deliver();assert(tracker.accepted==1);message.sender=10;
 message.x=std::nan("");deliver();assert(tracker.accepted==1);message.x=256;
 VSNativeSession::owner->state=VSNativeSession::Phase::Disconnected;deliver();assert(tracker.accepted==1);
 VSNativeSession::owner->state=VSNativeSession::Phase::Connected;tracker.tracked={};message.prey={};deliver();assert(tracker.accepted==1);
 tracker.tracked={2};message.prey={2};deliver();assert(tracker.accepted==2);
}
