"""Run the real console lifetimes, scalar event parser and circuit admission."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionConsoleTest(unittest.TestCase):
 def test_actual_http_scalar_async_reply_and_request_retirement(self):
  folder=Path(os.environ['VS_REGION_CONSOLE_SOURCE'])if 'VS_REGION_CONSOLE_SOURCE'in os.environ else ROOT/'indra/newview'
  source=(folder/'llfloaterregiondebugconsole.cpp').read_text();session=(folder/'vsnativesession.cpp').read_text()
  header=folder/'vsnativesession.h';header=header if header.exists()else ROOT/'indra/newview/vsnativesession.h'
  methods=[method(source,sig)for sig in ['LLFloaterRegionDebugConsole::LLFloaterRegionDebugConsole(', 'LLFloaterRegionDebugConsole::~LLFloaterRegionDebugConsole(', 'std::function<bool()> LLFloaterRegionDebugConsole::requestGuard(', 'void LLFloaterRegionDebugConsole::onClose(', 'void LLFloaterRegionDebugConsole::onInput(', 'void LLFloaterRegionDebugConsole::onReplyReceived(', 'void LLFloaterRegionDebugConsole::onConsoleSuccess(', 'void LLFloaterRegionDebugConsole::onConsoleError(', 'void LLFloaterRegionDebugConsole::onAsyncConsoleError(']]
  methods+=[method(session,'bool VSNativeSession::admit('),method((folder/'lleventpoll.cpp').read_text(),'void LLEventPollImpl::handleMessage(const LLSD&')]
  fixture=(HERE/'native_region_console_fixture.cpp').read_text().replace('NODE',method(source,'void post(')).replace('READY',method(header.read_text(),'bool regionReady() const')).replace('METHODS','\n'.join(methods))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'console.cpp';exe=Path(tmp)/'console.exe';cpp.write_text(fixture)
   build=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr)
   run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
