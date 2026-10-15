"""Exercise production transcript publication, close/reopen and worker completion."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class ChatHistoryHandoffTest(unittest.TestCase):
    def test_worker_never_accesses_preview_and_retired_results_cannot_reopen_it(self):
        source = (ROOT/'indra/newview/llfloaterconversationpreview.cpp').read_text()
        callback = method(source, '[pending = mPendingHistory]')
        draw = method(source, 'void LLFloaterConversationPreview::draw()')
        close = method(source, 'void LLFloaterConversationPreview::onClose(')
        destructor = method(source, 'LLFloaterConversationPreview::~LLFloaterConversationPreview()')
        opened = method(source, 'void LLFloaterConversationPreview::onOpen(')
        self.assertLess(opened.index('addDeleteHistoryThread'), opened.index('loadThread->start()'))
        fixture = r'''
#include "vschathistoryhandoff.h"
#include <cassert>
#include <list>
#include <memory>
#include <thread>
using LLSD=int;
struct Connection { void disconnect() {} };
struct DeleteThread { int starts=0; void start() { ++starts; } };
using LLDeleteHistoryThread=DeleteThread;
struct LLLogChat { DeleteThread deletion; static LLLogChat* getInstance() { static LLLogChat log; return &log; }
 static bool instanceExists() { return true; } DeleteThread* getDeleteHistoryThread(int) { return &deletion; } };
struct LLFloater { int frames=0; void draw() { ++frames; } };
struct LLFloaterConversationPreview:LLFloater {
 using HistoryHandoff=VSChatHistoryHandoff<std::list<LLSD>>;
 std::shared_ptr<HistoryHandoff> mPendingHistory=std::make_shared<HistoryHandoff>();
 std::list<LLSD>* mMessages=nullptr; Connection mHistoryConnection;
 bool mOpened=true,mHistoryThreadsBusy=false,mShowHistory=false; int mSessionID=1,pages=0,shown=0;
 std::thread::id ui=std::this_thread::get_id();
 ~LLFloaterConversationPreview(); void draw(); void onClose(bool);
 auto callback() { return CALLBACK; }
 void setPages(std::list<LLSD>* messages,const std::string& file) {
  assert(std::this_thread::get_id()==ui && file=="history");mMessages=messages;++pages;mShowHistory=true;
 }
 void showHistory() { assert(std::this_thread::get_id()==ui && mMessages && !mMessages->empty());++shown; }
};
DRAW
CLOSE
DESTRUCTOR
int main() {
 std::list<LLSD> messages{1,2,3}; auto& deletion=LLLogChat::getInstance()->deletion;
 {
  LLFloaterConversationPreview preview;auto callback=preview.callback();
  std::thread worker([&]{callback(&messages,"history");});
  // Drawing and worker completion can occur in either order; widgets remain on UI.
  preview.draw();worker.join();preview.draw();preview.draw();
  assert(preview.pages==1 && preview.shown==1 && preview.mMessages==&messages);
  preview.onClose(false);assert(!preview.mMessages && !preview.mPendingHistory && !preview.mShowHistory);
  int starts=deletion.starts;preview.onClose(false);assert(deletion.starts==starts);
 }
 {
  LLFloaterConversationPreview preview;auto old=preview.callback();preview.onClose(false);
  preview.mOpened=true;preview.mPendingHistory=std::make_shared<LLFloaterConversationPreview::HistoryHandoff>();
  std::thread late([&]{old(&messages,"history");});late.join();preview.draw();assert(preview.pages==0);
  auto current=preview.callback();std::thread worker([&]{current(&messages,"history");});worker.join();
  preview.draw();assert(preview.pages==1 && preview.shown==1);
 }
 {
  auto preview=std::make_unique<LLFloaterConversationPreview>();auto callback=preview->callback();
  auto weak=std::weak_ptr(preview->mPendingHistory);preview.reset();
  std::thread afterDestruction([&]{callback(&messages,"history");});afterDestruction.join();
  assert(!weak.expired()); // Only the callback retains the mailbox, never the UI.
 }
}
'''.replace('CALLBACK', callback).replace('DRAW', draw).replace('CLOSE', close).replace('DESTRUCTOR', destructor)
        self.compile(fixture)

    def test_completion_before_or_during_wait_is_not_lost(self):
        source = (ROOT/'indra/newview/lllogchat.cpp').read_text()
        methods = '\n'.join(method(source, name) for name in
                            ('void LLActionThread::waitFinished()', 'void LLActionThread::setFinished()'))
        self.compile(r'''
#include <cassert>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <iostream>
struct LLActionThread {
 bool mFinished=false;std::mutex mMutex;std::condition_variable mRunCondition;
 void waitFinished();void setFinished();
};
METHODS
int main() {
 for(int iteration=0;iteration<100;++iteration) {
  if(iteration%25==0)std::cerr<<"completion iteration "<<iteration<<std::endl;
  LLActionThread already;already.setFinished();already.waitFinished();
  LLActionThread pending;
  std::thread first([&]{pending.waitFinished();});
  std::thread second([&]{pending.waitFinished();});
  pending.setFinished();first.join();second.join();
 }
}
'''.replace('METHODS', methods))

    def compile(self, fixture):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/'history.cpp'; exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            run = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                                  '-Wno-unused-parameter', '-pthread', '-I', str(ROOT/'indra/newview'),
                                  str(cpp), '-o', str(exe)], capture_output=True, text=True, timeout=60)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=45)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
