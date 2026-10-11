"""Exercise the actual SDL native-window leave branch without SDL or hardware."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class NativePointerLeaveTests(unittest.TestCase):
    def test_sdl_leave_retires_native_hover_without_changing_focus(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llwindow/llwindowsdl2.cpp').read_text()
        branch = re.search(
            r'else if \(!mUseGL && event\.window\.event == SDL_WINDOWEVENT_LEAVE\)\s*\{[^{}]*\}',
            source)
        self.assertIsNotNone(branch, 'SDL native pointer departure must reach the viewer')
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture = r'''
#include <cassert>
enum { SDL_WINDOWEVENT_LEAVE = 1, SDL_WINDOWEVENT_FOCUS_LOST = 2 };
struct Callbacks {
    int leaves = 0;
    void* owner = nullptr;
    void handleMouseLeave(void* value) { ++leaves; owner = value; }
};
struct Window {
    bool mUseGL = false, mHaveInputFocus = true;
    Callbacks callbacks;
    Callbacks* mCallbacks = &callbacks;
    void dispatch(int kind) {
        struct { struct { int event; } window; } event{{kind}};
        if (false) {}
        BRANCH
    }
};
int main() {
    Window native;
    native.dispatch(SDL_WINDOWEVENT_FOCUS_LOST);
    assert(native.callbacks.leaves == 0);
    native.dispatch(SDL_WINDOWEVENT_LEAVE);
    assert(native.callbacks.leaves == 1 && native.callbacks.owner == &native);
    assert(native.mHaveInputFocus); // Pointer exit is not keyboard focus loss.
    Window legacy; legacy.mUseGL = true;
    legacy.dispatch(SDL_WINDOWEVENT_LEAVE);
    assert(legacy.callbacks.leaves == 0 && legacy.mHaveInputFocus);
}
'''.replace('BRANCH', branch[0])
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'pointer_leave.cpp'
            executable = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
