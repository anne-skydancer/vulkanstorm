"""Compile actual native feature/quality and raster-selection production bodies."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
ROOT=Path(os.environ.get('VS_GRAPHICS_TEST_ROOT',Path(__file__).resolve().parents[2]))
def body(source,name):
 start=source.index(name);end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 return source[start:end]
class NativeGraphicsSettingsTests(unittest.TestCase):
 def compile_run(self,code):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   src=Path(d)/'graphics.cpp';exe=Path(d)/'graphics.exe';src.write_text(code)
   built=subprocess.run([compiler,'-std=c++17','-I',str(ROOT/'indra/newview'),str(src),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
 def test_real_capability_filter_and_wireframe_pass_ownership(self):
  self.compile_run(r'''
#include "vsgraphicssettings.h"
#include <cassert>
#include <map>
struct Feature{bool mAvailable;float mRecommendedLevel;};
int main(){VSGraphicsCapabilities c;
 assert(!vs_graphics_feature_implemented("RenderFarClip",c));
 c.ready=true;c.anisotropy=16;c.maximumTextureDimension=8192;c.compression=true;
 for(const char* k:{"RenderFarClip","RenderTerrainPBRDetail","RenderAvatarMaxComplexity","RenderMaxTextureResolution","RenderCompressTextures"})assert(vs_graphics_feature_implemented(k,c));
 for(const char* k:{"RenderShadowDetail","RenderDeferredSSAO","RenderFSAAType","RenderTransparentWater","RenderGlow","RenderScreenSpaceReflections","RenderTonemapType"})assert(!vs_graphics_feature_implemented(k,c));
 std::map<std::string,Feature> f{{"RenderFarClip",{true,256}},{"RenderAnisotropic",{false,1}},{"RenderShadowDetail",{true,2}}};
 vs_graphics_limit_features(f,c);assert(f.at("RenderFarClip").mAvailable);assert(!f.at("RenderAnisotropic").mAvailable);assert(!f.at("RenderShadowDetail").mAvailable);assert(f.at("RenderShadowDetail").mRecommendedLevel==2);
 c.anisotropy=1;c.compression=false;c.maximumTextureDimension=0;
 assert(!vs_graphics_feature_implemented("RenderAnisotropic",c));assert(!vs_graphics_feature_implemented("RenderCompressTextures",c));assert(!vs_graphics_feature_implemented("RenderMaxTextureResolution",c));
 for(bool requested:{false,true})for(bool sky:{false,true})for(unsigned topology:{0u,1u,2u})for(bool supported:{false,true})
 assert(vs_graphics_wireframe_pass(requested,sky,topology,supported)==(requested&&supported&&!sky&&topology==0));}
''')
 def test_actual_quality_and_feature_application_preserve_unsupported_saved_values(self):
  source=(ROOT/'indra/newview/llfeaturemanager.cpp').read_text()
  quality=body(source,'void LLFeatureManager::setGraphicsLevel(U32 level, bool skipFeatures)')
  apply=body(source,'void LLFeatureManager::applyFeatures(bool skipFeatures)')
  self.compile_run(r'''
#include "vsgraphicssettings.h"
#include <cassert>
#include <map>
#include <set>
#include <optional>
#include <sstream>
using U32=unsigned;using S32=int;using F32=float;
#define LL_RELEASE_FOR_DOWNLOAD 1
std::ostringstream logStream;
#define LL_WARNS(x) logStream
#define LL_ENDL ""
enum{TYPE_BOOLEAN,TYPE_S32,TYPE_U32,TYPE_F32};
struct LLControlVariable{int type=TYPE_F32;float value=0;bool isType(int t)const{return t==type;}};
struct Saved{std::map<std::string,LLControlVariable>values;LLControlVariable*getControl(const std::string& k){auto p=values.find(k);return p==values.end()?nullptr:&p->second;}
 void setBOOL(const std::string&k,bool v){values[k].value=v;}void setS32(const std::string&k,int v){values[k].value=float(v);}void setU32(const std::string&k,unsigned v){values[k].value=float(v);}void setF32(const std::string&k,float v){values[k].value=v;}}gSavedSettings;
struct Feature{bool mAvailable=true;float mRecommendedLevel=0;};
bool native=true;std::optional<VSGraphicsCapabilities>native_graphics_capabilities(){return native?std::optional<VSGraphicsCapabilities>(VSGraphicsCapabilities{}):std::nullopt;}
int glCalls=0;void flush_glerror(){++glCalls;}
struct LLViewerShaderMgr{static bool sSkipReload;static LLViewerShaderMgr*instance(){static LLViewerShaderMgr m;return &m;}void setShaders(){++glCalls;}};bool LLViewerShaderMgr::sSkipReload=false;
struct Pipeline{void refreshCachedSettings(){++glCalls;}}gPipeline;
struct LLFeatureManager{using feature_map_t=std::map<std::string,Feature>;feature_map_t mFeatures;std::set<std::string>mSkippedFeatures;int base=0;std::string mask;
 void dump(){}void applyBaseMasks(){++base;}void maskFeatures(const std::string&s){mask=s;}
 bool isValidGraphicsLevel(unsigned i){return i<7;}std::string getNameForGraphicsLevel(unsigned i){return std::to_string(i);}
 float getRecommendedValue(const std::string&k){return mFeatures[k].mAvailable?mFeatures[k].mRecommendedLevel:0;}
 void setGraphicsLevel(U32,bool);void applyFeatures(bool);};
'''+quality+'\n'+apply+r'''
int main(){LLFeatureManager m;m.mFeatures={{"RenderFarClip",{true,96}},{"RenderShadowDetail",{false,2}}};gSavedSettings.values={{"RenderFarClip",{TYPE_F32,512}},{"RenderShadowDetail",{TYPE_S32,3}}};
 m.setGraphicsLevel(4,false);assert(m.base==1&&m.mask=="4"&&glCalls==0);assert(gSavedSettings.values["RenderFarClip"].value==96);assert(gSavedSettings.values["RenderShadowDetail"].value==3);
 m.mSkippedFeatures.insert("RenderFarClip");gSavedSettings.values["RenderFarClip"].value=128;m.setGraphicsLevel(100,true);assert(m.mask=="Low");assert(gSavedSettings.values["RenderFarClip"].value==128&&glCalls==0);
 native=false;m.setGraphicsLevel(2,false);assert(glCalls==3);assert(gSavedSettings.values["RenderShadowDetail"].value==0);assert(!LLViewerShaderMgr::sSkipReload);}
''')
 def test_real_status_landmark_action_uses_places_and_retains_rlva(self):
  status=(ROOT/'indra/newview/llstatusbar.cpp').read_text()
  action=body(status,'void LLStatusBar::onContextMenuItemClicked(const LLSD::String& item)')
  self.compile_run(r'''
#include <cassert>
#include <map>
#include <string>
const int RLV_BHVR_SHOWLOC=1;
struct LLSD{using String=std::string;std::map<std::string,std::string> fields;LLSD with(const std::string&k,const std::string&v)const{auto result=*this;result.fields[k]=v;return result;}};
struct Restriction{bool hide=false;bool hasBehaviour(int)const{return hide;}}gRlvHandler;
struct LLViewerInventoryItem{std::string getUUID()const{return "actual-landmark-id";}};
struct LLLandmarkActions{static bool present;static LLViewerInventoryItem*findLandmarkForAgentPos(){static LLViewerInventoryItem item;return present?&item:nullptr;}};bool LLLandmarkActions::present=false;
struct LLFloaterSidePanelContainer{static int calls;static std::string target;static LLSD key;static void showPanel(std::string s,LLSD k){++calls;target=s;key=k;}};int LLFloaterSidePanelContainer::calls=0;std::string LLFloaterSidePanelContainer::target;LLSD LLFloaterSidePanelContainer::key;
struct LLSLURL{std::string getSLURLString()const{return "actual-slurl";}};struct LLAgentUI{static void buildSLURL(LLSLURL&,bool){}};
struct LLUIString{std::string text;LLUIString(std::string t):text(t){}size_t length()const{return text.length();}};
struct LLClipboard{static LLClipboard&instance(){static LLClipboard c;return c;}void copyToClipboard(const LLUIString&,int,size_t){}};
struct LLStatusBar{void onContextMenuItemClicked(const LLSD::String&);};
'''+action+r'''
int main(){LLStatusBar bar;bar.onContextMenuItemClicked("landmark");assert(LLFloaterSidePanelContainer::calls==1);assert(LLFloaterSidePanelContainer::target=="places");assert(LLFloaterSidePanelContainer::key.fields["type"]=="create_landmark");
 LLLandmarkActions::present=true;bar.onContextMenuItemClicked("landmark");assert(LLFloaterSidePanelContainer::calls==2);assert(LLFloaterSidePanelContainer::key.fields["type"]=="landmark");assert(LLFloaterSidePanelContainer::key.fields["id"]=="actual-landmark-id");gRlvHandler.hide=true;bar.onContextMenuItemClicked("landmark");assert(LLFloaterSidePanelContainer::calls==2);}
''')
 def test_actual_controller_routes_and_preset_lifetimes(self):
  p=(ROOT/'indra/newview/llfloaterpreference.cpp').read_text()
  for method in ('void LLFloaterPreference::setHardwareDefaults()','void LLFloaterPreference::onChangeQuality(const LLSD& data)'):
   self.assertNotIn('if (VSNativeSession::active()) return;',body(p,method))
  for stem in ('llfloatersaveprefpreset','llfloaterloadprefpreset','llfloaterdeleteprefpreset','llpanelpresetspulldown'):
   self.assertIn('scoped_connection mPresetListConnection',(ROOT/('indra/newview/'+stem+'.h')).read_text())
   self.assertIn('mPresetListConnection = ',(ROOT/('indra/newview/'+stem+'.cpp')).read_text())
  refresh=body(p,'void LLFloaterPreference::refreshEnabledState()')
  self.assertIn('GRAPHICS_CARD_MEMORY_DETECTED',refresh);self.assertIn('vs_graphics_feature_implemented',refresh)
  status=(ROOT/'indra/newview/llstatusbar.cpp').read_text()
  self.assertNotIn('NativeWorldUnavailable',body(status,'void LLStatusBar::onMouseEnterPresets()'))
  renderer=(ROOT/'indra/newview/vsworldrenderer.cpp').read_text()
  self.assertIn('FILL_MODE_WIREFRAME',renderer);self.assertIn('vs_graphics_wireframe_pass(view.wireframe',renderer)
if __name__=='__main__':unittest.main()
