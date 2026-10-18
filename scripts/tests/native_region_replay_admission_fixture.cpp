#include <cassert>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>
using U32=unsigned;struct LLHost{int value;LLHost(int v=0):value(v){}operator int()const{return value;}bool isOk()const{return value!=0;}};
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
SETS
struct VSNativeSession {enum class Phase {Disconnected,Connecting,Connected,Logout};struct Peer{bool acknowledged=true,handshake=true;};std::map<int,Peer>mCircuits{{77,{}}};LLHost mHost=77;Phase mPhase=Phase::Connected;bool mChangingRegion=false;unsigned mRejected=0;bool regionReady()const{return mPhase==Phase::Connected&&!mChangingRegion;}int host()const{return mHost;}bool admit(const std::string&,const LLHost&);};
PRODUCTION_ADMISSION
struct LLPanel{template<class T>T*getChild(const char*){static T child;return &child;}void setCtrlsEnabled(bool){}void setAllChildrenEnabled(bool,bool){}};struct LLTabContainer:LLPanel{};
struct LLMessageSystem {std::function<bool(const std::string&,const LLHost&)>admission;std::string name,block;std::map<std::string,std::string>values;int destination=0;void setMessageAdmission(decltype(admission)a){admission=a;}void newMessage(const char*n){name=n;values.clear();}void newMessageFast(const char*n){newMessage(n);}void nextBlock(const char*n){block=n;}void nextBlockFast(const char*n){nextBlock(n);}void addUUID(const char*n,const std::string&v){values[block+"/"+n]=v;}void addUUIDFast(const char*n,const std::string&v){addUUID(n,v);}void addU32Fast(const char*n,unsigned v){values[block+"/"+n]=std::to_string(v);}void addS32Fast(const char*n,int v){values[block+"/"+n]=std::to_string(v);}void addStringFast(const char*n,const std::string&v){values[block+"/"+n]=v;}void sendReliable(int host){destination=host;}}message,*gMessageSystem=&message;
struct Agent{std::string getID()const{return "agent";}std::string getSessionID()const{return "session";}int getRegionHost()const{return 77;}void sendReliableMessage(){message.sendReliable(77);}}gAgent;
struct LLFloaterRegionInfo{template<class T>T*findChild(const char*){static T tabs;return &tabs;}void requestRegionInfo();void disableTopButtons(){}}region;
struct LLViewerWindow{bool isNativeVulkan()const{return true;}}window,*gViewerWindow=&window;struct LLFloaterReg{template<class T>static T*findTypedInstance(const char*){return &region;}template<class T>static T*getTypedInstance(const char*){return &region;}};
constexpr unsigned STAT_REPORT_TOP_SCRIPTS=1;
#define F(name) const char* _PREHASH_##name=#name;
F(LandStatRequest) F(AgentData) F(AgentID) F(SessionID) F(RequestData) F(ReportType) F(RequestFlags) F(Filter) F(ParcelLocalID)
struct LLFloaterTopObjects{std::function<bool()>mReplyCurrent=[] {return true;};unsigned mCurrentMode=STAT_REPORT_TOP_SCRIPTS,mFlags=0;std::string mFilter;void beginReply(){}void clearList(){}void disableRefreshBtn(){}void onRefresh();};
REGION_REQUEST
LAND_REQUEST
int main(){auto owner=std::make_shared<VSNativeSession>();const std::weak_ptr<VSNativeSession>weak=owner;
DIAGNOSTIC_ADMISSION
assert(!owner->admit("RequestRegionInfo",77)&&!owner->admit("LandStatRequest",77));
assert(message.admission("RequestRegionInfo",77)&&message.admission("LandStatRequest",77));assert(!message.admission("RequestRegionInfo",78)&&!message.admission("LandStatRequest",78));assert(!message.admission("UnknownClientPacket",77));assert(message.admission("RegionInfo",77)&&!message.admission("RegionInfo",78));owner->mChangingRegion=true;assert(!message.admission("RegionInfo",77));owner->mChangingRegion=false;
region.requestRegionInfo();assert(message.name=="RequestRegionInfo"&&message.destination==77&&message.values.size()==2&&message.values["AgentData/AgentID"]=="agent"&&message.values["AgentData/SessionID"]=="session"&&message.admission(message.name,message.destination));
LLFloaterTopObjects top;top.onRefresh();assert(message.name=="LandStatRequest"&&message.destination==77&&message.values.size()==6&&message.values["AgentData/AgentID"]=="agent"&&message.values["AgentData/SessionID"]=="session"&&message.values["RequestData/ReportType"]=="1"&&message.values["RequestData/RequestFlags"]=="0"&&message.values["RequestData/Filter"]==""&&message.values["RequestData/ParcelLocalID"]=="0"&&message.admission(message.name,message.destination));
owner.reset();assert(!message.admission("RequestRegionInfo",77)&&!message.admission("LandStatRequest",77)&&!message.admission("RegionInfo",77));}
