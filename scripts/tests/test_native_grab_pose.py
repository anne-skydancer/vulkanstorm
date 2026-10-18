from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Pose(unittest.TestCase):
 def test_actual_current_official_predicted_root_and_hud_metadata_without_rich_copy(self):
  s=(ROOT/'indra/newview/vsworldscene.cpp').read_text();source=(HERE/'fixtures/native_grab_pose_fixture.cpp').read_text().replace('METHOD',method(s,'std::optional<VSWorldScene::ManipulationObject> VSWorldScene::manipulationObject('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
