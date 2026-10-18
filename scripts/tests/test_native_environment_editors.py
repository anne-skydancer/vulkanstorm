"""Execute proposed shared environment editor ownership without network operations."""
from pathlib import Path
import importlib.util, subprocess, shutil, tempfile, unittest, sys
from native_source_fixture import production_sources,ROOT
sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
class Editors(unittest.TestCase):
 def test_actual_shared_init_releases_setting_callback_on_each_account_retirement(self):
  sources=production_sources();env=sources['indra/newview/llenvironment.cpp'];session=sources['indra/newview/vsnativesession.cpp']
  simpleton=(ROOT/'indra/llcommon/llsingleton.h').read_text();start=simpleton.index('template<class T>\nclass LLSimpleton');stop=simpleton.index('#ifdef LL_WINDOWS',start)
  fixture=sources['scripts/tests/native_environment_lifecycle_fixture.cpp'].replace('SIMPLETON',simpleton[start:stop]).replace('INITIALIZE',method(env,'void LLEnvironment::initSingleton()')).replace('CLEANUP',method(env,'void LLEnvironment::cleanupSingleton()')).replace('ENSURE',method(session,'void VSNativeSession::initializeEnvironment()'))
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'lifecycle.cpp';exe=Path(tmp)/'lifecycle.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_proposed_region_selection_view_asset_and_account_intent(self):
  sources=production_sources()
  header=sources['indra/newview/vsnativeim.h']
  code='template<class Callback>\n'+method(header,'auto vs_native_im_account_callback(')
  code+='\ntemplate<class Current,class Callback>\n'+method(header,'auto vs_native_im_lifetime_callback(')
  code+='\n'+method(sources['indra/newview/llpanelenvironment.cpp'],'std::function<bool()> LLPanelEnvironmentInfo::requestGuard()')
  code+='\n'+method(sources['indra/newview/llfloatereditenvironmentbase.cpp'],'std::function<bool()> LLFloaterEditEnvironmentBase::requestGuard()')
  code+='\n'+method(sources['indra/newview/llsettingspicker.cpp'],'void LLFloaterSettingsPicker::onAssetLoadedCb(')
  fixture=Path(__file__).with_name('native_environment_editors_fixture.cpp').read_text().replace('METHODS',code)
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'editors.cpp';exe=Path(tmp)/'editors.exe';cpp.write_text(fixture)
   r=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
