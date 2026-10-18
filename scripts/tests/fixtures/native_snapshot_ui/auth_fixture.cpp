#include <cassert>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <utility>
struct LLSD {
 std::string text;std::map<std::string,LLSD> map;
 LLSD& operator[](const std::string& key){return map[key];}
 const LLSD& operator[](const std::string& key)const {auto i=map.find(key);static LLSD empty;return i==map.end()?empty:i->second;}
 std::string asString()const{return text;}
};
struct Settings {std::map<std::string,std::string> values;int writes=0;
 std::string getString(const std::string& key){return values[key];}
 void setString(const std::string& key,const std::string& value){++writes;values[key]=value;}
}gSavedPerAccountSettings;
unsigned epoch=1;
std::function<bool()> vs_native_im_guard(){auto original=epoch;return[original]{return original==epoch;};}
namespace LLNotificationsUtil {void add(const char*){assert(false&&"unexpected auth failure");}}
AUTH_HEADER
std::shared_ptr<exoFlickrAuth> pending;
void exoFlickrAuth::beginAuthorisation(){pending=shared_from_this();}
void exoFlickrAuth::checkAuthorisation(){pending=shared_from_this();}
AUTH_CORE
AUTH_REPLY
int main(){
 int oldCalls=0,newCalls=0,reentrantCalls=0;
 exoFlickrAuth::create([&](bool,const LLSD&){++oldCalls;});auto old=pending;
 exoFlickrAuth::retireAccount();++epoch;
 exoFlickrAuth::create([&](bool ok,const LLSD&){assert(ok);++newCalls;
   exoFlickrAuth::create([&](bool,const LLSD&){++reentrantCalls;});
 });auto next=pending;assert(next!=old);
 LLSD reply;reply["oauth_token"].text="second-token";reply["oauth_token_secret"].text="second-secret";
 old->gotAccessToken(true,reply);
 assert(!oldCalls&&!newCalls&&gSavedPerAccountSettings.writes==0);
 assert(exoFlickrAuth::sOwner==next);
 next->gotAccessToken(true,reply);
 assert(newCalls==1&&!oldCalls&&!reentrantCalls);
 assert(gSavedPerAccountSettings.values["ExodusFlickrToken"]=="second-token");
 auto third=pending;assert(third!=next&&exoFlickrAuth::sOwner==third);
 const int writes=gSavedPerAccountSettings.writes;
 next->gotAccessToken(true,reply);old->gotAccessToken(true,reply);
 assert(gSavedPerAccountSettings.writes==writes&&newCalls==1);
 ++epoch;exoFlickrAuth::create([](bool,const LLSD&){});
 assert(exoFlickrAuth::sOwner!=third);third->gotAccessToken(true,reply);
 assert(gSavedPerAccountSettings.writes==writes&&!reentrantCalls);
 exoFlickrAuth::retireAccount();pending.reset();
}
