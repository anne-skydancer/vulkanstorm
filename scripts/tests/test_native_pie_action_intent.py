from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Intent(unittest.TestCase):
 def test_actual_deferred_action_selection_lease_current_identity_and_reentrant_one_shot(self):
  s=(ROOT/'indra/newview/lltoolpie.cpp').read_text();body=method(s,'void LLToolPie::selectionPropertiesReceived()');body=body[:body.index('#endif')+len('#endif')]+'\n}';reset=method(s,'void LLToolPie::resetSelection()');source=(HERE/'fixtures/native_pie_action_intent_fixture.cpp').read_text().replace('METHOD',body).replace('RESET',reset);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
