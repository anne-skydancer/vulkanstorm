"""Exercise production feature admission without a window, network or scene."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def method(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class NativeSimulatorFeaturesTest(unittest.TestCase):
    def test_inventory_fetch_starts_before_connected_ui_and_not_on_region_change(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        source = (ROOT / 'indra/newview/vsnativesession.cpp').read_text(encoding='utf-8')
        connected = method(source, 'void VSNativeSession::connected()')
        prefix = connected.split('LLStartUp::initExperiences();', 1)[0] + '}'
        fixture = r'''
#include <cassert>
struct Host {int id=0;bool isOk()const{return id!=0;}
 bool operator!=(const Host& h)const{return id!=h.id;}};
using LLHost=Host;
struct Message {struct Circuit {void removeCircuitData(Host){}} mCircuitInfo;
 void disableCircuit(Host){}};
Message* gMessageSystem=nullptr;
struct Agent {int getPositionGlobal(){return 0;}} gAgent;
struct LLTeleportHistory {static LLTeleportHistory& instance(){static LLTeleportHistory h;return h;}
 void updateNativeLocation(int){}};
double now(){return 5;}
struct LLInventoryModelBackgroundFetch {
 int starts=0;static LLInventoryModelBackgroundFetch& instance(){static LLInventoryModelBackgroundFetch f;return f;}
 void start(){++starts;}
};
struct VSNativeSession {
 enum class Phase {Connecting,Connected,Logout};Phase mPhase=Phase::Connecting;
 bool mChangingRegion=false,mHandshake=false,mMovement=false,mSeedReady=false,mCircuitAck=false;
 double mNavigationDeadline=0,mDeadline=0;Host mPreviousHost,mHost;
 void requestParcel(){}void connected();
};
CONNECTED
int main(){
 auto& fetch=LLInventoryModelBackgroundFetch::instance();VSNativeSession s;
 s.connected();assert(fetch.starts==0);
 s.mHandshake=s.mMovement=s.mCircuitAck=true;s.connected();assert(fetch.starts==0);
 s.mSeedReady=true;s.connected();assert(fetch.starts==1&&s.mPhase==VSNativeSession::Phase::Connected);
 s.connected();assert(fetch.starts==1);
 s.mChangingRegion=true;s.connected();assert(fetch.starts==1&&!s.mChangingRegion);
 s.mPhase=VSNativeSession::Phase::Logout;s.connected();assert(fetch.starts==1);
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
struct LLSD{};
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
 void capabilities(const LLSD&,U64,U64){assert(main_loop);++admitted;}
 void disconnect(const std::string&){assert(main_loop);++disconnected;}
 void applySimulatorFeatures(const LLSD&,U64,U64){assert(main_loop);++features;}
};
void enqueue(std::weak_ptr<Session> weak,U64 generation,U64 regionEpoch) {
 LLSD result;
COMPLETIONS
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
        self.assertIn('"SimulatorFeatures"', seed)
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
