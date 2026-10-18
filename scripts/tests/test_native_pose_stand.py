"""Actual Pose Stand controller preserves original animation sequence and owner retirement."""
from pathlib import Path
import os,unittest
from test_native_material_selection import body,run
ROOT=Path(os.environ.get('VS_POSE_ROOT',Path(__file__).resolve().parents[2]));OVERLAY=Path(os.environ['VS_POSE_PROPOSED'])if 'VS_POSE_PROPOSED'in os.environ else None
def source(path):
 p=OVERLAY/path if OVERLAY else ROOT/path
 return(p if p.exists()else ROOT/path).read_text()
class NativePoseStandTests(unittest.TestCase):
 def test_actual_pose_wire_sequence_and_account_character_generation_expiry(self):
  cpp=source('indra/newview/fspose.cpp');start=body(cpp,'void FSPose::setPose(');stop=body(cpp,'void FSPose::stopPose()');retire=body(cpp,'void FSPose::retireAccount()')
  run(self,r'''
#include <cassert>
#include <functional>
#include <string>
#include <vector>
using U64=unsigned long long;struct LLUUID{std::string value;LLUUID(){}LLUUID(std::string v):value(v){}static bool validate(std::string v){return!v.empty();}void set(std::string v){value=v;}void setNull(){value.clear();}bool notNull(){return!value.empty();}};
int character=1;U64 generation=1;int account=1;int*vs_avatar_character(){return character?&character:nullptr;}U64 vs_avatar_generation(){return generation;}std::function<bool()>vs_native_im_guard(){return[captured=account]{return captured==account;};}
constexpr int ANIM_REQUEST_START=1,ANIM_REQUEST_STOP=0;struct Agent{std::vector<std::pair<std::string,int>>requests;void sendAnimationRequest(LLUUID id,int type){requests.emplace_back(id.value,type);}}gAgent;
struct FSPose{LLUUID mCurrentPose;std::function<bool()>mCurrent=[]{return true;};void setPose(const std::string&new_pose,bool save_state=true)ACTUAL_SET void stopPose()ACTUAL_STOP void retireAccount()ACTUAL_RETIRE};
int main(){FSPose pose;pose.setPose("one");pose.setPose("two");assert((gAgent.requests==std::vector<std::pair<std::string,int>>{{"one",1},{"one",0},{"two",1}}));gAgent.requests.clear();pose.setPose("temp",false);pose.stopPose();assert((gAgent.requests==std::vector<std::pair<std::string,int>>{{"two",0},{"temp",1},{"two",0}}));pose.setPose("old");++account;gAgent.requests.clear();pose.stopPose();assert(gAgent.requests.empty());pose.setPose("new");++generation;gAgent.requests.clear();pose.setPose("next");assert((gAgent.requests==std::vector<std::pair<std::string,int>>{{"next",1}}));pose.retireAccount();gAgent.requests.clear();pose.stopPose();assert(gAgent.requests.empty());character=0;pose.setPose("unavailable");assert(gAgent.requests.empty());}
'''.replace('ACTUAL_SET',start).replace('ACTUAL_STOP',stop).replace('ACTUAL_RETIRE',retire))
 def test_actual_controller_cleanup_is_once_only_and_never_mutates_retired_account(self):
  actual=body(source('indra/newview/fsfloaterposestand.cpp'),'void FSFloaterPoseStand::onClose(')
  run(self,r'''
#include <cassert>
#include <functional>
#include <string>
bool character=true;void*vs_avatar_character(){return character?&character:nullptr;}struct Settings{bool ao=false;bool getBOOL(std::string){return ao;}void setBOOL(std::string,bool v){ao=v;}}gSavedPerAccountSettings;
struct Agent{int stands=0,stops=0;bool sitting=true,custom=true;bool isSitting(){return sitting;}void standUp(){++stands;sitting=false;}void setCustomAnim(bool v){custom=v;}void stopCurrentAnimations(bool){++stops;}}gAgent;
struct FSPose{int stopped=0;static FSPose*getInstance(){static FSPose value;return&value;}void stopPose(){++stopped;}};
struct FSFloaterPoseStand{bool mSessionActive=true,mPoseStandLock=true,mAOPaused=true;std::function<bool()>mCurrent=[]{return true;};void setLock(bool enabled){if(!enabled)gAgent.standUp();mPoseStandLock=enabled;}void onClose(bool app_quitting)ACTUAL};
int main(){FSFloaterPoseStand view;view.onClose(false);assert(!view.mSessionActive&&!gAgent.custom&&gAgent.stands==2&&gAgent.stops==1&&gSavedPerAccountSettings.ao&&FSPose::getInstance()->stopped==1);view.onClose(true);assert(gAgent.stands==2&&gAgent.stops==1);view.mSessionActive=true;view.mCurrent=[]{return false;};gSavedPerAccountSettings.ao=false;view.onClose(true);assert(!gSavedPerAccountSettings.ao&&gAgent.stops==1);}
'''.replace('ACTUAL',actual))
if __name__=='__main__':unittest.main()
