"""Execute actual My Environments delete confirmation/lifetime/permission policy."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,HERE,method
class MyEnvironments(unittest.TestCase):
 def test_real_confirmation_retains_account_view_and_current_permissions(self):
  source=(ROOT/'indra/newview/llfloatermyenvironment.cpp').read_text();header=(ROOT/'indra/newview/vsnativeim.h').read_text()
  methods='template<class Callback>\n'+method(header,'auto vs_native_im_account_callback(')+'\ntemplate<class Current,class Callback>\n'+method(header,'auto vs_native_im_lifetime_callback(')
  methods+='\n'+method(source,'bool can_delete(') # extractor skips prototype and selects real body
  for sig in ['std::function<bool()> LLFloaterMyEnvironment::requestGuard()', 'void LLFloaterMyEnvironment::onClose(', 'void LLFloaterMyEnvironment::onDeleteSelected()', 'void LLFloaterMyEnvironment::onItemsRemovalConfirmation(']:methods+='\n'+method(source,sig)
  fixture=(HERE/'native_my_environment_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'my_environment.cpp';exe=Path(tmp)/'my_environment.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
