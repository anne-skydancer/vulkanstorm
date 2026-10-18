"""Actual original material-drop policy and genuine target/source retirement guards."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeMaterialDrop(unittest.TestCase):
 def test_original_permission_inventory_policy_and_reentrant_account_retirement(self):
  source=(ROOT/'indra/newview/lltooldraganddrop.cpp').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/material_drop.cpp').read_text().replace('ORIGINAL',method(source,'bool LLToolDragAndDrop::handleDropMaterialProtections(LLViewerObject*')).replace('NATIVE',method(source,'bool LLToolDragAndDrop::handleDropMaterialProtections(const VSTaskInventoryObject&'))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'drop.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
