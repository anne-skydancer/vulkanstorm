from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Pose(unittest.TestCase):
 def test_actual_editor_live_region_pose_matches_original_and_node_attachment_branches(self):
  methods=method((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'std::optional<VSWorldScene::EditorTransform> VSWorldScene::editorTransform(');source=(HERE/'fixtures/native_editor_pose_fixture.cpp').read_text().replace('METHOD',methods).replace('REFERENCE_POSITION',(HERE/'fixtures/native_editor_original_position.cpp').read_text()).replace('REFERENCE_ROTATION',(HERE/'fixtures/native_editor_original_rotation.cpp').read_text());compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_gltf_asset_node_copy_and_retired_owner_refusal(self):
  methods=method((ROOT/'indra/newview/vsworldgltf.cpp').read_text(),'bool VSWorldGLTF::nodeMatrix(');source=(HERE/'fixtures/native_editor_gltf_node_fixture.cpp').read_text().replace('METHOD',methods);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
