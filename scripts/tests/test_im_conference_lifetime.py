"""Compile the shared IM host close observer and test remapped/retired owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ConferenceLifetimeTest(unittest.TestCase):
    def test_delayed_notification_and_name_result_require_live_conversation(self):
        source = (Path(__file__).resolve().parents[2] / 'indra/newview/llimview.cpp').read_text()
        queued = source.split('void notify_of_message(const LLSD& msg, bool is_dnd_msg)\n{', 1)[1].split('    // [CHUI Merge]', 1)[0]
        resolved = source.split('                                       LLSD msg)\n{', 1)[1].split('    LLSD args;', 1)[0]
        fixture = r"""#include <cassert>
struct LLUUID {int id;bool notNull()const{return id!=0;}};
struct LLSD {int id;LLSD operator[](const char*)const{return *this;}LLUUID asUUID()const{return {id};}};
struct LLIMModel {bool live=false;static LLIMModel& instance(){static LLIMModel model;return model;}
 void* findIMSession(LLUUID id){return live&&id.id==77?this:nullptr;}};
int notifications=0,toasts=0;
void notification(const LLSD& msg){QUEUED ++notifications;}
void nameResolved(LLSD msg){RESOLVED ++toasts;}
int main(){
 auto& model=LLIMModel::instance();model.live=true;
 notification({77});nameResolved({77});assert(notifications==1&&toasts==1);
 model.live=false;notification({77});nameResolved({77});assert(notifications==1&&toasts==1);
 notification({0});nameResolved({0});assert(notifications==2&&toasts==2);
}
""".replace('QUEUED',queued).replace('RESOLVED',resolved)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'notification.cpp';path.write_text(fixture)
            exe=Path(directory)/'notification.exe'
            built=subprocess.run([compiler,'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_remapped_conversation_and_retired_host(self):
        source = (Path(__file__).resolve().parents[2] / 'indra/newview/fsfloaterimcontainer.cpp').read_text()
        observer = source.split('    LLUUID session_id = floaterp->getKey();', 1)[1].split('\n}\n', 1)[0]
        close = source.split('void FSFloaterIMContainer::onCloseFloater(LLUUID& id)', 1)[1].split('\nvoid FSFloaterIMContainer::onNewMessageReceived', 1)[0]
        fixture = r"""#include <cassert>
#include <functional>
#include <map>
#include <memory>
using LLUUID=int;
struct LLSD { int value=0;int asUUID() const {return value;}operator int() const{return value;} };
struct LLUICtrl {virtual ~LLUICtrl()=default;};
struct LLFloater:LLUICtrl {
 int key=1;bool shown=true;
 LLSD getKey(){return LLSD{key};}bool isShown(){return shown;}
 struct Signal {std::function<void(LLUICtrl*,const LLSD&)> slot;
 template<class F> void connect(F f){slot=f;} } mCloseSignal;
};
namespace boost {
 template<class C> auto bind(void(C::*method)(LLUUID&),C* owner,LLUUID id){
  return [owner,method,id](LLUICtrl*,const LLSD&)mutable{(owner->*method)(id);};
 }
}
struct Handle {std::weak_ptr<LLFloater*> weak;LLFloater* get() const {auto p=weak.lock();return p?*p:nullptr;}};
struct FSFloaterIMContainer:LLFloater {
 std::map<LLUUID,LLFloater*> mSessions;std::shared_ptr<LLFloater*> lifetime=std::make_shared<LLFloater*>(this);
 int focused=0;bool minimized=false;
 Handle getHandle(){return Handle{lifetime};}void setFocus(bool){++focused;}
 bool isMinimized(){return minimized;}void setMinimized(bool v){minimized=v;}
 void add(LLFloater* floaterp){LLUUID session_id=floaterp->getKey();OBSERVER}
 void onCloseFloater(LLUUID& id) CLOSE
};
int main(){
 LLFloater conversation;
 std::function<void(LLUICtrl*,const LLSD&)> delayed;
 {
  FSFloaterIMContainer host;host.add(&conversation);
  // Production sessionIDUpdated moves the map, while the panel updates its key.
  host.mSessions.erase(1);conversation.key=77;host.mSessions[77]=&conversation;
  conversation.mCloseSignal.slot(&conversation,LLSD{});
  assert(host.mSessions.empty());assert(host.focused==1);
  // Duplicate close cannot manufacture a new session or steal focus.
  conversation.mCloseSignal.slot(&conversation,LLSD{});
  assert(host.mSessions.empty());assert(host.focused==1);
  delayed=conversation.mCloseSignal.slot;
 }
 // A live detached panel can outlive its tab host; its signal is harmless.
 delayed(&conversation,LLSD{});
 FSFloaterIMContainer ordinary;conversation.key=12;ordinary.add(&conversation);
 conversation.mCloseSignal.slot(&conversation,LLSD{});assert(ordinary.mSessions.empty());
}
""".replace('OBSERVER', observer).replace('CLOSE', close)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'conference.cpp'; path.write_text(fixture)
            exe = Path(directory) / 'conference.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
