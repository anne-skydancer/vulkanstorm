"""Exercise the actual menu callbacks with and without the scene snapshot owner."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class MenuCallbacksTest(unittest.TestCase):
    def test_close_callbacks_preserve_native_and_snapshot_behavior(self):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        source = (ROOT / 'indra/newview/llviewermenufile.cpp').read_text()
        callbacks = source[source.index('class LLFileEnableCloseWindow :'):
                           source.index('// </FS:Ansariel>', source.index('class FSFileCloseWindowGroup :'))]
        # The production callbacks are private; expose them only in this fixture.
        callbacks = callbacks.replace(': public view_listener_t\n{', ': public view_listener_t\n{\npublic:')
        fixture = r'''
#include <cassert>
#include <vector>
struct LLSD{};
struct view_listener_t{virtual bool handleEvent(const LLSD&)=0;};
struct LLFloater {
    bool focused=false;int closes=0;
    bool hasFocus(){return focused;}
    void closeFloater(){++closes;}
    static void closeFrontmostFloater();
};
struct LLFloaterView {
    LLFloater* front=nullptr;int focused=0;
    LLFloater* getFrontmostClosableFloater(){return front;}
    void focusFrontFloater(){++focused;}
} ordinary,snapshot;
LLFloaterView* gFloaterView=&ordinary;
LLFloaterView* gSnapshotFloaterView=nullptr;
void LLFloater::closeFrontmostFloater(){if(ordinary.front)ordinary.front->closeFloater();}
struct Focus {void* getKeyboardFocus(){return nullptr;}}gFocusMgr;
struct Holder{int hides=0;void hideMenus(){++hides;}}holder;
Holder* gMenuHolder=&holder;
namespace LLNotificationsUI {struct LLToast{static bool alert;static bool isAlertToastShown(){return alert;}};bool LLToast::alert=false;}
struct LLFloaterReg{static std::vector<LLFloater*> getAllFloatersInGroup(LLFloater* f){assert(f);return {f};}};
''' + callbacks + r'''
int main(){
    LLSD event;LLFileEnableCloseWindow enabled;FSFileEnableCloseWindowGroup group_enabled;
    LLFileCloseWindow close;FSFileCloseWindowGroup group_close;LLFloater ui,shot;
    // Opening a menu evaluates enable callbacks even for hidden items.
    assert(!enabled.handleEvent(event) && !group_enabled.handleEvent(event));
    close.handleEvent(event);group_close.handleEvent(event);
    ordinary.front=&ui;
    assert(enabled.handleEvent(event) && group_enabled.handleEvent(event));
    close.handleEvent(event);group_close.handleEvent(event);assert(ui.closes==2);
    LLNotificationsUI::LLToast::alert=true;
    assert(!enabled.handleEvent(event) && !group_enabled.handleEvent(event));
    LLNotificationsUI::LLToast::alert=false;
    // The existing OpenGL snapshot owner retains its precedence and focus behavior.
    gSnapshotFloaterView=&snapshot;snapshot.front=&shot;shot.focused=true;
    close.handleEvent(event);group_close.handleEvent(event);
    assert(shot.closes==2 && ui.closes==2 && ordinary.focused==2);
    shot.focused=false;close.handleEvent(event);assert(ui.closes==3);
    ordinary.front=nullptr;group_close.handleEvent(event);assert(shot.closes==3);
    gSnapshotFloaterView=nullptr;assert(!enabled.handleEvent(event));
}
'''
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'menu_callbacks.cpp'
            executable = Path(directory) / 'menu_callbacks.exe'
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
