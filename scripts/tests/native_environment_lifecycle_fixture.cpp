#include <cassert>
#include <memory>
#include <utility>
#include <functional>
#include <vector>
#include <algorithm>
#define llassert assert
SIMPLETON
struct LLSD{float asReal()const{return 0.f;}};using S32=int;using F32=float;
struct LLControlVariable{};
struct Connection{
 std::shared_ptr<bool> live=std::make_shared<bool>(false);
 Connection()=default;Connection(bool value):live(std::make_shared<bool>(value)){}
 bool connected()const{return *live;}void disconnect(){*live=false;}
};
struct Signal{
 std::vector<Connection> connections;
 template<class F>Connection connect(F){Connection value(true);connections.push_back(value);return value;}
 int active()const{return std::count_if(connections.begin(),connections.end(),[](const Connection&value){return value.connected();});}
};
struct Control{Signal signal;auto*getSignal(){return &signal;}};
struct Settings{Control control;auto*getControl(const char*){return &control;}float getF32(const char*){return .5f;}}gSavedSettings;

struct LLSettingsSky{using ptr_t=std::shared_ptr<LLSettingsSky>;static inline F32 sAutoAdjustProbeAmbiance=0;};
struct LLSettingsWater{using ptr_t=std::shared_ptr<LLSettingsWater>;};
struct LLSettingsVOSky{static auto buildDefaultSky(){return std::make_shared<LLSettingsSky>();}};
struct LLSettingsVOWater{static auto buildDefaultWater(){return std::make_shared<LLSettingsWater>();}};
struct LLUUID{};struct LLVector3{};struct LLVector3d{};
struct Agent{template<class F>Connection addParcelChangedCallback(F){return {true};}template<class F>Connection addRegionChangedCallback(F){return {true};}template<class F>Connection whenPositionChanged(F){return {true};}}gAgent;
struct LLRegionInfoModel{static auto&instance(){static LLRegionInfoModel m;return m;}template<class F>Connection setUpdateCallback(F){return {true};}};
constexpr int MESSAGE_PUSHENVIRONMENT=1;int environment_push_dispatch_handler;
struct Dispatcher{bool present=false;bool isHandlerPresent(int){return present;}void addHandler(int,int*){present=true;}}gGenericDispatcher;
constexpr int PUMP_EXPERIENCE=2,LISTENER_NAME=3;
struct Pump{bool listening=false;int callbacks=0;void stopListening(int){listening=false;}template<class F>void listen(int,F){listening=true;++callbacks;}};
struct LLEventPumps{Pump pump;static auto&instance(){static LLEventPumps p;return p;}auto&obtain(int){return pump;}};
struct LLEnvironment:LLSimpleton<LLEnvironment>{
 enum{ENV_DEFAULT=0};struct DayInstance{explicit DayInstance(int){}LLSettingsSky::ptr_t sky;LLSettingsWater::ptr_t water;void setSky(LLSettingsSky::ptr_t p){sky=p;}void setWater(LLSettingsWater::ptr_t p){water=p;}};
 std::shared_ptr<DayInstance>mCurrentEnvironment,mEnvironments[1];
 Connection mSkyAutoAdjustConnection,mParcelCallbackConnection,mRegionUpdateCallbackConnection,mRegionChangeCallbackConnection,mPositionCallbackConnection;
 int requests=0,crossings=0;
 void initSingleton();void cleanupSingleton();~LLEnvironment(){cleanupSingleton();}
 void requestRegion(){assert(mCurrentEnvironment && mCurrentEnvironment->sky && mCurrentEnvironment->water);++requests;}
 void onRegionChange(){assert(mEnvironments[ENV_DEFAULT]);++crossings;requestRegion();}
 void onParcelChange(){}void onAgentPositionHasChanged(const LLVector3&){}void listenExperiencePump(LLSD){}
};
INITIALIZE
CLEANUP
struct VSNativeSession{void initializeEnvironment();};
ENSURE
int main(){
 assert(!LLEnvironment::instanceExists());VSNativeSession session;
 session.initializeEnvironment();auto*first=LLEnvironment::getInstance();
 assert(first && first->requests==1 && first->mParcelCallbackConnection.connected());
 assert(gSavedSettings.control.signal.active()==1);
 first->onRegionChange();session.initializeEnvironment();assert(first==LLEnvironment::getInstance() && first->requests==2);
 assert(LLEventPumps::instance().pump.callbacks==1);
 LLEnvironment::deleteSingleton();assert(!LLEnvironment::instanceExists() && !LLEventPumps::instance().pump.listening);
 assert(gSavedSettings.control.signal.active()==0);
 session.initializeEnvironment();assert(LLEnvironment::instance().requests==1);
 assert(LLEventPumps::instance().pump.callbacks==2);LLEnvironment::instance().onRegionChange();
 assert(gSavedSettings.control.signal.active()==1);
 LLEnvironment::instance().initSingleton();assert(gSavedSettings.control.signal.active()==1);
 LLEnvironment::deleteSingleton();assert(!LLEnvironment::instanceExists());
 assert(gSavedSettings.control.signal.active()==0);
}

