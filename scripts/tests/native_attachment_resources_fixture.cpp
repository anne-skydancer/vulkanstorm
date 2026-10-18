#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>
using U64=unsigned long long;
struct LLSD{int status=200;LLSD& operator[](const char*){return *this;}void erase(const char*){}};
namespace LLCore{struct HttpRequest{static constexpr int DEFAULT_POLICY_ID=0;};}
int httpStatus=200,httpCalls=0;std::function<void()> httpHook;std::string requestedURL;
namespace LLCoreHttpUtil{
struct HttpCoroutineAdapter {
 using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;static constexpr auto HTTP_RESULTS="http_result";
 bool cancelled=false;HttpCoroutineAdapter(const char*,int){}
 void cancelSuspendedOperation(){cancelled=true;}
 LLSD getAndSuspend(std::shared_ptr<LLCore::HttpRequest>,const std::string& url){
  ++httpCalls;requestedURL=url;if(httpHook)httpHook();return {httpStatus};
 }
 static bool getStatusFromLLSD(const LLSD& result){return result.status==200;}
};}
std::vector<std::function<void()>> tasks;
struct LLCoros{
 static LLCoros& instance(){static LLCoros c;return c;}
 void launch(const char*,std::function<void()> task){tasks.push_back(std::move(task));}
};
void run(){auto next=std::move(tasks);tasks.clear();for(auto& task:next)task();}
bool accountCurrent=true;std::function<bool()> vs_native_im_guard(){return []{return accountCurrent;};}
U64 regionEpoch=1;std::string capability="attachment-resources";bool connected=true;
struct VSRegionContext{
 U64 epoch=regionEpoch;bool current()const{return connected&&epoch==regionEpoch;}
 std::string getCapability(const char*)const{return current()?capability:std::string();}
};
std::optional<VSRegionContext> vs_current_region_context(){return connected?std::optional<VSRegionContext>({regionEpoch}):std::nullopt;}
struct VSNativeSession{static bool active(){return true;}};
struct LLUICtrl{std::string value;void setValue(const std::string& v){value=v;}};
struct LLButton:LLUICtrl{bool enabled=false;void setEnabled(bool v){enabled=v;}};
struct LLTrans{static std::string getString(const char* key){return key;}};
struct Base{virtual ~Base()=default;};
struct Handle{std::weak_ptr<Base> value;Base* get()const{return value.lock().get();}};
struct LLFloaterMyScripts:Base,std::enable_shared_from_this<LLFloaterMyScripts>{
 std::optional<VSRegionContext> mNativeRegion;std::string mNativeCapability;
 LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t mNativeRequest;U64 mNativeRequestSerial=0;
 bool mAttachmentDetailsRequested=true;LLButton refresh;LLUICtrl loading;int publications=0;
 Handle getHandle(){return {shared_from_this()};}
 template<class T>T* getChild(const char*){if constexpr(std::is_same_v<T,LLButton>)return &refresh;else return &loading;}
 void setAttachmentDetails(LLSD){++publications;}
 bool requestAttachmentDetails();
};
REQUEST
int main(){
 auto self=std::make_shared<LLFloaterMyScripts>();
 capability="";assert(!self->requestAttachmentDetails()&&tasks.empty());
 capability="attachment-resources";assert(self->requestAttachmentDetails());assert(self->requestAttachmentDetails()&&tasks.size()==1);
 run();assert(httpCalls==1&&self->publications==1&&self->refresh.enabled&&!self->mNativeRequest&&requestedURL==capability);
 httpStatus=503;self->refresh.enabled=false;assert(self->requestAttachmentDetails());run();
 assert(self->publications==1&&self->refresh.enabled&&!self->mAttachmentDetailsRequested&&self->loading.value=="ScriptLimitsRequestError");
 httpStatus=200;assert(self->requestAttachmentDetails());httpHook=[]{++regionEpoch;};run();httpHook={};
 assert(self->publications==1);self->mNativeRequest.reset();
 assert(self->requestAttachmentDetails());httpHook=[]{accountCurrent=false;};run();httpHook={};
 assert(self->publications==1);accountCurrent=true;self->mNativeRequest.reset();
 assert(self->requestAttachmentDetails());httpHook=[&]{++self->mNativeRequestSerial;};run();httpHook={};assert(self->publications==1);
 self->mNativeRequest.reset();assert(self->requestAttachmentDetails());httpHook=[&]{self.reset();};run();httpHook={};assert(!self);
 auto closing=std::make_shared<LLFloaterMyScripts>();assert(closing->requestAttachmentDetails());closing.reset();
 const auto before=httpCalls;run();assert(httpCalls==before);
}
