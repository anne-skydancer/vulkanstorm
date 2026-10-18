from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Pose(unittest.TestCase):
 def test_exact_original_edit_position_rotation_and_volume_vs_avatar_root_predicate(self):
  source=(HERE/'fixtures/native_edit_transform_fixture.cpp').read_text();native=(ROOT/'indra/newview/vsworldscene.cpp').read_text();source=source.replace('REFERENCE_POSITION',(HERE/'fixtures/native_edit_transform_original_position.cpp').read_text()).replace('REFERENCE_ROTATION',(HERE/'fixtures/native_edit_transform_original_rotation.cpp').read_text()).replace('REFERENCE_ROOT',(HERE/'fixtures/native_edit_transform_original_root.cpp').read_text()).replace('ROOT_QUERY',method(native,'std::optional<bool> VSWorldScene::objectIsRootEdit(')).replace('EDIT_QUERY',method(native,'std::optional<VSWorldScene::LocalTransform> VSWorldScene::objectEditTransform('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
