"""Exercise production feature admission without a window, network or scene."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
import re

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.find(signature)
    if start < 0:
        # Formatting must not select or suppress a production method. Match
        # the same signature tokens across whitespace changes only.
        match = re.search(r'\s*'.join(re.escape(char) for char in signature if not char.isspace()), source)
        if not match:
            raise ValueError('Missing production method: ' + signature)
        start = match.start()
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeSimulatorFeaturesTest(unittest.TestCase):
    def test_native_rlva_cleanup_precedes_account_and_fiber_teardown(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        rlv = (ROOT / 'indra/newview/rlvhandler.cpp').read_text(encoding='utf-8')
        app = (ROOT / 'indra/newview/llappviewer.cpp').read_text(encoding='utf-8')
        prefix = method(app, 'bool LLAppViewer::cleanupNativeVulkanLogin()').split(
            'LLStartUp::cleanupNameCache();', 1)[0] + 'return true;}'
        fixture = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#define VS_NATIVE_VULKAN 1
#define RLV_ASSERT(value) assert(value)
#define SUBSYSTEM_CLEANUP(type) type::cleanupClass()
bool fibers_alive=true,account_alive=true,gDoDisconnect=false;
int status_queries=0,handler_deletes=0,listener_removals=0,observer_removals=0;
struct LLApp {static bool isExiting(){assert(fibers_alive);++status_queries;return true;}};
struct LLUUID {int id=0;bool notNull()const{return id!=0;}
 bool operator<(const LLUUID& other)const{return id<other.id;}static const LLUUID null;};
const LLUUID LLUUID::null{};
namespace LLStringUtil {const std::string null;}
using uuid_vec_t=std::vector<LLUUID>;
struct Agent {void removeListener(void*){assert(account_alive);++listener_removals;}} gAgent;
struct LLGroupMgr {static bool instanceExists(){return true;}
 static LLGroupMgr& instance(){static LLGroupMgr mgr;return mgr;}
 void removeObserver(LLUUID,void*){assert(account_alive);++observer_removals;}};
struct RlvExtCommandHandler {~RlvExtCommandHandler(){assert(account_alive);++handler_deletes;}};
using S16=short;constexpr int RLV_BHVR_COUNT=1;
struct RlvHandler {
 bool m_fEnabled=true;std::map<LLUUID,int> m_Objects{{LLUUID{1},1}};
 S16 m_Behaviours[RLV_BHVR_COUNT]{1};
 std::vector<int> m_Exceptions{1},m_CurCommandStack,m_CurObjectStack,m_Retained{1};
 std::pair<LLUUID,std::string> m_PendingGroupChange{LLUUID{1},"group"};
 std::vector<RlvExtCommandHandler*> m_CommandHandlers{new RlvExtCommandHandler};
 using rlv_object_map_t=decltype(m_Objects);
 void processCommand(LLUUID id,const char*,bool){assert(account_alive);m_Objects.erase(id);
  m_Exceptions.clear();m_Behaviours[0]=0;}
 void cleanup();~RlvHandler();static void cleanupClass();
} gRlvHandler;
RLV_DEFINITIONS
struct Session {void shutdown(){assert(!gRlvHandler.m_fEnabled&&handler_deletes==1);
 account_alive=false;}};
struct Message {void setMessageAdmission(std::function<void()>){}};
Message* gMessageSystem=nullptr;
int drained_workers=0,fetch_waits=0;
struct Worker {
 bool stopped=false,fetch=false,requestsDeleted=false,cacheThreadStopped=false,waited=false;
 unsigned updates=0;
 explicit Worker(bool isFetch=false):fetch(isFetch){}
 void deleteAllRequests(){assert(fetch&&!account_alive&&!stopped);requestsDeleted=true;}
 int update(int){assert(!account_alive&&!stopped);++drained_workers;return ++updates==1?1:0;}
 void shutdown(){assert(!account_alive);assert(!fetch||requestsDeleted);stopped=true;}
 void shutDownTextureCacheThread(){assert(fetch&&stopped);cacheThreadStopped=true;}
 void waitOnPending(){assert(cacheThreadStopped);waited=true;++fetch_waits;}
 ~Worker(){assert(stopped);assert(!fetch||(cacheThreadStopped&&waited));}
};
Worker* sImageDecodeThread=new Worker;Worker* sTextureCache=new Worker;Worker* sTextureFetch=new Worker(true);
struct LLTimer {unsigned elapsed=0;double getElapsedTimeF64(){return elapsed++;}};
struct LLViewerStatsRecorder {
 static bool instanceExists(){return true;}
 static void deleteSingleton(){assert(!sTextureFetch&&!sImageDecodeThread&&!sTextureCache);}
};
struct Mainloop {template<class Duration>void runFor(Duration){assert(!sTextureFetch&&!sImageDecodeThread&&!sTextureCache);}} gMainloopWork;
struct LLAppViewer {std::shared_ptr<Session> mNativeSession=std::make_shared<Session>();
 bool cleanupNativeVulkanLogin();};
NATIVE_PREFIX
int main(){
 LLAppViewer app;assert(app.cleanupNativeVulkanLogin());
 assert(drained_workers==6&&fetch_waits==1);
 assert(!gRlvHandler.m_fEnabled&&listener_removals==1&&observer_removals==1&&status_queries==1);
 fibers_alive=false;
 gRlvHandler.cleanup();assert(status_queries==1&&handler_deletes==1);
 // The actual global destructor below must also leave the retired fiber mutex alone.
}
'''.replace('RLV_DEFINITIONS', '\n'.join(method(rlv, signature) for signature in (
            'void RlvHandler::cleanup()', 'RlvHandler::~RlvHandler()', 'void RlvHandler::cleanupClass()'))).replace(
                'NATIVE_PREFIX', prefix)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'native_rlva_teardown.cpp'
            exe = Path(directory) / 'native_rlva_teardown.exe'
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_inventory_fetch_starts_before_connected_ui_and_not_on_region_change(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        connected = method(source, 'void VSNativeSession::connected()')
        prefix = connected.split('LLStartUp::initExperiences();', 1)[0] + '}'
        fixture = r'''
#include <cassert>
#include <map>
#include <memory>
struct Host {int id=0;bool isOk()const{return id!=0;}
 bool operator!=(const Host& h)const{return id!=h.id;}bool operator<(const Host& h)const{return id<h.id;}};
using LLHost=Host;
struct Message {struct Circuit {void removeCircuitData(Host){}} mCircuitInfo;
 void disableCircuit(Host){}};
Message* gMessageSystem=nullptr;
struct Agent {int getPositionGlobal(){return 0;}} gAgent;
struct Window {int hides=0;void setShowProgress(bool show,bool){assert(!show);++hides;}
 int getWorldViewHeightRaw()const{return 600;}int getWorldViewWidthRaw()const{return 800;}
 float getWorldViewAspectRatio()const{return 800.f/600;}};Window* gViewerWindow=nullptr;
struct Settings {bool getBOOL(const char*)const{return true;}float getF32(const char*)const{return 1.1f;}}gSavedSettings;
struct FSLSLBridge {int starts=0;static FSLSLBridge& instance(){static FSLSLBridge b;return b;}void initBridge(){++starts;}};
struct LLViewerCamera {static inline bool created=false;int heights=0,aspects=0,views=0;
 static bool instanceExists(){return created;}static void createInstance(){assert(!created);created=true;}
 static LLViewerCamera* getInstance(){assert(created);static LLViewerCamera camera;return &camera;}
 void setViewHeightInPixels(int n){assert(n==600);++heights;}void setAspect(float f){assert(f==800.f/600);++aspects;}
 void setDefaultFOV(float f){assert(f==1.1f);++views;}};
struct LLTeleportHistory {static LLTeleportHistory& instance(){static LLTeleportHistory h;return h;}
 void updateNativeLocation(int){}};
double now(){return 5;}
struct LLInventoryModelBackgroundFetch {
 int starts=0;static LLInventoryModelBackgroundFetch& instance(){static LLInventoryModelBackgroundFetch f;return f;}
 void start(){++starts;}
};
struct VSNativeAppearance {explicit VSNativeAppearance(int){} void fallback(int){}};
struct VSNativeSession {
 enum class Phase {Connecting,Connected,Logout};Phase mPhase=Phase::Connecting;
 enum class Startup {Precache,Complete};Startup mStartup=Startup::Precache;
 bool mChangingRegion=false,mHandshake=false,mMovement=false,mSeedReady=false,mCircuitAck=false;
 double mNavigationDeadline=0,mDeadline=0;Host mPreviousHost,mHost;
 struct Circuit {double deadline=60;};std::map<Host,Circuit> mCircuits;
 std::unique_ptr<VSNativeAppearance> mAppearance;int mWearablesMetadata=0;
 int shared_from_this(){return 0;}
 void publishProgress(){}
 void requestParcel(){}void connected();
};
CONNECTED
int main(){
 auto& fetch=LLInventoryModelBackgroundFetch::instance();VSNativeSession s;
 s.connected();assert(fetch.starts==0);
 s.mHandshake=s.mMovement=s.mCircuitAck=true;s.connected();assert(fetch.starts==0);
 s.mSeedReady=true;s.connected();assert(fetch.starts==1&&s.mPhase==VSNativeSession::Phase::Connected);
 s.connected();assert(fetch.starts==1);
 Window window;gViewerWindow=&window;
 s.mChangingRegion=true;s.connected();assert(fetch.starts==1&&!s.mChangingRegion&&window.hides==0);
 s.mStartup=VSNativeSession::Startup::Complete;s.mChangingRegion=true;s.connected();
 assert(fetch.starts==1&&!s.mChangingRegion&&window.hides==1);
 s.mPhase=VSNativeSession::Phase::Logout;s.connected();assert(fetch.starts==1);
 assert(FSLSLBridge::instance().starts==2);
 VSNativeSession fresh;fresh.mHandshake=fresh.mMovement=fresh.mSeedReady=fresh.mCircuitAck=true;
 fresh.connected();assert(fetch.starts==2 && LLViewerCamera::instanceExists());
 auto* camera=LLViewerCamera::getInstance();assert(camera->heights==1 && camera->aspects==1 && camera->views==1);
 fresh.connected();assert(fetch.starts==2 && camera->views==1);

}
'''.replace('CONNECTED', prefix)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'inventory_start.cpp'
            exe = Path(directory) / 'inventory_start.exe'
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_http_completions_execute_on_main_loop_and_reject_retired_owners(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        queued = []
        for signature in ('void VSNativeSession::seedCoro(',
                          'void VSNativeSession::simulatorFeaturesCoro('):
            definition = method(source, signature)
            cursor = 0
            while True:
                start = definition.find('LL::WorkQueue::getInstance("mainloop")->post(', cursor)
                if start < 0:
                    break
                end = definition.index('});', start) + 3
                queued.append(definition[start:end])
                cursor = end
        self.assertEqual(len(queued), 3)
        fixture = r'''
#include <cassert>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using U64=unsigned long long;
struct LLSD{int requestedPolicy=0;};
namespace LLTrans { std::string getString(const char*) { return "timeout"; } }
bool main_loop=false;
namespace LL { struct WorkQueue {
 std::vector<std::function<void()>> tasks;
 static WorkQueue* getInstance(const char*) { static WorkQueue queue; return &queue; }
 void post(std::function<void()> task) { tasks.push_back(task); }
 void drain() { main_loop=true; auto work=std::move(tasks);tasks.clear();
  for(auto& task:work) task(); main_loop=false; }
}; }
struct Session {
 enum class Phase {Connecting,Connected,Logout};
 bool mShutdown=false,mChangingRegion=false;
 U64 mGeneration=1,mRegionEpoch=2;
 Phase mPhase=Phase::Connecting;
 std::function<void()> mCancelSeed,mCancelSimulatorFeatures;
 int admitted=0,disconnected=0,features=0;
 void capabilities(const LLSD&,U64,U64,const LLSD& requested){assert(main_loop && requested.requestedPolicy==4);++admitted;}
 void disconnect(const std::string&){assert(main_loop);++disconnected;}
 void applySimulatorFeatures(const LLSD&,U64,U64){assert(main_loop);++features;}
};
void enqueue(std::weak_ptr<Session> weak,U64 generation,U64 regionEpoch) {
 LLSD result;LLSD wanted{4};
COMPLETIONS
 wanted.requestedPolicy=9; // Mutable policy after dispatch must not alter the captured request.
}
int main(){
 auto queue=LL::WorkQueue::getInstance("mainloop");
 auto owner=std::make_shared<Session>();
 enqueue(owner,1,2);assert(owner->admitted==0&&owner->disconnected==0&&owner->features==0);
 queue->drain();assert(owner->admitted==1&&owner->disconnected==1&&owner->features==1);
 auto unchanged=[&]{assert(owner->admitted==1&&owner->disconnected==1&&owner->features==1);};
 enqueue(owner,1,2);++owner->mGeneration;queue->drain();unchanged();
 enqueue(owner,2,2);++owner->mRegionEpoch;queue->drain();unchanged();
 enqueue(owner,2,3);owner->mShutdown=true;queue->drain();unchanged();
 owner->mShutdown=false;owner->mPhase=Session::Phase::Logout;
 enqueue(owner,2,3);queue->drain();unchanged();
 enqueue(owner,2,3);owner.reset();queue->drain();
}
'''.replace('COMPLETIONS', '\n'.join(queued).replace('Phase::', 'Session::Phase::'))
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'capability_main_loop.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_actual_shared_parser_has_no_native_scene_side_effects(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        parser = (ROOT / 'indra/newview/lfsimfeaturehandler.cpp').read_text(encoding='utf-8')
        definition = method(parser, 'void LFSimFeatureHandler::setSupportedFeatures(const LLSD&')
        fixture = r'''
#include <cassert>
#include <map>
#include <sstream>
#include <string>
using F32=float;using U32=unsigned;
struct LLSD{
 std::map<std::string,LLSD> children;std::string text;
 LLSD()=default;LLSD(const char* s):text(s){}
 bool has(const char* k)const{return children.count(k);}
 LLSD& operator[](const char* k){return children[k];}
 const LLSD& operator[](const char* k)const{static LLSD none;auto i=children.find(k);return i==children.end()?none:i->second;}
 std::string asString()const{return text;}int asInteger()const{return std::stoi(text);}
 double asReal()const{return std::stod(text);}bool asBoolean()const{return text=="true";}
};
std::ostream& operator<<(std::ostream& stream,const LLSD&){return stream;}
std::ostringstream messages;
#define LL_INFOS(...) messages
#define LL_DEBUGS(...) messages
#define LL_ENDL ""
struct LLStringUtil{static inline std::string null;};
struct Settings{std::string getString(const char* k){return std::string("setting:")+k;}}gSavedSettings;
struct LLGridManager{
 bool sl=false;static LLGridManager& instance(){static LLGridManager grid;return grid;}
 static LLGridManager* getInstance(){return &instance();}
 bool isInSecondLife(){return sl;}std::string getGrid(){return "grid.example";}
};
struct LLLoginInstance{
 static LLLoginInstance* getInstance(){static LLLoginInstance login;return &login;}
 bool hasResponse(const char*){return false;}LLSD getResponse(const char*){return {};}
};
struct LLNotificationsUtil{static void add(const char*){}};
struct LLWorld{
 static inline int touches=0;static LLWorld* getInstance(){++touches;static LLWorld world;return &world;}
 void refreshLimits(){}void setRegionMinPrimScale(F32){}void setRegionMaxPrimScale(F32){}
 void setRegionMaxPrimScaleNoMesh(F32){}void setMaxPhysPrimScale(F32){}
};
struct Tea{
 static inline std::string currency;static std::string getCurrency(){return currency;}
 static void setRegionCurrency(const std::string& s){currency=s;}
};
const std::string DEFAULT_GRID_STATUS_URL="https://status.example/";
struct LFSimFeatureHandler{
 bool mSupportsExport=false,mHasAvatarPicker=false,mHasDestinationGuide=false;
 std::string mMapServerURL,mGridStatusURL,mGridStatusRSS,mHyperGridPrefix,mSearchURL;
 std::string mAvatarPickerURL,mDestinationGuideURL,mHelperUriOverride,mCurrencySymbolOverride;
 U32 mSayRange=0,mShoutRange=0,mWhisperRange=0;
 F32 mSimulatorFPS=0,mSimulatorFPSFactor=0,mSimulatorFPSWarn=0,mSimulatorFPSCrit=0;
 std::string destinationGuideURL(){return mDestinationGuideURL;}
 std::string avatarPickerURL(){return mAvatarPickerURL;}
 static void updateCurrencySymbols(){}
 void setSupportedFeatures(const LLSD&,bool);
};
DEFINITION
int main(){
 LFSimFeatureHandler handler;LLSD features;
 auto& extras=features["OpenSimExtras"];
 extras["GridStatus"]="https://region.example/status";
 extras["GridStatusRSS"]="https://region.example/rss";
 extras["destination-guide-url"]="https://region.example/destinations";
 extras["search-server-url"]="https://region.example/search";
 extras["say-range"]="25";extras["MinPrimScale"]="0.001";
 handler.setSupportedFeatures(features,false);
 assert(handler.mGridStatusURL=="https://region.example/status");
 assert(handler.mGridStatusRSS=="https://region.example/rss");
 assert(handler.mDestinationGuideURL=="https://region.example/destinations"&&handler.mHasDestinationGuide);
 assert(handler.mSearchURL=="https://region.example/search"&&handler.mSayRange==25);
 assert(LLWorld::touches==0);
 handler.setSupportedFeatures(LLSD{},false);
 assert(handler.mGridStatusURL.empty()&&handler.mGridStatusRSS.empty());
 assert(handler.mDestinationGuideURL.empty()&&!handler.mHasDestinationGuide);
 assert(handler.mSayRange==20&&LLWorld::touches==0);
 handler.setSupportedFeatures(features,true);assert(LLWorld::touches==1);
 handler.setSupportedFeatures(LLSD{},true);assert(LLWorld::touches==2);
}
'''.replace('DEFINITION', definition)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'simulator_features_parser.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_actual_feature_admission_and_retirement(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        source = (ROOT / 'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        definitions = '\n'.join(method(source, name) for name in (
            'void VSNativeSession::clearSimulatorFeatures()',
            'bool VSNativeSession::applySimulatorFeatures('))
        fixture = r'''
#include <cassert>
#include <functional>
#include <string>
#include <utility>
using U64 = unsigned long long;
struct LLSD {
 bool map=true; std::string advertised;
 static LLSD emptyMap(){return {};}
 bool isMap()const{return map;}
};
struct LFSimFeatureHandler {
 LLSD applied; int calls=0;
 static LFSimFeatureHandler& instance(){static LFSimFeatureHandler handler;return handler;}
 void setSupportedFeatures(const LLSD& features,bool scene){assert(!scene);applied=features;++calls;}
};
struct VSNativeSession {
 enum class Phase{Login,Connecting,Connected,Logout,Disconnected};
 Phase mPhase=Phase::Connecting; bool mShutdown=false,mSimulatorFeaturesReceived=false;
 U64 mGeneration=7,mRegionEpoch=3; LLSD mSimulatorFeatures;
 std::function<void()> mCancelSimulatorFeatures;
 void clearSimulatorFeatures(); bool applySimulatorFeatures(const LLSD&,U64,U64);
};
DEFINITIONS
int main(){
 VSNativeSession session;LLSD first{true,"first-region-advertised-data"};
 assert(session.applySimulatorFeatures(first,7,3));
 assert(session.mSimulatorFeaturesReceived&&session.mSimulatorFeatures.advertised==first.advertised);
 int calls=LFSimFeatureHandler::instance().calls;
 assert(!session.applySimulatorFeatures({true,"stale-account"},6,3));
 assert(!session.applySimulatorFeatures({true,"stale-region"},7,2));
 assert(!session.applySimulatorFeatures({false,"malformed"},7,3));
 for(auto phase:{VSNativeSession::Phase::Login,VSNativeSession::Phase::Logout,VSNativeSession::Phase::Disconnected}){
  session.mPhase=phase;assert(!session.applySimulatorFeatures(first,7,3));
 }
 assert(LFSimFeatureHandler::instance().calls==calls);
 session.mPhase=VSNativeSession::Phase::Connected;session.mShutdown=true;
 assert(!session.applySimulatorFeatures(first,7,3));session.mShutdown=false;
 bool cancelled=false;session.mCancelSimulatorFeatures=[&]{cancelled=true;};
 ++session.mRegionEpoch;session.clearSimulatorFeatures();
 assert(cancelled&&!session.mCancelSimulatorFeatures&&!session.mSimulatorFeaturesReceived);
 assert(session.mSimulatorFeatures.advertised.empty());
 assert(!session.applySimulatorFeatures(first,7,3));
 assert(session.applySimulatorFeatures({true,"second-region"},7,4));
 assert(LFSimFeatureHandler::instance().applied.advertised=="second-region");
 ++session.mGeneration;session.clearSimulatorFeatures();
 assert(!session.applySimulatorFeatures(first,7,4));
}
'''.replace('DEFINITIONS', definitions)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'simulator_features.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_feature_transport_and_shared_consumer_seams(self):
        source = (ROOT / 'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        seed = method(source, 'void VSNativeSession::seedCoro(')
        caps = method(source, 'void VSNativeSession::capabilities(')
        catalogue=(ROOT/'indra/newview/vscapabilities.cpp').read_text(encoding='utf-8-sig')
        self.assertIn('vs_native_capability_names()',seed)
        self.assertIn('capabilityNames.append("SimulatorFeatures")',catalogue)
        self.assertIn('owner->capabilities(result, generation, regionEpoch, wanted)',seed)
        self.assertIn('"SimulatorFeatures"', caps)
        coroutine = method(source, 'void VSNativeSession::simulatorFeaturesCoro(')
        self.assertIn('std::weak_ptr<VSNativeSession>', coroutine)
        self.assertIn('options->setTransferTimeout(15)', coroutine)
        self.assertIn('options->setRetries(0)', coroutine)
        self.assertIn('attempt < 2', coroutine)
        for signature in ('bool VSNativeSession::changeRegion(', 'void VSNativeSession::reset()'):
            self.assertIn('clearSimulatorFeatures()', method(source, signature))
        parser = (ROOT / 'indra/newview/lfsimfeaturehandler.cpp').read_text(encoding='utf-8')
        legacy = method(parser, 'void LFSimFeatureHandler::setSupportedFeatures()')
        self.assertIn('setSupportedFeatures(info, true)', legacy)
        shared = method(parser, 'void LFSimFeatureHandler::setSupportedFeatures(const LLSD&')
        for key in ('GridStatus', 'GridStatusRSS', 'destination-guide-url', 'search-server-url'):
            self.assertIn('"' + key + '"', shared)
        self.assertIn('if (update_scene_limits)', shared)


if __name__ == '__main__':
    unittest.main()
