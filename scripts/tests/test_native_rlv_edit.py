"""Keep original RLVa edit/interact policies across real renderer-peer owners."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT
HERE=Path(__file__).resolve().parent
class RlvEditTest(unittest.TestCase):
 def test_reference_policies_lazy_access_and_retired_native_owner(self):
  current=(ROOT/'indra/newview/rlvactions.cpp').read_text(encoding='utf-8')
  start=current.index('namespace\n{\nstruct ReferenceEditObject')
  stop=current.index('// Handles: @pay',start)
  source=(HERE/'native_rlv_edit_fixture.cpp').read_text(encoding='utf-8').replace('COMMON',(HERE/'native_rlv_edit_common.cpp').read_text(encoding='utf-8')).replace('REFERENCE_METHODS',(HERE/'native_rlv_edit_gl_reference.cpp').read_text(encoding='utf-8')).replace('CURRENT_METHODS',current[start:stop])
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'edit.cpp';exe=Path(tmp)/'edit.exe';cpp.write_text(source,encoding='utf-8')
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
