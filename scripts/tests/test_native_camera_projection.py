from pathlib import Path
import shutil,subprocess,tempfile,unittest,os
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
def method(source,signature):
 begin=source.index(signature);start=source.index('{',begin);n=0
 for end in range(start,len(source)):
  if source[end]=='{':n+=1
  elif source[end]=='}':
   n-=1
   if not n:return source[begin:end+1]
 raise AssertionError(signature)
class CameraProjection(unittest.TestCase):
 def test_actual_shared_projection_against_native_projection(self):
  compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
  paths=[ROOT/os.environ['VS_TEST_BUILD_DIRECTORY']/'packages/include'] if os.environ.get('VS_TEST_BUILD_DIRECTORY') else [ROOT/'build-vc170-64/packages/include',ROOT/'build-linux-x86_64/packages/include']
  glm=next((path for path in paths if (path/'glm/glm.hpp').exists()),None);self.assertIsNotNone(glm,'Autobuild-installed viewer GLM headers required')
  read=lambda name:(ROOT/'indra'/name).read_text(encoding='utf-8-sig')
  camera=read('newview/llviewercamera.cpp');proposed=camera;math=read('llmath/m4math.cpp')
  matrix='\n'.join(method(math,'void LLMatrix4::'+name+'(') for name in ['setFwdCol','setLeftCol','setUpCol'])+'\n'+method(read('llmath/m4math.h'),'inline const LLMatrix4& operator*=(LLMatrix4 &a, const LLMatrix4 &b)')
  methods=method(read('llmath/llcoordframe.cpp'),'void LLCoordFrame::getMatrixToLocal(').replace('LLCoordFrame::','LLViewerCamera::')+'\n'+'\n'.join(method(camera,s) for s in ['const LLMatrix4 &LLViewerCamera::getModelview()','const LLMatrix4 &LLViewerCamera::getProjection()','void LLViewerCamera::calcProjection('])
  methods=methods.replace('const F32 far_distance','const F32 far_distance')
  render=read('llrender/llrender.h');a=render.index('const F32 OGL_TO_CFR_ROTATION');b=render.index(';',a)+1;constants=render[a:b]
  projection='\n'.join(method(camera,signature) for signature in ['glm::mat4 uiCameraModelview(', 'glm::mat4 uiCameraProjection('])+'\n'+'\n'.join(method(proposed,s) for s in ['void LLViewerCamera::projectScreenToPosAgent(', 'bool LLViewerCamera::projectPosAgentToScreen(', 'bool LLViewerCamera::projectPosAgentToScreenEdge('])
  fixture=(HERE/'native_camera_projection_fixture.cpp').read_text(encoding='utf-8-sig')
  for key,value in {'MATRIX_METHODS':matrix,'CAMERA_METHODS':methods,'CONSTANTS':constants,'NATIVE_VIEW':method(read('newview/vsworldscene.cpp'),'VSWorldRenderer::View VSWorldScene::cameraView('),'PROJECTION_METHODS':projection}.items():fixture=fixture.replace(key,value)
  with tempfile.TemporaryDirectory() as tmp:
   cpp=Path(tmp)/'projection.cpp';exe=Path(tmp)/'projection.exe';cpp.write_text(fixture)
   result=subprocess.run([compiler,'-std=c++17','-I',str(glm),str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
