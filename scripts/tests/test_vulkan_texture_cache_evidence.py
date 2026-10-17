"""Reject incomplete cache evidence and exercise real loopback range responses."""
import hashlib
import http.client
from pathlib import Path
import sys
import tempfile
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import run_vulkan_texture_cache_tests as cache
from run_vulkan_session_replay import SimulatorHTTP


class TextureCacheEvidenceTests(unittest.TestCase):
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
