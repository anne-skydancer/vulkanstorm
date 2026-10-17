#include <algorithm>
#include <cassert>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
using U8=unsigned char;using U32=unsigned;using U64=unsigned long long;using S32=int;using LLExtStat=int;
struct LLUUID {int value=0;bool isNull()const{return !value;}bool notNull()const{return value;}bool operator==(const LLUUID& o)const{return value==o.value;}bool operator!=(const LLUUID& o)const{return !(*this==o);}};
using LLHost=int;
template<class T>struct LLHandle {T* raw=nullptr;std::weak_ptr<int> alive;T* get()const{return alive.expired()?nullptr:raw;}};
struct LLView {std::shared_ptr<int>alive=std::make_shared<int>(0);LLHandle<LLView>getHandle(){return {this,alive};}};
int epoch=1;bool circuit=true,target=true;
std::function<bool()>vs_native_im_guard(){int before=epoch;return [before]{return before==epoch;};}
struct VSRegionContext {LLHost host=1;int incarnation=1;bool current()const{return circuit && incarnation==1;}LLHost getHost()const{return host;}};
struct LLSD {int integer=0;LLSD&operator[](const char*){return *this;}LLSD&operator=(int n){integer=n;return *this;}};
struct LLTextBase {virtual~LLTextBase()=default;};
struct LLViewerTextEditor:LLTextBase {struct Params {void name(const char*){}void max_text_length(int){}};static inline bool valid=true;bool importBuffer(const char*,int){return valid;}};
struct LLUICtrlFactory {template<class T>static T*create(const typename T::Params&){return new T;}};
struct LLTrans {static std::string getString(const char* name){return name;}};
struct LLStringUtil {static void format(std::string&,const LLSD&) {}};
struct LLSLURL {LLSLURL(const char*,LLUUID,const char*){}std::string getSLURLString()const{return "owner";}};
struct Agent {LLUUID getID()const{return {1};}LLUUID getSessionID()const{return {2};}}gAgent;
namespace LLAssetType {enum EType {AT_NOTECARD};}
constexpr int ET_Covenant=1,LL_ERR_ASSET_REQUEST_NOT_IN_DATABASE=-1,LL_ERR_FILE_EMPTY=-2,LL_ERR_INSUFFICIENT_PERMISSIONS=-3;
struct LLFileSystem {enum{READ};static inline std::string data="Linden text version 2\ntext";LLFileSystem(LLUUID,LLAssetType::EType,int){}int getSize()const{return int(data.size());}void read(U8* out,int n){memcpy(out,data.data(),n);}};
struct Storage {
 using Callback=void(*)(const LLUUID&,LLAssetType::EType,void*,S32,LLExtStat);
 struct Pending{LLUUID asset;Callback callback;void*data;};std::vector<Pending>pending;
 void getEstateAsset(LLHost host,LLUUID,LLUUID,LLUUID asset,LLAssetType::EType,int,Callback cb,void*data,bool){assert(host==1);pending.push_back({asset,cb,data});}
 void finish(S32 status=0){auto work=std::move(pending);pending.clear();for(auto&p:work)p.callback(p.asset,LLAssetType::AT_NOTECARD,p.data,status,0);}
}storage;Storage*gAssetStorage=&storage;
constexpr auto _PREHASH_AgentData="AgentData",_PREHASH_AgentID="AgentID",_PREHASH_SessionID="SessionID";
struct LLMessageSystem {int sends=0;LLHost sender=1;LLUUID asset,owner{1};std::string estate="estate";U32 date=0;
 LLHost getSender()const{return sender;}void getUUID(const char*,const char*name,LLUUID&out){out=std::string(name)=="CovenantID"?asset:owner;}void getString(const char*,const char*,std::string&out){out=estate;}void getU32(const char*,const char*,U32&out){out=date;}
 void newMessage(const char*){}void nextBlockFast(const char*){}void addUUIDFast(const char*,LLUUID){}void sendReliable(LLHost){++sends;}
}messages;LLMessageSystem*gMessageSystem=&messages;
#define LL_DEBUGS(...) std::ostringstream()
#define LL_WARNS(...) std::ostringstream()
#define LL_ENDL std::endl
PRODUCTION
int main(){
 int names=0,texts=0,contents=0,owners=0,dates=0;std::string last;
 VSCovenantCallbacks cb;cb.estateName=[&](auto&){++names;};cb.ownerName=[&](auto&){++owners;};cb.lastModified=[&](auto&){++dates;};cb.text=[&](auto&s,auto&){last=s;++texts;};cb.content=[&](auto*,auto&){++contents;};
 LLView view;VSRegionContext region;
 vs_request_covenant(region,view,cb,[]{return target;});assert(messages.sends==1);
 messages.sender=2;vs_receive_covenant(&messages);assert(names==0);
 messages.sender=1;vs_receive_covenant(&messages);assert(names==1 && owners==1 && dates==1 && texts==1 && last=="RegionNoCovenantOtherOwner");
 messages.asset={3};vs_receive_covenant(&messages);assert(storage.pending.size()==1);storage.finish();assert(contents==1);
 // A new publication supersedes even an older asset reply for the same live view.
 vs_receive_covenant(&messages);messages.asset={4};vs_receive_covenant(&messages);storage.finish();assert(contents==2);
 vs_receive_covenant(&messages);++epoch;storage.finish();assert(contents==2);
 vs_request_covenant(region,view,cb,[]{return target;});vs_receive_covenant(&messages);target=false;storage.finish();assert(contents==2);target=true;
 vs_request_covenant(region,view,cb);vs_receive_covenant(&messages);circuit=false;storage.finish();assert(contents==2);circuit=true;
 vs_request_covenant(region,view,cb);vs_receive_covenant(&messages);vs_reset_covenants();storage.finish();assert(contents==2);
 {auto dead=std::make_unique<LLView>();vs_request_covenant(region,*dead,cb);vs_receive_covenant(&messages);dead.reset();storage.finish();assert(contents==2);}
 // Exact shared notecard parser refusal/error presentation remains intact.
 vs_request_covenant(region,view,cb);LLFileSystem::data="invalid";vs_receive_covenant(&messages);storage.finish();assert(last.find("format error")!=std::string::npos);
 LLFileSystem::data="Linden text version 2\ntext";vs_receive_covenant(&messages);storage.finish(LL_ERR_INSUFFICIENT_PERMISSIONS);assert(last.find("Insufficient permissions")!=std::string::npos);
 LLViewerTextEditor::valid=false;vs_receive_covenant(&messages);storage.finish();assert(last=="Problem importing estate covenant.");LLViewerTextEditor::valid=true;
 // Reentrant logout in the first observer suppresses later observers and asset IO.
 vs_reset_covenants();const auto before=storage.pending.size();int later=0;
 cb.estateName=[](auto&){++epoch;};cb.ownerName=[&](auto&){++later;};
 vs_request_covenant(region,view,cb);vs_receive_covenant(&messages);assert(!later && storage.pending.size()==before);
}
