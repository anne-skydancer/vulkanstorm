"""Execute actual shared XY/trackball drawing and input without a window or GPU.

Primitive/image ports record calls; these are controller/routing regressions, not
GPU pixel qualification (the connected Vulkan replay remains authoritative).
"""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,HERE,method
class EnvironmentWidgets(unittest.TestCase):
 def test_actual_draw_and_pointer_keyboard_paths(self):
  xy=(ROOT/'indra/llui/llxyvector.cpp').read_text();track=(ROOT/'indra/llui/llvirtualtrackball.cpp').read_text()
  fixture=(HERE/'native_environment_widgets_fixture.cpp').read_text()
  track_methods=['bool LLVirtualTrackball::pointInTouchCircle(', 'void LLVirtualTrackball::drawThumb(', 'void LLVirtualTrackball::draw(', 'bool LLVirtualTrackball::handleHover(', 'bool LLVirtualTrackball::handleMouseDown(', 'bool LLVirtualTrackball::handleMouseUp(', 'bool LLVirtualTrackball::handleKeyHere(', 'void LLVirtualTrackball::getAzimuthAndElevation(']+['void LLVirtualTrackball::onRotate'+direction+'Click('for direction in ['Top','Bottom','Left','Right']]
  xy_methods=['void LLXYVector::onEditChange(', 'void drawArrow(', 'void LLXYVector::draw(', 'bool LLXYVector::handleHover(', 'bool LLXYVector::handleMouseDown(', 'bool LLXYVector::handleMouseUp(']
  fixture=fixture.replace('TRACKBALL_METHODS','\n'.join(method(track,s)for s in track_methods)).replace('XY_METHODS','\n'.join(method(xy,s)for s in xy_methods))
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'widgets.cpp';exe=Path(tmp)/'widgets.exe';cpp.write_text(fixture)
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
