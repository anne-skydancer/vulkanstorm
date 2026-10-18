from pathlib import Path
import subprocess,shutil,tempfile,unittest,importlib.util,sys,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent/'fixtures/native_particles';sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Frustum(unittest.TestCase):
 def test_actual_reference_all_projection_modes_and_native_no_gl(self):
  s=(ROOT/'indra/newview/llviewercamera.cpp').read_text();gold=(HERE/'frustum_reference.cpp').read_text().replace('void LLViewerCamera::updateFrustumPlanes(','void referenceFrustum(');kernel=method(s,'void updateCameraFrustum(');native=method(s,'void LLViewerCamera::updateNativeFrustum(');source=(HERE/'frustum_fixture.cpp').read_text().replace('GOLD',gold).replace('KERNEL',kernel).replace('NATIVE',native);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler);paths=[ROOT/os.environ['VS_TEST_BUILD_DIRECTORY']/'packages/include']if os.environ.get('VS_TEST_BUILD_DIRECTORY')else[ROOT/'build-vc170-64/packages/include',ROOT/'build-linux-x86_64/packages/include'];glm=next((p for p in paths if(p/'glm/glm.hpp').exists()),None);self.assertIsNotNone(glm)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1','-I'+str(glm),str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
