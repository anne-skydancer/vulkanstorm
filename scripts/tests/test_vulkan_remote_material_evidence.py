"""Remote material qualification needs actual failure/recovery HTTP bytes and archived inputs."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import append_llsd,remote_material_contracts_passed


class RemoteMaterialEvidenceTests(unittest.TestCase):
    def test_parallel_http_hashes_remain_on_their_own_receipts(self):
        import threading,http.client,concurrent.futures
        from unittest.mock import patch
        from run_vulkan_session_replay import SimulatorHTTP
        with tempfile.TemporaryDirectory() as temporary,SimulatorHTTP() as server:
            server.r3_directory=Path(temporary);folder=server.r3_directory/'remote-materials';folder.mkdir()
            small='b3100000-0000-0000-0000-000000000002';large='b3100000-0000-0000-0000-000000000003'
            bodies={small:b'invalid material',large:b' '*(1024*1024+1)}
            for identity,body in bodies.items():(folder/(identity+'.asset')).write_bytes(body)
            original_hash=hashlib.sha256;entered=threading.Event();release=threading.Event()
            def held_hash(payload,*args,**kwargs):
                if len(payload)>1024*1024:
                    entered.set()
                    if not release.wait(5):raise RuntimeError('Parallel receipt fixture did not release its hash')
                return original_hash(payload,*args,**kwargs)
            def get(identity):
                client=http.client.HTTPConnection(*server.server_address,timeout=5)
                try:
                    client.request('GET','/asset?material_id='+identity)
                    response=client.getresponse();return response.status,response.read()
                finally:client.close()
            worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
            try:
                with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool,patch('run_vulkan_session_replay.hashlib.sha256',side_effect=held_hash):
                    first=pool.submit(get,large)
                    self.assertTrue(entered.wait(3))
                    second=pool.submit(get,small)
                    self.assertEqual(second.result(timeout=3),(200,bodies[small]));release.set()
                    self.assertEqual(first.result(timeout=3),(200,bodies[large]))
                rows={row['query']['material_id'][0]:row for row in server.requests}
                self.assertEqual(set(rows),set(bodies))
                for identity,body in bodies.items():
                    self.assertEqual(rows[identity]['sha256'],original_hash(body).hexdigest())
                    self.assertEqual(rows[identity]['bytes'],len(body))
            finally:
                release.set();server.shutdown();worker.join()

    def test_failure_and_recovery_cannot_be_replaced_with_success_flags(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);folder=root/'r3/remote-materials';folder.mkdir(parents=True)
            result=dict(actual_object_faces=6,replacement_faces=6,object_retired=True,native_only=True,
                        shared_parser_factors=True,account_retired=True,asset_recovered=True,assets=[])
            requests=[];bad=None
            for index in range(1,5):
                identity=f'b3100000-0000-0000-0000-{index:012d}'
                relative=f'r3/remote-materials/{identity}.asset'
                if index==3:payload=b' '*(1024*1024+1)
                else:
                    data='invalid native material fixture'
                    if index!=2:
                        data=json.dumps(dict(materials=[dict(doubleSided=True,alphaMode='MASK',pbrMetallicRoughness=dict(
                            baseColorFactor=[.5 if index==4 else .25,.5,.75,.625],metallicFactor=.3,roughnessFactor=.6))]))
                    document=ET.Element('llsd');append_llsd(document,dict(data=data));payload=ET.tostring(document)
                (root/relative).write_bytes(payload)
                result['assets'].append(dict(id=identity,loaded=index in (1,4),file=relative))
                if index==2:bad=payload
                for body in ([bad,payload] if index==4 else [payload]):
                    requests.append(dict(query=dict(material_id=[identity]),bytes=len(body),sha256=hashlib.sha256(body).hexdigest()))
            report=dict(r3_remote_materials=result)
            self.assertTrue(remote_material_contracts_passed(report,requests,root))
            for key in ('native_only','account_retired','asset_recovered','shared_parser_factors'):
                failed=copy.deepcopy(report);failed['r3_remote_materials'][key]=False
                self.assertFalse(remote_material_contracts_passed(failed,requests,root))
            failed=copy.deepcopy(report);failed['r3_remote_materials']['replacement_faces']=0
            self.assertFalse(remote_material_contracts_passed(failed,requests,root))
            self.assertFalse(remote_material_contracts_passed(report,requests[:-2]+requests[-1:],root))
            changed=copy.deepcopy(requests);changed[-2]['sha256']='0'*64
            self.assertFalse(remote_material_contracts_passed(report,changed,root))
            (root/result['assets'][-1]['file']).unlink()
            self.assertFalse(remote_material_contracts_passed(report,requests,root))


if __name__=='__main__':unittest.main()
