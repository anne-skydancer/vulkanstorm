"""Execute native mode completion and the unchanged shared keyboard mode selector."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2]
class NativeCameraMode(unittest.TestCase):
    def test_actual_transition_and_keyboard_mode(self):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture=(Path(__file__).with_name('native_camera_mode_fixture.cpp')).read_text(encoding='utf-8')
        fixture=fixture.replace('CAMERA_METHOD',method((ROOT/'indra/newview/llagentcamera.cpp').read_text(encoding='utf-8'),'bool LLAgentCamera::completeNativeCameraMode('))
        fixture=fixture.replace('INPUT_METHOD',method((ROOT/'indra/newview/llviewerinput.cpp').read_text(encoding='utf-8'),'EKeyboardMode LLViewerInput::getMode() const'))
        with tempfile.TemporaryDirectory() as temporary:
            cpp=Path(temporary)/'camera.cpp';exe=Path(temporary)/'camera.exe'
            cpp.write_text(fixture,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
