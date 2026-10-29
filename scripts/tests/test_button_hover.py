"""Qualify real shared hover ownership without opening a viewer window."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class ButtonHoverTest(unittest.TestCase):
    def test_capture_release_popup_retirement_and_pointer_departure(self):
        root = Path(__file__).resolve().parents[2]
        source = (root/'indra/newview/llviewerwindow.cpp').read_text(encoding='utf-8')
        own = 'void LLViewerWindow::updateMouseHoverViews(S32 x, S32 y, MASK mask, bool enabled)\n'
        if own in source:
            # The Vulkan lines move the hover update out of updateUI() into
            # its own method, which the fixture then compiles as it is.
            begin = source.index(own)
            method = source[begin:source.index('\n}\n', begin) + 3]
        else:
            begin = source.index('void LLViewerWindow::updateUI()')
            source = source[begin:]
            setup = source[source.index('    LLView* captor_view ='):source.index('    static LLCachedControl<bool> dump_menu_holder')]
            block = source[source.index('    // only update mouse hover set when UI is visible'):source.index('    // only handle hover events when UI is enabled')]
            method = """void LLViewerWindow::updateMouseHoverViews(S32 x,S32 y,MASK mask,bool enabled) {
 gPipeline.enabled=enabled;
 LLUICtrl* top_ctrl=gFocusMgr.getTopCtrl();
 LLMouseHandler* mouse_captor=gFocusMgr.getMouseCapture();
""" + setup + block + '}\n'
        fixture = (root/'scripts/tests/fixtures/button_hover.cpp').read_text(encoding='utf-8').replace('PRODUCTION_HOVER',method)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler,'C++ compiler required')
        with tempfile.TemporaryDirectory() as folder:
            cpp=Path(folder)/'hover.cpp';exe=Path(folder)/'hover.exe'
            cpp.write_text(fixture,encoding='utf-8')
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True,timeout=60)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=15)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

if __name__ == '__main__':
    unittest.main()
