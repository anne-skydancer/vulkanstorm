from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class DefaultPieDispatch(unittest.TestCase):
 def test_actual_window_dispatch_and_genuine_script_error_identity(self):
  source=(ROOT/'indra/newview/lltoolpie.cpp').read_text();left=method(source,'bool LLToolPie::handleLeftClickPick(')
  self.assertIn('if(!object)show_script_error_icon(mPick.mHUDIcon);',left)
  fixture=(HERE/'native_default_pie_dispatch_fixture.cpp').read_text().replace('ICON_HELPER',method(source,'bool show_script_error_icon(')).replace('NATIVE_MOUSE',method((ROOT/'indra/newview/vsviewerwindow.cpp').read_text(),'bool LLViewerWindow::nativeMouse('))
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'dispatch.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++20',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
