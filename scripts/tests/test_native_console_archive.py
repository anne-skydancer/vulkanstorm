"""Asynchronous console qualification must survive actual evidence ZIP creation."""
from pathlib import Path
import os,sys,unittest,importlib.util,tempfile,json,hashlib,zipfile,copy
from unittest.mock import patch
from native_source_fixture import ROOT
class ConsoleArchive(unittest.TestCase):
 def module(self):
  sys.path.insert(0,str(ROOT/'scripts'));p=(ROOT/'scripts/package_vulkan_evidence.py');spec=importlib.util.spec_from_file_location('console_package',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
 def fixture(self,root):
  from run_vulkan_session_replay import REQUIRED
  prefix='.ci/ui-console-async-evidence';source='a'*40;digest='b'*64
  report={name:True for name in REQUIRED};report.update(schema=1,mode='viewer-native-session-replay',passed=True,exit_code=0,world_owners=0,live_server_qualified=False,sent=4,expired=1,source_qualified=True,source_commit=source,viewer_executable_sha256=digest,console_api='async',console_capability='http://127.0.0.1:1234/ui-sim-console-async',console_output='> native qualification\nnative console accepted\n\n> ',readback_sha256={},loaded_library_sha256={'driver.dll':'c'*64})
  files={}
  def put(name,data):
   path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data);files[name]=path
  for i in range(10):
   name='pixel-'+str(i)+'.rgba';data=bytes((i,2,3,255));put(prefix+'/'+name,data);report['readback_sha256'][name]=hashlib.sha256(data).hexdigest()
  put(prefix+'/results.json',json.dumps(report).encode());put(prefix+'/http-replay.json',json.dumps([{'path':'/seed','request':['SimConsole','SimConsoleAsync']},{'path':'/ui-sim-console-async','request':'native qualification'}]).encode());put(prefix+'/viewer.log',b'Native Vulkan normal login shut down\n');put(prefix+'/session-replay.xml',b'<testsuite failures="0" errors="0"><testcase name="actual connected session"/></testsuite>');put(prefix+'/vk_layer_settings.txt',b'khronos_validation.validate_sync = true\n');put('.ci/evidence/source.txt',source.encode());put('build/parity-build-provenance.json',json.dumps({'complete':True,'executable_sha256':digest,'source_commit':source}).encode());return files,report,source,digest
 def test_mandatory_async_files_metadata_pixels_and_zip_hashes(self):
  m=self.module();self.assertIn('.ci/ui-console-async-evidence',m.inputs('build'))
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp);files,report,source,digest=self.fixture(root);self.assertEqual(m.async_console_completeness(files,source,digest),[])
   with patch.object(m,'completeness',side_effect=lambda values,viewer:m.async_console_completeness(values,source,digest)):manifest=m.package(root,root/'upload',['.ci/ui-console-async-evidence','.ci/evidence/source.txt','build/parity-build-provenance.json'],'build',True)
   self.assertTrue(manifest['complete'])
   with zipfile.ZipFile(root/'upload/qualification-evidence.zip')as archive:self.assertEqual(m.verify_async_console_archive(archive,manifest,'build'),[]);members={name:archive.read(name)for name in archive.namelist()}
   base='.ci/ui-console-async-evidence/';name=base+'results.json'
   for phase in ['missing','corrupt','truncate','api','capability','output','path','http','seed','failure']:
    altered=dict(members);metadata=copy.deepcopy(manifest)
    if phase=='missing':altered.pop(name)
    elif phase in ('corrupt','truncate'):altered[base+'pixel-0.rgba']=b'corrupt'if phase=='corrupt'else b''
    elif phase in ('http','seed'):
     key=base+'http-replay.json';requests=json.loads(altered[key]);requests[1]['path']='/ui-sim-console'if phase=='http'else requests[1]['path'];requests[0]['request']=[]if phase=='seed'else requests[0]['request'];altered[key]=json.dumps(requests).encode()
    else:
     record=json.loads(altered[name])
     if phase=='api':record['console_api']='sync'
     elif phase=='capability':record['console_capability']='http://127.0.0.1:1234/incorrect'
     elif phase=='output':record['console_output']='native qualification\nno accepted reply'
     elif phase=='path':record['readback_sha256']['../escape.rgba']=record['readback_sha256'].pop('pixel-0.rgba')
     else:record['connected_console_http']=False
     altered[name]=json.dumps(record).encode()
    # Hash/size checks catch transport damage. Recomputed hashes then separately
    # prove the full semantic contract rejects bad caps/API/output/request data.
    path=root/(phase+'.zip')
    with zipfile.ZipFile(path,'w')as archive:
     for key,data in altered.items():archive.writestr(key,data)
    with zipfile.ZipFile(path)as archive:self.assertTrue(m.verify_async_console_archive(archive,metadata,'build'),phase)
    for key,data in altered.items():metadata['files'][key]={'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    with zipfile.ZipFile(path)as archive:self.assertTrue(m.verify_async_console_archive(archive,metadata,'build'),phase+' semantic')
   absent=dict(files);absent.pop(base+'http-replay.json');self.assertTrue(m.async_console_completeness(absent,source,digest));self.assertTrue(m.async_console_completeness(files,'wrong',digest));self.assertTrue(m.async_console_completeness(files,source,'wrong'))
if __name__=='__main__':unittest.main()
