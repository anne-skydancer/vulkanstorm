from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Overrides(unittest.TestCase):
 def test_actual_current_selected_overrides_match_original_policy_and_no_redundant_mesh_publication(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();source=(HERE/'fixtures/native_transform_override_fixture.cpp').read_text().replace('METHOD',method(s,'void VSObjectSelection::overrideObjectUpdates(')).replace('ORIGINAL',(HERE/'fixtures/native_transform_override_original.cpp').read_text());compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
