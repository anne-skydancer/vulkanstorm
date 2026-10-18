PREFIX
#include <optional>
using U64=unsigned long long;
struct Region {
 std::shared_ptr<bool> alive;
 bool current()const{return *alive;}
 bool capabilitiesReceived()const{return true;}
 std::string getCapability(const char*)const{return "real-parcel-voice-cap";}
};
std::shared_ptr<bool> regionAlive=std::make_shared<bool>(true);
std::optional<Region> vs_current_region_context(){return Region{regionAlive};}
struct LLViewerParcelMgr {static LLViewerParcelMgr* getInstance(){static LLViewerParcelMgr p;return &p;}bool allowAgentVoice()const{return true;}};
unsigned accountEpoch=1;
struct LLVivoxVoiceClient {
 inline static bool sShuttingDown=false;
 U64 mAccountRevision=1;
 std::function<bool()> mAccountCurrent;
 bool mSessionTerminateRequested=false,mVoiceEnabled=true,mIsInitialized=true;
 std::string mCurrentRegionName="actual-region";int mCurrentParcelLocalID=17;
 int applied=0,terminated=0;struct AccountRetired{};
 LLVivoxVoiceClient(){const auto epoch=accountEpoch;mAccountCurrent=[epoch]{return epoch==accountEpoch;};}
 CHECK_REVISION
 bool checkParcelChanged(bool){return false;}
 bool terminateAudioSession(bool){++terminated;return true;}void sessionTerminate(){++terminated;}
 bool setSpatialChannel(const LLSD& value){assert(value["session"].asString()=="accepted");++applied;return true;}
 bool requestParcelVoiceInfo();
};
PARCEL_METHOD
int main(){
 LLVivoxVoiceClient client;
 assert(client.requestParcelVoiceInfo()&&client.applied==1);
 suspendAction=[&]{*regionAlive=false;};
 assert(!client.requestParcelVoiceInfo()&&client.applied==1);
 regionAlive=std::make_shared<bool>(true);
 suspendAction=[&]{++accountEpoch;++client.mAccountRevision;};
 bool retired=false;try{client.requestParcelVoiceInfo();}catch(const LLVivoxVoiceClient::AccountRetired&){retired=true;}
 assert(retired&&client.applied==1&&!client.terminated);
 int requests=requestCount;assert(!client.requestParcelVoiceInfo()&&requestCount==requests);
 // New genuine account gets its own current guard; old daemon/HTTP request
 // cannot deliver credentials into the new channel after suspension.
 LLVivoxVoiceClient next;suspendAction={};assert(next.requestParcelVoiceInfo()&&next.applied==1);
 suspendAction=[&]{LLVivoxVoiceClient::sShuttingDown=true;};
 assert(!next.requestParcelVoiceInfo()&&next.applied==1);
}
