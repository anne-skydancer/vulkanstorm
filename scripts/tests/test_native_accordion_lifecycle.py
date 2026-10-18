"""Exercise deferred accordion layout through the actual native UI pump."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import method

ROOT = Path(__file__).resolve().parents[2]


class NativeAccordionLifecycle(unittest.TestCase):
    def test_pending_arrange_drains_before_layout_focus_and_hover(self):
        source = (ROOT / 'indra/newview/llviewerwindow.cpp').read_text()
        pump = method(source, 'void LLViewerWindow::updateUI()')
        # Compile the real native branch, with a witness for entering GL work.
        pump = pump[:pump.index('    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;')] + '    ++legacyCalls;\n}'
        accordion = (ROOT / 'indra/llui/llaccordionctrl.cpp').read_text()
        methods = '\n'.join(method(accordion, name) for name in (
            'void LLAccordionCtrl::scheduleArrange()',
            'void LLAccordionCtrl::updateClass()'))
        fixture = r'''
#include <cassert>
#include <set>
#include <vector>
std::vector<int> events;
struct LLAccordionCtrl {
    static inline std::set<LLAccordionCtrl*> sPendingArrange;
    bool mArrangePending=false;
    int arranged=0;
    void scheduleArrange();
    static void updateClass();
    void arrange(){assert(!mArrangePending);++arranged;events.push_back(1);}
};
struct LLLayoutStack {
    static void updateClass(){assert(LLAccordionCtrl::sPendingArrange.empty());events.push_back(2);}
};
constexpr int MASK_NONE=0;
struct Keyboard {int currentMask(bool){return 7;}};
Keyboard* gKeyboard=nullptr;
struct LLViewerWindow {
    bool mNativeVulkan=true;
    int legacyCalls=0;
    void updateUI();
    void updateKeyboardFocus(){events.push_back(3);}
    void nativeHoverUpdate(int mask){assert(mask==(gKeyboard?7:MASK_NONE));events.push_back(4);}
};
METHODS
PUMP
int main(){
    LLAccordionCtrl first,second;
    first.scheduleArrange();first.scheduleArrange();second.scheduleArrange();
    assert(LLAccordionCtrl::sPendingArrange.size()==2);
    LLViewerWindow window;window.updateUI();
    assert(first.arranged==1 && second.arranged==1 && !window.legacyCalls);
    assert((events==std::vector<int>{1,1,2,3,4}));
    events.clear();Keyboard keyboard;gKeyboard=&keyboard;
    first.scheduleArrange();window.updateUI();
    assert(first.arranged==2 && second.arranged==1);
    assert((events==std::vector<int>{1,2,3,4}));
    events.clear();window.updateUI();
    assert((events==std::vector<int>{2,3,4}));
    window.mNativeVulkan=false;window.updateUI();assert(window.legacyCalls==1);
}
'''.replace('METHODS', methods).replace('PUMP', pump)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'C++ compiler is required')
        with tempfile.TemporaryDirectory() as temp:
            cpp = Path(temp) / 'accordion.cpp'
            exe = Path(temp) / 'accordion.exe'
            cpp.write_text(fixture)
            build = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
