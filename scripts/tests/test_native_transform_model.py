from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Model(unittest.TestCase):
 def test_actual_local_queries_atomic_finite_preview_field_mask_and_retired_identity(self):
  s=(ROOT/'indra/newview/vsworldscene.cpp').read_text();query=method(s,'std::optional<VSWorldScene::LocalTransform> VSWorldScene::objectLocalTransform(');preview=method(s,'bool VSWorldScene::previewLocalTransform(');source=(HERE/'fixtures/native_transform_model_fixture.cpp').read_text().replace('QUERY',query).replace('PREVIEW',preview);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
