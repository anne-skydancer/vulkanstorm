#include <cassert>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include <sstream>
#define LL_DEBUGS(x) std::ostringstream()
#define LL_ENDL ""
using U64=unsigned long long;struct LLSD {std::map<std::string,std::string> map;std::string& operator[](std::string k){return map[k];}friend std::ostream& operator<<(std::ostream& out,const LLSD&){return out;}};
struct Agent {std::string mode="default";auto getInterestListMode(){return mode;}}gAgent;
namespace LLCore {struct HttpRequest {enum {DEFAULT_POLICY_ID=0};};}
namespace LLCoreHttpUtil {struct HttpCoroutineAdapter {struct Sent{std::string url;LLSD body;std::function<void(const LLSD&)> callback;};static inline std::vector<Sent> sent;static void callbackHttpPost(std::string url,int,LLSD body,std::function<void(const LLSD&)> callback){sent.push_back({url,body,callback});}};}
struct VSNativeSession:std::enable_shared_from_this<VSNativeSession> {enum class Phase{Connecting,Connected};bool mShutdown=false,mChangingRegion=false;Phase mPhase=Phase::Connected;U64 mGeneration=1;struct Region {bool acknowledged=true,handshake=true,capabilitiesReady=true;std::string interestListMode="default",url;U64 incarnation=1;};std::map<int,Region> mCircuits;std::string capability(int host,const char*){return mCircuits.at(host).url;}U64 circuitGeneration(int host){auto it=mCircuits.find(host);return it==mCircuits.end()?0:it->second.incarnation;}void updateInterestListMode();};
INTEREST
int main(){auto s=std::make_shared<VSNativeSession>();s->mCircuits[1].url="primary";s->mCircuits[2].url="neighbor";s->mCircuits[2].handshake=false;s->updateInterestListMode();assert(LLCoreHttpUtil::HttpCoroutineAdapter::sent.empty());gAgent.mode="360";s->updateInterestListMode();assert(LLCoreHttpUtil::HttpCoroutineAdapter::sent.size()==1);s->updateInterestListMode();assert(LLCoreHttpUtil::HttpCoroutineAdapter::sent.size()==1);s->mCircuits[2].handshake=true;s->updateInterestListMode();assert(LLCoreHttpUtil::HttpCoroutineAdapter::sent.size()==2&&LLCoreHttpUtil::HttpCoroutineAdapter::sent.back().url=="neighbor");gAgent.mode="default";s->updateInterestListMode();assert(LLCoreHttpUtil::HttpCoroutineAdapter::sent.size()==4);for(auto& sent:LLCoreHttpUtil::HttpCoroutineAdapter::sent)assert(sent.body.map.at("mode")=="360"||sent.body.map.at("mode")=="default");s->mGeneration=2;for(auto& sent:LLCoreHttpUtil::HttpCoroutineAdapter::sent)sent.callback({});s.reset();for(auto& sent:LLCoreHttpUtil::HttpCoroutineAdapter::sent)sent.callback({});}
