#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>
#include "vsresourcebudget.h"
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;using S16=int16_t;using S32=int32_t;using F64=double;
constexpr S32 UUID_BYTES=16;
struct LLUUID{U8 mData[16]{};bool operator==(const LLUUID&)const=default;bool operator<(const LLUUID& rhs)const{return std::lexicographical_compare(mData,mData+16,rhs.mData,rhs.mData+16);}};
LLUUID uuid(int value){LLUUID id;id.mData[0]=U8(value);id.mData[1]=U8(value>>8);return id;}
using uuid_vec_t=std::vector<LLUUID>;
struct LLHost{int id=1;bool operator==(const LLHost&)const=default;bool operator<(const LLHost& other)const{return id<other.id;}};
namespace boost::signals2{
struct connection{};template<class>class signal;
template<>class signal<void()>{std::vector<std::function<void()>> callbacks;public:connection connect(std::function<void()> f){callbacks.push_back(std::move(f));return {};}void operator()(){const auto copy=callbacks;for(const auto& f:copy)f();}};
}
class LLMessageSystem;
struct LLCategory{void unpackMultiMessage(LLMessageSystem*,const char*,S32){}void unpackMessage(LLMessageSystem*,const char*){}};
struct LLSaleInfo:LLCategory{};
struct LLAggregatePermissions{void unpackMessage(LLMessageSystem*,const char*,const char*,S32){}};
struct LLPermissions{void init(LLUUID,LLUUID,LLUUID,LLUUID){}void initMasks(U32,U32,U32,U32,U32){}};
PROPERTIES_HEADER
PREHASH
class LLMessageSystem{
public:
 struct Packet{std::string name;LLHost host;S32 objects=0;};std::vector<Packet> sent;Packet packet;
 std::string name="ObjectProperties";LLHost sender;LLUUID replyID=uuid(1);U32 flags=0;S32 count=1;
 const char* getMessageName(){return name.c_str();}LLHost getSender(){return sender;}
 void newMessage(const char* value){packet={value,{1},0};}void nextBlock(const char* value){if(std::string(value)=="ObjectData")++packet.objects;}
 void addUUID(const char*,LLUUID){}void addU32(const char*,U32){}bool isSendFull(void*){return packet.objects>=100;}
 void sendReliable(LLHost host){packet.host=host;sent.push_back(packet);}
 S32 getNumberOfBlocks(const char*){return count;}
 bool replaceHighlights=true;S32 local=1;void getBOOL(const char*,const char*,bool& value){value=replaceHighlights;}void getS32(const char*,const char*,S32& value,S32=0){value=local;}
 void getUUID(const char*,const char*,LLUUID& result,S32=0){result=replyID;}
 void getUUIDFast(const char*,const char*,LLUUID& result,S32=0){result=replyID;}
 void getU64Fast(const char*,const char*,U64& result,S32=0){result=123;}
 void getU32Fast(const char*,const char* field,U32& result,S32=0){result=std::string(field)=="RequestFlags"?flags:123;}
 void getS32Fast(const char*,const char*,S32& result,S32=0){result=123;}
 void getS16Fast(const char*,const char*,S16& result,S32=0){result=123;}
 void getStringFast(const char*,const char* field,std::string& result,S32=0){result=field;}
 S32 getSizeFast(const char*,S32,const char*){return 0;}
 void getBinaryDataFast(const char*,const char*,void*,S32,S32,S32){}
} message,*gMessageSystem=&message;
struct VSWorldScene{
 struct ObjectHandle{LLHost host;LLUUID id;U32 local=1;U64 region=1,generation=1;int kind=0;LLUUID account=uuid(1);U64 accountGeneration=1;};
 std::map<LLUUID,ObjectHandle> objects;std::map<LLUUID,bool> frozen;VSResourceBudget budget{1024*1024};bool refuse=false;
 bool current(const ObjectHandle& token)const{auto found=objects.find(token.id);return found!=objects.end() && found->second.generation==token.generation && found->second.host==token.host && found->second.local==token.local && found->second.region==token.region && found->second.account==token.account && found->second.accountGeneration==token.accountGeneration;}
 std::optional<ObjectHandle> object(LLHost host,U32 local){for(const auto& [id,value]:objects)if(value.host==host && value.local==local)return value;return {};}
 bool freeze(const ObjectHandle& token,bool selected){if(!current(token))return false;frozen[token.id]=selected;return true;}
 std::shared_ptr<const VSResourceBudget::Reservation> reserveSpatialBytes(std::size_t bytes){return refuse?nullptr:budget.reserve(bytes);}
 std::function<bool()> guard(ObjectHandle token){return [this,token]{return current(token);};}
} world;
struct Context{VSWorldScene& world(){return ::world;}} context;
struct ViewerWindow{bool native=true;bool isNativeVulkan(){return native;}Context* nativeContext(){return &context;}} window,*gViewerWindow=&window;
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession>{
 enum class Phase{Connected,Disconnected};Phase state=Phase::Connected;U64 generation=1;struct Account{LLUUID agent=uuid(1),session=uuid(2);U64 generation=1;};
 Phase phase(){return state;}Account account(){return {uuid(1),uuid(2),generation};}
 bool current(Account value){return state==Phase::Connected && generation==value.generation;}
 std::function<bool()> accountGuard(){const auto token=account();return [weak=weak_from_this(),token]{auto self=weak.lock();return self && self->current(token);};}
 bool admit(const std::string&,LLHost host){return state==Phase::Connected && (host.id==1 || host.id==2);}
};
struct LLFrameTimer{static inline double now=0;static double getTotalSeconds(){return now;}};
SELECTION_HEADER
PROPERTIES_SOURCE
SELECTION_SOURCE
int main(){
 auto owner=std::make_shared<VSNativeSession>();VSObjectSelection selection(owner);
 VSWorldScene::ObjectHandle first;first.id=uuid(1);first.local=1;world.objects[first.id]=first;
 auto second=first;second.id=uuid(2);second.local=2;second.host={2};world.objects[second.id]=second;
 unsigned changed=0;selection.changed([&]{++changed;});assert(selection.select({first,second}));assert(changed==1);
 assert(message.sent.size()==2 && message.sent[0].name=="ObjectSelect" && message.sent[0].host.id==1 && message.sent[1].host.id==2);
 auto guard=selection.selectionGuard();assert(guard());assert(!selection.properties(first));
 bool panelCurrent=true;
 assert(!selection.highlight({first},true));
 assert(selection.beginHighlightRequest([&]{return panelCurrent;}));
 message.name="ForceObjectSelect";message.local=first.local;selection.receive(message);
 assert(selection.highlights().size()==1 && selection.highlights()[0].id==first.id);
 world.refuse=true;assert(!selection.highlight({second},true));assert(selection.highlights()[0].id==first.id);world.refuse=false;
 message.sender={2};message.local=second.local;message.replaceHighlights=false;selection.receive(message);assert(selection.highlights().size()==2);
 panelCurrent=false;selection.receive(message);assert(selection.highlights().empty());selection.tick();
 panelCurrent=true;assert(selection.beginHighlightRequest([&]{return panelCurrent;}));assert(selection.highlight({second},true));selection.clearHighlights();selection.receive(message);assert(selection.highlights().empty());

 message.name="ObjectProperties";message.replyID=first.id;message.sender={3};selection.receive(message);assert(!selection.properties(first));
 message.sender={1};world.refuse=true;selection.receive(message);assert(!selection.properties(first));world.refuse=false;
 selection.receive(message);auto publication=selection.properties(first);assert(publication && publication->properties->complete && publication->properties->name=="Name");
 const auto old=publication->properties;world.refuse=true;selection.receive(message);assert(!selection.properties(first) && old->name=="Name");
 assert(!selection.select({first,first}));assert(guard());
 auto third=first;third.id=uuid(3);third.local=3;world.objects[third.id]=third;assert(!selection.select({third}));assert(guard());world.refuse=false;
 LLFrameTimer::now=6;selection.tick();assert(message.sent.back().name=="ObjectSelect");selection.receive(message);assert(selection.properties(first));
 message.replyID=third.id;selection.receive(message);assert(!selection.properties(third));
 unsigned families=0;assert(selection.requestFamily(second,8,[&](const auto& p){assert(p && p->object.id==second.id && !p->properties->complete);++families;}));
 assert(selection.requestFamily(second,8,[&](const auto& p){assert(p);++families;}));
 assert(message.sent.back().name=="RequestObjectPropertiesFamily");
 message.name="ObjectPropertiesFamily";message.replyID=second.id;message.sender={1};message.flags=8;selection.receive(message);assert(families==0);
 message.sender={2};message.flags=9;selection.receive(message);assert(families==0);
 message.flags=8;selection.receive(message);assert(families==2);selection.receive(message);assert(families==2);
 assert(selection.requestFamily(first,4,[&](const auto& p){assert(!p);++families;}));LLFrameTimer::now=67;selection.tick();assert(families==3);
 bool retired=false;assert(selection.requestFamily(first,1,[&](const auto&){retired=true;}));++world.objects[first.id].generation;
 message.sender={1};message.replyID=first.id;message.flags=1;selection.receive(message);assert(!retired);selection.tick();assert(!retired && !guard());
 assert(selection.selected().size()==1 && selection.selected()[0].id==second.id);
 assert(selection.select({second}));auto current=selection.selectionGuard();assert(current());
 bool stale=false;assert(selection.requestFamily(second,1,[&](const auto&){stale=true;}));++owner->generation;
 message.sender={2};message.replyID=second.id;selection.receive(message);assert(!stale && !current() && selection.selected().empty() && !selection.properties(second));selection.reset();assert(selection.selected().empty());
 owner->state=VSNativeSession::Phase::Disconnected;assert(!selection.select({second}));
}
