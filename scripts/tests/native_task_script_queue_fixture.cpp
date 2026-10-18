#include <cassert>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <vector>
#include <sstream>
#define VS_NATIVE_VULKAN 1
struct LLUUID{int value;LLUUID(int v=0):value(v){}operator int()const{return value;}std::string asString()const{return std::to_string(value);}};using S32=int;using F32=float;
#define LL_INFOS(x) queueLog
#define LL_DEBUGS(x) queueLog
#define LL_WARNS(x) queueLog
#define LL_ENDL std::endl
std::ostringstream queueLog;
struct LLSD{using Boolean=bool;std::map<std::string,bool>values;bool has(std::string key)const{return values.count(key);}bool&operator[](std::string key){return values[key];}};
struct LLSDMap:LLSD{LLSDMap(std::string key,bool value){values[key]=value;}};
LLSD pending;int posts=0;
struct LLEventPump{std::string getName()const{return"queue";}void post(LLSD value){pending=value;++posts;}};
struct LLEventMailDrop:LLEventPump{LLEventMailDrop(std::string,bool){}};
struct LLEventPumps{static LLEventPumps&instance(){static LLEventPumps value;return value;}void post(std::string,LLSD value){pending=value;++posts;}};
std::function<void()>onWait;
namespace llcoro{LLSD suspendUntilEventOnWithTimeout(LLEventPump&,float,LLSD fallback){if(onWait)onWait();auto result=pending;pending={};return result.values.empty()?fallback:result;}void suspend(){if(onWait)onWait();}}
struct LLCoros{static void set_consuming(bool){}};
struct LLAssetType{enum EType{AT_CATEGORY,AT_LSL_TEXT};};
struct LLInventoryObject;
struct ItemPtr{std::shared_ptr<LLInventoryObject>value;LLInventoryObject*operator->()const{return value.get();}operator LLInventoryObject*()const{return value.get();}};
struct LLInventoryObject{using object_list_t=std::vector<ItemPtr>;int id;LLAssetType::EType type=LLAssetType::AT_LSL_TEXT;LLUUID getUUID()const{return id;}auto getType()const{return type;}std::string getName()const{return std::to_string(id);}};
struct VSTaskInventory{struct Result{bool success=false;LLInventoryObject::object_list_t contents;};};
int account=1;struct Source{int id;bool valid=true,success=true,deferred=false;std::vector<int>items;};
std::function<void(const VSTaskInventory::Result&)>late;
struct VSTaskInventoryObject{std::shared_ptr<Source>source;const VSTaskInventoryObject*operator->()const{return this;}explicit operator bool()const{return source&&source->valid;}bool native()const{return true;}void*legacy()const{return nullptr;}int getID()const{return source->id;}int getLocalID()const{return source->id+100;}int getHost()const{return source->id+200;}auto itemGuard(int)const{auto own=source;int old=account;return[own,old]{return own&&own->valid&&old==account;};}bool requestInventory(std::function<void(const VSTaskInventory::Result&)> callback)const{if(source->deferred){late=callback;return true;}VSTaskInventory::Result result;result.success=source->success;for(int id:source->items)result.contents.push_back({std::make_shared<LLInventoryObject>(LLInventoryObject{id})});callback(result);return true;}};
struct ObjectInventoryFetcher{using ptr_t=std::shared_ptr<ObjectInventoryFetcher>;LLInventoryObject::object_list_t contents;ObjectInventoryFetcher(LLEventPump&,void*,void*){}void fetchInventory(){}auto&getInventoryList(){return contents;}};
template<class T>struct LLHandle{T*value;};
struct LLCheckedHandleBase{struct Stale{};};
template<class T>struct LLCheckedHandle{T*value;LLCheckedHandle(LLHandle<T>handle):value(handle.value){}T*operator->(){check();return value;}void check(){if(!value)throw LLCheckedHandleBase::Stale{};}};
namespace LLStringUtil{using format_map_t=std::map<std::string,std::string>;}
struct Agent{int getID()const{return 9;}int getSessionID()const{return 10;}}gAgent;
#define _PREHASH_ScriptReset "ScriptReset"
#define _PREHASH_AgentData "AgentData"
#define _PREHASH_AgentID "AgentID"
#define _PREHASH_SessionID "SessionID"
#define _PREHASH_Script "Script"
#define _PREHASH_ObjectID "ObjectID"
#define _PREHASH_ItemID "ItemID"
struct LLMessageSystem{struct Packet{int object=0,item=0,host=0;};Packet current;std::vector<Packet>sent;void newMessageFast(const char*){current={};}void nextBlockFast(const char*){}void addUUIDFast(std::string key,int value){if(key=="ObjectID")current.object=value;if(key=="ItemID")current.item=value;}void sendReliable(int host){current.host=host;sent.push_back(current);}}message;LLMessageSystem*gMessageSystem=&message;
const std::string QUEUE_EVENTPUMP_NAME="ScriptActionQueue";constexpr float QUEUE_INVENTORY_FETCH_TIMEOUT=300;
struct LLFloaterScriptQueue{struct ObjectData{LLUUID mObjectId;std::string mObjectName;VSTaskInventoryObject mSource;};using object_data_list_t=std::vector<ObjectData>;using fnQueueAction_t=std::function<bool(const VSTaskInventoryObject&,LLInventoryObject*,LLEventPump&)>;int original=account;std::shared_ptr<bool>mQueueLive=std::make_shared<bool>(true);int done=0;auto queueGuard()const{int old=original;auto live=mQueueLive;return[old,live]{return *live&&old==account;};}std::string getString(std::string text,const LLStringUtil::format_map_t& args={}){return text;}void addStringMessage(std::string text){if(text=="Done")++done;}struct Close{void setEnabled(bool){}}close;Close*getChildView(std::string){return&close;}static void objectScriptProcessingQueueCoro(std::string action,LLHandle<LLFloaterScriptQueue>hfloater,object_data_list_t objectList,fnQueueAction_t func)QUEUE};
struct LLFloaterResetQueue{static bool resetObjectScripts(LLHandle<LLFloaterScriptQueue>hfloater,const VSTaskInventoryObject&object,LLInventoryObject*inventory,LLEventPump&pump)ACTION};
int main(){LLFloaterScriptQueue queue;auto a=std::make_shared<Source>(Source{1,true,true,false,{11,12}}),failed=std::make_shared<Source>(Source{2,true,false,false,{21}}),b=std::make_shared<Source>(Source{3,true,true,false,{31}});auto action=[&](const VSTaskInventoryObject&o,LLInventoryObject*i,LLEventPump&p){return LLFloaterResetQueue::resetObjectScripts({&queue},o,i,p);};LLFloaterScriptQueue::objectScriptProcessingQueueCoro("reset",{&queue},{{{1},"a",{a}},{{2},"failed",{failed}},{{3},"b",{b}}},action);assert(queue.done==1&&message.sent.size()==3);assert(message.sent[0].object==1&&message.sent[0].item==11&&message.sent[0].host==201);assert(message.sent[1].item==12&&message.sent[2].object==3&&message.sent[2].item==31);message.sent.clear();queue.done=0;onWait=[&]{++account;onWait={};};LLFloaterScriptQueue::objectScriptProcessingQueueCoro("reset",{&queue},{{{1},"a",{a}}},action);assert(message.sent.empty()&&queue.done==0);queue.original=account;auto deferred=std::make_shared<Source>(Source{4,true,true,true,{41}});LLFloaterScriptQueue::objectScriptProcessingQueueCoro("reset",{&queue},{{{4},"late",{deferred}}},action);auto old=posts;late({true,{}});assert(posts==old);}
