"""Differential test against the actual original selected-TE value controller."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeBuildTEValue(unittest.TestCase):
 def test_actual_original_primary_last_default_tolerance_and_lazy_getter_order(self):
  original=(ROOT/'indra/newview/llselectmgr.h').read_text();kernel=(ROOT/'indra/newview/vsbuildtevalue.h').read_text()
  body=method(original,'bool LLObjectSelection::getSelectedTEValue(')
  code=(Path(__file__).parent/'fixtures/native_face_edit/te_value.cpp').read_text().replace('ORIGINAL','template<class T>'+body).replace('KERNEL',kernel[kernel.index('template<class Selector'):])
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'value.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],text=True,capture_output=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],text=True,capture_output=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
