#include <cassert>
#include <cmath>
#include <limits>
#include <string>
#include <map>
using U32=unsigned;using U64=unsigned long long;
enum{VX,VY,VZ};constexpr U32 TELEPORT_FLAGS_DISABLE_CANCEL=8,TELEPORT_FLAGS_VIA_GODLIKE_LURE=16,TELEPORT_FLAGS_VIA_LURE=32;
struct LLVector3 {float mV[3]{};LLVector3()=default;LLVector3(float x,float y,float z):mV{x,y,z}{}bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}LLVector3 operator+(LLVector3 rhs)const{return {mV[0]+rhs.mV[0],mV[1]+rhs.mV[1],mV[2]+rhs.mV[2]};}static const LLVector3 x_axis;};const LLVector3 LLVector3::x_axis{1,0,0};
struct LLVector3d {double mdV[3]{};LLVector3d()=default;explicit LLVector3d(LLVector3 v):mdV{v.mV[0],v.mV[1],v.mV[2]}{}LLVector3d operator+(LLVector3d rhs)const{LLVector3d v;for(int i=0;i<3;++i)v.mdV[i]=mdV[i]+rhs.mdV[i];return v;}};
LLVector3d from_region_handle(U64 handle){LLVector3d value;value.mdV[0]=double(handle);return value;}
struct LLUUID {int value=0;bool notNull()const{return value!=0;}bool isNull()const{return value==0;}};
struct State {bool enabled=false,handlerEnabled=false,force=false,started=true,local=false,localAllowed=true,remoteAllowed=true,avatar=true,sitting=false,command=false,tpCommand=false,canCancel=true,alwaysCancel=false;U32 restrictions=0;int notices=0,cancelChanges=0;}state;
constexpr int STATE_STARTED=10,RLV_BHVR_TPLM=1,RLV_BHVR_TPLOC=2,RLV_BHVR_UNSIT=4,RLV_BHVR_TPTO=7;
struct RlvActions {static bool isRlvEnabled(){return state.enabled;}static bool isLocalTp(const LLVector3d&){return state.local;}static bool canTeleportToLocal(const LLVector3d&){return state.localAllowed;}static bool canTeleportToLocation(){return state.remoteAllowed;}};
struct RlvStringKeys {struct Blocked {static constexpr int Teleport=0;};};
struct RlvUtil {static bool isForceTp(){return state.force;}static void notifyBlocked(int){++state.notices;}};
struct LLStartUp {static int getStartupState(){return state.started?STATE_STARTED:0;}};
struct Command {int getBehaviourType(){return state.tpCommand?RLV_BHVR_TPTO:0;}}command;
struct Handler {Command*getCurrentCommand(){return state.command?&command:nullptr;}bool hasBehaviour(U32 mask){return (state.restrictions&mask)!=0;}void setCanCancelTp(bool value){state.canCancel=value;++state.cancelChanges;}bool getCanCancelTp(){return state.canCancel;}}gRlvHandler;
struct rlv_handler_t {static bool isEnabled(){return state.handlerEnabled;}};
struct Avatar {bool isSitting(){return state.sitting;}}avatar,*gAgentAvatarp=&avatar;
bool isAgentAvatarValid(){return state.avatar;}
struct Agent {bool isSitting(){return state.avatar&&state.sitting;}}gAgent;
struct Settings {bool getBOOL(const char*){return state.alwaysCancel;}}gSavedSettings;
POLICY_FUNCTIONS
REFERENCE_FUNCTIONS
struct LLHost{};
struct Messages {int sends=0,result=1;std::string name;std::map<std::string,LLVector3> vectors;void newMessage(const char*s){name=s;}void nextBlock(const char*){}void addUUID(const char*,LLUUID){}void addU64(const char*,U64){}void addU32(const char*,U32 value){flags=value;}U32 flags=0;void addVector3(const char*s,LLVector3 v){vectors[s]=v;}int sendReliable(LLHost){++sends;return result;}}messages,*gMessageSystem=&messages;
using LLMessageSystem=Messages;
struct AgentCamera {int resets=0;void resetView(){++resets;}}gAgentCamera;
double now(){return 10;}
struct Viewer {void setShowProgress(bool,bool){}}viewer,*gViewerWindow=&viewer;
struct VSNativeSession {enum class Phase{Connected,Logout};enum class Startup{Complete,Inventory};Phase mPhase=Phase::Connected;Startup mStartup=Startup::Complete;bool mChangingRegion=false;LLUUID mAgent{1},mSession{2};LLHost mHost;std::string mRequestedRegion;double mNavigationDeadline=0;U64 mSent=0;U32 mTeleportFlags=0;int typings=0;
 void typing(bool){++typings;}bool teleportRequest(U64,const LLVector3&,const LLVector3* = nullptr);bool teleportToLandmark(const LLUUID&);bool teleportToLure(const LLUUID&,bool);bool cancelTeleport();};
WIRE_FUNCTIONS
int main(){
 LLVector3d destination;
 for(unsigned bits=0;bits<4096;++bits){
  State seed;seed.enabled=bits&1;seed.handlerEnabled=bits&2;seed.force=bits&4;seed.started=bits&8;seed.local=bits&16;seed.localAllowed=bits&32;seed.remoteAllowed=bits&64;seed.avatar=bits&128;seed.sitting=bits&256;seed.command=bits&512;seed.tpCommand=bits&1024;seed.canCancel=bits&2048;
  state=seed;bool expected=reference_location(destination);auto old=state;state=seed;assert(vs_teleport_location_allowed(destination)==expected && state.notices==old.notices && state.cancelChanges==old.cancelChanges && state.canCancel==old.canCancel);
  for(unsigned restrictions=0;restrictions<8;++restrictions)for(int id:{0,1}){
   seed.restrictions=restrictions;state=seed;expected=reference_landmark({id});old=state;state=seed;assert(vs_teleport_landmark_allowed({id})==expected && state.notices==old.notices);
  }
 }
 for(bool allow:{false,true})for(bool always:{false,true})for(U32 flags:{0u,TELEPORT_FLAGS_DISABLE_CANCEL}){state.canCancel=allow;state.alwaysCancel=always;assert(vs_teleport_cancel_allowed(flags)==(allow&&(!(flags&TELEPORT_FLAGS_DISABLE_CANCEL)||always)));}
 state=State{};VSNativeSession session;assert(session.teleportRequest(256,{3,4,5}));auto look=messages.vectors["LookAt"];assert(look.mV[0]==4&&look.mV[1]==4&&look.mV[2]==5);
 LLVector3 direction{0,1,0};assert(session.teleportRequest(256,{3,4,5},&direction));assert(messages.vectors["LookAt"].mV[0]==0 && messages.vectors["LookAt"].mV[1]==1);
 int sent=messages.sends;state.enabled=true;state.remoteAllowed=false;assert(!session.teleportRequest(256,{3,4,5}) && messages.sends==sent);
 state=State{};state.handlerEnabled=true;state.restrictions=RLV_BHVR_TPLM;assert(!session.teleportToLandmark({1}) && messages.sends==sent);assert(session.teleportToLandmark({0}) && messages.sends==sent+1);
 state.canCancel=false;sent=messages.sends;assert(!session.cancelTeleport() && messages.sends==sent);state.canCancel=true;session.mTeleportFlags=TELEPORT_FLAGS_DISABLE_CANCEL;assert(!session.cancelTeleport());state.alwaysCancel=true;assert(session.cancelTeleport() && !session.mNavigationDeadline);
 state=State{};assert(session.teleportToLure({1},true));assert(messages.flags==(TELEPORT_FLAGS_VIA_GODLIKE_LURE|TELEPORT_FLAGS_DISABLE_CANCEL) && session.mTeleportFlags==messages.flags && !session.cancelTeleport());
 assert(session.teleportToLure({1},false));assert(messages.flags==TELEPORT_FLAGS_VIA_LURE && session.cancelTeleport());
 session.mPhase=VSNativeSession::Phase::Logout;sent=messages.sends;assert(!session.teleportRequest(256,{3,4,5}) && !session.teleportToLandmark({0}) && messages.sends==sent);
}
