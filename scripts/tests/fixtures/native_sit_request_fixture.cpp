#include <vector>
#include <tuple>
#include <cassert>
#include <functional>
using U8=unsigned char;using LLUUID=int;constexpr int LL_PCODE_VOLUME=9,RLV_BHVR_STANDTP=3;constexpr int _PREHASH_AgentRequestSit=10,_PREHASH_AgentData=20,_PREHASH_AgentID=30,_PREHASH_SessionID=40,_PREHASH_TargetObject=50,_PREHASH_TargetID=60,_PREHASH_Offset=70;
struct LLVector3{int value=18;};bool enabled=false,allowSit=true,standTP=false,avatarValid=true,sitting=false;std::vector<int>state;
struct Message{std::vector<std::tuple<int,int,int>>events;void newMessageFast(int x){events.emplace_back(0,x,0);}void nextBlockFast(int x){events.emplace_back(1,x,0);}void addUUIDFast(int x,int y){events.emplace_back(2,x,y);}void addVector3Fast(int x,LLVector3 y){events.emplace_back(3,x,y.value);}void sendReliable(int host){events.emplace_back(4,host,0);}}message;Message*gMessageSystem=&message;
struct Region{void sendReliableMessage(){message.sendReliable(999);}}region;
struct LLViewerObject{int mID=777;U8 type=9;U8 getPCode(){return type;}Region*getRegion(){return &region;}};
struct VSObjectContext{int id=777,host=999;U8 pcode=9;std::function<bool()>current;};
struct RlvActions{static bool canSit(LLViewerObject*,const LLVector3&){return allowSit;}static bool canSit(const VSObjectContext&,const LLVector3&){return allowSit;}};
struct RlvHandler{bool hasBehaviour(int){return standTP;}void setSitSource(int p){state.push_back(p);}}gRlvHandler;
struct rlv_handler_t{static bool isEnabled(){return enabled;}};
struct Avatar{bool isSitting(){return sitting;}}avatar;Avatar*gAgentAvatarp=&avatar;bool isAgentAvatarValid(){return avatarValid;}Avatar*vs_avatar_character(){return avatarValid?&avatar:nullptr;}
struct Agent{bool isSitting(){return sitting;}void standUp(){state.push_back(88);}int getPositionGlobal(){return 42;}int getID(){return 111;}int getSessionID(){return 222;}}gAgent;
PEERS
SHARED
REFERENCE
int main(){LLViewerObject object;VSObjectContext native;bool live=true;native.current=[&]{return live;};for(int mask=0;mask<512;++mask){enabled=mask&1;allowSit=mask&2;standTP=mask&4;avatarValid=mask&8;sitting=mask&16;object.type=native.pcode=(mask&32)?9:17;bool exists=mask&64;live=exists;for(int mode=0;mode<2;++mode){message.events.clear();state.clear();referenceSit(exists?&object:nullptr,{});auto expectedMessages=message.events;auto expectedState=state;message.events.clear();state.clear();if(mode)requestObjectSit(NativeMenuSit{native},{});else requestObjectSit(ReferenceMenuSit{exists?&object:nullptr},{});assert(message.events==expectedMessages&&state==expectedState);}}
live=false;message.events.clear();requestObjectSit(NativeMenuSit{native},{});assert(message.events.empty());}
