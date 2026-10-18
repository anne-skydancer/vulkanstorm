from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from native_source_fixture import method
class SelectInput(unittest.TestCase):
 def compile(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   p=Path(temp);cpp=p/'main.cpp';exe=p/'main.exe';cpp.write_text(text);r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_original_inspect_click_and_fast_click_controller_on_both_peers(self):
  text=(HERE/'fixtures/native_select_input_inspect.cpp').read_text().replace('REFERENCE',(HERE/'fixtures/native_select_input_original.cpp').read_text()).replace('ACTUAL',method((ROOT/'indra/newview/lltoolcomp.cpp').read_text(),'void LLToolCompInspect::pickCallback('));self.compile(text)
 def test_actual_click_uses_native_identity_and_shared_release(self):
  source=(ROOT/'indra/newview/lltoolselect.cpp').read_text();text=(HERE/'fixtures/native_select_input_click.cpp').read_text().replace('DOWN',method(source,'bool LLToolSelect::handleMouseDown(')).replace('UP',method(source,'bool LLToolSelect::handleMouseUp('));self.compile(text)
if __name__=='__main__':unittest.main()
