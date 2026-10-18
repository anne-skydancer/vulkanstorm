"""Exercise actual navigation completion/retirement bodies, including callback reentry."""
from pathlib import Path
import sys,subprocess,tempfile,shutil
ROOT=Path(__file__).resolve().parents[2];sys.path.insert(0,str(ROOT/'scripts/tests'))
from native_source_fixture import method
src=(ROOT/'indra/newview/llagent.cpp').read_text(encoding='utf-8')
stop=method(src,'void LLAgent::stopAutoPilot(');retire=method(src,'void LLAgent::retireNativeAutoPilot(')
code=r'''
#include <cassert>
#include <functional>
#include <utility>
#include <string>
#include <cmath>
#include <cstdint>
#include <sstream>
using U64=uint64_t;using F32=float;
struct LLUUID{int value=0;static const LLUUID null;};const LLUUID LLUUID::null{};
struct LLVector3{float x=0;};
float dist_vec(float a,float b){return std::abs(a-b);}
constexpr int AGENT_CONTROL_STOP=1;
#define LL_INFOS(...) std::ostringstream()
#define LL_ENDL ""
struct LLNotificationsUtil{static inline std::string last;static void add(const char*s){last=s;}};
struct VSAgentNavigation{static inline int faces=0;void face(LLVector3){++faces;}};
struct LLAgent{
 bool mAutoPilot=true,mAutoPilotUseRotation=true,mAutoPilotFlyOnStop=true,mNativeAutoPilotRetiring=false;
 U64 mNativeAutoPilotIntent=0;std::function<bool()>mNativeAutoPilotAccount;
 LLUUID mLeaderID{4};LLVector3 mAutoPilotTargetFacing;float mAutoPilotTargetGlobal=1,mAutoPilotStopDistance=2;
 std::string mAutoPilotBehaviorName="Attach";
 void(*mAutoPilotFinishedCallback)(bool,void*)=nullptr;void*mAutoPilotCallbackData=nullptr;
 int axes=0,controls=0,fly=0;float position=1;
 float getPositionGlobal()const{return position;}
 void resetAxes(LLVector3){++axes;}void setFlying(bool v){fly=v?1:-1;}void setControlFlags(int f){controls|=f;}
 void stopAutoPilot(bool=false);void retireNativeAutoPilot();
};LLAgent gAgent;
STOP
RETIRE
struct Outcome{int calls=0;bool success=false;std::function<void()> action;};
void callback(bool success,void*data){auto&out=*static_cast<Outcome*>(data);++out.calls;out.success=success;if(out.action)out.action();}
void arm(LLAgent&agent,Outcome&out,bool*alive){agent=LLAgent{};agent.mNativeAutoPilotAccount=[alive]{return *alive;};agent.mAutoPilotFinishedCallback=callback;agent.mAutoPilotCallbackData=&out;}
int main(){
 bool alive=true;Outcome complete;
 arm(gAgent,complete,&alive);gAgent.stopAutoPilot();assert(complete.calls==1&&complete.success);assert(gAgent.axes==1&&gAgent.fly==1&&VSAgentNavigation::faces==1);assert(!gAgent.mAutoPilotFinishedCallback&&!gAgent.mNativeAutoPilotAccount&&gAgent.controls==1);
 Outcome cancelled;arm(gAgent,cancelled,&alive);gAgent.stopAutoPilot(true);assert(cancelled.calls==1&&!cancelled.success&&gAgent.axes==0&&gAgent.fly==0&&LLNotificationsUtil::last=="CancelledAttach");
 Outcome expired;arm(gAgent,expired,&alive);alive=false;gAgent.stopAutoPilot();assert(expired.calls==1&&!expired.success&&gAgent.axes==0&&gAgent.fly==0);alive=true;
 Outcome replacement,next;arm(gAgent,replacement,&alive);replacement.action=[&]{gAgent.mAutoPilot=true;gAgent.mNativeAutoPilotAccount=[&]{return alive;};gAgent.mAutoPilotFinishedCallback=callback;gAgent.mAutoPilotCallbackData=&next;};gAgent.stopAutoPilot();assert(replacement.calls==1&&gAgent.mAutoPilot&&gAgent.mAutoPilotFinishedCallback==callback&&gAgent.mAutoPilotCallbackData==&next);gAgent.stopAutoPilot();assert(next.calls==1&&next.success);
 Outcome retired;arm(gAgent,retired,&alive);retired.action=[&]{assert(gAgent.mNativeAutoPilotRetiring);assert(!gAgent.mAutoPilotFinishedCallback&&!gAgent.mNativeAutoPilotAccount);gAgent.retireNativeAutoPilot();};gAgent.retireNativeAutoPilot();assert(retired.calls==1&&!retired.success&&!gAgent.mNativeAutoPilotRetiring&&!gAgent.mAutoPilot);gAgent.retireNativeAutoPilot();assert(retired.calls==1);
}
'''.replace('\nSTOP\n','\n'+stop+'\n').replace('\nRETIRE\n','\n'+retire+'\n')
with tempfile.TemporaryDirectory() as tmp:
 path=Path(tmp)/'navigation.cpp';path.write_text(code,encoding='utf-8');exe=path.with_suffix('.exe');r=subprocess.run([shutil.which('clang++')or shutil.which('g++'),'-std=c++17',str(path),'-o',str(exe)],capture_output=True,text=True);print(r.stdout+r.stderr);r.check_returncode();subprocess.run([str(exe)],check=True)
print('PASS: actual completion, cancellation, expired account, callback replacement and idempotent retirement')
