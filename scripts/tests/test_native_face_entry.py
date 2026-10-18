"""Actual native source-entry edit transaction and original TE message packing."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeFaceEntry(unittest.TestCase):
 def test_source_revision_current_guard_and_exact_cpu_te_serialization(self):
  source=(ROOT/'indra/newview/vsworldscene.cpp').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/entry.cpp').read_text().replace('EDIT',method(source,'S32 VSWorldScene::editTextureEntry(')).replace('PACK',method(source,'bool VSWorldScene::packTextureEntries('))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'entry.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],text=True,capture_output=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
