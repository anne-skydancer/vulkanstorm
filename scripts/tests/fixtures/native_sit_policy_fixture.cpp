#include <vector>
#include <cassert>
#include <functional>
using U8=unsigned char;
constexpr int LL_PCODE_VOLUME=9,RLV_BHVR_SIT=0,RLV_BHVR_UNSIT=1,RLV_BHVR_STANDTP=2,RLV_BHVR_SITTP=3,RLV_BHVR_FARTOUCH=4,RLV_MODIFIER_FARTOUCHDIST=3,RLV_MODIFIER_SITTPDIST=4;
unsigned rules=0;bool standing=true,force=false,hasCommand=false;std::vector<int>events;
struct LLVector3{float v=0;};struct LLVector3d{float v=0;LLVector3d()=default;LLVector3d(LLVector3 a):v(a.v){}LLVector3d(float x):v(x){}LLVector3d operator+(LLVector3d b)const{return v+b.v;}};
float dist_vec_squared(LLVector3d a,LLVector3d b){float d=a.v-b.v;return d*d;}
struct LLViewerObject{U8 type=9;LLVector3d position{0};U8 getPCode()const{events.push_back(1);return type;}LLVector3d getPositionGlobal()const{events.push_back(2);return position;}};
struct VSObjectContext{U8 pcode=9;LLVector3d position{0};std::function<bool()>current;};
struct Agent{LLVector3d getPositionGlobal(){events.push_back(3);return 0;}}gAgent;
struct Command{int getBehaviourType(){events.push_back(4);return force?RLV_BHVR_SIT:99;}}command;
struct Handler{Command*getCurrentCommand(){events.push_back(5);return hasCommand?&command:nullptr;}}gRlvHandler;
bool agentAvatarStanding(){events.push_back(6);return standing;}
template<class T>struct RlvCachedBehaviourModifier{int type;RlvCachedBehaviourModifier(int t):type(t){}operator T()const{return T(type);}};
struct RlvActions{static bool hasBehaviour(int b){events.push_back(10+b);return (rules&(1u<<b))!=0;}static bool canSit(const LLViewerObject*,const LLVector3&);static bool canSit(const VSObjectContext&,const LLVector3&);};
REFERENCE
METHOD
int main(){LLViewerObject object;VSObjectContext native;int guards=0;bool live=true;native.current=[&]{++guards;return live;};for(unsigned state=0;state<1024;++state){rules=state&31;standing=state&32;force=state&64;hasCommand=state&128;object.type=native.pcode=(state&256)?9:47;const bool exists=state&512;live=exists;for(int distance=0;distance<12;++distance){object.position=native.position=LLVector3d(float(distance));events.clear();bool expected=referenceSit(exists?&object:nullptr,{.25f});auto trace=events;events.clear();bool legacy=RlvActions::canSit(exists?&object:nullptr,{.25f});assert(legacy==expected&&events==trace);guards=0;bool actual=RlvActions::canSit(native,{.25f});assert(actual==expected&&guards==1);}}}
