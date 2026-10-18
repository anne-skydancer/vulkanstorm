"""Decisive actual GPU-consumption artifacts, without claiming full shader parity."""
from pathlib import Path
import tempfile,unittest,copy,sys
sys.path.insert(0,str(Path(__file__).resolve().parents[2]/"scripts"))
from vulkan_environment_evidence import environment_pixels_passed
class EnvironmentPixels(unittest.TestCase):
 def setup(self,root):
  (root/'environment').mkdir();rows={}
  for name,color in [('day',(120,130,200,255)),('night',(3,4,15,255)),('edit',(200,30,30,255))]:
   (root/'environment'/f'{name}.rgba').write_bytes(bytes(color)*4096)
   rows[name]=dict(file=f'environment/{name}.rgba',width=64,height=64,filled_pixels=4096,sun_up=name!='night',moon_up=name=='night',ambient=[.1,.2,.3],blue_horizon=[.8,.01,.01]if name=='edit'else[.1,.2,.4])
  return {'environment_pixels':rows}
 def test_actual_archive_requirements_and_finite_current_metadata(self):
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp);report=self.setup(root);self.assertTrue(environment_pixels_passed(report,root))
   self.assertEqual(len(report['environment_pixels']['day']['sha256']),64)
   for field,value in [('file','../day.rgba'),('width',1),('filled_pixels',0),('ambient',[float('nan'),0,0]),('sun_up',False)]:
    bad=copy.deepcopy(report);bad['environment_pixels']['day'][field]=value;self.assertFalse(environment_pixels_passed(bad,root),field)
 def test_missing_identical_partial_and_nonopaque_pixels_fail(self):
  with tempfile.TemporaryDirectory()as tmp:
   root=Path(tmp);report=self.setup(root);path=root/'environment/edit.rgba';original=path.read_bytes()
   path.write_bytes((root/'environment/day.rgba').read_bytes());self.assertFalse(environment_pixels_passed(report,root))
   path.write_bytes(original[:-1]);self.assertFalse(environment_pixels_passed(report,root))
   path.write_bytes(bytes((200,30,30,0))*4096);self.assertFalse(environment_pixels_passed(report,root))
   path.unlink();self.assertFalse(environment_pixels_passed(report,root))
if __name__=='__main__':unittest.main()
