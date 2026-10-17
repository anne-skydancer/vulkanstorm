#include <cassert>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
using F32=float;using U32=unsigned;using U64=unsigned long long;
struct LLUUID{std::string value;LLUUID()=default;LLUUID(const char*s):value(s){}void setNull(){value.clear();}};
struct LLSD{std::string text;double number=0;std::map<std::string,LLSD>values;LLSD&operator[](const char*key){return values[key];}LLSD&operator=(std::string s){text=s;return *this;}LLSD&operator=(double v){number=v;return *this;}LLSD&operator=(LLUUID v){text=v.value;return *this;}};
std::ostream&operator<<(std::ostream&out,const LLSD&){return out;}
int epoch=1;bool circuit=true,contextAvailable=true,alive=true;std::string url="https://real/cap",publishedOwner;
struct VSRegionContext{int token=epoch;bool current()const{return token==epoch&&circuit;}std::string getCapability(const char*)const{return current()?url:"";}};
std::optional<VSRegionContext>vs_current_region_context(){return contextAvailable?std::optional<VSRegionContext>(VSRegionContext{}):std::nullopt;}
bool vs_publish_region_owner(const VSRegionContext&context,const LLUUID&owner){if(!context.current())return false;publishedOwner=owner.value;return true;}
std::function<bool()>vs_native_im_guard(){int token=epoch;return[token]{return epoch==token;};}
struct LLCoros{std::vector<std::function<void()>>tasks;static LLCoros&instance(){static LLCoros c;return c;}void launch(const char*,std::function<void()>task){tasks.push_back(task);}};
int posts=0;bool httpSuccess=true;LLSD posted;std::function<void()>onPost;
namespace LLCore{struct HttpRequest{using policy_t=int;using ptr_t=std::shared_ptr<HttpRequest>;static constexpr int DEFAULT_POLICY_ID=0;};using HttpStatus=bool;}
namespace LLCoreHttpUtil{struct HttpCoroutineAdapter{using ptr_t=std::shared_ptr<HttpCoroutineAdapter>;static constexpr const char*HTTP_RESULTS="result";HttpCoroutineAdapter(const char*,int){}LLSD postAndSuspend(LLCore::HttpRequest::ptr_t,const std::string&target,const LLSD&body){assert(target=="https://real/cap");++posts;posted=body;if(onPost)onPost();return {};}static bool getStatusFromLLSD(const LLSD&){return httpSuccess;}};}
struct LLFloaterRegionInfo{static LLUUID getLastInvoice(){return LLUUID("invoice");}};
std::ostringstream logs;
#define LL_DEBUGS(...) logs
#define LL_INFOS(...) logs
#define LL_WARNS(...) logs
#define LL_ENDL ""
CONSTANTS
struct LLEstateInfoModel{
 using strings_t=std::vector<std::string>;
 std::string mName;LLUUID mOwnerID;U32 mID=0;U64 mFlags=0;F32 mSunHour=0;int updates=0,commits=0;
 std::function<void()>mUpdateSignal=[this]{++updates;};
 static LLEstateInfoModel&instance(){static LLEstateInfoModel m;return m;}static bool instanceExists(){return alive;}
 bool getUseFixedSun()const;bool getIsExternallyVisible()const;bool getAllowDirectTeleport()const;bool getDenyAnonymous()const;bool getDenyAgeUnverified()const;bool getDenyScriptedAgents()const;bool getAllowVoiceChat()const;bool getAllowAccessOverride()const;bool getAllowEnvironmentOverride()const;
 bool getFlag(U64 flag)const{return mFlags&flag;}std::string getName()const{return mName;}float getSunHour()const{return getUseFixedSun()?mSunHour:0;}std::string getInfoDump(){return "estate";}
 void update(const strings_t&);void reset();bool commitEstateInfoCaps();static void commitEstateInfoCapsCoro(std::string,LLSD,std::function<bool()>);void notifyCommit(){++commits;}
};
GETTERS
METHODS
using LLHost=int;
struct VSNativeSession{
 LLHost mHost=1;bool ready=true;
 struct Circuit{bool acknowledged=true,handshake=true;U64 incarnation=7;LLUUID owner{"initial"};};std::map<LLHost,Circuit>mCircuits;
 bool regionReady()const{return ready;}U64 circuitGeneration(LLHost host)const{auto found=mCircuits.find(host);return found==mCircuits.end()?0:found->second.incarnation;}
 bool publishRegionOwner(const LLHost&,U64,const LLUUID&);
};
OWNER_METHOD
int main(){
 VSNativeSession owner;owner.mCircuits[1]={};assert(owner.publishRegionOwner(1,7,LLUUID("real")));assert(owner.mCircuits[1].owner.value=="real");
 assert(!owner.publishRegionOwner(2,7,LLUUID("forged")));assert(!owner.publishRegionOwner(1,6,LLUUID("stale")));owner.ready=false;assert(!owner.publishRegionOwner(1,7,LLUUID("retired")));owner.ready=true;
 owner.mCircuits[1].handshake=false;assert(!owner.publishRegionOwner(1,7,LLUUID("unready")));owner.mCircuits[1].handshake=true;owner.mCircuits[1].acknowledged=false;assert(!owner.publishRegionOwner(1,7,LLUUID("unacked")));assert(owner.mCircuits[1].owner.value=="real");
 auto&model=LLEstateInfoModel::instance();model.update({"short"});assert(model.updates==0);
 model.update({std::string("Real Estate\0",12),"owner-uuid","7","1","1024"});assert(model.updates==1&&model.mName=="Real Estate"&&model.mOwnerID.value=="owner-uuid"&&publishedOwner=="owner-uuid"&&model.mID==7&&model.mFlags==1&&model.mSunHour==1);
 contextAvailable=false;model.update({"forged","bad","2","0","0"});assert(model.updates==1&&model.mName=="Real Estate");contextAvailable=true;
 auto run=[&]{auto task=LLCoros::instance().tasks.front();LLCoros::instance().tasks.erase(LLCoros::instance().tasks.begin());task();};
 assert(model.commitEstateInfoCaps());model.mName="later-edit";run();assert(posts==1&&model.commits==1&&posted["estate_name"].text=="Real Estate"&&posted["invoice"].text=="invoice");
 assert(model.commitEstateInfoCaps());++epoch;run();assert(posts==1&&model.commits==1);
 assert(model.commitEstateInfoCaps());onPost=[] {++epoch;};run();assert(posts==2&&model.commits==1);onPost={};
 assert(model.commitEstateInfoCaps());alive=false;run();assert(posts==2&&model.commits==1);alive=true;
 assert(model.commitEstateInfoCaps());circuit=false;run();assert(posts==2&&model.commits==1);circuit=true;
 url.clear();assert(!model.commitEstateInfoCaps());url="https://real/cap";httpSuccess=false;assert(model.commitEstateInfoCaps());run();assert(posts==3&&model.commits==1);
 model.reset();assert(model.mName.empty()&&model.mOwnerID.value.empty()&&!model.mID&&!model.mFlags&&!model.mSunHour);
}
