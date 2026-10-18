// Actual Telehub model parser and exact reference message transport. LGPL-2.1.
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>
#include <functional>
using S32=int;using U32=unsigned;constexpr int VX=0,VY=1,VZ=2,MAX_SPAWNPOINTS_PER_TELEHUB=16,LL_PCODE_VOLUME=9;
struct LLVector3{float mV[3]={};bool isFinite()const{return std::isfinite(mV[0])&&std::isfinite(mV[1])&&std::isfinite(mV[2]);}};
struct LLQuaternion{float v=1;bool isFinite()const{return std::isfinite(v);}};
struct LLUUID{int v=0;bool isNull()const{return !v;}bool operator<(const LLUUID&o)const{return v<o.v;}static const LLUUID null;};const LLUUID LLUUID::null{};
using LLHost=int;
std::string llformat(const char*f,...){char b[256];va_list a;va_start(a,f);vsnprintf(b,sizeof(b),f,a);va_end(a);return b;}
struct LLUICtrl{bool visible=false;std::string arg;void setVisible(bool b){visible=b;}void setTextArg(const char*,const std::string&s){arg=s;}};
struct LLScrollListCtrl:LLUICtrl{std::vector<std::string>rows;int selected=-1;void deleteAllItems(){rows.clear();}void addSimpleElement(const std::string&s){rows.push_back(s);}void selectNthItem(int i){selected=i;}};
struct Packet{std::string name;LLHost host=0;std::vector<std::string>params;std::map<std::string,int>ids;std::string method;};
struct LLMessageSystem{int count=0;LLUUID object{42};std::string objectName="Actual hub";LLVector3 position{{5,6,7}};LLQuaternion rotation{};std::array<LLVector3,16>spawns{};Packet pending;std::vector<Packet>sent;int limit=3;
int getNumberOfBlocks(const char*){return count;}void getUUID(const char*,const char*,LLUUID&o){o=object;}void getString(const char*,const char*,std::string&o){o=objectName;}void getVector3(const char*block,const char*,LLVector3&o,int i=0){o=std::string(block)=="SpawnPointBlock"?spawns[i]:position;}void getQuat(const char*,const char*,LLQuaternion&o){o=rotation;}
void newMessage(const char*n){pending=Packet{};pending.name=n;}void nextBlockFast(const char*){}void nextBlock(const char*){}void addUUIDFast(const char*n,LLUUID id){pending.ids[n]=id.v;}void addUUID(const char*n,LLUUID id){pending.ids[n]=id.v;}void addString(const char*n,const std::string&v){if(std::string(n)=="Parameter")pending.params.push_back(v);else if(std::string(n)=="Method")pending.method=v;}bool isSendFull(const char*){return int(pending.params.size())>=limit;}void sendReliable(LLHost host){pending.host=host;sent.push_back(pending);}
}wire,*gMessageSystem=&wire;
const char*_PREHASH_AgentData="AgentData",*_PREHASH_AgentID="AgentID",*_PREHASH_SessionID="SessionID";
struct Agent{bool god=false;LLUUID getID(){return{10};}LLUUID getSessionID(){return{20};}bool isGodlike(){return god;}}gAgent;
struct Selected{LLUUID id;};struct Selection{std::vector<Selected>rows;const auto&selected(){return rows;}}selection;
struct VSNativeSession{bool ready=true;static VSNativeSession*active();bool regionReady(){return ready;}Selection*objectSelection(){return &selection;}LLHost host(){return 500;}}session;VSNativeSession*VSNativeSession::active(){return &session;}
struct VSObjectContext{LLUUID id,rootEditID;LLHost host=0;U32 localID=0;std::function<bool()>current;static std::optional<VSObjectContext>resolve(LLUUID);};std::map<int,VSObjectContext>objects;std::optional<VSObjectContext>VSObjectContext::resolve(LLUUID id){auto i=objects.find(id.v);return i==objects.end()?std::nullopt:std::optional<VSObjectContext>(i->second);}
struct LLFloaterTelehub{LLUUID mTelehubObjectID;std::string mTelehubObjectName;LLVector3 mTelehubPos;LLQuaternion mTelehubRot;S32 mNumSpawn=0;LLVector3 mSpawnPointPos[16];std::map<std::string,LLUICtrl>views;LLScrollListCtrl list;
LLUICtrl*getChildView(const char*n){return &views[n];}template<class T>T*getChild(const char*n){if constexpr(std::is_same_v<T,LLScrollListCtrl>)return &list;else return &views[n];}void unpackTelehubInfo(LLMessageSystem*);};
METHODS
int main(){
 LLFloaterTelehub ui;wire.count=16;for(int i=0;i<16;++i)wire.spawns[i]={{float(i),2,3}};ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.v==42&&ui.mNumSpawn==16&&ui.list.rows.size()==16&&ui.list.selected==15&&ui.views["status_text_connected"].visible);
 wire.object={99};wire.count=17;ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.v==42&&ui.list.rows.size()==16);wire.count=-1;ui.unpackTelehubInfo(&wire);assert(ui.mNumSpawn==16);
 wire.count=3;wire.spawns[2].mV[0]=NAN;ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.v==42&&ui.mNumSpawn==16);wire.spawns[2].mV[0]=2;wire.rotation.v=NAN;ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.v==42);wire.rotation.v=1;wire.position.mV[0]=INFINITY;ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.v==42);wire.position.mV[0]=5;
 wire.object={};wire.count=0;ui.unpackTelehubInfo(&wire);assert(ui.mTelehubObjectID.isNull()&&ui.mNumSpawn==0&&ui.list.rows.empty()&&ui.views["status_text_not_connected"].visible);
 objects[1]={{1},{1},100,51,[]{return true;}};objects[2]={{2},{1},100,52,[]{return true;}};objects[3]={{3},{3},100,53,[]{return true;}};objects[4]={{4},{4},200,54,[]{return true;}};
 selection.rows={{{1}},{{2}},{{3}},{{4}}};assert(telehubSelection().size()==3);sendTelehubRequest("connect");assert(wire.sent.size()==2);assert(wire.sent[0].host==100&&wire.sent[0].params==std::vector<std::string>({"connect","51","53"}));assert(wire.sent[1].host==200&&wire.sent[1].params==std::vector<std::string>({"connect","54"}));for(const auto&p:wire.sent){assert(p.name=="EstateOwnerMessage"&&p.method=="telehub"&&p.ids.at("AgentID")==10&&p.ids.at("SessionID")==20&&p.ids.at("Invoice")==0&&p.ids.at("TransactionID")==0);}
 wire.sent.clear();wire.limit=2;sendTelehubRequest("spawnpoint add");assert(wire.sent.size()==3&&wire.sent[0].params.size()==2&&wire.sent[1].params==std::vector<std::string>({"spawnpoint add","53"}));
 wire.sent.clear();selection.rows.clear();gAgent.god=true;sendTelehubRequest("info ui");assert(wire.sent.size()==1&&wire.sent[0].name=="GodlikeMessage"&&wire.sent[0].host==500&&wire.sent[0].params==std::vector<std::string>({"info ui"}));
 wire.sent.clear();session.ready=false;sendTelehubRequest("delete");assert(wire.sent.empty());session.ready=true;selection.rows={{{2}}};objects[1].current=[]{return false;};assert(telehubSelection().empty());
}
