#define VS_NATIVE_VULKAN 1
#include <map>
#include <vector>
#include <cassert>
constexpr int AGENT_CONTROL_AWAY=1,AGENT_CONTROL_STOP=2,ANIM_AGENT_AWAY=3,ANIM_REQUEST_START=4,ANIM_REQUEST_STOP=5,RLV_BHVR_SIT=6,RLV_BHVR_UNSIT=7;
std::vector<int>events;bool gNonInteractive=false,ready=true,valid=true,sitting=false,sitSetting=true,noSit=false,noUnsit=false,nativeMode=true,gDisconnected=false;float awayElapsed=0;int regionReads=0,avatarReads=0;
struct Timer{float getElapsedTimeF32(){return awayElapsed;}void start(){events.push_back(10);}void reset(){events.push_back(11);}}gAwayTimer,gAwayTriggerTimer;
struct Settings{bool getBOOL(const char*){events.push_back(12);return sitSetting;}}gSavedSettings;
struct Rlv{bool hasBehaviour(int b){events.push_back(20+b);return b==RLV_BHVR_SIT?noSit:noUnsit;}}gRlvHandler;
struct Avatar{std::map<int,int>mSignaledAnimations;bool isSitting(){events.push_back(13);return sitting;}}avatar;Avatar*gAgentAvatarp=&avatar;
bool isAgentAvatarValid(){++avatarReads;return valid;}Avatar*vs_avatar_character(){++avatarReads;return valid?&avatar:nullptr;}const std::map<int,int>&vs_avatar_signaled_animations(){static std::map<int,int>empty;return valid?avatar.mSignaledAnimations:empty;}
struct Session{bool regionReady(){return ready;}static Session*active(){static Session s;return nativeMode?&s:nullptr;}};using VSNativeSession=Session;
struct LLAgent{static constexpr float MIN_AFK_TIME=2;int mControlFlags=0;bool afkSit=false;void*getRegion(){++regionReads;return ready?this:nullptr;}bool isSitting(){events.push_back(13);return sitting;}void sendAnimationRequest(int a,int r){events.push_back(100+a*10+r);}void setControlFlags(int f){mControlFlags|=f;events.push_back(30+f);}void clearControlFlags(int f){mControlFlags&=~f;events.push_back(40+f);}void sitDown(){events.push_back(50);}void standUp(){events.push_back(51);}bool isAfkSitting(){events.push_back(52);return afkSit;}void setIsAfkSitting(bool b){afkSit=b;events.push_back(53+b);}void setAFK();void clearAFK();void setAFKReference();void clearAFKReference();}gAgent;
REFERENCE
METHOD
MOUSE
int main(){for(int mode=0;mode<2;++mode)for(int mask=0;mask<1024;++mask){nativeMode=mode;gNonInteractive=mask&1;ready=mask&2;valid=mask&4;sitting=mask&8;sitSetting=mask&16;noSit=mask&32;noUnsit=mask&64;avatar.mSignaledAnimations.clear();if(mask&128)avatar.mSignaledAnimations[ANIM_AGENT_AWAY]=1;
for(int op=0;op<2;++op){gAgent.mControlFlags=(mask&256)?AGENT_CONTROL_AWAY:0;gAgent.afkSit=mask&512;events.clear();regionReads=avatarReads=0;if(op)gAgent.clearAFKReference();else gAgent.setAFKReference();auto expected=events;auto flags=gAgent.mControlFlags;auto afk=gAgent.afkSit;gAgent.mControlFlags=(mask&256)?AGENT_CONTROL_AWAY:0;gAgent.afkSit=mask&512;events.clear();regionReads=avatarReads=0;if(op)gAgent.clearAFK();else gAgent.setAFK();assert(events==expected&&gAgent.mControlFlags==flags&&gAgent.afkSit==afk);if(mode)assert(regionReads==0);}}
for(int connected=0;connected<2;++connected)for(int elapsed=0;elapsed<4;++elapsed){gDisconnected=!connected;awayElapsed=float(elapsed);gAgent.mControlFlags=0;gAgent.afkSit=false;events.clear();clear_away_on_mouse_move();if(!connected)assert(events.empty());else assert(!events.empty());}
}
