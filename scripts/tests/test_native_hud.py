"""Frozen GL icon geometry and actual native account script-message routing."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_hud'
def viewer(name):return(ROOT/'indra/newview'/name).read_text(encoding='utf-8-sig')
class NativeHud(unittest.TestCase):
 def run_cpp(self,text,options=()):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'hud.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++20',*options,str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_actual_icon_retirement_saved_pick_and_budget_lifetime(self):
  text=(FIXTURES/'icon_lifetime_fixture.cpp').read_text().replace('MARK',method(viewer('llhudicon.cpp'),'void LLHUDIcon::markDead()')).replace('CLEAN',method(viewer('llhudobject.cpp'),'void LLHUDObject::cleanupUnreferenced()'))
  self.run_cpp(text)
 def test_frozen_icon_geometry_animation_fade_and_current_source(self):
  source=viewer('llhudicon.cpp');text=(FIXTURES/'icon_quad_fixture.cpp').read_text()
  text=text.replace('BOUNCE',method(source,'static F32 calc_bouncy_animation(')).replace('REFERENCE',(FIXTURES/'icon_quad_gl_reference.cpp').read_text()).replace('CURRENT',method(source,'bool LLHUDIcon::nativeQuad(')).replace('IDENTITY',method(source,'LLUUID LLHUDIcon::getSourceID() const'))
  self.run_cpp(text)
 def test_shared_script_routing_prefs_ownership_quit_and_grid(self):
  text=(FIXTURES/'script_route_fixture.cpp').read_text().replace('ROUTE',method(viewer('llfloaterscriptdebug.cpp'),'bool LLFloaterScriptDebug::routeChat('))
  self.run_cpp(text);self.run_cpp(text,['-DOPENSIM'])
if __name__=='__main__':unittest.main()
