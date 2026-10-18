"""Actual Pathfinding account/region/deferred and reentrant request controllers."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_PATHFINDING_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_PATHFINDING_PROPOSED'])if 'VS_PATHFINDING_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativePathfindingTests(unittest.TestCase):
 def test_actual_request_guard_and_deferred_callback_retire_original_source(self):
  cpp=source('indra/newview/llpathfindingmanager.cpp');guard=body(cpp,'LLPathfindingManager::requestGuard(');deferred=body(cpp,'LLPathfindingManager::guardedDeferred(')
  run(self,r'''
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
using LLUUID=int;int account=1;std::function<bool()>vs_native_im_guard(){return[old=account]{return old==account;};}
struct VSRegionContext{std::shared_ptr<bool>live;bool current()const{return*live;}};
struct LLPathfindingManager{std::shared_ptr<bool>mLifetime=std::make_shared<bool>(true);std::function<bool()>requestGuard(const std::optional<VSRegionContext>&region)const GUARD std::function<void(const LLUUID&)>guardedDeferred(const std::optional<VSRegionContext>&region,std::function<void(const LLUUID&)>callback,std::function<bool()>original={})const DEFERRED};
int main(){LLPathfindingManager manager;auto live=std::make_shared<bool>(true);std::optional<VSRegionContext>region=VSRegionContext{live};auto current=manager.requestGuard(region);int calls=0;auto callback=manager.guardedDeferred(region,[&](int value){calls+=value;});assert(current());callback(1);assert(calls==1);*live=false;assert(!current());callback(4);assert(calls==1);*live=true;*manager.mLifetime=false;manager.mLifetime=std::make_shared<bool>(true);assert(!current());callback(4);assert(calls==1);current=manager.requestGuard(region);assert(current());++account;assert(!current());assert(!manager.requestGuard({})());}
'''.replace('GUARD',guard).replace('DEFERRED',deferred))
 def test_actual_character_request_does_not_launch_after_started_callback_retires_account(self):
  actual=body(source('indra/newview/llpathfindingmanager.cpp'),'void LLPathfindingManager::requestGetCharacters(')
  run(self,r'''
#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
using LLUUID=int;using LLPathfindingObjectListPtr=std::shared_ptr<int>;using request_id_t=unsigned;enum ERequestStatus{kRequestStarted,kRequestCompleted,kRequestNotEnabled,kRequestError};using object_request_callback_t=std::function<void(unsigned,ERequestStatus,LLPathfindingObjectListPtr)>;
struct Region{bool caps=true;bool capabilitiesReceived()const{return caps;}void whenCapabilitiesReceived(std::function<void(int)>fn,std::function<bool()> current={})const{}};
namespace boost{using std::bind;}using std::placeholders::_1;
struct LLCoros{int launches=0;static LLCoros&instance(){static LLCoros value;return value;}template<class F>std::string launch(std::string,F fn){++launches;return"name";}};
struct LLPathfindingManager{std::optional<Region>region=Region{};std::shared_ptr<bool>live=std::make_shared<bool>(true);auto getCurrentRegion()const{return region;}auto requestGuard(std::optional<Region>)const{return[weak=std::weak_ptr<bool>(live)]{auto v=weak.lock();return v&&*v;};}std::string getCharactersURLForCurrentRegion()const{return"cap";}void handleDeferredGetCharactersForRegion(int,unsigned,object_request_callback_t)const{}auto guardedDeferred(std::optional<Region>,std::function<void(int)>fn,std::function<bool()>original)const{return fn;}static void charactersCoro(std::string,unsigned,object_request_callback_t,std::function<bool()>current){}void requestGetCharacters(request_id_t pRequestId,object_request_callback_t pCharactersCallback)const ACTUAL};
int main(){LLPathfindingManager manager;int started=0;manager.requestGetCharacters(1,[&](unsigned,ERequestStatus status,LLPathfindingObjectListPtr){assert(status==kRequestStarted);++started;*manager.live=false;});assert(started==1&&LLCoros::instance().launches==0);manager.live=std::make_shared<bool>(true);manager.requestGetCharacters(2,[&](unsigned,ERequestStatus status,LLPathfindingObjectListPtr){assert(status==kRequestStarted);++started;});assert(started==2&&LLCoros::instance().launches==1);}
'''.replace('ACTUAL',actual))
 def test_actual_navmesh_request_supersedes_old_query_and_honors_reentrant_retirement(self):
  actual=body(source('indra/newview/llpathfindingmanager.cpp'),'void LLPathfindingManager::requestGetNavMeshForRegion(const std::optional<VSRegionContext>&')
  run(self,r'''#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <map>
#include <vector>
using LLUUID=int;using U64=unsigned long long;
struct VSRegionContext{bool capabilitiesReceived()const{return true;}int getRegionID()const{return 1;}U64 getHandle()const{return 2;}void whenCapabilitiesReceived(std::function<void(int)>,std::function<bool()>)const{}};
struct Mesh{std::function<void()>onCheck;void handleNavMeshNotEnabled(){}void handleNavMeshWaitForRegionLoad(){}void handleNavMeshCheckVersion(){if(onCheck)onCheck();}};using LLPathfindingNavMeshPtr=std::shared_ptr<Mesh>;
namespace boost{using std::bind;}using std::placeholders::_1;
#define llassert assert
struct LLCoros{std::vector<std::function<void()>>work;static LLCoros&instance(){static LLCoros v;return v;}template<class F>std::string launch(std::string,F f){work.emplace_back(f);return{};}};
int publications=0;
struct LLPathfindingManager{std::map<int,std::shared_ptr<bool>>mNavMeshIntents;std::shared_ptr<Mesh>mesh=std::make_shared<Mesh>();std::shared_ptr<bool>live=std::make_shared<bool>(true);std::function<bool()>requestGuard(std::optional<VSRegionContext>){return[w=std::weak_ptr<bool>(live)]{auto s=w.lock();return s&&*s;};}auto getNavMeshForRegion(std::optional<VSRegionContext>){return mesh;}bool isPathfindingEnabledForRegion(std::optional<VSRegionContext>){return true;}std::string getNavMeshStatusURLForRegion(std::optional<VSRegionContext>){return"url";}void handleDeferredGetNavMeshForRegion(int,bool){}std::function<void(int)>guardedDeferred(std::optional<VSRegionContext>,std::function<void(int)>f,std::function<bool()>){return f;}static void navMeshStatusRequestCoro(std::string,U64,bool,std::function<bool()>current){if(current())++publications;}void requestGetNavMeshForRegion(const std::optional<VSRegionContext>&pRegion,bool pIsGetStatusOnly) ACTUAL};
int main(){LLPathfindingManager m;std::optional<VSRegionContext>r=VSRegionContext{};m.requestGetNavMeshForRegion(r,false);m.requestGetNavMeshForRegion(r,false);auto&w=LLCoros::instance().work;assert(w.size()==2);w[0]();assert(publications==0);w[1]();assert(publications==1);m.mesh->onCheck=[&]{*m.live=false;};m.requestGetNavMeshForRegion(r,false);assert(w.size()==2);w[1]();assert(publications==1);}
'''.replace('ACTUAL',actual))
 def test_actual_console_unavailable_library_policy_uses_original_state_and_notification(self):
  actual=body(source('indra/newview/llfloaterpathfindingconsole.cpp'),'bool LLFloaterPathfindingConsole::showUnavailableLibrary(')
  run(self,r'''#include <cassert>
#include <string>
#define NULL nullptr
struct LLSD{std::string feature;std::string&operator[](const char*){return feature;}};
struct LLPathingLib{static inline bool ready=false;static void*getInstance(){return ready?reinterpret_cast<void*>(1):nullptr;}};
struct LLNotificationsUtil{static inline int alerts=0;static void add(std::string name,const LLSD&args){assert(name=="NoHavok"&&args.feature=="No Havok");++alerts;}};
struct LLFloaterPathfindingConsole{enum{kConsoleStateLibraryNotImplemented=4};int state=0;void setConsoleState(int s){state=s;}std::string getString(const char*){return"No Havok";}bool showUnavailableLibrary()ACTUAL};
int main(){LLFloaterPathfindingConsole ui;assert(ui.showUnavailableLibrary());assert(ui.state==4&&LLNotificationsUtil::alerts==1);LLPathingLib::ready=true;ui.state=7;assert(!ui.showUnavailableLibrary());assert(ui.state==7&&LLNotificationsUtil::alerts==1);}
'''.replace('ACTUAL',actual))
  cpp=source('indra/newview/llfloaterpathfindingconsole.cpp');opened=body(cpp,'void LLFloaterPathfindingConsole::onOpen(');self.assertLess(opened.index('if (VSNativeSession::active()) return;'),opened.index('mRegionBoundarySlot'))
  closed=body(cpp,'void LLFloaterPathfindingConsole::onClose(');self.assertLess(closed.index('!LLPathingLib::isFunctional()'),closed.index('switchOutOfTestPathMode'))
 def test_every_actual_http_suspend_rechecks_original_intent_before_publication(self):
  cpp=source('indra/newview/llpathfindingmanager.cpp')
  for name in ('navMeshStatusRequestCoro','navAgentStateRequestCoro','navMeshRebakeCoro','linksetObjectsCoro','linksetTerrainCoro','charactersCoro'):
   function=body(cpp,'void LLPathfindingManager::'+name+'(')
   import re
   suspends=list(re.finditer(r'result = httpAdapter->(?:get|post|put)AndSuspend\([^;]+;',function));self.assertTrue(suspends,name)
   for event in suspends:self.assertTrue(function[event.end():].lstrip().startswith('if (!current()) return;'),name)
   self.assertNotIn('boost::bind(&LLPathfindingManager::'+name+', this',cpp)
  ui=source('indra/newview/llfloaterpathfindingobjects.cpp');self.assertIn('mNativeRows[pObjectPtr->getUUID()] = *token;',ui);self.assertIn('selector->selectFamily(token->second, append)',ui);self.assertIn('if (selected()) response(notification, answer)',ui)
if __name__=='__main__':unittest.main()
