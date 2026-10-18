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
LLUUID estateOwner{7};bool estateManager=true,god=false;int regionEpoch=1;
struct LLViewerRegion{LLHost host;LLUUID getOwner()const{return estateOwner;}bool isEstateManager()const{return estateManager;}LLHost getHost()const{return host;}};std::map<int,LLViewerRegion>regions;
bool getRegionFromAgentID(LLUUID id,const LLViewerRegion**r){auto i=regions.find(id.value);if(i==regions.end())return false;*r=&i->second;return true;}bool getPosGlobalFromAgentID(LLUUID id,LLVector3d&p){p.value=id.value;return regions.count(id.value);}
struct VSAvatarContext{const LLViewerRegion*region;static std::optional<VSAvatarContext>resolve(LLUUID id){const LLViewerRegion*r=nullptr;if(!getRegionFromAgentID(id,&r))return{};return VSAvatarContext{r};}bool current()const{return region!=nullptr;}LLHost host()const{return region->getHost();}};
struct VSRegionContext{int epoch=regionEpoch;bool current()const{return epoch==regionEpoch;}LLUUID getOwner()const{return estateOwner;}bool isEstateManager()const{return estateManager;}bool operator==(const VSRegionContext&o)const{return epoch==o.epoch;}};std::optional<VSRegionContext>vs_current_region_context(){return VSRegionContext{};}
struct LLStringUtil{using format_map_t=std::map<std::string,std::string>;};struct LLTrans{static std::string getString(const char*key,const LLStringUtil::format_map_t&){return key;}static std::string getString(const char*key){return key;}};
struct Agent{bool isGodlike(){return god;}LLViewerRegion*getRegion(){return &regions[0];}LLUUID getID(){return{7};}LLUUID getSessionID(){return{8};}}gAgent;
struct Packet{std::string name;int host=0;std::vector<std::string>fields;bool operator==(const Packet&o)const{return name==o.name&&host==o.host&&fields==o.fields;}};struct Message{Packet pending;std::vector<Packet>packets;void newMessage(const char*n){pending=Packet{};pending.name=n;}void nextBlock(const char*n){pending.fields.push_back(std::string("block:")+n);}void nextBlockFast(const char*n){nextBlock(n);}void addUUID(const char*n,LLUUID id){pending.fields.push_back(std::string(n)+":"+id.asString());}void addUUIDFast(const char*n,LLUUID id){addUUID(n,id);}void addU32(const char*n,unsigned v){pending.fields.push_back(std::string(n)+":"+std::to_string(v));}void addString(const char*n,const std::string&v){pending.fields.push_back(std::string(n)+":"+v);}void addString(const char*n,const char*v){addString(n,std::string(v?v:""));}void sendReliable(LLHost host){pending.host=host.value;packets.push_back(pending);}}message,*gMessageSystem=&message;
const char*_PREHASH_AgentData="AgentData",*_PREHASH_AgentID="AgentID",*_PREHASH_SessionID="SessionID",*_PREHASH_TransactionID="TransactionID";using strings_t=std::vector<std::string>;
int accountEpoch=1;std::function<bool()>vs_native_im_guard(){int epoch=accountEpoch;return[epoch]{return epoch==accountEpoch;};}

constexpr unsigned ESTATE_ACCESS_BANNED_AGENT_ADD=1,ESTATE_ACCESS_ALLOWED_AGENT_REMOVE=2,ESTATE_ACCESS_NO_REPLY=4,ESTATE_ACCESS_APPLY_TO_ALL_ESTATES=8,ESTATE_ACCESS_APPLY_TO_MANAGED_ESTATES=16;
std::vector<std::pair<unsigned,int>>deltas;int invoices=0;struct LLFloaterRegionInfo{static void nextInvoice(){++invoices;}};struct LLPanelEstateAccess{static void sendEstateAccessDelta(unsigned flags,LLUUID id){deltas.push_back({flags,id.value});}};
