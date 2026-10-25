"""Compile production native profile location routes without a scene region."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_event_notifier_account import function


class ProfileNativeLocationTest(unittest.TestCase):
    def test_new_pick_and_update_use_current_native_region_and_parcel(self):
        root = Path(__file__).resolve().parents[2]
        action = (root / 'indra/newview/llavataractions.cpp').read_text()
        create = function(action, 'void LLAvatarActions::createPick()')
        create_native = function(create, 'if (const auto owner = VSNativeSession::active())')
        source = (root / 'indra/newview/llpanelprofilepicks.cpp').read_text()
        location = function(source, 'void LLPanelProfilePick::onClickSetLocation()')
        classified = (root / 'indra/newview/llpanelprofileclassifieds.cpp').read_text()
        classified_location = function(classified, 'void LLPanelProfileClassified::onSetLocationClick()')
        fixture = r'''
#include <cassert>
#include <string>
#define VS_NATIVE_VULKAN 1
struct LLUUID {int id=0;static LLUUID null;};LLUUID LLUUID::null;
struct Position {double x=1040,y=2050,z=30;};
struct LLViewerRegion {std::string getName(){return "legacy";}};
struct Agent {Position getPositionGlobal(){return {};}LLViewerRegion*getRegion(){return nullptr;}}gAgent;
struct VSNativeSession {enum class Phase {Connected,Disconnected};Phase state=Phase::Connected;
 static VSNativeSession*active(){static VSNativeSession owner;return &owner;}
 Phase phase(){return state;}std::string regionName(){return "Current Native Region";}std::string parcelName(){return "Current Native Parcel";}};
struct LLPickData {Position pos_global;std::string sim_name,name;};
struct LLFloaterProfile {int created=0;LLPickData data;void createPick(LLPickData value){++created;data=value;}};
struct LLParcel {LLUUID getID(){return {99};}std::string getName(){return "stale scene parcel";}};
struct LLViewerParcelMgr {static LLViewerParcelMgr*getInstance(){static LLViewerParcelMgr manager;return &manager;}
 LLParcel*getAgentParcel(){static LLParcel stale;return &stale;}};
void createPick(LLFloaterProfile*profilefloater){CREATE_NATIVE}
constexpr int RLV_BHVR_SHOWLOC=1;
struct RlvHandler {bool showloc=false;bool hasBehaviour(int behaviour){return behaviour==RLV_BHVR_SHOWLOC&&showloc;}}gRlvHandler;
struct LLPanelProfilePick {LLUUID mParcelId{77};bool mLocationChanged=false,saving=false;std::string region,parcel;Position pos;
 void setPosGlobal(Position p){pos=p;}Position getPosGlobal(){return pos;}std::string getLocationNotice(){return "";}
 std::string createLocationText(std::string,std::string p,std::string r,Position){parcel=p;region=r;return r;}
 void setPickLocation(std::string){}void setParcelID(LLUUID id){mParcelId=id;}void enableSaveButton(bool value){saving=value;}void onClickSetLocation();};
struct LLSD {};
struct LLTrans {static std::string getString(const char*){return "legacy placeholder";}};
struct LLPanelProfileClassified {Position pos;std::string region;LLUUID parcel{77};bool changed=false;
 void setPosGlobal(Position p){pos=p;}Position getPosGlobal(){return pos;}void setParcelId(LLUUID id){parcel=id;}
 std::string getLocationNotice(){return "";}std::string createLocationText(std::string,std::string r,Position){region=r;return r;}
 void setClassifiedLocation(std::string){}void setValue(LLSD){}void onChange(){changed=true;}void onSetLocationClick();};
LOCATION
CLASSIFIED_LOCATION
int main(){LLFloaterProfile profile;createPick(&profile);assert(profile.created==1&&profile.data.sim_name=="Current Native Region"&&profile.data.name=="Current Native Parcel"&&profile.data.pos_global.x==1040);
 VSNativeSession::active()->state=VSNativeSession::Phase::Disconnected;createPick(&profile);assert(profile.created==1);
 VSNativeSession::active()->state=VSNativeSession::Phase::Connected;
 LLPanelProfilePick restricted;gRlvHandler.showloc=true;restricted.onClickSetLocation();
 assert(restricted.region.empty()&&restricted.mParcelId.id==77&&!restricted.mLocationChanged&&!restricted.saving);gRlvHandler.showloc=false;
 LLPanelProfilePick pick;pick.onClickSetLocation();assert(pick.region=="Current Native Region"&&pick.parcel=="Current Native Parcel"&&pick.mParcelId.id==0&&pick.mLocationChanged&&pick.saving);
 LLPanelProfileClassified classified;classified.onSetLocationClick();assert(classified.region=="Current Native Region"&&classified.parcel.id==0&&classified.changed&&classified.pos.y==2050);
}
'''.replace('CREATE_NATIVE', create_native).replace('CLASSIFIED_LOCATION', classified_location).replace('LOCATION', location)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler, 'A C++ compiler is required')
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'profile_native_location.cpp'
            path.write_text(fixture)
            executable = Path(directory) / 'profile_native_location.exe'
            built = subprocess.run([compiler, '-std=c++17', str(path), '-o', str(executable)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
