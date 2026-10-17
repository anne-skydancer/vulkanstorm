"""Actual local reload evidence needs original pixels and generation transitions."""
import base64
import copy
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from run_vulkan_session_replay import local_image_contracts_passed, local_material_contracts_passed


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


    def test_material_requires_all_channel_pixels_and_atomic_reload(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder=Path(temporary)
            data=dict(stable_identities=True,retired=True,retained_generations_unchanged=True,native_only=True)
            for stage,phase,generation in (('initial',0,1),('replaced',1,2),('refused',1,2),('recovered',1,3)):
                rows=[]
                colors=((73,121,199,255) if not phase else (199,73,121,255),
                        (128,128,255,255),(77,64,192,255),(9,19,29,255))
                for channel,color in enumerate(colors):
                    name=f'{stage}-{channel}.rgba';(folder/name).write_bytes(bytes(color)*256)
                    rows.append(dict(id=f'bb000000-0000-0000-0000-{channel+1:012d}',file=name,
                        generation=base64.b64encode(generation.to_bytes(8,'big')).decode()))
                data[stage]=rows
            record=dict(r3_local_material=data)
            self.assertTrue(local_material_contracts_passed(record,folder))
            bad=copy.deepcopy(record);bad['r3_local_material']['refused'][2]['generation']=data['recovered'][2]['generation']
            self.assertFalse(local_material_contracts_passed(bad,folder))
            (folder/'replaced-2.rgba').write_bytes(bytes((11,64,192,255))*256)
            self.assertFalse(local_material_contracts_passed(record,folder))
            self.assertFalse(local_material_contracts_passed({},folder))


if __name__=='__main__':unittest.main()
