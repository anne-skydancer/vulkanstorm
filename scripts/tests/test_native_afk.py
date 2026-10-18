from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class AFK(unittest.TestCase):
 def test_original_away_sit_animation_and_lazy_legacy_policy(self):
  s=(ROOT/'indra/newview/llagent.cpp').read_text();original=(HERE/'fixtures/native_afk_original.cpp').read_text();sig=['void LLAgent::setAFK()','void LLAgent::clearAFK()'];ref='\n'.join(method(original,v)for v in sig).replace('LLAgent::setAFK()','LLAgent::setAFKReference()').replace('LLAgent::clearAFK()','LLAgent::clearAFKReference()');actual='\n'.join(method(s,v)for v in sig);source=(HERE/'fixtures/native_afk_fixture.cpp').read_text().replace('REFERENCE',ref).replace('METHOD',actual).replace('MOUSE',method((ROOT/'indra/newview/llviewerwindow.cpp').read_text(),'static void clear_away_on_mouse_move()'));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_away_checks_run_once_per_connected_frame_and_only_physical_hover_clears_timer(self):
  sources={ROOT/'indra/newview'/name:(ROOT/'indra/newview'/name).read_text() for name in ('llagent.cpp','llappviewer.cpp','llviewerwindow.cpp')};frame=method(sources[ROOT/'indra/newview/llappviewer.cpp'],'bool LLAppViewer::frameNativeVulkanLogin()');self.assertEqual(frame.count('idle_afk_check();'),1);self.assertIn('if(!gDisconnected)idle_afk_check();',frame)
  move=method(sources[ROOT/'indra/newview/llviewerwindow.cpp'],'void LLViewerWindow::handleMouseMove(');self.assertEqual(move.count('clear_away_on_mouse_move();'),2)
  hover=method((ROOT/'indra/newview/vsviewerwindow.cpp').read_text(),'void LLViewerWindow::nativeHoverUpdate(');self.assertNotIn('clear_away_on_mouse_move',hover);self.assertNotIn('clearAFK',hover);self.assertNotIn('gAwayTriggerTimer',hover)
if __name__=='__main__':unittest.main()
