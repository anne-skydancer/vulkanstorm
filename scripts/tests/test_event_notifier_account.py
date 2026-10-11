"""Compile production reminder ownership and retired-account responder paths."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


def function(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


class EventNotifierAccountTest(unittest.TestCase):
    def test_replaced_reminders_retire_locally_and_old_response_cannot_open_new_account(self):
        source = (Path(__file__).resolve().parents[2] / 'indra/newview/lleventnotifier.cpp').read_text()
        methods = '\n'.join(function(source, signature) for signature in (
            'void LLEventNotifier::clearAccount()',
            'bool LLEventNotifier::add(U32 eventId, F64 eventEpoch',
            'bool LLEventNotifier::handleResponse(U32 eventId',
        ))
        fixture = r'''
#include <cassert>
#include <map>
#include <sstream>
#include <string>
using U32=unsigned;using F64=double;using S32=int;
#define LL_INFOS() std::ostringstream()
#define LL_ENDL std::endl
struct LLSD {int epoch=0;LLSD operator[](const char*)const{return *this;}};
int currentEpoch=1,opened=0,live=0;
bool vs_native_im_notification_current(LLSD payload){return payload.epoch==currentEpoch;}
struct LLNotificationsUtil {static int getSelectedOption(LLSD,LLSD response){return response.epoch;}};
struct LLFloaterEvent {void setEventID(U32){}};
struct LLFloaterReg {template<class T>static T* getTypedInstance(const char*){static T owner;return &owner;}
 template<class T>static T* showTypedInstance(const char*){++opened;return getTypedInstance<T>("");}};
struct Timer {int resetCount=0;void reset(){++resetCount;}};
struct LLEventNotification {U32 id;F64 date;std::string name;
 LLEventNotification(U32 i,F64 d,const std::string&,const std::string& n):id(i),date(d),name(n){++live;}
 ~LLEventNotification(){--live;}bool isValid(){return id>0&&date!=0&&!name.empty();}U32 getEventID(){return id;}};
struct LLEventNotifier {std::map<U32,LLEventNotification*> mEventNotifications;Timer mNotificationTimer;
 void clearAccount();bool add(U32,F64,const std::string&,const std::string&);bool handleResponse(U32,const LLSD&,const LLSD&);};
METHODS
int main(){LLEventNotifier notifier;assert(notifier.add(4,100,"date","original"));assert(live==1);
 assert(notifier.add(4,101,"date","refreshed"));assert(live==1&&notifier.mEventNotifications.at(4)->name=="refreshed");
 assert(!notifier.add(0,101,"date","invalid"));assert(live==1);
 assert(notifier.handleResponse(4,{1},{0}));assert(opened==1);
 notifier.clearAccount();assert(live==0&&notifier.mEventNotifications.empty()&&notifier.mNotificationTimer.resetCount==1);
 ++currentEpoch;assert(notifier.add(7,102,"date","new account"));assert(!notifier.handleResponse(4,{1},{0}));assert(opened==1);
 assert(notifier.handleResponse(7,{2},{0}));assert(opened==2);notifier.clearAccount();assert(live==0);
}
'''.replace('METHODS', methods)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'events.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'events.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
