"""Exercise the production scan dispatcher and callback ownership without a window."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

def method(source, signature):
    start = source.index(signature)
    end = source.index('{', start)
    depth = 1
    end += 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class NativeKeyboardDispatchTest(unittest.TestCase):
    def test_actual_scan_preserves_callback_across_repeat_mask_mode_and_release(self):
        source = (ROOT / 'indra/newview/llviewerinput.cpp').read_text()
        scan = method(source, 'bool LLViewerInput::scanKey(KEY key, bool key_down, bool key_up, bool key_level) const')
        reset = method(source, 'void LLViewerInput::resetNativeKeys() const')
        constants = (ROOT / 'indra/llcommon/indra_constants.h').read_text()
        constants = '\n'.join(line for line in constants.splitlines() if line.startswith(('constexpr U32 CONTROL_', 'constexpr U32 AGENT_CONTROL_')))
        fixture = r'''
#include <cassert>
#include <map>
#include <vector>
#include <string>
#include <functional>
#define VS_NATIVE_VULKAN 1
using KEY=unsigned;using MASK=unsigned;using S32=int;using U32=unsigned;
CONSTANTS
struct Agent{U32 flags=0;void clearControlFlags(U32 mask){flags&=~mask;}bool getTempRun(){return false;}void clearTempRun(){}}gAgent;
struct Camera{void clearGeneralKeys(){}void clearOrbitKeys(){}void clearPanKeys(){}}gAgentCamera;
enum EKeystate{KEYSTATE_DOWN,KEYSTATE_UP,KEYSTATE_LEVEL};
struct LLKeyboardBinding{KEY mKey;MASK mMask;std::function<bool(EKeystate)>mFunction;std::string mFunctionName;};
struct App{static bool isExiting(){return false;}};using LLApp=App;
struct Owner{bool ready=true;bool regionReady(){return ready;}}owner;
struct VSNativeSession{static Owner*active(){return &owner;}};
struct Window{bool isNativeVulkan(){return true;}}window,*gViewerWindow=&window;
struct LLFocusableElement{};
struct Focus{LLFocusableElement*focus=nullptr;LLFocusableElement*getKeyboardFocus(){return focus;}bool focusLocked(){return false;}}gFocusMgr;
struct LLMenuGL{static bool getKeyboardMode(){return false;}};
struct Keyboard{MASK mask=0;bool repeat=false;MASK currentMask(bool){return mask;}bool getKeyRepeated(KEY){return repeat;}}keyboard,*gKeyboard=&keyboard;
struct LLViewerInput{
 int mode=0;int getMode()const{return mode;}
 std::map<U32,U32>mRemapKeys[2];std::vector<LLKeyboardBinding>mKeyBindings[2];
 mutable std::map<KEY,LLKeyboardBinding>mNativeHeldBindings;mutable LLFocusableElement*mNativeKeyFocus=nullptr;bool mKeyHandledByUI[256]{};
 void resetNativeKeys()const;
 bool scanKey(KEY,bool,bool,bool)const;
 bool scanKey(const std::vector<LLKeyboardBinding>&,S32,KEY,MASK,bool,bool,bool,bool)const{return false;}
};
RESET
SCAN
int main(){
 LLViewerInput input;std::vector<EKeystate>events,other;
 auto original=[&](EKeystate s){events.push_back(s);return true;};
 auto replaced=[&](EKeystate s){other.push_back(s);return true;};
 input.mKeyBindings[0]={{'W',0,original,"push_forward"},{'/',0,original,"start_gesture"}};
 input.mKeyBindings[1]={{'W',0,replaced,"move_forward_sitting"}};
 assert(input.scanKey('W',true,false,true));
 keyboard.repeat=true;assert(input.scanKey('W',true,false,true));
 keyboard.repeat=false;keyboard.mask=3;input.mode=1;
 assert(input.scanKey('W',false,true,false));
 assert((events==std::vector<EKeystate>{KEYSTATE_DOWN,KEYSTATE_LEVEL,KEYSTATE_UP}));assert(other.empty());
 events.clear();input.mode=0;keyboard.mask=0;
 // Press and release within one frame must still invoke both edges exactly once.
 assert(input.scanKey('W',true,true,false));assert(events.size()==2&&events[0]==KEYSTATE_DOWN&&events[1]==KEYSTATE_UP);
 assert(input.mNativeHeldBindings.empty());events.clear();
 // Unrelated UI-owned shortcut cannot cancel an independently held key.
 input.scanKey('W',true,false,true);input.mKeyHandledByUI['X']=true;
 assert(!input.scanKey('X',true,false,true));input.scanKey('W',false,false,true);
 assert(events.back()==KEYSTATE_LEVEL);input.scanKey('W',false,true,false);events.clear();
 // Modal/editor ownership cancels only this key and releases temporary state.
 input.scanKey('W',true,false,true);input.mKeyHandledByUI['W']=true;
 assert(!input.scanKey('W',false,false,true));assert(events.back()==KEYSTATE_UP);
 assert(input.mNativeHeldBindings.empty());input.mKeyHandledByUI['W']=false;events.clear();
 // Focus takeover must not open a gesture bar as a cancellation side effect.
 input.scanKey('/',true,false,true);input.mKeyHandledByUI['/']=true;
 input.scanKey('/',false,false,true);assert(events.size()==1);assert(input.mNativeHeldBindings.empty());
 // Normal release still executes release-only actions.
 input.mKeyHandledByUI['/']=false;events.clear();input.scanKey('/',true,false,true);input.scanKey('/',false,true,false);
 assert(events.size()==2&&events.back()==KEYSTATE_UP);events.clear();
 // Keyboard/asynchronous focus takeover cancels even without a W repeat.
 events.clear();input.scanKey('W',true,false,true);LLFocusableElement editor;gFocusMgr.focus=&editor;
 assert(!input.scanKey('W',false,false,true));assert(input.mNativeHeldBindings.empty());assert(events.size()==1);
 gFocusMgr.focus=nullptr;events.clear();
 // Bindings use the same ordered subset-mask semantics and remapping as UI.
 input.mKeyBindings[0].insert(input.mKeyBindings[0].begin(), {'W',2,replaced,"pan_in"});
 keyboard.mask=3;other.clear();input.scanKey('W',true,false,true);input.scanKey('W',false,true,false);
 assert(other.size()==2);other.clear();keyboard.mask=4;
 input.mRemapKeys[0][(4<<16)|'Z']=(2<<16)|'W';
 input.scanKey('Z',true,false,true);keyboard.mask=0;input.scanKey('Z',false,true,false);
 assert(other.size()==2);events.clear();
 // Authenticated owner retirement discards held actions, without replaying them.
 input.scanKey('W',true,false,true);owner.ready=false;assert(!input.scanKey('W',false,false,true));
 assert(input.mNativeHeldBindings.empty());assert(events.size()==1);
 gAgent.flags=AGENT_CONTROL_AT_POS|AGENT_CONTROL_FAST_AT|AGENT_CONTROL_YAW_POS|AGENT_CONTROL_FLY|AGENT_CONTROL_AWAY|AGENT_CONTROL_STAND_UP|AGENT_CONTROL_LBUTTON_UP;
 input.resetNativeKeys();
 assert(gAgent.flags==(AGENT_CONTROL_FLY|AGENT_CONTROL_AWAY|AGENT_CONTROL_STAND_UP|AGENT_CONTROL_LBUTTON_UP));
}
'''.replace('CONSTANTS', constants).replace('RESET', reset).replace('SCAN', scan)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'native_keyboard.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_global_push_to_talk_owns_original_callback_until_release(self):
        source = (ROOT / 'indra/newview/llviewerinput.cpp').read_text()
        methods = '\n'.join(method(source, signature) for signature in (
            'bool LLViewerInput::handleGlobalBindsKeyDown(',
            'bool LLViewerInput::handleGlobalBindsKeyUp(',
            'void LLViewerInput::resetNativeGlobalKeys() const'))
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
struct LLKeyboardBinding{KEY mKey;MASK mMask;std::function<bool(EKeystate)>mFunction;std::string mFunctionName;};
struct LLSetKeyBindDialog{static bool recording;static bool isRecording(){return recording;}};bool LLSetKeyBindDialog::recording=false;
struct Window{bool isNativeVulkan(){return true;}}window,*gViewerWindow=&window;
struct LLViewerInput{int mode=0;int getMode(){return mode;}std::vector<LLKeyboardBinding>mGlobalKeyBindings[2];
 mutable std::map<KEY,LLKeyboardBinding>mNativeHeldGlobalBindings;
 bool handleGlobalBindsKeyDown(KEY,MASK);bool handleGlobalBindsKeyUp(KEY,MASK);void resetNativeGlobalKeys()const;
 bool scanKey(const std::vector<LLKeyboardBinding>&,S32,KEY,MASK,bool,bool,bool,bool){return false;}
};
METHODS
int main(){
 LLViewerInput input;std::vector<EKeystate>original,replacement;
 input.mGlobalKeyBindings[0]={{'V',1,[&](EKeystate s){original.push_back(s);return true;},"voice_follow_key"}};
 input.mGlobalKeyBindings[1]={{'V',0,[&](EKeystate s){replacement.push_back(s);return true;},"toggle_voice"}};
 assert(input.handleGlobalBindsKeyDown('V',1));assert(input.handleGlobalBindsKeyDown('V',1));assert(original.size()==1);
 input.mode=1;LLSetKeyBindDialog::recording=true;
 assert(input.handleGlobalBindsKeyUp('V',0));assert(original.size()==2&&original.back()==KEYSTATE_UP);assert(replacement.empty());
 assert(!input.handleGlobalBindsKeyDown('V',0));LLSetKeyBindDialog::recording=false;
 input.mode=0;assert(input.handleGlobalBindsKeyDown('V',1));input.resetNativeGlobalKeys();
 assert(original.size()==4&&original.back()==KEYSTATE_UP);assert(input.mNativeHeldGlobalBindings.empty());
 input.resetNativeGlobalKeys();assert(original.size()==4);
}
'''.replace('METHODS', methods)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'native_global_keyboard.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

if __name__ == '__main__':
    unittest.main()
