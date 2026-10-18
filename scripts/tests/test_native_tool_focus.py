from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import method
ROOT=Path(__file__).resolve().parents[2];HERE=Path(__file__).resolve().parent
class NativeToolFocus(unittest.TestCase):
 def test_actual_focus_and_inspect_alt_release_use_registered_owner(self):
  source=(ROOT/'indra/newview/llviewerwindow.cpp').read_text();manager=(ROOT/'indra/newview/lltoolmgr.cpp').read_text()
  methods='\n'.join(method(source,sig)for sig in('void LLViewerWindow::handleFocus(LLWindow *window)','void LLViewerWindow::handleFocusLost(LLWindow *window)','bool LLViewerWindow::handleTranslatedKeyUp('))+'\n'+'\n'.join(method(manager,sig)for sig in('void LLToolMgr::onAppFocusLost(', 'void LLToolMgr::onAppFocusGained('))
  fixture=(HERE/'native_tool_focus_fixture.cpp').read_text().replace('METHODS',methods)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'focus.cpp';exe=cpp.with_suffix('.exe');cpp.write_text(fixture)
   r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
   r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
