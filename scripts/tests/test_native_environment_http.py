"""Exercise the actual HTTP handler's EEP routes and typed LLSD bodies."""
from pathlib import Path
import sys,tempfile,threading,unittest,http.client,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'scripts'))
import vulkan_ui_environment_fixture as service
import run_vulkan_session_replay as module

class RequestError(Exception):
 def __init__(self,code):self.code=code

class EEPHTTP(unittest.TestCase):
 def test_actual_seed_typed_get_put_and_reset_require_accepted_protocol(self):
  with tempfile.TemporaryDirectory() as tmp,module.SimulatorHTTP() as server:
   server.environment_fixture=Path(tmp)/'day.llsd'
   server.environment_fixture.write_text('<llsd><map><key>type</key><string>daycycle</string><key>asset_id</key><undef /><key>frames</key><map><key>sky:0</key><map><key>asset_id</key><undef /><key>sun_id</key><uuid>30000000-0000-0000-0000-000000000003</uuid></map></map></map></llsd>')
   service.configure(server,module.read_llsd)
   worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
   connection=http.client.HTTPConnection(*server.server_address,timeout=5)
   try:
    def request(path,method='GET',body=None):
     data=None
     if body is not None:
      root=ET.Element('llsd');module.append_llsd(root,body);data=ET.tostring(root,encoding='utf-8')
     connection.request(method,path,body=data,headers={'Content-Type':'application/llsd+xml'})
     response=connection.getresponse();raw=response.read()
     if response.status>=400:raise RequestError(response.status)
     return service.decode(ET.fromstring(raw)),raw
    seed,_=request('/seed','POST',['EventQueueGet','GetDisplayNames','ChatSessionRequest','GetTexture','AvatarPickerSearch','RegionExperiences','ExtEnvironment'])
    self.assertEqual(seed['ExtEnvironment'],server.url+service.ROUTE)
    experiences,_=request('/region-experiences')
    self.assertEqual(experiences,dict(allowed=[],blocked=[],trusted=[]))
    self.assertEqual(seed['RegionExperiences'],server.url+'/region-experiences')
    self.assertEqual(seed['RemoteParcelRequest'],server.url+'/remote-parcel')
    parcel,_=request('/remote-parcel','POST',dict(region_id='20000000-0000-0000-0000-000000000002',location=[6,6,0]))
    self.assertIsInstance(parcel['parcel_id'],service.uuid.UUID)
    repeated,_=request('/remote-parcel','POST',dict(region_id='20000000-0000-0000-0000-000000000002',location=[128,128,0]))
    self.assertEqual(parcel,repeated)
    initial,wire=request(service.ROUTE+'?parcelid=31')
    self.assertIn(b'<uuid>',wire);self.assertIsInstance(initial['environment']['day_cycle']['frames']['sky:0']['sun_id'],service.uuid.UUID)
    self.assertEqual(initial['environment']['day_length'],14400)
    self.assertIsNone(initial['environment']['day_cycle']['frames']['sky:0']['asset_id'])
    self.assertIn(b'<undef',wire)
    response,wire=request(service.ROUTE+'?parcelid=31','PUT',{'environment':{'day_length':19800,'flags':0}})
    self.assertTrue(response['success']);self.assertEqual(response['environment']['day_length'],21600)
    self.assertFalse(response['environment']['is_default']);self.assertEqual(response['environment']['env_version'],1)
    accepted,_=request(service.ROUTE+'?parcelid=31');self.assertEqual(accepted['environment']['day_length'],21600)
    self.assertTrue(service.exchange_observed(server.requests))
    with self.assertRaises(RequestError) as error:request(service.ROUTE+'?parcelid=32')
    self.assertEqual(error.exception.code,400)
    with self.assertRaises(RequestError) as error:request(service.ROUTE+'?parcelid=31','PUT',{'environment':{'invented_field':1}})
    self.assertEqual(error.exception.code,400)
    reset,_=request(service.ROUTE+'?parcelid=31','DELETE')
    self.assertTrue(reset['environment']['is_default']);self.assertEqual(reset['environment']['day_length'],14400)
    self.assertEqual(reset['environment']['env_version'],2)

   finally:
    connection.close();server.shutdown();worker.join(timeout=5);self.assertFalse(worker.is_alive())
if __name__=='__main__':unittest.main()
