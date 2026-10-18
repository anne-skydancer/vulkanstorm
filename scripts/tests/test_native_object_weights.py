"""Actual accounting request/response ownership and Object Weights source tests."""
from pathlib import Path
import os,re,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_WEIGHTS_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_WEIGHTS_PROPOSED'])if 'VS_WEIGHTS_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativeObjectWeightsTests(unittest.TestCase):
 def test_actual_request_copies_ids_and_requires_live_publication_intent(self):
  actual=body(source('indra/newview/llaccountingcostmanager.cpp'),'void LLAccountingCostManager::fetchSelectionCosts(')
  run(self,r'''
#include <cassert>
#include <functional>
#include <set>
#include <string>
using LLUUID=int;using uuid_set_t=std::set<int>;using eSelectionType=int;
struct LLAccountingCostObserver{int transaction=7;int getTransactionID(){return transaction;}};
template<class T>struct LLHandle{T*p;bool isDead()const{return !p;}T*get()const{return p;}};
struct LLCoros{std::function<void()>queued;static LLCoros&instance(){static LLCoros value;return value;}void launch(std::string,std::function<void()>fn){queued=fn;}};
struct LLAccountingCostManager{int calls=0;static int capture;static uuid_set_t ids;static void accountingCostCoro(std::string,int,LLHandle<LLAccountingCostObserver>,uuid_set_t values,std::function<bool()>current,int transaction){if(current()){capture=transaction;ids=values;}}
 void fetchSelectionCosts(eSelectionType type,const std::string&url,const LLHandle<LLAccountingCostObserver>&observer,const uuid_set_t&objects,std::function<bool()>current)ACTUAL};int LLAccountingCostManager::capture=0;uuid_set_t LLAccountingCostManager::ids;
int main(){LLAccountingCostManager manager;LLAccountingCostObserver observer;bool live=true;auto guard=[&]{return live;};uuid_set_t ids{1,2};manager.fetchSelectionCosts(0,"",{&observer},ids,guard);assert(!LLCoros::instance().queued);manager.fetchSelectionCosts(0,"cap",{nullptr},ids,guard);assert(!LLCoros::instance().queued);manager.fetchSelectionCosts(0,"cap",{&observer},ids,guard);assert(LLCoros::instance().queued);ids={9};LLCoros::instance().queued();assert(manager.ids==uuid_set_t({1,2})&&manager.capture==7);live=false;manager.capture=0;LLCoros::instance().queued();assert(manager.capture==0);}
'''.replace('ACTUAL',actual))
 def test_actual_post_suspend_gate_expires_account_region_selection_observer_and_transaction(self):
  cpp=source('indra/newview/llaccountingcostmanager.cpp');start=cpp.index('        if (LLApp::isQuitting()');end=cpp.index('\n        LLAccountingCostObserver* observer',start);actual=cpp[start:end]
  run(self,r'''
#include <cassert>
#include <functional>
struct Observer{int transaction=5;int getTransactionID(){return transaction;}};struct Handle{Observer*p;bool isDead(){return !p;}Observer*get(){return p;}};
struct LLApp{static bool quitting;static bool isQuitting(){return quitting;}};bool LLApp::quitting=false;struct LLAccountingCostManager{static bool exists;static bool instanceExists(){return exists;}};bool LLAccountingCostManager::exists=true;
bool admitted(Handle observerHandle,std::function<bool()>current,int transaction){ACTUAL return true;}
int main(){Observer observer;bool live=true;auto current=[&]{return live;};assert(admitted({&observer},current,5));assert(!admitted({nullptr},current,5));observer.transaction=6;assert(!admitted({&observer},current,5));observer.transaction=5;live=false;assert(!admitted({&observer},current,5));live=true;LLApp::quitting=true;assert(!admitted({&observer},current,5));LLApp::quitting=false;LLAccountingCostManager::exists=false;assert(!admitted({&observer},current,5));}
'''.replace('ACTUAL',actual.replace('return;','return false;')))
 def test_actual_face_cache_prunes_removed_replaced_and_unrigged_publications(self):
  actual=body(source('indra/newview/vsworldscene.cpp'),'void pruneMetricsRigging()')
  run(self,r'''
#include <cassert>
#include <algorithm>
#include <vector>
#include <memory>
using S32=int;
struct Asset{};struct Raw{bool rigged=true;int face=0;std::shared_ptr<Asset>asset;};struct Face{bool published=true,mesh=true;std::shared_ptr<Raw>pickSurface;};
struct Object{std::vector<std::pair<std::shared_ptr<Asset>,std::shared_ptr<int>>>metricsRigging;std::vector<Face>admittedFaces;void pruneMetricsRigging()ACTUAL};
int main(){Object object;auto old=std::make_shared<Asset>(),next=std::make_shared<Asset>();auto table=std::make_shared<int>();std::weak_ptr<int>weak=table;object.metricsRigging={{old,table},{old,table},{old,table}};table.reset();object.admittedFaces={{true,true,std::make_shared<Raw>(Raw{true,0,old})}};object.pruneMetricsRigging();assert(object.metricsRigging.size()==1&&!weak.expired());object.admittedFaces[0].pickSurface->asset=next;object.pruneMetricsRigging();assert(object.metricsRigging.empty()&&weak.expired());table=std::make_shared<int>();weak=table;object.metricsRigging={{next,table}};table.reset();object.admittedFaces[0].pickSurface->rigged=false;object.pruneMetricsRigging();assert(object.metricsRigging.empty()&&weak.expired());object.metricsRigging={{next,std::make_shared<int>()}};object.admittedFaces.clear();object.pruneMetricsRigging();assert(object.metricsRigging.empty());}
'''.replace('ACTUAL',actual))
 def test_real_geometry_pixel_and_selection_routes_keep_reference_ownership(self):
  weights=source('indra/newview/llfloaterobjectweights.cpp');self.assertIn('fetchSelectionCosts(Roots, url, getObserverHandle(), objects, guard)',weights);self.assertIn('selection.objectsCrossParcel()',weights);self.assertIn('count(*object);',weights);self.assertIn('object->getChildren()) count(*child)',weights)
  scene=body(source('indra/newview/vsworldscene.cpp'),'VSWorldScene::objectMetrics(');self.assertIn('faceRiggingBounds(*cached.second, rigged)',scene);self.assertIn('reserveSpatialBytes(bytes)',scene);self.assertIn('face.pickSurface->asset',scene);self.assertIn('radius * radius * 3.14159f',scene)
  rig=source('indra/newview/vsavatarrigginginfo.cpp');self.assertIn('selectedFace + 1',rig);self.assertIn('influence>.2f',rig)
  self.assertIn('name == "object_weights"',source('indra/newview/vsstartupui.cpp'));self.assertIn('destroyInstance("object_weights")',source('indra/newview/vsnativeim.cpp'))
if __name__=='__main__':unittest.main()
