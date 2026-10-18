"""Real QuickPrefs callbacks and native hover permission/transport boundaries."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2]
def body(s,name):
 start=s.index(name);end=s.index('{',start)+1;depth=1
 while depth:depth+=(s[end]=='{')-(s[end]=='}');end+=1
 return s[start:end]
class QuickPrefsTests(unittest.TestCase):
 def source(self):
  target=ROOT/'indra/newview/vsnativeappearance.cpp'
  return target.read_text() if target.exists() else (ROOT/'hover_methods.cppfrag').read_text()
 def test_real_hover_callbacks_and_availability(self):
  appearance=self.source();ui=(ROOT/'indra/newview/quickprefs.cpp').read_text()
  fixture=r'''
#include <cassert>
#include <memory>
#include <functional>
#include <string>
#include <algorithm>
#include <iostream>
#define LL_INFOS(x) std::cerr
#define LL_ENDL "\n"
const int VZ=2;
using F32=float;
#define VS_NATIVE_VULKAN 1
const F32 MIN_HOVER_Z=-3,MAX_HOVER_Z=3;
float llclamp(float v,float lo,float hi){return std::clamp(v,lo,hi);}
struct Value{bool value=false;bool asBoolean()const{return value;}};
struct Features{Value hover;const Value& operator[](const char*)const{return hover;}};
struct VSNativeAppearance;
struct VSNativeSession{bool ready=true;Features features;VSNativeAppearance* service=nullptr;static std::shared_ptr<VSNativeSession> active();bool regionReady()const{return ready;}const Features& simulatorFeatures()const{return features;}VSNativeAppearance* appearance(){return service;}};
std::shared_ptr<VSNativeSession> owner;std::shared_ptr<VSNativeSession> VSNativeSession::active(){return owner;}
struct Avatar{bool serverBakes=true;Avatar& appearanceOwner(){return *this;}bool isUsingServerBakes()const{return serverBakes;}template<class T>void setHoverOffset(T,bool){}} avatar,*gAgentAvatarp=&avatar;
Avatar* selfAvatar(){return &avatar;}
struct VSNativeAppearance{struct Impl{bool valid=true;std::weak_ptr<VSNativeSession> session;bool current()const{return valid;}};std::shared_ptr<Impl>mImpl=std::make_shared<Impl>();int calls=0;F32 last=0;bool sent=false;bool hoverEnabled()const;bool setHoverHeight(F32 value,bool send){++calls;last=value;sent=send;return true;}};
struct Slider{F32 value=1;bool held=true;F32 getValueF32(){return value;}bool isMouseHeldDown(){return held;}};
struct FloaterQuickPrefs{Slider slider;Slider*mAvatarZOffsetSlider=&slider;bool current=true;std::function<bool()>mAccountCurrent=[this]{return current;};void onAvatarZOffsetSliderMoved();void onAvatarZOffsetFinalCommit();};
struct LLVector3{float z;LLVector3(float,float,float v):z(v){}float operator[](int)const{return z;}};
struct Region{bool avatarHoverHeightEnabled(){return false;}};
struct Agent{Region*getRegion(){return nullptr;}}gAgent;
struct Settings{int calls=0;F32 value=0;void setF32(const char*,F32 v){++calls;value=v;}}gSavedPerAccountSettings;
''' + body(appearance,'bool VSNativeAppearance::hoverEnabled() const') + '\n' + body(ui,'void FloaterQuickPrefs::onAvatarZOffsetSliderMoved()')+'\n'+body(ui,'void FloaterQuickPrefs::onAvatarZOffsetFinalCommit()')+r'''
int main(){owner=std::make_shared<VSNativeSession>();VSNativeAppearance service;service.mImpl->session=owner;owner->service=&service;FloaterQuickPrefs panel;
 assert(!service.hoverEnabled());owner->features.hover.value=true;assert(service.hoverEnabled());
 panel.onAvatarZOffsetSliderMoved();assert(service.calls==1&&!service.sent&&gSavedPerAccountSettings.calls==0);
 panel.slider.value=10;panel.onAvatarZOffsetFinalCommit();assert(service.calls==2&&service.sent&&service.last==3&&gSavedPerAccountSettings.value==3);
 owner->features.hover.value=false;panel.onAvatarZOffsetSliderMoved();assert(service.calls==2);
 avatar.serverBakes=false;assert(service.hoverEnabled());panel.slider.value=-1;panel.onAvatarZOffsetSliderMoved();assert(service.calls==3&&!service.sent&&gSavedPerAccountSettings.value==-1);
 panel.current=false;panel.onAvatarZOffsetFinalCommit();assert(service.calls==3);panel.current=true;
 owner->ready=false;assert(!service.hoverEnabled());panel.onAvatarZOffsetSliderMoved();assert(service.calls==3);
 owner->ready=true;service.mImpl->valid=false;assert(!service.hoverEnabled());panel.onAvatarZOffsetFinalCommit();assert(service.calls==3);
}
'''
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   cpp=Path(d)/'hover.cpp';exe=Path(d)/'hover.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr);self.assertEqual(subprocess.run([str(exe)]).returncode,0)
 def test_actual_hover_transient_completion_cap_readiness_and_no_frame_retry(self):
  source=self.source();fixture=Path(__file__).with_name('hover_transport_fixture.hpp').read_text()
  for name in ['bool VSNativeAppearance::hoverEnabled() const','void VSNativeAppearance::syncHoverHeight()','bool VSNativeAppearance::setHoverHeight(']:fixture+='\n'+body(source,name)
  fixture+=r"""
int main(){auto owner=std::make_shared<VSNativeSession>();owner->features["AvatarHoverHeightEnabled"].b=true;VSNativeAppearance a;a.mImpl->session=owner;
 owner->cap="";a.syncHoverHeight();assert(tasks.empty()&&httpCalls==0&&avatar.local==1);
 owner->cap="preferences";a.syncHoverHeight();assert(tasks.size()==1);httpStatus=503;run();assert(httpCalls==1&&!a.mImpl->hoverLastSent&&!a.mImpl->hoverRequest);
 a.syncHoverHeight();assert(tasks.empty());a.setHoverHeight(1,true);assert(tasks.size()==1);httpStatus=200;run();assert(httpCalls==2&&a.mImpl->hoverLastSent==1&&sentValue==1);
 a.setHoverHeight(1,true);assert(tasks.empty());
 a.setHoverHeight(2,true);assert(tasks.size()==1);postHook=[](){++avatar.gen;};run();postHook={};assert(a.mImpl->hoverLastSent==1); // retired avatar cannot publish success
 a.syncHoverHeight();assert(tasks.size()==1);run();assert(a.mImpl->hoverLastSent==1);
 a.setHoverHeight(2,true);++owner->epoch;run();assert(a.mImpl->hoverLastSent==1); // old region cannot publish
 a.syncHoverHeight();assert(tasks.size()==1);run();assert(a.mImpl->hoverLastSent==1);
 a.setHoverHeight(2,true);a.mImpl->valid=false;run();assert(a.mImpl->hoverLastSent==1);
}
"""
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as d:
   cpp=Path(d)/'transport.cpp';exe=Path(d)/'transport.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
   self.assertEqual(built.returncode,0,built.stdout+built.stderr);self.assertEqual(subprocess.run([str(exe)]).returncode,0)
 def test_hover_transport_is_account_region_avatar_scoped(self):
  s=body(self.source(),'bool VSNativeAppearance::setHoverHeight(')
  before,after=s.split('adapter->postAndSuspend',1)
  for token in ['owner->current(account)','owner->regionEpoch() != epoch','avatar->ownerGeneration() != avatarGeneration','state->hoverSerial != serial']:
   self.assertIn(token,before)
  self.assertIn('owner->regionEpoch() != epoch',after)
  self.assertIn('body["hover_height"] = value',before)
  self.assertIn('owner->capability("AgentPreferences")',before)
  ui=(ROOT/'indra/newview/quickprefs.cpp').read_text()
  for line in ui.splitlines():
   if '->connect(' in line and ('gSavedSettings.getControl(' in line or 'gSavedPerAccountSettings.getControl(' in line):self.assertIn('mSettingsConnections.emplace_back',line)
  self.assertIn('vs_native_im_ui_callback(this',body(ui,'void FloaterQuickPrefs::onClickRestoreDefaults()'))
if __name__=='__main__':unittest.main()

