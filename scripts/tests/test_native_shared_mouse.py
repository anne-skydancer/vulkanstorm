from pathlib import Path
import sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(HERE));from native_source_fixture import method
class SharedMouse(unittest.TestCase):
 def test_actual_shared_dispatch_priority_current_tool_and_reentrant_retirement(self):
  s=(ROOT/'indra/newview/llviewerwindow.cpp').read_text();source=(HERE/'fixtures/native_shared_mouse_dispatch_fixture.cpp').read_text().replace('DISPATCH',method(s,'bool LLViewerWindow::dispatchAnyMouseClick(')).replace('NATIVE_MOUSE',method((ROOT/'indra/newview/vsviewerwindow.cpp').read_text(),'bool LLViewerWindow::nativeMouse(')).replace('ICON_HELPER',method((ROOT/'indra/newview/lltoolpie.cpp').read_text(),'bool show_script_error_icon('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_mouse_binding_state_matches_original_and_calls_registered_callbacks(self):
  s=(ROOT/'indra/newview/llviewerinput.cpp').read_text();methods='\n'.join(method(s,signature)for signature in ('bool LLViewerInput::handleMouse(','bool LLViewerInput::scanMouse(\n','bool LLViewerInput::scanMouse(EMouseClickType','void LLViewerInput::scanMouse()'))
  source=(HERE/'fixtures/native_shared_mouse_bindings_fixture.cpp').read_text().replace('METHODS',methods).replace('REFERENCE',(HERE/'fixtures/native_shared_mouse_bindings_original.cpp').read_text());compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
