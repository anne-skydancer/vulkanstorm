"""Frozen GL CPU algorithms versus actual peer-shared camera controller."""
from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method

def production_source():
 return (ROOT/'indra/newview/llagentcamera.cpp').read_text()

class SharedCameraController(unittest.TestCase):
 def run_fixture(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   cpp=Path(temp)/'camera.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_frozen_original_targets_and_extent_policy(self):
  source=production_source()
  current=method(source,'LLVector3d LLAgentCamera::applyCameraTargets(').replace('LLAgentCamera::','Camera::')
  current_min=method(source,'template<class Focus>\nbool LLAgentCamera::sharedCameraMinDistance(').replace('LLAgentCamera::','Camera::')
  text=(HERE/'camera_controller_fixture.cpp').read_text().replace('REFERENCE_TARGETS',(HERE/'camera_targets_reference.cpp').read_text()).replace('CURRENT_TARGETS',current).replace('REFERENCE_MIN',(HERE/'camera_min_reference.cpp').read_text()).replace('CURRENT_MIN',current_min)
  self.run_fixture(text)
 def test_actual_focus_owner_identity_account_parent_and_read_publication(self):
  source=production_source();begin=source.index('struct LLAgentCamera::NativeFocusState');end=source.index('\n};',begin)+3
  methods='\n'.join(method(source,sig)for sig in('void LLAgentCamera::ensureNativeFocusState(', 'void LLAgentCamera::bindNativeFocusObject(', 'const VSCameraFocusObject* LLAgentCamera::nativeFocusObject(', 'bool LLAgentCamera::nativeCameraPose('))
  text=(HERE/'camera_focus_owner_fixture.cpp').read_text().replace('STATE',source[begin:end]).replace('METHODS',methods)
  self.run_fixture(text)
if __name__=='__main__':unittest.main()
