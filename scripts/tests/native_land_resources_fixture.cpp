#include <cassert>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
using U64=unsigned long long;
struct LLSD{
 std::map<std::string,LLSD> fields;std::string text;int integer=0;
 LLSD& operator[](const std::string& key){return fields[key];}
 LLSD& operator=(int value){integer=value;return *this;}
 LLSD& operator=(const std::string& value){text=value;return *this;}
 std::string asString()const{return text;}void erase(const std::string& key){fields.erase(key);}
};
namespace LLCore{struct HttpRequest{static constexpr int DEFAULT_POLICY_ID=0;};}
int postStatus=200,summaryStatus=200,detailsStatus=200;bool summaryURL=true,detailsURL=true;
int postCalls=0,getCalls=0;std::function<void()> httpHook;
LLSD reply(int status){LLSD result;result["http_result"]=status;return result;}
namespace LLCoreHttpUtil{
struct HttpCoroutineAdapter{
 using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;static constexpr auto HTTP_RESULTS="http_result";
 bool cancelled=false;HttpCoroutineAdapter(const char*,int){}
 void cancelSuspendedOperation(){cancelled=true;}
 static bool getStatusFromLLSD(const LLSD& status){return status.integer==200;}
 LLSD postAndSuspend(std::shared_ptr<LLCore::HttpRequest>,const std::string& url,const LLSD& body){
  assert(url=="resources"&&body.fields.at("parcel_id").integer==7);++postCalls;if(httpHook)httpHook();
  auto result=reply(postStatus);if(summaryURL)result["ScriptResourceSummary"]=std::string("summary");
  if(detailsURL)result["ScriptResourceDetails"]=std::string("details");return result;
 }
 LLSD getAndSuspend(std::shared_ptr<LLCore::HttpRequest>,const std::string& url){
  assert(url=="summary"||url=="details");++getCalls;if(httpHook)httpHook();return reply(url=="summary"?summaryStatus:detailsStatus);
 }
};}
std::vector<std::function<void()>> tasks;
struct LLCoros{static LLCoros& instance(){static LLCoros value;return value;}
 void launch(const char*,std::function<void()> task){tasks.push_back(std::move(task));}};
void run(){auto next=std::move(tasks);tasks.clear();for(auto& task:next)task();}
bool accountCurrent=true;U64 epoch=1;
struct Base{virtual ~Base()=default;};struct Handle{std::weak_ptr<Base> weak;Base* get()const{return weak.lock().get();}};
struct LLUICtrl{std::string value;void setValue(const std::string& v){value=v;}};
struct LLButton:LLUICtrl{bool enabled=false;void setEnabled(bool v){enabled=v;}};
struct LLTrans{static std::string getString(const char* key){return key;}};
struct LLPanelScriptLimitsRegionMemory:Base,std::enable_shared_from_this<LLPanelScriptLimitsRegionMemory>{
 std::function<bool()> mNativeQueryCurrent;std::vector<LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t> mNativeRequests;
 U64 mNativeRequestSerial=0;unsigned mNativePendingStages=0;bool mNativeRequestFailed=false;int mParcelId=7;
 int summaries=0,details=0;LLUICtrl loading;LLButton refresh;std::function<void()> publishHook;
 Handle getHandle(){return {shared_from_this()};}
 template<class T>T* getChild(const char*){if constexpr(std::is_same_v<T,LLButton>)return &refresh;else return &loading;}
 void setRegionSummary(LLSD){++summaries;if(publishHook)publishHook();}
 void setRegionDetails(LLSD){++details;if(publishHook)publishHook();}
 void retireNativeRequests();bool requestNativeResources(const std::string&);
 void requestNativeResourceStage(const std::string&,bool,U64,std::function<bool()>);
 void finishNativeResourceStage(U64,const std::function<bool()>&,bool);
};
METHODS
std::shared_ptr<LLPanelScriptLimitsRegionMemory> panel(){
 auto self=std::make_shared<LLPanelScriptLimitsRegionMemory>();const auto original=epoch;
 self->mNativeQueryCurrent=[original]{return accountCurrent&&epoch==original;};return self;
}
int main(){
 auto self=panel();assert(!self->requestNativeResources(""));assert(self->requestNativeResources("resources"));
 run();assert(tasks.size()==2&&self->mNativePendingStages==2);run();
 assert(self->summaries==1&&self->details==1&&self->refresh.enabled&&self->loading.value.empty()&&self->mNativeRequests.empty());
 summaryStatus=503;self->refresh.enabled=false;assert(self->requestNativeResources("resources"));run();run();
 assert(self->summaries==1&&self->details==2&&self->refresh.enabled&&self->loading.value=="ScriptLimitsRequestError");summaryStatus=200;
 postStatus=503;assert(self->requestNativeResources("resources"));const auto gets=getCalls;run();assert(tasks.empty()&&getCalls==gets&&self->mNativePendingStages==0);postStatus=200;
 detailsURL=false;assert(self->requestNativeResources("resources"));run();run();assert(self->loading.value=="ScriptLimitsRequestError");detailsURL=true;
 assert(self->requestNativeResources("resources"));run();self->retireNativeRequests();const auto old=self->summaries+self->details;run();assert(self->summaries+self->details==old);
 assert(self->requestNativeResources("resources"));run();self->publishHook=[&]{++epoch;};run();self->publishHook={};assert(self->mNativePendingStages==2);
 self=panel();assert(self->requestNativeResources("resources"));httpHook=[]{accountCurrent=false;};run();httpHook={};assert(tasks.empty()&&self->summaries==0&&self->details==0);accountCurrent=true;
 self=panel();assert(self->requestNativeResources("resources"));run();self.reset();const auto before=getCalls;run();assert(getCalls==before);
}
