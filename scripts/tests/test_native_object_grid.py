from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Grid(unittest.TestCase):
 def test_actual_native_grid_matches_original_local_world_joint_and_region_geometry_paths(self):
  source=(HERE/'fixtures/native_object_grid_fixture.cpp').read_text().replace('REFERENCE',(HERE/'fixtures/native_object_grid_original.cpp').read_text()).replace('NATIVE',method((ROOT/'indra/newview/vsobjectselection.cpp').read_text(),'bool VSObjectSelection::getGrid('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_published_bounds_retains_accepted_generation_and_account_ancestry(self):
  source=(HERE/'fixtures/native_object_grid_bounds_fixture.cpp').read_text().replace('METHOD',method((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'std::optional<std::array<LLVector3,2>> VSWorldScene::publishedObjectBounds('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
