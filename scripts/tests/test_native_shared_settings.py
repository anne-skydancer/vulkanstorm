"""Execute both actual settings wrappers, preserving native/GL callback isolation."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeSharedSettings(unittest.TestCase):
    def test_inventory_accounts_for_every_registration(self):
        import csv,json
        records=json.loads((ROOT/'doc/vulkan/milestone1-boundaries.json').read_text())['registries']['settings']
        with (ROOT/'doc/vulkan/r3-settings-listeners.csv').open() as stream:rows=list(csv.DictReader(stream))
        self.assertEqual(len(rows),len(records))
        self.assertEqual({row['registration'] for row in rows},{row['call'] for row in records})
        self.assertTrue(all(row['effect_owner'] and row['roadmap_stage'] and row['status'] in ('shared-callback','native-poll','open','later-owner') for row in rows))

    def test_real_wrappers_route_shared_effects_and_block_gl_cascades(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        source=(ROOT/'indra/newview/llviewercontrol.cpp').read_text()
        helper=method(source,'bool native_shared_setting_callback(')
        callbacks='\n'.join(method(source,'void setting_setup_signal_listener(LLControlGroup& group, const std::string& setting, '+signature+' callback)') for signature in ('std::function<void(const LLSD& newvalue)>','std::function<void()>'))
        fixture=r'''
#include <algorithm>
#include <cassert>
#include <functional>
#include <iterator>
#include <map>
#include <string>
#include <vector>
#define VS_NATIVE_VULKAN 1
struct LLSD {int value=0;};
struct LLControlVariable;
struct Signal {using F=std::function<void(LLControlVariable*,const LLSD&,const LLSD&)>;std::vector<F> slots;void connect(F f){slots.push_back(f);}};
struct LLControlVariable {std::string name;Signal signal;const std::string& getName(){return name;}Signal* getSignal(){return &signal;}
 void fire(){for(const auto& f:signal.slots)f(this,LLSD{2},LLSD{1});}};
struct LLControlGroup {std::map<std::string,LLControlVariable> controls;};
LLControlVariable* setting_get_control(LLControlGroup& g,const std::string& s){auto& c=g.controls[s];c.name=s;return &c;}
struct VSNativeSession {static bool running;static bool active(){return running;}};bool VSNativeSession::running=false;
HELPER
CALLBACKS
int main(){
 for(bool native:{false,true})for(const auto& name:{"ChatFontSize","ShowNavbarFavoritesPanel","SpellCheck","AudioLevelVoice","FSOutputDeviceUUID","FSDiskCacheSize","RenderAnisotropic","RenderShadowDetail","OctreeDistanceFactor","UnknownFutureSetting"}){
  VSNativeSession::running=native;LLControlGroup group;int values=0,empty=0;
  setting_setup_signal_listener(group,name,std::function<void(const LLSD&)>([&](const LLSD& value){assert(value.value==2);++values;}));
  setting_setup_signal_listener(group,name,std::function<void()>([&](){++empty;}));
  VSNativeSession::running=false; // Account retirement cannot admit a deferred GL callback.
  setting_get_control(group,name)->fire();
  bool render=std::string(name)=="RenderAnisotropic"||std::string(name)=="RenderShadowDetail"||std::string(name)=="OctreeDistanceFactor"||std::string(name)=="UnknownFutureSetting";
  assert(values==int(!native||!render)&&empty==values);
 }
}
'''.replace('HELPER',helper).replace('CALLBACKS',callbacks)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'settings.cpp';path.write_text(fixture);exe=Path(temp)/'settings.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
