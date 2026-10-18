from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class NativeTooltips(unittest.TestCase):
 def compile_fixture(self,name,relative,signature):
  source=(ROOT/relative).read_text();fixture=(HERE/name).read_text().replace('METHOD',method(source,signature))
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'tooltip.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17','-DVS_NATIVE_VULKAN=1',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_shared_captor_top_root_tool_and_sticky_debug_dispatch(self):
  self.compile_fixture('native_tooltip_dispatch_fixture.cpp','indra/newview/llviewerwindow.cpp','void LLViewerWindow::updateToolTip(')
 def test_account_root_retirement_releases_visible_and_deferred_callbacks(self):
  self.compile_fixture('native_tooltip_lifecycle_fixture.cpp','indra/llui/lltooltip.cpp','void LLToolTipMgr::resetToolTips(')
if __name__=='__main__':unittest.main()
