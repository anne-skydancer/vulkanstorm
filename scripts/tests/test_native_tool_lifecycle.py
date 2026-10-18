"""Actual native bootstrap and original resolution with capture/retirement reentry."""
from pathlib import Path
import importlib.util,shutil,subprocess,tempfile,unittest,sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1]
sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
class Tools(unittest.TestCase):
 def test_actual_account_reconnect_and_final_policy_cleanup_callers(self):
  session=(ROOT/'indra/newview/vsnativesession.cpp').read_text();app=(ROOT/'indra/newview/llappviewer.cpp').read_text()
  connected=method(session,'void VSNativeSession::connected(');reset=method(session,'void VSNativeSession::reset(');cleanup=method(app,'bool LLAppViewer::cleanupNativeVulkanLogin(')
  self.assertLess(connected.index('initNativeTools()'),connected.index('setNativeConnected(true)'))
  self.assertLess(reset.index('retireNativeTools()'),reset.index('mObjectSelection->reset()'))
  self.assertLess(reset.index('mObjectAttention.reset()'),reset.index('world().reset()'))
  self.assertLess(cleanup.index('retireNativeTools()'),cleanup.index('SUBSYSTEM_CLEANUP(RlvHandler)'))
 def test_genuine_default_override_capture_and_reentrant_account_retirement(self):
  source=(ROOT/'indra/newview/lltoolmgr.cpp').read_text()
  methods='\n'.join(method(source,s)for s in ['void LLToolMgr::initNativeTools(', 'void LLToolMgr::retireNativeTools(', 'bool LLToolMgr::nativeToolsReady(', 'LLTool* LLToolMgr::nativeCurrentTool(', 'LLTool* LLToolMgr::getCurrentTool(', 'void LLToolMgr::setCurrentToolset(', 'void LLToolMgr::setCurrentTool(', 'void LLToolMgr::updateToolStatus(', 'void LLToolMgr::setTransientTool(', 'void LLToolMgr::clearTransientTool('])
  fixture=(HERE/'native_tool_lifecycle_fixture.cpp').read_text().replace('METHODS',methods)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   folder=Path(tmp);cpp=folder/'tool.cpp';exe=folder/'tool.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
