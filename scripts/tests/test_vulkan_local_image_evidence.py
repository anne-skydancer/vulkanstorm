"""Actual local reload evidence needs original pixels and generation transitions."""
import base64
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import local_image_contracts_passed


class LocalImageEvidenceTests(unittest.TestCase):
    def test_original_pixels_refusal_and_generation_identity_are_decisive(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            values=dict(updates=2,retirements=2,stable_facade=True,retained_generation_unchanged=True,default_alias=True)
            for name,width,height,blue,generation in (('initial',32,32,0,1),('replaced',32,16,255,2),
                    ('refused',32,16,255,2),('recovered',16,32,128,3)):
                # Independent quadrant construction; file rows are top-first.
                top=(bytes((255,0,blue,255))*(width//2)+bytes((0,0,blue,255))*(width//2))*(height//2)
                bottom=(bytes((255,255,blue,255))*(width//2)+bytes((0,255,blue,255))*(width//2))*(height//2)
                (folder/(name+'.rgba')).write_bytes(top+bottom)
                values[name]=dict(id='aa000000-0000-0000-0000-000000000001',width=width,height=height,
                    phase={0:0,255:1,128:2}[blue],file=name+'.rgba',
                    generation=base64.b64encode(generation.to_bytes(8,'big')).decode())
            record=dict(r3_local_images=values)
            self.assertTrue(local_image_contracts_passed(record,folder))
            values['refused']['generation']=values['recovered']['generation']
            self.assertFalse(local_image_contracts_passed(record,folder))
            values['refused']['generation']=values['replaced']['generation']
            values['replaced']['file']='../outside.rgba'
            self.assertFalse(local_image_contracts_passed(record,folder))
            values['replaced']['file']='replaced.rgba'
            (folder/'replaced.rgba').write_bytes(bytes(32*16*4))
            self.assertFalse(local_image_contracts_passed(record,folder))


if __name__=='__main__':unittest.main()
