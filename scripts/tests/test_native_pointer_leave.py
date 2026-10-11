"""Exercise actual platform pointer-exit dispatch without hardware."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class NativePointerLeaveTests(unittest.TestCase):
    def test_win32_tracks_native_window_and_posts_leave_to_owner(self):
        root = Path(__file__).resolve().parents[2]
        source = (root / 'indra/llwindow/llwindowwin32.cpp').read_text()

        def branch(name):
            start = source.index('case ' + name + ':')
            opening = source.index('{', start)
            end, depth = opening + 1, 1
            while depth:
                depth += (source[end] == '{') - (source[end] == '}')
                end += 1
            return source[start:end]

        fixture = r'''
#include <cassert>
#include <functional>
#include <vector>
using MASK=int; using HWND=void*;
enum { WM_MOUSEMOVE=1, WM_MOUSELEAVE=2, TME_LEAVE=4 };
#define LL_PROFILE_ZONE_NAMED_CATEGORY_WIN32(name)
struct TRACKMOUSEEVENT {
 unsigned cbSize=0, dwFlags=0; HWND hwndTrack=nullptr; unsigned dwHoverTime=0;
};
std::vector<TRACKMOUSEEVENT> trackingCalls;
bool TrackMouseEvent(TRACKMOUSEEVENT* tracking) { trackingCalls.push_back(*tracking); return true; }
struct Keyboard { int currentMask(bool value) { assert(value); return 7; } } keyboard;
Keyboard* gKeyboard=&keyboard;
struct Window;
struct Callbacks {
 int leaves=0,moves=0; Window* owner=nullptr;
 void handleMouseLeave(Window* window) { ++leaves; owner=window; }
 void handleMouseMove(Window* window,int,int) { ++moves; owner=window; }
};
struct Window {
 bool mUseGL=false; int mMouseMask=0,mCursorPosition=0,mLastCursorPosition=0;
 Callbacks callbacks; Callbacks* mCallbacks=&callbacks;
 std::vector<std::function<void()>> queue;
 void drain() {
  auto pending=std::move(queue);queue.clear();for(auto& callback:pending)callback();
  // Match gatherInput: queued callbacks precede the coalesced motion delivery.
  if(mLastCursorPosition!=mCursorPosition) mCallbacks->handleMouseMove(this,mCursorPosition,mMouseMask);
  mLastCursorPosition=mCursorPosition;
 }
 int dispatch(int message,HWND h_wnd,int window_coord=37) {
  auto* window_imp=this;
  #define WINDOW_IMP_POST(expression) queue.emplace_back([=] { expression; })
  switch(message) {
   MOVE_BRANCH
   LEAVE_BRANCH
  }
  return -1;
 }
};
int main() {
 Window native; int hwndOwner=0; HWND hwnd=&hwndOwner;
 assert(native.dispatch(WM_MOUSEMOVE,hwnd)==0);
 assert(trackingCalls.size()==1);
 assert(trackingCalls[0].cbSize==sizeof(TRACKMOUSEEVENT));
 assert(trackingCalls[0].dwFlags==TME_LEAVE && trackingCalls[0].hwndTrack==hwnd);
 assert(trackingCalls[0].dwHoverTime==0);
 assert(native.queue.size()==1 && native.mMouseMask==0 && native.mCursorPosition==0);
 assert(native.dispatch(WM_MOUSELEAVE,hwnd)==0);
 assert(native.callbacks.leaves==0 && native.queue.size()==2); // no window-thread UI invocation
 native.drain();
 assert(native.mMouseMask==7 && native.mCursorPosition==37);
 assert(native.callbacks.leaves==1 && native.callbacks.owner==&native);
 assert(native.callbacks.moves==0 && native.mLastCursorPosition==37);
 // A new move after Leave must still be coalesced and delivered on reentry.
 native.dispatch(WM_MOUSELEAVE,hwnd);
 native.dispatch(WM_MOUSEMOVE,hwnd,38);native.drain();
 assert(native.callbacks.leaves==2 && native.callbacks.moves==1 && native.mCursorPosition==38);
 // Tracking rearms on a later native entry/movement, retaining the correct HWND.
 native.dispatch(WM_MOUSEMOVE,hwnd); assert(trackingCalls.size()==3);native.drain();
 Window legacy; legacy.mUseGL=true;
 legacy.dispatch(WM_MOUSEMOVE,hwnd);
 assert(trackingCalls.size()==3 && legacy.queue.size()==1);
 legacy.dispatch(WM_MOUSELEAVE,hwnd);
 assert(legacy.queue.size()==1 && legacy.callbacks.leaves==0);
 legacy.drain();
 assert(legacy.mMouseMask==7 && legacy.mCursorPosition==37 && legacy.callbacks.leaves==0);
 assert(legacy.callbacks.moves==1);
}
'''.replace('MOVE_BRANCH', branch('WM_MOUSEMOVE')).replace('LEAVE_BRANCH', branch('WM_MOUSELEAVE'))
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'win32_pointer_leave.cpp'
            executable = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(executable)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

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
