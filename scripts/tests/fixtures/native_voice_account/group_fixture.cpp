#include <cassert>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <sstream>
#include <utility>
using LLUUID=std::string;
struct LLSD {
 std::string text;std::map<std::string,LLSD> values;
 using map_const_iterator=std::map<std::string,LLSD>::const_iterator;
 LLSD& operator[](const std::string& key){return values[key];}
 const LLSD& operator[](const std::string& key)const{static LLSD empty;auto i=values.find(key);return i==values.end()?empty:i->second;}
 LLSD& operator=(const std::string& value){text=value;return *this;}
 LLSD& operator=(const char* value){text=value;return *this;}
 std::string asString()const{return text;}
 map_const_iterator beginMap()const{return values.begin();}map_const_iterator endMap()const{return values.end();}
 void erase(const std::string& key){values.erase(key);}
};
std::ostream& operator<<(std::ostream& out,const LLSD&){return out;}
std::ostringstream logger;
#define LL_INFOS(...) logger
#define LL_DEBUGS(...) logger
#define LL_WARNS(...) logger
#define LL_ENDL std::endl
constexpr int HTTP_FORBIDDEN=403;
struct Settings {std::string getString(const char*){return "";}}gSavedSettings;
struct LLVoiceVersionInfo {std::string internalVoiceServerType="webrtc";};
struct LLVoiceClient {static LLVoiceClient* getInstance(){static LLVoiceClient c;return &c;}LLVoiceVersionInfo getVersion(){return {};}};
LLSD posted;std::function<void()> suspendAction;int requestCount=0,responseStatus=200;
namespace LLCore {
struct HttpRequest {using ptr_t=std::shared_ptr<HttpRequest>;using policy_t=int;static constexpr int DEFAULT_POLICY_ID=0;};
struct HttpStatus {int code;explicit HttpStatus(int c=200):code(c){}explicit operator bool()const{return code==200;}bool operator==(const HttpStatus& b)const{return code==b.code;}};
}
namespace LLCoreHttpUtil {
struct HttpCoroutineAdapter {
 using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;
 static inline std::string HTTP_RESULTS="http_results";
 HttpCoroutineAdapter(const char*,int){}
 LLSD postAndSuspend(std::shared_ptr<LLCore::HttpRequest>,const std::string&,const LLSD& body){
  ++requestCount;posted=body;if(suspendAction)suspendAction();LLSD result;result["voice_credentials"]["session"]="accepted";return result;}
 static LLCore::HttpStatus getStatusFromLLSD(const LLSD&){return LLCore::HttpStatus(responseStatus);}
};
}
struct LLVoiceChannel {
 static inline std::map<LLUUID,LLVoiceChannel*> channels;
 int applied=0,deactivated=0;
 std::shared_ptr<const void> lifetime=std::make_shared<char>(0);
 explicit LLVoiceChannel(const LLUUID& id){channels[id]=this;}
 static LLVoiceChannel* getChannelByID(const LLUUID& id){auto p=channels.find(id);return p==channels.end()?nullptr:p->second;}
 LLSD getNotifyArgs(){return {};}
 void deactivate(){++deactivated;}void setChannelInfo(const LLSD& result){assert(result["session"].asString()=="accepted");++applied;}
};
int notifications=0;
namespace LLNotificationsUtil {void add(const char*,const LLSD&){++notifications;}}
struct LLVoiceChannelGroup:LLVoiceChannel {
 using LLVoiceChannel::LLVoiceChannel;
 static void voiceCallCapCoro(std::string,LLUUID,std::weak_ptr<const void>,LLVoiceChannel*,std::function<bool()>);
};
GROUP_METHOD
int main(){
 bool current=true;auto guard=[&]{return current;};
 auto source=std::make_unique<LLVoiceChannelGroup>("conference-real");auto* original=source.get();
 LLVoiceChannelGroup::voiceCallCapCoro("actual-cap","conference-real",source->lifetime,source.get(),guard);
 assert(source->applied==1&&requestCount==1);
 assert(posted["method"].asString()=="call"&&posted["session-id"].asString()=="conference-real");
 assert(posted["alt_params"]["preferred_voice_server_type"].asString()=="webrtc");
 suspendAction=[&]{current=false;};
 LLVoiceChannelGroup::voiceCallCapCoro("actual-cap","conference-real",source->lifetime,source.get(),guard);
 assert(source->applied==1&&notifications==0);current=true;
 auto oldLifetime=std::weak_ptr<const void>(source->lifetime);
 suspendAction=[&]{LLVoiceChannel::channels.erase("conference-real");source.reset();source=std::make_unique<LLVoiceChannelGroup>("conference-real");};
 LLVoiceChannelGroup::voiceCallCapCoro("actual-cap","conference-real",oldLifetime,original,guard);
 assert(source->applied==0&&oldLifetime.expired()&&notifications==0);
 const int requests=requestCount;
 LLVoiceChannelGroup::voiceCallCapCoro("actual-cap","conference-real",oldLifetime,original,guard);
 assert(requestCount==requests);
 suspendAction={};responseStatus=403;
 LLVoiceChannelGroup::voiceCallCapCoro("actual-cap","conference-real",source->lifetime,source.get(),guard);
 assert(source->deactivated==1&&notifications==1);
}
