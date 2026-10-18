from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Flow(unittest.TestCase):
 def test_full_original_left_click_flow_results_state_and_exact_lazy_getter_effect_order(self):
  s=(ROOT/'indra/newview/lltoolpie.cpp').read_text();original=(HERE/'fixtures/native_pie_original_flow.cpp').read_text().replace('LLToolPie::handleLeftClickPick','originalClick');shared=method(s,'template<class Target>bool LLToolPie::handleLeftClickPickTarget(').replace('LLToolPie::handleLeftClickPickTarget','handleLeftClickPickTarget');start=s.index('struct ReferencePieActions:');end=s.index('#if VS_NATIVE_VULKAN',start);peer=s[start:end];priority=method(s,'struct ReferencePiePriority')+';\n'+(ROOT/'indra/newview/vspieclickpolicy.h').read_text().replace('#pragma once','').replace('#include \"indra_constants.h\"','').replace('#include \"llprimitive.h\"','');source=(HERE/'fixtures/native_pie_action_flow_fixture.cpp').read_text().replace('PRIORITY',priority).replace('PEER',peer).replace('ORIGINAL',original).replace('TEMPLATE',shared);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
