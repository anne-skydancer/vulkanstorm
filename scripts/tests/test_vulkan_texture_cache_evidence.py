"""Reject incomplete cache evidence and exercise real loopback range responses."""
import hashlib
import http.client
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_texture_cache_tests as cache
from run_vulkan_session_replay import SimulatorHTTP


class TextureCacheEvidenceTests(unittest.TestCase):
    def test_shutdown_fetch_remains_pending_until_the_viewer_finishes(self):
        with tempfile.TemporaryDirectory() as temporary,SimulatorHTTP() as server:
            folder=Path(temporary);server.r3_directory=folder
            server.stream_texture=folder/'texture.j2c'
            worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
            errors=[]
            def fetch():
                client=http.client.HTTPConnection(*server.server_address,timeout=4)
                try:
                    client.request('GET','/asset?texture_id=a3000000-0000-0000-0000-000000000003')
                    client.getresponse()
                    errors.append('Pending HTTP unexpectedly supplied a response')
                except (http.client.RemoteDisconnected,ConnectionResetError):
                    pass
                except Exception as error:
                    errors.append(str(error))
                finally:
                    client.close()
            reader=threading.Thread(target=fetch,daemon=True);reader.start()
            try:
                marker=folder/'pending-cache-read.requested';deadline=time.monotonic()+3
                while not marker.exists() and time.monotonic()<deadline:time.sleep(.01)
                self.assertTrue(marker.is_file())
                self.assertTrue(reader.is_alive())
                self.assertEqual(len(server.requests),1)
                self.assertTrue(server.requests[0]['shutdown_pending'])
            finally:
                server.replay_finished.set();reader.join(3)
                server.shutdown();worker.join()
            self.assertFalse(reader.is_alive());self.assertEqual(errors,[])

    def test_pending_shutdown_requires_real_http_and_completed_retired_write(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);(folder/'r3').mkdir()
            shutdown=dict(http_pending=True,write_queued=True,ordinary_cleanup=True,additional_http_requests=1,
                          write_id='a3000000-0000-0000-0000-000000000002',
                          read_id='a3000000-0000-0000-0000-000000000003',
                          write_completion='r3/pending-cache-write.xml',read_observation='r3/pending-cache-read.requested')
            record={'r3_shutdown':shutdown}
            requests=[dict(shutdown_pending=True,query={'texture_id':[shutdown['read_id']]},bytes=0)]
            observed=folder/shutdown['read_observation'];observed.write_text('HTTP request observed; pending\n')
            complete=folder/shutdown['write_completion']
            complete.write_text('<llsd><map><key>completed</key><boolean>true</boolean>'
                '<key>success</key><boolean>true</boolean><key>writer_retired</key><boolean>true</boolean>'
                '<key>id</key><uuid>'+shutdown['write_id']+'</uuid></map></llsd>')
            self.assertEqual(cache.validate_shutdown(record,folder,requests),1)
            with self.assertRaisesRegex(ValueError,'actual HTTP'):cache.validate_shutdown(record,folder,[])
            original=complete.read_text();complete.write_text(original.replace('<key>writer_retired</key><boolean>true',
                                                                            '<key>writer_retired</key><boolean>false'))
            with self.assertRaisesRegex(ValueError,'retire'):cache.validate_shutdown(record,folder,requests)
            complete.unlink()
            with self.assertRaisesRegex(ValueError,'Missing'):cache.validate_shutdown(record,folder,requests)

    def test_pressure_requires_real_generations_retirement_and_full_source_pixels(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            full=bytearray()
            for y in range(1023,-1,-1):
                for x in range(1024):
                    full.extend((255*((x//32)&1),255*((y//32)&1),255*(((x//32)+(y//32))&1),255))
            def image(name,width,discard,data,bias):
                (folder/name).write_bytes(data)
                return dict(id=cache.STREAM,width=width,height=width,discard=discard,gpu_matches_cpu=True,
                            gpu_rgba_file=name,gpu_rgba_sha256=hashlib.sha256(data).hexdigest(),bias=bias)
            pressure=dict(complete=True,object_retired=True)
            for name,bias in (('initial',1),('full_resolution_preference',4),('restored',1)):
                pressure[name]=image(name+'.rgba',1024,0,full,bias)
            pressure['pressure']=image('low.rgba',128,3,bytes((0,255,0,255))*128*128,4)
            pressure['minimized']=image('mini.rgba',256,2,bytes((0,255,0,255))*256*256,4)
            pressure['minimized']['previous_scene_images_retired']=True
            references={};(folder/'r3').mkdir()
            for name in ('pressure','minimized'):
                row=pressure[name];key='pressure-discard-'+str(row['discard'])
                relative='r3/reference-'+key+'.rgba'
                (folder/relative).write_bytes((folder/row['gpu_rgba_file']).read_bytes());references[key]=relative
            record={'r3_pressure':pressure,'r3_texture_streaming':dict(references=references)}
            cache.validate_pressure(record,folder)
            pressure['minimized']['id']='a different image'
            with self.assertRaisesRegex(ValueError,'identity'):cache.validate_pressure(record,folder)
            pressure['minimized']['id']=cache.STREAM
            reference=folder/references['pressure-discard-2'];original=reference.read_bytes()
            # Change a nonzero byte so the independent codec comparison is decisive.
            reference.write_bytes(original[:1]+b'\x00'+original[2:])
            with self.assertRaisesRegex(ValueError,'checksum'):cache.validate_pressure(record,folder)
            reference.write_bytes(original)
            pressure['minimized']['previous_scene_images_retired']=False
            with self.assertRaisesRegex(ValueError,'retained'):cache.validate_pressure(record,folder)
            pressure['minimized']['previous_scene_images_retired']=True
            pressure['full_resolution_preference']['bias']=1
            with self.assertRaisesRegex(ValueError,'controller'):cache.validate_pressure(record,folder)
            pressure['full_resolution_preference']['bias']=4
            (folder/'restored.rgba').write_bytes(full[:-1])
            with self.assertRaisesRegex(ValueError,'Truncated'):cache.validate_pressure(record,folder)

    def test_gpu_artifacts_require_matching_bytes_and_confined_paths(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)/'evidence';folder.mkdir()
            pixels=bytes((12,34,56,255))*4
            artifact=folder/'image.rgba';artifact.write_bytes(pixels)
            row=dict(width=2,height=2,gpu_matches_cpu=True,gpu_rgba_file='image.rgba',
                     gpu_rgba_sha256=hashlib.sha256(pixels).hexdigest())
            cache.validate_image(row,folder)
            for changed in (dict(gpu_matches_cpu=False),dict(width=3),
                            dict(gpu_rgba_sha256='0'*64),dict(gpu_rgba_file='../outside.rgba'),
                            dict(gpu_rgba_file='missing.rgba')):
                with self.subTest(changed=changed),self.assertRaises(ValueError):
                    cache.validate_image(dict(row,**changed),folder)
            artifact.write_bytes(pixels[:-1])
            with self.assertRaisesRegex(ValueError,'Truncated'):
                cache.validate_image(row,folder)

    def test_retirement_keeps_distinct_snapshots_and_all_http_counts(self):
        import copy
        snapshot=dict(images=[],pending_fetches=0,pending_uploads=0,process_instance='same',network_requests=2)
        record=dict(texture_cache=dict(process_instance='same'),retired_texture_cache=snapshot,
                    final_texture_cache=dict(snapshot,network_requests=3))
        requests=[dict(query=dict(texture_id=['identity'])) for _ in range(4)]
        cache.validate_retirement(record,requests,1)
        for field in ('retired_texture_cache','final_texture_cache'):
            for key,value in (('images',[dict(id='still-published')]),('pending_fetches',1),
                              ('pending_uploads',1),('process_instance','different')):
                changed=copy.deepcopy(record);changed[field][key]=value
                with self.subTest(field=field,key=key),self.assertRaises(ValueError):
                    cache.validate_retirement(changed,requests,1)
        for count in (1,4):
            changed=copy.deepcopy(record);changed['final_texture_cache']['network_requests']=count
            with self.assertRaises(ValueError):cache.validate_retirement(changed,requests,1)

    def test_cache_reuse_requires_actual_account_and_grid_transition(self):
        import copy
        cold=dict(texture_cache=dict(grid_identity='util.agni.lindenlab.com',
            account_identity='10000000-0000-0000-0000-000000000001'))
        warm=dict(texture_cache=dict(grid_identity='util.aditi.lindenlab.com',
            account_identity='10000000-0000-0000-0000-000000000002'))
        cache.validate_identity_transition(cold,warm)
        for key in ('grid_identity','account_identity'):
            for invalid in (None,cold['texture_cache'][key]):
                changed=copy.deepcopy(warm);changed['texture_cache'][key]=invalid
                with self.subTest(key=key,invalid=invalid),self.assertRaises(ValueError):
                    cache.validate_identity_transition(cold,changed)

    def test_terrain_fetch_enters_the_same_http_counter_receipts(self):
        with tempfile.TemporaryDirectory() as temporary,SimulatorHTTP() as server:
            fixture=Path(temporary)/'ground.j2c';fixture.write_bytes(b'actual ground input')
            server.terrain_fixture=fixture
            worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
            try:
                client=http.client.HTTPConnection(*server.server_address)
                identity='c3800000-0000-0000-0000-000000000001'
                client.request('GET','/asset?texture_id='+identity)
                response=client.getresponse();self.assertEqual(response.read(),fixture.read_bytes());client.close()
                rows=cache.texture_requests(server.requests)
                self.assertEqual(len(rows),1)
                self.assertEqual(rows[0]['query'],dict(texture_id=[identity]))
                self.assertEqual(rows[0]['method'],'GET');self.assertIs(rows[0]['terrain'],True)
            finally:
                server.shutdown();worker.join()

    def test_loopback_texture_ranges_are_real_partial_content(self):
        with tempfile.TemporaryDirectory() as temporary,SimulatorHTTP() as server:
            fixture=Path(temporary)/'texture.j2c';fixture.write_bytes(bytes(range(256))*5)
            server.stream_texture=fixture
            worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
            try:
                client=http.client.HTTPConnection(*server.server_address)
                for header,status,expected in [('bytes=0-599',206,fixture.read_bytes()[:600]),
                        ('bytes=600-',206,fixture.read_bytes()[600:]),
                        ('bytes=1280-',416,b''),('bytes=800-700',416,b''),('junk',400,None)]:
                    client.request('GET','/asset?texture_id='+cache.STREAM,headers={'Range':header})
                    response=client.getresponse();data=response.read()
                    self.assertEqual(response.status,status)
                    if expected is not None:self.assertEqual(data,expected)
                    if status==206:self.assertIn('/1280',response.getheader('Content-Range'))
                client.close()
                self.assertEqual([row['range_start'] for row in server.requests],[0,600])
            finally:
                server.shutdown();worker.join()

    def test_cache_header_corruption_is_limited_to_the_selected_entry(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary);root=folder/'texturecache';root.mkdir()
            (root/'texture.entries').write_bytes(bytes(44)+bytes(28)+cache.UUID(cache.STREAM).bytes+bytes(12))
            original=b'first!'+bytes(594)+b'\xff\x4f'+bytes(598)
            (root/'texture.cache').write_bytes(original)
            changed=cache.mutate_entry(folder,'corrupt-header')
            self.assertEqual(changed['offset'],600)
            self.assertEqual((root/'texture.cache').read_bytes(),original[:600]+b'\0\0'+original[602:])


if __name__=='__main__':unittest.main()
