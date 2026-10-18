from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Save(unittest.TestCase):
 def test_real_selected_node_capture_gltf_ratios_atomic_failure_and_retirement(self):
  s=(ROOT/'indra/newview/vsobjectselection.cpp').read_text();methods=method(s,'bool VSObjectSelection::saveSelectedObjectTransform(')+'\n'+method(s,'std::optional<LLBBox> VSObjectSelection::savedSelectionBBox(');source=(HERE/'fixtures/native_save_transform_fixture.cpp').read_text().replace('METHODS',methods);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
