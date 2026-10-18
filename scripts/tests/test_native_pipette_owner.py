"""Actual Pipette tool and shared picker lifetimes under native account ownership."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativePipetteOwner(unittest.TestCase):
 def test_real_tool_initiating_picker_retired_intent_and_capture_ownership(self):
  source=(ROOT/'indra/newview/lltoolpipette.cpp').read_text();color=(ROOT/'indra/newview/llfloatercolorpicker.cpp').read_text()
  code=(Path(__file__).parent/'fixtures/native_face_edit/pipette_owner.cpp').read_text()
  for key,sig in [('ACTIVE','bool LLToolPipette::nativeActive('),('CONSUMER','bool LLToolPipette::nativeConsumer('),('SET','void LLToolPipette::setNativeActive('),('DESELECT','void LLToolPipette::handleDeselect('),('LOST','void LLToolPipette::onMouseCaptureLost(')]:code=code.replace('\n'+key+'\n','\n'+method(source,sig)+'\n')
  code=code.replace('\nCOLOR\n','\n'+method(color,'void LLFloaterColorPicker::onColorSelect(')+'\n').replace('\nSTOP\n','\n'+method(color,'void LLFloaterColorPicker::stopUsingPipette(')+'\n')
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as directory:
   cpp=Path(directory)/'pipette.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(code)
   p=subprocess.run([compiler,'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
   p=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(p.returncode,0,p.stdout+p.stderr)
if __name__=='__main__':unittest.main()
