"""Execute real global mouse bindings and their keyboard/focus release owner."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_source_fixture import ROOT, method


class GlobalMouseTest(unittest.TestCase):
    def test_actual_dispatch_and_release_ownership(self):
        source = (ROOT / 'indra/newview/llviewerinput.cpp').read_text(encoding='utf-8')
        methods = '\n'.join(method(source, signature) for signature in (
            'bool LLViewerInput::handleGlobalBindsMouse(',
            'void LLViewerInput::resetNativeGlobalKeys() const',
            'bool LLViewerInput::scanMouse(\n'))
        window = (ROOT / 'indra/newview/vsviewerwindow.cpp').read_text(encoding='utf-8')
        mouse = method(window, 'bool LLViewerWindow::nativeMouse(')
        prefix = next(line for line in mouse.splitlines() if 'gViewerInput.handleGlobalBindsMouse(' in line) + '\n' + next(line for line in mouse.splitlines() if 'if(!hadRoot)return false;' in line).replace('hadRoot','mRootView')
        fixture = r'''
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>
#define VS_NATIVE_VULKAN 1
using KEY=unsigned;using MASK=unsigned;using S32=int;
enum EKeystate{KEYSTATE_DOWN,KEYSTATE_UP,KEYSTATE_LEVEL};
enum EMouseState{MOUSE_STATE_DOWN,MOUSE_STATE_CLICK,MOUSE_STATE_LEVEL,MOUSE_STATE_UP};
enum EMouseClickType{CLICK_LEFT,CLICK_MIDDLE,CLICK_RIGHT};
struct LLMouseBinding{EMouseClickType mMouse;MASK mMask;std::function<bool(EKeystate)>mFunction;std::string mFunctionName;};
struct LLKeyboardBinding{std::function<bool(EKeystate)>mFunction;};
struct LLSetKeyBindDialog{static bool recording;static bool isRecording(){return recording;}};bool LLSetKeyBindDialog::recording=false;
struct Window{bool native=true;bool isNativeVulkan(){return native;}}window,*gViewerWindow=&window;
struct LLViewerInput{
 int mode=0;int getMode(){return mode;}
 std::vector<LLMouseBinding>mGlobalMouseBindings[2];
 mutable std::map<EMouseClickType,LLMouseBinding>mNativeHeldGlobalMouseBindings;
 mutable std::map<KEY,LLKeyboardBinding>mNativeHeldGlobalBindings;
 bool handleGlobalBindsMouse(EMouseClickType,MASK,bool);
 void resetNativeGlobalKeys()const;
 bool scanMouse(const std::vector<LLMouseBinding>&,S32,EMouseClickType,MASK,EMouseState,bool)const;
};
METHODS
LLViewerInput gViewerInput;
bool pointerPrefix(bool mRootView,EMouseClickType type,MASK mask,bool down){PREFIX return true;}
int main(){
 auto& input=gViewerInput;std::vector<EKeystate>original,replacement;
 input.mGlobalMouseBindings[0]={{CLICK_MIDDLE,1,[&](EKeystate s){original.push_back(s);return true;},"voice_follow_key"}};
 input.mGlobalMouseBindings[1]={{CLICK_MIDDLE,0,[&](EKeystate s){replacement.push_back(s);return true;},"toggle_voice"}};
 // Global side effects run before UI ownership, even if the root is absent.
 assert(!pointerPrefix(false,CLICK_MIDDLE,3,true));assert(original.size()==1);
 assert(pointerPrefix(true,CLICK_MIDDLE,3,true));assert(original.size()==1);
 // Release belongs to the original callback after mode/modifier/recording changes.
 input.mode=1;LLSetKeyBindDialog::recording=true;
 assert(pointerPrefix(true,CLICK_MIDDLE,0,false));assert((original==std::vector<EKeystate>{KEYSTATE_DOWN,KEYSTATE_UP}));assert(replacement.empty());
 assert(!input.handleGlobalBindsMouse(CLICK_MIDDLE,0,true));
 LLSetKeyBindDialog::recording=false;input.mode=0;original.clear();
 // Focus loss releases keyboard and mouse exactly once and preserves reentrant new owners.
 input.handleGlobalBindsMouse(CLICK_MIDDLE,1,true);
 unsigned reentrantReleases=0;
 input.mNativeHeldGlobalBindings[1]={[&](EKeystate s){assert(s==KEYSTATE_UP);assert(input.mNativeHeldGlobalMouseBindings.empty());
  input.mNativeHeldGlobalMouseBindings[CLICK_RIGHT]={CLICK_RIGHT,0,[&](EKeystate edge){assert(edge==KEYSTATE_UP);++reentrantReleases;return true;},"voice_follow_key"};return true;}};
 input.resetNativeGlobalKeys();assert(original.size()==2&&original.back()==KEYSTATE_UP);
 assert(reentrantReleases==0&&input.mNativeHeldGlobalMouseBindings.count(CLICK_RIGHT));
 input.resetNativeGlobalKeys();assert(original.size()==2&&reentrantReleases==1);
 assert(!input.handleGlobalBindsMouse(CLICK_MIDDLE,1,false));
 // A false-returning binding still owns a press/release without consuming UI.
 input.mGlobalMouseBindings[0][0].mFunction=[&](EKeystate s){original.push_back(s);return false;};
 assert(pointerPrefix(true,CLICK_MIDDLE,1,true));assert(pointerPrefix(true,CLICK_MIDDLE,0,false));
 assert(original.size()==4&&original.back()==KEYSTATE_UP);
 // Existing GL subset-mask and release selection behavior is unchanged.
 window.native=false;input.mGlobalMouseBindings[0][0].mFunction=[&](EKeystate s){original.push_back(s);return true;};
 assert(input.handleGlobalBindsMouse(CLICK_MIDDLE,3,true));input.mode=1;
 assert(input.handleGlobalBindsMouse(CLICK_MIDDLE,0,false));assert(replacement.size()==1&&replacement.back()==KEYSTATE_UP);
 LLSetKeyBindDialog::recording=true;assert(!input.handleGlobalBindsMouse(CLICK_MIDDLE,0,true));
}
'''.replace('METHODS', methods).replace('PREFIX', prefix)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'global_mouse.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
