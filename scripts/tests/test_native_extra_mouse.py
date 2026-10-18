from pathlib import Path
import importlib.util,sys,subprocess,shutil,tempfile,unittest
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[1];sys.path.insert(0,str(ROOT/'scripts/tests'));from native_source_fixture import method
class Buttons(unittest.TestCase):
 def test_actual_extra_buttons_dispatch_once_and_pair_releases(self):
  source=(HERE/'fixtures/native_extra_mouse_fixture.cpp').read_text().replace('METHOD',method((ROOT/'indra/newview/llviewerwindow.cpp').read_text(),'bool LLViewerWindow::handleOtherMouse('));compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
 def test_actual_sdl2_native_xbuttons_are_not_scroll_and_reference_gl_delivery_is_unchanged(self):
  s=(ROOT/'indra/llwindow/llwindowsdl2.cpp').read_text();a=s.index('                if (event.button.button == SDL_BUTTON_LEFT)  // left');down=s[a:s.index('                break;',a)];a=s.index('                if (event.button.button == SDL_BUTTON_LEFT)  // left',s.index('case SDL_MOUSEBUTTONUP:'));up=s[a:s.index('                break;',a)];source=(HERE/'fixtures/native_extra_mouse_sdl_fixture.cpp').read_text().replace('DOWN',down).replace('UP',up);compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   p=Path(tmp);(p/'main.cpp').write_text(source);exe=p/'test.exe';r=subprocess.run([compiler,'-std=c++17',str(p/'main.cpp'),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr);r=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(r.returncode,0,r.stdout+r.stderr)
if __name__=='__main__':unittest.main()
