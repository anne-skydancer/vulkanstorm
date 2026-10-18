"""Actual native RLVa overlay image publication, mask and account lifetime."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
FIXTURES=Path(__file__).resolve().parent/'fixtures/native_overlay'
def viewer(name):return(ROOT/'indra/newview'/name).read_text(encoding='utf-8-sig')
class NativeOverlay(unittest.TestCase):
 def test_actual_renderer_overlay_hud_transition(self):
  source=method(viewer('vsworldrenderer.cpp'),'void VSWorldRenderer::draw(')
  text=(FIXTURES/'composition_fixture.cpp').read_text()
  start=source.index('    bool                        drawingHud = false;');end=source.index('    for (const auto& p:submitted.packets)',start)
  text=text.replace('SEAM',source[start:end]).replace('EMPTY',method(source,'if (packets.empty())'))
  text=text.replace('FIRST',method(source,'if (!drawingHud)')).replace('FINISH','composeBeforeHud();')
  self.run_cpp(text)
 def run_cpp(self,text):
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'overlay.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(text)
   result=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
   result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=20);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
 def test_actual_overlay_drawn_generation_and_account_retirement(self):
  source=viewer('rlveffects.cpp');text=(FIXTURES/'overlay_fixture.cpp').read_text()
  policy='\n'.join(line for line in viewer('vspickpolicy.h').splitlines()if not line.startswith('#'))
  text=text.replace('POLICY',policy).replace('CLEAR',method(source,'void RlvOverlayEffect::clearImage()')).replace('SET',method(source,'void RlvOverlayEffect::setImage('))
  for label,signature in [('HIT','bool RlvOverlayEffect::hitTest('),('RUN','void RlvOverlayEffect::run(')]:
   body=method(source,signature);text=text.replace(label,method(body,'if(LLRender2D::isNativeUI())'))
  self.run_cpp(text)
if __name__=='__main__':unittest.main()
