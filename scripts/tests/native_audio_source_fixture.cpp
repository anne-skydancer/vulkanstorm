// Console peers for executing frozen and production audio-source policies.
#include <algorithm>
#include <cassert>
#include <cmath>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
using F32=float;using S32=int;
std::vector<int> eventLog;
struct LLUUID{int value=0;static const LLUUID null;bool notNull()const{return value!=0;}bool operator==(const LLUUID&)const=default;};const LLUUID LLUUID::null{};
struct LLVector3d{double x=0,y=0,z=0;LLVector3d operator-(LLVector3d b)const{return{x-b.x,y-b.y,z-b.z};}double magVec()const{return std::sqrt(x*x+y*y+z*z);}bool operator==(const LLVector3d&)const=default;};
using LLVector3=LLVector3d;
template<class T>struct LLPointer{T*p=nullptr;LLPointer()=default;LLPointer(T*v):p(v){}operator T*()const{return p;}T*operator->()const{return p;}LLPointer&operator=(T*v){p=v;return*this;}};
struct LLViewerObject{LLUUID id{1};LLViewerObject*parent=nullptr;bool dead=false,attachment=false,hud=false,avatar=false;float cutoff=0;LLVector3d position;LLVector3 velocity;
 bool isDead()const{return dead;}bool isAttachment()const{return attachment;}bool isHUDAttachment()const{return hud;}bool isAvatar()const{return avatar;}LLUUID getID()const{return id;}float getSoundCutOffRadius()const{return cutoff;}
 LLVector3d getPositionGlobal()const{return position;}LLVector3 getVelocity()const{return velocity;}LLViewerObject*getParent()const{return parent;}LLViewerObject*getSubParent()const{return parent;}const LLViewerObject*getRoot()const{auto*r=this;while(r->parent)r=r->parent;return r;}};
LLPointer<LLViewerObject>gAgentAvatarp;bool isAgentAvatarValid(){return gAgentAvatarp.p!=nullptr;}
struct Camera{LLVector3d position;LLVector3d getCameraPositionGlobal(){return position;}}gAgentCamera;
struct Agent{LLVector3d position;LLVector3d getPositionGlobal(){return position;}}gAgent;
struct Settings{int ear=0;}gSavedSettings;
template<class T>struct LLCachedControl{Settings&s;LLCachedControl(Settings&v,const char*,int):s(v){}T operator()()const{return T(s.ear);}};
struct Parcel{bool hear=true;static Parcel*getInstance(){static Parcel p;return&p;}bool canHearSound(LLVector3d){return hear;}};
using LLViewerParcelMgr=Parcel;
struct LLMute{static constexpr int flagObjectSounds=1;};
struct LLMuteList{unsigned mask=0;static LLMuteList*getInstance(){static LLMuteList m;return&m;}bool isMuted(LLUUID id,int flag=0){return(id.value==1&&(mask&1))||(id.value==2&&flag&&(mask&2))||(id.value==3&&(mask&4));}};
std::ostringstream infoLog;
#define LL_INFOS() infoLog
#define LL_ENDL ""
struct Data{LLUUID id{7};LLUUID getID()const{return id;}};
struct LLAudioSource{bool mSourceMuted=false;LLUUID mOwnerID{2};LLVector3d mPositionGlobal;LLVector3 mVelocity;Data data;Data*mCurrentDatap=nullptr;
 void play(LLUUID id){eventLog.push_back(id.value);}void update(){eventLog.push_back(100);} };
struct VSAudioObjectState{LLUUID id,root,avatarRoot,agentRoot;LLVector3d position,avatarRootPosition;LLVector3 velocity;F32 cutoff=0;bool dead=false,attachment=false,hud=false,agentRiding=false;};
using VSAudioObjectQuery=std::function<std::optional<VSAudioObjectState>()>;
struct OriginalAudio:LLAudioSource{LLPointer<LLViewerObject>mObjectp;void update();void checkCutOffRadius();LLVector3d getPosGlobal()const;bool isInCutOffRadius(LLVector3d,F32)const;void updateMute();};
struct NativeAudio:LLAudioSource{LLPointer<LLViewerObject>mObjectp;VSAudioObjectQuery mNativeObject;void update();void checkCutOffRadius();bool isInCutOffRadius(LLVector3d,F32)const;
 template<class Peer>LLVector3d getPosGlobalFor(const Peer&)const;template<class Peer>void checkCutOffRadiusFor(const Peer&);template<class Peer>void updateMuteFor(const Peer&);template<class Peer>void updateFor(Peer&);};
