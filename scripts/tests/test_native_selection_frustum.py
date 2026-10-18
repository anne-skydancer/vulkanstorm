from pathlib import Path
import shutil,subprocess,sys,tempfile,unittest,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE));from native_source_fixture import method
class SelectionFrustum(unittest.TestCase):
 def test_actual_projection_and_independent_frustum_match_original(self):
  camera=(ROOT/'indra/newview/llviewercamera.cpp').read_text();text=(HERE/'fixtures/native_selection_frustum.cpp').read_text().replace('ORIGINAL',(HERE/'fixtures/native_selection_projection_original.cpp').read_text()).replace('FRUSTUM',method(camera,'void updateCameraFrustum(')).replace('METHOD',method(camera,'std::optional<LLCamera> LLViewerCamera::selectionFrustum('))
  paths=[ROOT/os.environ['VS_TEST_BUILD_DIRECTORY']/'packages/include']if os.environ.get('VS_TEST_BUILD_DIRECTORY')else[ROOT/'build-vc170-64/packages/include',ROOT/'build-linux-x86_64/packages/include'];glm=next((p for p in paths if(p/'glm/glm.hpp').exists()),None);self.assertIsNotNone(glm,'Autobuild installed GLM required')
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory(dir=HERE)as tmp:
   p=Path(tmp);cpp=p/'main.cpp';exe=p/'main.exe';cpp.write_text(text);r=subprocess.run([compiler,'-std=c++17','-DGLM_ENABLE_EXPERIMENTAL','-I',str(glm),'-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
