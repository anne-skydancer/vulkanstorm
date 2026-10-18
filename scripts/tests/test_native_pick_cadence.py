from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class Queue(unittest.TestCase):
 def test_actual_stable_batch_reentry_retirement_deleted_window_and_once_frame_delta_hover(self):
  s=(ROOT/'indra/newview/llviewerwindow.cpp').read_text();source=(HERE/'fixtures/native_pick_cadence_fixture.cpp').read_text()
  for key,name in [('SCHEDULE','schedulePick'),('PERFORM','performPick'),('EMPTY','returnEmptyPicks')]:source=source.replace(key,method(s,'void LLViewerWindow::'+name+'('))
  fetch=method(s,'void LLPickInfo::fetchResults(');fetch=method(fetch,'if (gViewerWindow && gViewerWindow->isNativeVulkan())');source=source.replace('FETCH',fetch)
  ui=method(s,'void LLViewerWindow::updateUI(');source=source.replace('UI}',method(ui,'if (mNativeVulkan)')+'}')
  keyStart=s.index('    LLTool* keyboardTool = mNativeVulkan ?');keyEnd=s.index('    if (keyboardTool && keyboardTool->handleKey(',keyStart);source=source.replace('KEYBOARD',s[keyStart:keyEnd])
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17','-DVS_NATIVE_VULKAN=1',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
  physical=method((ROOT/'indra/newview/vsviewerwindow.cpp').read_text(),'void LLViewerWindow::nativeHover(');self.assertNotIn('nativeHoverUpdate(',physical)
if __name__=='__main__':unittest.main()
