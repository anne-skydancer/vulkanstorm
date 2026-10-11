"""Compile real emoji prediction, picker commit and account-retirement code."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_event_notifier_account import function


class EmojiConnectedControllerTest(unittest.TestCase):
    def test_prediction_replaces_shortcode_picker_commits_and_retirement_clears_old_editor(self):
        root = Path(__file__).resolve().parents[2]
        helper = (root / 'indra/llui/llemojihelper.cpp').read_text()
        editor = (root / 'indra/llui/lltexteditor.cpp').read_text()
        picker = (root / 'indra/newview/llfloateremojipicker.cpp').read_text()
        methods = '\n'.join(function(source, signature) for source, signature in (
            (helper, 'bool LLEmojiHelper::isCursorInEmojiCode('),
            (helper, 'void LLEmojiHelper::onCommitEmoji('),
            (helper, 'void LLEmojiHelper::resetAccount()'),
            (editor, 'void LLTextEditor::handleEmojiCommit('),
            (picker, 'void LLFloaterEmojiPicker::onEmojiMouseUp('),
            (picker, 'bool LLFloaterEmojiPicker::isNativeInternalType('),
            (picker, 'void LLFloaterEmojiPicker::resetAccount()'),
        ))
        fixture = r'''
#include <cassert>
#include <cctype>
#include <codecvt>
#include <functional>
#include <list>
#include <locale>
#include <string>
#include <typeinfo>
using llwchar=char32_t;using LLWString=std::u32string;using S32=int;using U32=unsigned;
struct LLSD {std::string text;LLSD(std::string t):text(t){}};
std::string wstring_to_utf8str(const LLWString& w){return std::wstring_convert<std::codecvt_utf8<char32_t>,char32_t>().to_bytes(w);}
bool popup=true;template<class T>struct LLUICachedControl {LLUICachedControl(const char*,T){}operator T()const{return popup;}};
struct LLStringOps {static bool isAlnum(llwchar c){return c<128&&std::isalnum(static_cast<unsigned char>(c));}};
struct LLUICtrl {virtual ~LLUICtrl()=default;};
template<class T>struct Handle {T* pointer=nullptr;bool isDead(){return !pointer;}T* get(){return pointer;}void markDead(){pointer=nullptr;}};
struct Connection {bool connected=true;void disconnect(){connected=false;}};
struct LLEmojiHelper {Handle<LLUICtrl>mHostHandle,mHelperHandle;Connection mHostCtrlFocusLostConn,mHelperCommitConn,mHelperCloseConn;
 std::function<void(llwchar)>mEmojiCommitCb;bool mIsHideDisabled=true;int hidden=0;
 void hideHelper(void*,bool){++hidden;}void resetAccount();void onCommitEmoji(llwchar);
 static bool isCursorInEmojiCode(const LLWString&,S32,S32*);};
struct LLTextEditor {LLWString text;int mCursorPos=0;const LLWString&getWText(){return text;}
 void remove(int start,int count,bool){text.erase(start,count);}void setCursorPos(int n){mCursorPos=n;}
 void insertEmoji(llwchar c){text.insert(mCursorPos,1,c);++mCursorPos;}void handleEmojiCommit(llwchar);};
struct LLEmojiGridRow:LLUICtrl{};struct LLEmojiGridDivider:LLUICtrl{};struct LLEmojiPreviewPanel:LLUICtrl{};
struct LLScrollingPanelList:LLUICtrl{};struct LLEmojiGridIcon:LLUICtrl {LLWString value;const LLWString&getChar(){return value;}};
constexpr int MOUSE_UP=2,MASK_SHIFT=1;struct Keyboard {int mask=0;int currentMask(bool){return mask;}}keyboard;Keyboard*gKeyboard=&keyboard;
void make_ui_sound(const char*){}
std::string sStateFileName="account-A.xml",savedFile;std::list<llwchar>sRecentlyUsed={0x1f603},savedRecent;
std::list<std::pair<llwchar,U32>>sFrequentlyUsed={{0x1f603,4}};
struct LLFloaterEmojiPicker {std::string mHint;LLSD value{std::string{}};bool hidden=false;std::function<void(const LLSD&)>commit;
 int getSoundFlags(){return 0;}void setValue(LLSD v){value=v;}void onCommit(){commit(value);}void hideFloater(){hidden=true;}
 void onEmojiMouseUp(LLUICtrl*);static bool isNativeInternalType(const std::type_info&);static void resetAccount();
 static void saveState(){savedFile=sStateFileName;savedRecent=sRecentlyUsed;}};
METHODS
int main(){
 int shortcode=-1;assert(LLEmojiHelper::isCursorInEmojiCode(U"hello :smi",10,&shortcode)&&shortcode==6);
 assert(!LLEmojiHelper::isCursorInEmojiCode(U"12:30",5,&shortcode));
 assert(!LLEmojiHelper::isCursorInEmojiCode(U"",0,&shortcode));
 LLTextEditor editor;editor.text=U"hello :smi";editor.mCursorPos=10;
 LLEmojiHelper helper;LLUICtrl host;helper.mHostHandle.pointer=&host;
 helper.mEmojiCommitCb=[&](llwchar c){editor.handleEmojiCommit(c);};
 LLFloaterEmojiPicker picker;picker.mHint=":smi";LLEmojiGridIcon icon;icon.value=LLWString(1,0x1f603);
 picker.commit=[&](const LLSD& value){assert(value.text=="\xf0\x9f\x98\x83");helper.onCommitEmoji(0x1f603);};
 picker.onEmojiMouseUp(&icon);assert(editor.text==U"hello \U0001f603"&&editor.mCursorPos==7&&picker.hidden);
 assert(LLFloaterEmojiPicker::isNativeInternalType(typeid(LLEmojiGridIcon)));
 assert(LLFloaterEmojiPicker::isNativeInternalType(typeid(LLScrollingPanelList)));
 assert(!LLFloaterEmojiPicker::isNativeInternalType(typeid(LLUICtrl)));
 helper.resetAccount();helper.onCommitEmoji(0x1f603);assert(editor.text==U"hello \U0001f603");
 assert(helper.mHostHandle.isDead()&&helper.mHelperHandle.isDead()&&!helper.mEmojiCommitCb&&!helper.mIsHideDisabled);
 assert(!helper.mHostCtrlFocusLostConn.connected&&!helper.mHelperCommitConn.connected&&!helper.mHelperCloseConn.connected);
 LLFloaterEmojiPicker::resetAccount();assert(savedFile=="account-A.xml"&&savedRecent.size()==1);
 assert(sStateFileName.empty()&&sRecentlyUsed.empty()&&sFrequentlyUsed.empty());
 popup=false;assert(!LLEmojiHelper::isCursorInEmojiCode(U":smi",4,&shortcode));
}
'''.replace('METHODS', methods)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'emoji_controller.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'emoji_controller.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
