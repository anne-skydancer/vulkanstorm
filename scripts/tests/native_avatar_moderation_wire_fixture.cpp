// Exact shared moderation notification and wire bodies; no real agent/host.
#include <cassert>
#include <functional>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>
using U32=unsigned;using S32=int;
struct LLUUID{int value=0;std::string asString()const{return std::to_string(value);}bool operator==(LLUUID b)const{return value==b.value;}static const LLUUID null;static inline int serial=0;static LLUUID generateNewID(){return{++serial};}};const LLUUID LLUUID::null{};using uuid_vec_t=std::vector<LLUUID>;
struct LLHost{int value=0;bool isOk()const{return value!=0;}};struct LLVector3d{int value=0;};
struct LLSD{enum Kind{Nil,Integer,Identity,String,Array,Map}kind=Nil;int value=0;std::string text;std::vector<LLSD>array;std::map<std::string,LLSD>map;using array_const_iterator=std::vector<LLSD>::const_iterator;LLSD()=default;LLSD(int v):kind(Integer),value(v){}LLSD(LLUUID v):kind(Identity),value(v.value){}LLSD(const std::string&s):kind(String),text(s){}std::string asString()const{return kind==String?text:std::to_string(value);}
bool asBoolean()const{return value!=0;}int asInteger()const{return value;}LLUUID asUUID()const{return{value};}void append(LLUUID id){kind=Array;array.emplace_back(id);}int size()const{return int(array.size());}array_const_iterator beginArray()const{return array.begin();}array_const_iterator endArray()const{return array.end();}LLSD&operator[](const std::string&k){kind=Map;return map[k];}const LLSD&operator[](const std::string&k)const{static LLSD none;auto i=map.find(k);return i==map.end()?none:i->second;}};
struct Notice{std::string name;LLSD args,payload;std::function<bool(const LLSD&,const LLSD&)>callback;};std::vector<Notice>notices;
struct LLNotificationsUtil{template<class Callback>static void add(const std::string&name,LLSD args,LLSD payload,Callback callback){notices.push_back({name,args,payload,callback});}static int getSelectedOption(const LLSD&,const LLSD&response){return response.asInteger();}};
std::ostringstream logStream;
#define LL_INFOS() logStream
#define LL_WARNS() logStream
#define LL_ENDL ""
struct LLSLURL{LLUUID id;LLSLURL(const char*,LLUUID i,const char*):id(i){}std::string getSLURLString(){return id.asString();}};constexpr int RLV_BHVR_SHOWNAMES=1,GP_LAND_MANAGE_BANNED=2;struct Rlv{bool hasBehaviour(int){return false;}}gRlvHandler;
struct LLParcel{};struct Selection{const LLParcel*getParcel(){static LLParcel p;return&p;}};bool banEnabled=true;
struct LLViewerParcelMgr{static LLViewerParcelMgr*getInstance(){static LLViewerParcelMgr p;return&p;}Selection*selectParcelAt(LLVector3d){static Selection s;return&s;}bool isParcelOwnedByAgent(const LLParcel*,int){return banEnabled;}};
struct LLViewerRegion{LLHost host;LLHost getHost()const{return host;}};std::map<int,LLViewerRegion>regions;
bool getRegionFromAgentID(LLUUID id,const LLViewerRegion**r){auto i=regions.find(id.value);if(i==regions.end())return false;*r=&i->second;return true;}bool getPosGlobalFromAgentID(LLUUID id,LLVector3d&p){p.value=id.value;return regions.count(id.value);}
struct VSAvatarContext{const LLViewerRegion*region;static std::optional<VSAvatarContext>resolve(LLUUID id){const LLViewerRegion*r=nullptr;if(!getRegionFromAgentID(id,&r))return{};return VSAvatarContext{r};}bool current()const{return region!=nullptr;}LLHost host()const{return region->getHost();}};
struct Agent{LLUUID getID(){return{7};}LLUUID getSessionID(){return{8};}}gAgent;
struct Packet{std::string name;int host=0;std::vector<std::string>fields;bool operator==(const Packet&o)const{return name==o.name&&host==o.host&&fields==o.fields;}};struct Message{Packet pending;std::vector<Packet>packets;void newMessage(const char*n){pending=Packet{};pending.name=n;}void nextBlock(const char*n){pending.fields.push_back(std::string("block:")+n);}void nextBlockFast(const char*n){nextBlock(n);}void addUUID(const char*n,LLUUID id){pending.fields.push_back(std::string(n)+":"+id.asString());}void addUUIDFast(const char*n,LLUUID id){addUUID(n,id);}void addU32(const char*n,unsigned v){pending.fields.push_back(std::string(n)+":"+std::to_string(v));}void addString(const char*n,const std::string&v){pending.fields.push_back(std::string(n)+":"+v);}void addString(const char*n,const char*v){addString(n,std::string(v?v:""));}void sendReliable(LLHost host){pending.host=host.value;packets.push_back(pending);}}message,*gMessageSystem=&message;
const char*_PREHASH_AgentData="AgentData",*_PREHASH_AgentID="AgentID",*_PREHASH_SessionID="SessionID",*_PREHASH_TransactionID="TransactionID";using strings_t=std::vector<std::string>;
int accountEpoch=1;std::function<bool()>vs_native_im_guard(){int epoch=accountEpoch;return[epoch]{return epoch==accountEpoch;};}
WRAPPERS
struct LLAvatarActions{static bool canLandFreezeOrEjectMultiple(uuid_vec_t&,bool){return true;}static bool canEstateKickOrTeleportHomeMultiple(uuid_vec_t&,bool){return true;}static void landEjectMultiple(const uuid_vec_t&);static void landFreezeMultiple(const uuid_vec_t&);static void estateKickMultiple(const uuid_vec_t&);static void estateTeleportHomeMultiple(const uuid_vec_t&);static bool callbackLandEject(const LLSD&,const LLSD&);static bool callbackLandFreeze(const LLSD&,const LLSD&);static bool callbackEstateKick(const LLSD&,const LLSD&);static bool callbackEstateTeleportHome(const LLSD&,const LLSD&);};struct GoldAvatarActions{static bool callbackLandEject(const LLSD&,const LLSD&);static bool callbackLandFreeze(const LLSD&,const LLSD&);static bool callbackEstateKick(const LLSD&,const LLSD&);static bool callbackEstateTeleportHome(const LLSD&,const LLSD&);};
GOLD
METHODS
int main(){
for(int action=0;action<4;++action)for(bool ban:{false,true})for(int option=0;option<3;++option){regions={{1,{{101}}},{2,{{102}}}};banEnabled=ban;notices.clear();auto invoke=[&]{switch(action){case 0:LLAvatarActions::landEjectMultiple({{1},{2}});break;case 1:LLAvatarActions::landFreezeMultiple({{1},{2}});break;case 2:LLAvatarActions::estateKickMultiple({{1},{2}});break;default:LLAvatarActions::estateTeleportHomeMultiple({{1},{2}});}};invoke();assert(notices.size()==1&&notices.back().payload["ids"].size()==2);const auto note=notices.back();LLSD notification;notification["payload"]=note.payload;message.packets.clear();LLUUID::serial=0;switch(action){case 0:GoldAvatarActions::callbackLandEject(notification,LLSD(option));break;case 1:GoldAvatarActions::callbackLandFreeze(notification,LLSD(option));break;case 2:GoldAvatarActions::callbackEstateKick(notification,LLSD(option));break;default:GoldAvatarActions::callbackEstateTeleportHome(notification,LLSD(option));}auto expected=message.packets;message.packets.clear();LLUUID::serial=0;note.callback(notification,LLSD(option));assert(message.packets==expected);
message.packets.clear();invoke();const auto old=notices.back();++accountEpoch;old.callback(notification,LLSD(0));assert(message.packets.empty());
// A disappeared target is skipped; another current target still gets the exact operation.
invoke();const auto current=notices.back();regions.erase(1);message.packets.clear();current.callback(notification,LLSD(0));assert(message.packets.size()==1&&message.packets[0].host==102);
}
}
