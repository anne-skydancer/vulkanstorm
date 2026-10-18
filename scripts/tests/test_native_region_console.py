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
 def test_every_shipped_console_overlay_uses_actual_control_schema(self):
  import xml.etree.ElementTree as ET
  schemas=list((ROOT/'indra/newview/skins').rglob('floater_region_debug_console.xml'));self.assertEqual(len(schemas),13)
  base=ET.parse(ROOT/'indra/newview/skins/default/xui/en/floater_region_debug_console.xml').getroot()
  controls={item.attrib['name']:dict(item.attrib)for item in base if 'name'in item.attrib}
  for path in schemas:
   effective={name:dict(attributes)for name,attributes in controls.items()}
   for item in ET.parse(path).getroot():
    if item.get('name')in effective:effective[item.get('name')].update(item.attrib)
   with self.subTest(path=str(path)):
    line=effective['region_debug_console_input'];self.assertNotIn('max_length',line);self.assertEqual(line['max_length_bytes'],'127')
    self.assertEqual(effective['region_debug_console_output']['max_length'],'65536')
  # Match actual parameter declarations rather than accepting another invented key.
  declaration=(ROOT/'indra/llui/lllineeditor.h').read_text();self.assertIn('bytes("max_length_bytes", 4096)',declaration);self.assertIn('max_length("")',(ROOT/'indra/llui/lllineeditor.cpp').read_text())

if __name__=='__main__':unittest.main()
