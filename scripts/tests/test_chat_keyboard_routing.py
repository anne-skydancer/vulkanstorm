"""Compile actual chat key handlers; shared window dispatch is qualified by viewer replay; no graphics/audio device."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ChatKeyboardRoutingTest(unittest.TestCase):
    def test_successful_backspace_is_consumed_once_and_failed_delete_sounds_once(self):
        root = Path(__file__).resolve().parents[2]
        chat = (root / 'indra/llui/llchatentry.cpp').read_text()
        body = chat.split('bool LLChatEntry::handleSpecialKey(const KEY key, const MASK mask)', 1)[1].split('void LLChatEntry::enableSingleLineMode', 1)[0]
        editor = (root / 'indra/llui/lltexteditor.cpp').read_text().split('bool LLTextEditor::handleSpecialKey', 1)[1]
        backspace = editor.split('    case KEY_BACKSPACE:', 1)[1].split('    case KEY_DELETE:', 1)[0]
        window = (root / 'indra/newview/llviewerwindow.cpp').read_text()
        # The shared dispatcher owns focused controls on both backends now.
        route = 'return gFocusMgr.entry->handleKey(key,mask,false);'
        self.assertIn('return gViewerInput.handleKey(key, mask, repeated);', window)
        self.assertNotIn('if (handled) { owner->releaseMovement(); return true; }', window)
        fixture = r"""#include <cassert>
#include <vector>
#include <string>
using KEY=int;using MASK=int;
constexpr KEY KEY_BACKSPACE=8,KEY_RETURN=13,KEY_UP=20,KEY_DOWN=21;
constexpr MASK MASK_NONE=0,MASK_CONTROL=1;
struct LLUI {int bad=0;static LLUI* getInstance(){static LLUI ui;return &ui;}void reportBadKeystroke(){++bad;}};
struct LLTextEditor {
 bool mReadOnly=false,selected=false;int mCursorPos=0,removed=0,words=0;
 std::string text;
 bool hasSelection(){return selected;}void deleteSelection(bool){text.clear();selected=false;mCursorPos=0;++removed;}
 void removeCharOrTab(){text.erase(--mCursorPos,1);++removed;}
 void removeWord(bool){text.erase(0,mCursorPos);mCursorPos=0;++words;}
 bool handleSpecialKey(KEY key,MASK mask){if(mReadOnly)return false;bool handled=true;
 switch(key){case KEY_BACKSPACE: EDITOR_BODY default:handled=key==KEY_RETURN;break;}return handled;}
};
struct LLChatEntry:LLTextEditor {
 bool mHasHistory=false;std::vector<std::string> mLineHistory;std::vector<std::string>::iterator mCurrentHistoryLine;
 std::string mCurrentInput;int reflow=0;
 void needsReflow(){++reflow;}void endOfDoc(){}std::string getText(){return text;}void setText(const std::string& s){text=s;}
 bool handleSpecialKey(const KEY key,const MASK mask) BODY
 bool wantsReturnKey(){return false;}bool handleKey(KEY key,MASK mask,bool){return handleSpecialKey(key,mask);}
};
struct Focus {LLChatEntry* entry;LLChatEntry* getKeyboardFocus(){return entry;}} gFocusMgr;
struct Menu {bool handleAcceleratorKey(KEY,MASK){return false;}};Menu* gLoginMenuBarView=nullptr;
struct Root {int retries=0;bool handleKey(KEY key,MASK mask,bool){++retries;return gFocusMgr.entry->handleKey(key,mask,true);}};
struct Window {Root* mRootView;bool down(KEY key,MASK mask){ROUTE}};
int main(){
 LLChatEntry entry;Root root;Window window{&root};gFocusMgr.entry=&entry;
 entry.text="a";entry.mCursorPos=1;assert(window.down(KEY_BACKSPACE,MASK_NONE));
 assert(entry.text.empty()&&entry.removed==1&&LLUI::getInstance()->bad==0&&root.retries==0);
 entry.text="ab";entry.mCursorPos=2;assert(window.down(KEY_BACKSPACE,MASK_NONE));
 assert(entry.text=="a"&&entry.removed==2&&LLUI::getInstance()->bad==0&&root.retries==0);
 entry.text.clear();entry.mCursorPos=0;assert(window.down(KEY_BACKSPACE,MASK_NONE));
 assert(LLUI::getInstance()->bad==1&&root.retries==0);
 entry.text="selected";entry.selected=true;entry.mCursorPos=8;assert(window.down(KEY_BACKSPACE,MASK_NONE));
 assert(entry.text.empty()&&LLUI::getInstance()->bad==1&&root.retries==0);
 entry.text="word";entry.mCursorPos=4;assert(window.down(KEY_BACKSPACE,MASK_CONTROL));
 assert(entry.text.empty()&&entry.words==1&&LLUI::getInstance()->bad==1&&root.retries==0);
 entry.mReadOnly=true;entry.text="read only";entry.mCursorPos=9;window.down(KEY_BACKSPACE,MASK_NONE);
 assert(entry.text=="read only"&&LLUI::getInstance()->bad==1);
 entry.mReadOnly=false;entry.mCurrentInput="draft";
 assert(!entry.handleSpecialKey(KEY_RETURN,MASK_NONE));assert(entry.mCurrentInput.empty()&&entry.reflow==1);
 entry.mHasHistory=true;entry.mLineHistory={"one","two"};entry.mCurrentHistoryLine=entry.mLineHistory.end();
 entry.text="draft";assert(entry.handleSpecialKey(KEY_UP,MASK_CONTROL));assert(entry.text=="two");
 assert(entry.handleSpecialKey(KEY_DOWN,MASK_CONTROL));assert(entry.text=="draft");
}
""".replace('EDITOR_BODY',backspace).replace('BODY',body).replace('ROUTE',route)
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'chat_keyboard.cpp';path.write_text(fixture)
            exe=Path(directory)/'chat_keyboard.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
