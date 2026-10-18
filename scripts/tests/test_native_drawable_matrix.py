from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Matrix(unittest.TestCase):
 def test_native_focus_uses_original_drawable_unit_scale_not_geometry_scale(self):
  source=(HERE/'fixtures/native_drawable_matrix_fixture.cpp').read_text().replace('METHOD',method((ROOT/'indra/newview/vsworldscene.cpp').read_text(),'std::optional<VSCameraFocusObject> VSWorldScene::focusObject(')).replace('ORIGINAL_FRAME',(HERE/'fixtures/native_drawable_matrix_original_frame.cpp').read_text()).replace('ORIGINAL_RENDER',(HERE/'fixtures/native_drawable_matrix_original_render.cpp').read_text());compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
