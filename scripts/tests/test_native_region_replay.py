"""Complete encoded RegionInfo fixture schema and actual loopback console HTTP."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest,sys,importlib.util,threading,http.client,xml.etree.ElementTree as ET
from native_source_fixture import ROOT,method
HERE=Path(__file__).resolve().parent
class RegionReplay(unittest.TestCase):
 def test_actual_packet_builder_preserves_complete_schema_and_current_owner(self):
  source=Path(os.environ.get('VS_REGION_REPLAY_CPP',ROOT/'indra/newview/vsnativeimreplay.cpp')).read_text();methods='\n'.join(method(source,sig)for sig in ['void publishRegionUI(', 'void publishEstateUI('])
  fixture='#include <algorithm>\n'+(HERE/'native_region_replay_wire_fixture.cpp').read_text().replace('METHODS',methods.replace('void publishRegionUI(','void Replay::publishRegionUI(',1).replace('void publishEstateUI(','void Replay::publishEstateUI(',1))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'wire.cpp';exe=Path(tmp)/'wire.exe';cpp.write_text(fixture);build=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(build.returncode,0,build.stdout+build.stderr);run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
 def test_actual_console_fixture_endpoint_uses_exact_llsd_command(self):
  sys.path.insert(0,str(ROOT/'scripts'));p=Path(os.environ.get('VS_REGION_REPLAY_RUNNER',ROOT/'scripts/run_vulkan_session_replay.py'));spec=importlib.util.spec_from_file_location('region_session_runner',p);runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
  with runner.SimulatorHTTP()as server:
   worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start();connection=http.client.HTTPConnection(*server.server_address,timeout=5)
   try:
    root=ET.Element('llsd');runner.append_llsd(root,'native qualification');connection.request('POST','/ui-sim-console',ET.tostring(root,encoding='utf-8'));response=connection.getresponse();self.assertEqual(response.status,200);self.assertEqual(runner.read_llsd(ET.fromstring(response.read())),'native console accepted')
    root=ET.Element('llsd');runner.append_llsd(root,'unapproved command');connection.request('POST','/ui-sim-console',ET.tostring(root,encoding='utf-8'));response=connection.getresponse();self.assertEqual(response.status,400);response.read()
   finally:connection.close();server.shutdown();worker.join(timeout=5)
if __name__=='__main__':unittest.main()
