"""Real LLSD report request -> mock HTTP -> EventQueueGet preserves wire types."""
from pathlib import Path
import os,sys,importlib.util,unittest,threading,http.client,xml.etree.ElementTree as ET,copy
from native_source_fixture import ROOT
PAYLOAD=b'''<llsd><map><key>message</key><string>LandStatReply</string><key>body</key><map><key>RequestData</key><array><map><key>ReportType</key><binary>AAAAAA==</binary><key>RequestFlags</key><binary>gAAAAA==</binary><key>TotalObjectCount</key><binary>AAAAAQ==</binary></map></array><key>ReportData</key><array><map><key>TaskLocalID</key><binary>AAAAew==</binary><key>TaskID</key><uuid>50000000-0000-0000-0000-000000000005</uuid><key>LocationX</key><real>8</real><key>LocationY</key><real>8</real><key>LocationZ</key><real>25</real><key>Score</key><real>0.25</real><key>TaskName</key><string>Native tracked CPU target</string><key>OwnerName</key><string>CPU Sender</string></map></array></map></map></llsd>'''
class LandStatHTTP(unittest.TestCase):
 def module(self):
  sys.path.insert(0,str(ROOT/'scripts'));p=Path(os.environ.get('VS_LANDSTAT_RUNNER',ROOT/'scripts/run_vulkan_session_replay.py'));spec=importlib.util.spec_from_file_location('landstat_session',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
 def test_actual_http_event_delivery_preserves_complete_binary_uuid_body_once(self):
  m=self.module()
  with m.SimulatorHTTP()as server:
   worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start();client=http.client.HTTPConnection(*server.server_address,timeout=5)
   def post(path,payload):
    client.request('POST',path,payload,{'Content-Type':'application/llsd+xml'});response=client.getresponse();body=response.read();return response.status,body
   try:
    self.assertEqual(post('/message',PAYLOAD)[0],200)
    status,body=post('/events',b'<llsd><map><key>ack</key><undef/><key>done</key><boolean>false</boolean></map></llsd>');self.assertEqual(status,200)
    response=m.read_llsd(ET.fromstring(body));events=[x for x in response['events']if x['message']=='LandStatReply'];self.assertEqual(len(events),1);self.assertEqual(events[0]['body'],m.read_llsd(ET.fromstring(PAYLOAD))['body'])
    # The actual server EventQueue body retains binary+uuid types, never strings.
    parsed=ET.fromstring(body);binaries=parsed.findall('.//binary');self.assertEqual([x.text for x in binaries],['AAAAAA==','gAAAAA==','AAAAAQ==','AAAAew==']);self.assertTrue(any(x.text=='50000000-0000-0000-0000-000000000005' for x in parsed.findall('.//uuid')))
    self.assertFalse(any(x['message']=='LandStatReply' for x in m.read_llsd(ET.fromstring(post('/events',b'<llsd><map/></llsd>')[1]))['events']))
    for invalid in [PAYLOAD.replace(b'<binary>AAAAew==</binary>',b'<string>AAAAew==</string>'),PAYLOAD.replace(b'gAAAAA==',b'AAAAAA=='),PAYLOAD.replace(b'<real>8</real>',b'<real>nan</real>'),PAYLOAD.replace(b'50000000-0000-0000-0000-000000000005',b'50000000-0000-0000-0000-000000000099')]:self.assertEqual(post('/message',invalid)[0],400)
    self.assertEqual(server.region_events,[])
    self.assertEqual(post('/message',b'<llsd><map><key>message</key><string>ChatFromViewer</string><key>body</key><map/></map></llsd>')[0],200);self.assertEqual(server.region_events,[])
   finally:client.close();server.shutdown();worker.join(timeout=5)
 def test_reference_message_flavor_remains_llsd(self):
  root=ET.parse(ROOT/'indra/newview/app_settings/message.xml').getroot();matches=[]
  for element in root.iter('map'):
   children=list(element)
   for i,item in enumerate(children[:-1]):
    if item.tag=='key'and item.text=='LandStatReply':matches.append(children[i+1])
  self.assertTrue(matches)
  for element in matches:self.assertIn('<string>llsd</string>',ET.tostring(element,encoding='unicode'))
  self.assertIn('LandStatReply Low 422 Trusted Unencoded UDPDeprecated',(ROOT/'scripts/messages/message_template.msg').read_text())
if __name__=='__main__':unittest.main()
