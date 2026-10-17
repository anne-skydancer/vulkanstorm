"""Reject incomplete, changed and missing actual imported-image evidence."""
from pathlib import Path
import sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
import vulkan_r3_gltf_fixture as fixture
class GLTFImageEvidence(unittest.TestCase):
 def test_gpu_pixels_inputs_and_generation_evidence_are_decisive(self):
  with tempfile.TemporaryDirectory() as temp:
   root=Path(temp);fixture.write(root/'r3/gltf-images')
   data=dict(object_retired=True,immutable_generations=True,account_retired=True);generation=0
   for phase in ('initial','full-resolution','limited-again','replacement','minimized','restored'):
    width=256 if phase=='minimized' else 1024 if phase=='full-resolution' else 512;height=width//2;variant=int(phase in ('replacement','minimized','restored'))
    row=dict(material_contracts=True,width=width,height=height,variant=variant,images=[]);data[phase]=row
    if phase=='minimized':row['bias']=4
    if phase=='restored':row['restored_window']=True
    for channel,colors in enumerate(fixture.COLORS):
     pixels=bytearray()
     for y in range(height):
      half=2 if y>=height//2 else 0
      pixels+=bytes(colors[(half+variant)%4])*(width//2)+bytes(colors[(half+1+variant)%4])*(width//2)
     name=f'r3/gltf-images/{phase}-{channel}.rgba';(root/name).write_bytes(pixels);generation+=1
     row['images'].append(dict(channel=channel,file=name,generation=generation))
   record=dict(r3_gltf_images=data)
   self.assertTrue(fixture.contracts_passed(record,root))
   self.assertEqual(len(fixture.required_files(record)),32)
   image=root/'r3/gltf-images/replacement-0.rgba';original=image.read_bytes();image.write_bytes(bytes([0])+original[1:])
   self.assertFalse(fixture.contracts_passed(record,root));image.write_bytes(original)
   original_generation=data['replacement']['images'][0]['generation'];data['replacement']['images'][0]['generation']=1
   self.assertFalse(fixture.contracts_passed(record,root));data['replacement']['images'][0]['generation']=original_generation
   path=root/'r3/gltf-images/embedded.gltf';content=path.read_bytes();path.write_bytes(content+b' ')
   self.assertFalse(fixture.contracts_passed(record,root));path.write_bytes(content)
   image.unlink();self.assertFalse(fixture.contracts_passed(record,root))
if __name__=='__main__':unittest.main()
