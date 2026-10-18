"""Real captured drag tools receive Escape on the native window input route."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from native_source_fixture import ROOT,method
class KeyboardToolTest(unittest.TestCase):
 def test_captured_owner_and_reference_dispatch(self):
  window=(ROOT/'indra/newview/llviewerwindow.cpp').read_text(encoding='utf-8')
  a=window.index('    LLTool* keyboardTool = mNativeVulkan ?')
  b=window.index('    // Try for a new-format gesture',a)
  dispatch=window[a:b]
  drag=method((ROOT/'indra/newview/lltooldraganddrop.cpp').read_text(encoding='utf-8'),'bool LLToolDragAndDrop::handleKey(')
  fixture=r"""#include <cassert>
using KEY=int;using MASK=int;constexpr KEY KEY_ESCAPE=27;
struct MouseHandler{virtual ~MouseHandler()=default;};
struct LLTool:MouseHandler{virtual bool handleKey(KEY,MASK){return false;}};
struct LLToolDragAndDrop:LLTool{bool dragging=true;unsigned ended=0;void endDrag(){dragging=false;++ended;}bool handleKey(KEY,MASK)override;};
DRAG
struct Focus{MouseHandler*owner=nullptr;MouseHandler*getMouseCapture(){return owner;}}gFocusMgr;
struct LLToolMgr{unsigned reads=0;LLTool*tool=nullptr;static LLToolMgr*getInstance(){static LLToolMgr x;return &x;}LLTool*getCurrentTool(){++reads;return tool;}};
struct Sink{template<class T>Sink&operator<<(T){return *this;}};
#define LL_DEBUGS() Sink()
constexpr int LL_ENDL=0;
struct LLViewerEventRecorder{static LLViewerEventRecorder&instance(){static LLViewerEventRecorder x;return x;}void logKeyEvent(KEY,MASK){}};
bool dispatch(bool mNativeVulkan,KEY key,MASK mask){DISPATCH return false;}
int main(){LLToolDragAndDrop drag;MouseHandler control;auto*manager=LLToolMgr::getInstance();manager->tool=&drag;
 gFocusMgr.owner=&drag;assert(!dispatch(true,65,0)&&drag.dragging);assert(dispatch(true,KEY_ESCAPE,0)&&!drag.dragging&&drag.ended==1&&manager->reads==0);
 gFocusMgr.owner=&control;assert(!dispatch(true,KEY_ESCAPE,0)&&manager->reads==0&&drag.ended==1);
 gFocusMgr.owner=nullptr;assert(!dispatch(true,KEY_ESCAPE,0)&&manager->reads==0);
 drag.dragging=true;assert(dispatch(false,KEY_ESCAPE,0)&&drag.ended==2&&manager->reads==1);
}
""".replace('DRAG',drag).replace('DISPATCH',dispatch)
  compiler=shutil.which('clang++')or shutil.which('g++');self.assertIsNotNone(compiler)
  with tempfile.TemporaryDirectory()as tmp:
   cpp=Path(tmp)/'keyboard.cpp';exe=Path(tmp)/'keyboard.exe';cpp.write_text(fixture,encoding='utf-8')
   built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True);self.assertEqual(built.returncode,0,built.stdout+built.stderr)
   ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
