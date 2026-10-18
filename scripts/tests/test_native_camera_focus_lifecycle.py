from pathlib import Path
import importlib.util,shutil,subprocess,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
def production_source():
 return (ROOT/'indra/newview/llagentcamera.cpp').read_text()

class CameraFocusLifecycle(unittest.TestCase):
 def test_actual_focus_transition_survives_native_mode_completion(self):
  body=method(production_source(),'void LLAgentCamera::setFocusOnAvatar(')
  body=body[:body.index('#endif')+len('#endif')]+'\n}'
  text='#define VS_NATIVE_VULKAN 1\n'+(HERE/'camera_focus_lifecycle_fixture.cpp').read_text().replace('METHOD',body.replace('LLUUID::null','0'))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as temp:
   cpp=Path(temp)/'focus.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
