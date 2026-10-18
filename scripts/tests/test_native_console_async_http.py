"""Actual scalar console response crosses mock HTTP/EventQueue and real controller."""
from pathlib import Path
import os,sys,importlib.util,threading,http.client,xml.etree.ElementTree as ET,unittest,json
from unittest.mock import patch
from native_source_fixture import ROOT
class AsyncConsole(unittest.TestCase):
 def test_actual_http_event_and_extracted_controller_callback_chain(self):
  sys.path[:0]=[str(ROOT/'scripts'),str(ROOT/'scripts/tests')]
  path=Path(os.environ.get('VS_ASYNC_CONSOLE_RUNNER',ROOT/'scripts/run_vulkan_session_replay.py'));spec=importlib.util.spec_from_file_location('async_console_runner',path);runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
  with runner.SimulatorHTTP(console_async=True)as server:
   worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start();connection=http.client.HTTPConnection(*server.server_address,timeout=5)
   def post(path,payload):
    connection.request('POST',path,payload,{'Content-Type':'application/llsd+xml'});response=connection.getresponse();return response.status,response.read()
   try:
    seed=ET.Element('llsd');runner.append_llsd(seed,['EventQueueGet','GetDisplayNames','ChatSessionRequest','GetTexture','AvatarPickerSearch','SimConsole','SimConsoleAsync'])
    status,reply=post('/seed',ET.tostring(seed));self.assertEqual(status,200);caps=runner.read_llsd(ET.fromstring(reply));self.assertEqual(caps.get('SimConsoleAsync'),server.url+'/ui-sim-console-async');self.assertNotIn('SimConsole',caps)
    status,body=post('/ui-sim-console-async',b'<llsd><string>native qualification</string></llsd>');self.assertEqual(status,200);self.assertEqual(runner.read_llsd(ET.fromstring(body)),{})
    status,body=post('/events',b'<llsd><map/></llsd>');self.assertEqual(status,200);events=runner.read_llsd(ET.fromstring(body))['events'];responses=[e for e in events if e.get('message')=='SimConsoleResponse'];self.assertEqual(responses,[{'message':'SimConsoleResponse','body':'native console accepted'}]);self.assertIsInstance(responses[0]['body'],str)
    # Feed the received wire value through actual LLEventPoll dispatch, actual
    # HTTP node admission and actual request-owned console callback. Every other
    # request/close/account/circuit/forgery regression remains in that fixture.
    import test_native_region_console as controller
    fixture=ROOT/'scripts/tests/native_region_console_fixture.cpp';original=fixture.read_text();literal=json.dumps(responses[0]['body']);expected=json.dumps('native console accepted')
    supplied=original.replace('LLSD("first")','LLSD('+literal+')').replace('=="first"+PROMPT','=='+expected+'+PROMPT');self.assertNotEqual(supplied,original);read=Path.read_text
    def source(path,*args,**kwargs):return supplied if path==fixture else read(path,*args,**kwargs)
    with patch.object(Path,'read_text',source):controller.RegionConsoleTest('test_actual_http_scalar_async_reply_and_request_retirement').test_actual_http_scalar_async_reply_and_request_retirement()
    self.assertEqual(post('/ui-sim-console-async',b'<llsd><string>unapproved command</string></llsd>')[0],400);self.assertEqual(server.region_events,[])
   finally:connection.close();server.shutdown();worker.join(timeout=5)
 def test_default_seed_retains_genuine_requested_synchronous_fallback(self):
  sys.path.insert(0,str(ROOT/'scripts'));path=Path(os.environ.get('VS_ASYNC_CONSOLE_RUNNER',ROOT/'scripts/run_vulkan_session_replay.py'));spec=importlib.util.spec_from_file_location('sync_console_runner',path);runner=importlib.util.module_from_spec(spec);spec.loader.exec_module(runner)
  with runner.SimulatorHTTP()as server:
   worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start();connection=http.client.HTTPConnection(*server.server_address,timeout=5)
   try:
    seed=ET.Element('llsd');runner.append_llsd(seed,['EventQueueGet','GetDisplayNames','ChatSessionRequest','GetTexture','AvatarPickerSearch','SimConsole','SimConsoleAsync']);connection.request('POST','/seed',ET.tostring(seed));reply=connection.getresponse();self.assertEqual(reply.status,200);caps=runner.read_llsd(ET.fromstring(reply.read()));self.assertEqual(caps.get('SimConsole'),server.url+'/ui-sim-console');self.assertNotIn('SimConsoleAsync',caps)
   finally:connection.close();server.shutdown();worker.join(timeout=5)
if __name__=='__main__':unittest.main()
