"""Compile real tracker and nearby teleport bodies with deterministic CPU services."""
from pathlib import Path
import shutil, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[2]

def body(source, signature):
    start = source.index(signature)
    end = source.index('{', start) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

class NativeMapsTests(unittest.TestCase):
    def compile(self, source):
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory) / 'maps.cpp'
            exe = Path(directory) / 'maps.exe'
            cpp.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_actual_tracker_updates_retires_and_does_not_override_reentrant_selection(self):
        source = (ROOT / 'indra/newview/lltracker.cpp').read_text()
        fixture = r'''
#include <cassert>
#include <cmath>
#include <functional>
#include <string>
#include <utility>
using U64 = unsigned long long;
struct LLVector3d { double x=0; bool isFinite()const{return std::isfinite(x);} void zeroVec(){x=0;} };
struct Map {void clearLocationSelection(bool,bool){}} *gFloaterWorldMap=nullptr;
struct LLTracker {
enum ETrackingLocationType {LOCATION_NOTHING,LOCATION_AVATAR};
enum {TRACKING_NOTHING,TRACKING_LOCATION};
static LLTracker* sTrackerp;
static LLTracker* instance(){if(!sTrackerp)sTrackerp=new LLTracker;return sTrackerp;}
LLVector3d mTrackedPositionGlobal;
std::function<bool(LLVector3d&)>mFollowLocation;
U64 mFollowSerial=0;
std::string mTrackedLocationName,mLabel,mToolTip;
int mTrackingStatus=0;bool mIsTrackingLocation=false;
ETrackingLocationType mTrackingLocationType=LOCATION_NOTHING;
void stopTrackingAvatar(){} void stopTrackingLandmark(){} void purgeBeaconText(){}
static void trackLocation(const LLVector3d&,const std::string&,const std::string&,ETrackingLocationType=LOCATION_NOTHING);
static void followLocation(const LLVector3d&,const std::string&,std::function<bool(LLVector3d&)>);
static void updateFollowLocation();static void clearFocus();
void stopTrackingLocation(bool=false,bool=false);
};LLTracker* LLTracker::sTrackerp=nullptr;
'''
        for signature in ['void LLTracker::trackLocation(', 'void LLTracker::followLocation(', 'void LLTracker::updateFollowLocation()', 'void LLTracker::stopTrackingLocation(', 'void LLTracker::clearFocus()']:
            fixture += body(source, signature) + '\n'
        fixture += r'''
int main(){
 LLTracker::updateFollowLocation();assert(!LLTracker::sTrackerp);
 bool current=true;int polls=0;double x=4;
 auto follow=[&](LLVector3d& p){++polls;p.x=x;return current;};
 LLTracker::followLocation({2},"Resident",follow);
 LLTracker::updateFollowLocation();assert(LLTracker::instance()->mTrackedPositionGlobal.x==4&&polls==1);
 x=8;LLTracker::updateFollowLocation();assert(LLTracker::instance()->mTrackedPositionGlobal.x==8);
 current=false;LLTracker::updateFollowLocation();assert(!LLTracker::instance()->mFollowLocation&&LLTracker::instance()->mTrackingStatus==LLTracker::TRACKING_NOTHING);
 current=true;LLTracker::followLocation({2},"Resident",follow);
 LLTracker::trackLocation({20},"New destination","");LLTracker::updateFollowLocation();assert(LLTracker::instance()->mTrackedPositionGlobal.x==20);
 LLTracker::followLocation({2},"Resident",follow);LLTracker::clearFocus();LLTracker::updateFollowLocation();assert(!LLTracker::instance()->mFollowLocation);
 LLTracker::followLocation({2},"Resident",[](LLVector3d& p){p.x=999;LLTracker::trackLocation({30},"Reentrant destination","");return true;});
 LLTracker::updateFollowLocation();assert(LLTracker::instance()->mTrackedPositionGlobal.x==30);
 LLTracker::followLocation({2},"Resident",[](LLVector3d& p){p.x=std::nan("");return true;});LLTracker::updateFollowLocation();assert(!LLTracker::instance()->mFollowLocation);
 delete LLTracker::sTrackerp;LLTracker::sTrackerp=nullptr;LLTracker::updateFollowLocation();assert(!LLTracker::sTrackerp);
}
'''
        self.compile(fixture)

    def test_actual_nearby_teleport_retains_reference_offsets_and_unknown_height(self):
        source = (ROOT / 'indra/newview/llavataractions.cpp').read_text()
        fixture = r'''
#include <cassert>
#include <cmath>
#include <optional>
#include <string>
using F32=float;using LLUUID=int;constexpr int VZ=2;constexpr double AVATAR_UNKNOWN_Z_OFFSET=-1;
struct LLVector3d{double mdV[3];LLVector3d(double x=0,double y=0,double z=0):mdV{x,y,z}{}double normalize(){double n=std::sqrt(mdV[0]*mdV[0]+mdV[1]*mdV[1]+mdV[2]*mdV[2]);if(n)for(auto&v:mdV)v/=n;return n;}};
LLVector3d operator+(LLVector3d a,LLVector3d b){return {a.mdV[0]+b.mdV[0],a.mdV[1]+b.mdV[1],a.mdV[2]+b.mdV[2]};}
LLVector3d operator-(LLVector3d a,LLVector3d b){return {a.mdV[0]-b.mdV[0],a.mdV[1]-b.mdV[1],a.mdV[2]-b.mdV[2]};}
LLVector3d operator*(LLVector3d a,float b){return {a.mdV[0]*b,a.mdV[1]*b,a.mdV[2]*b};}
struct Window{bool native=true;bool isNativeVulkan(){return native;}}window,*gViewerWindow=&window;
struct Agent{int sends=0;LLVector3d destination;void teleportViaLocation(LLVector3d p){++sends;destination=p;}}gAgent;
struct Settings{float lateral=3,vertical=2;float getF32(const char*key){return std::string(key)=="FSTeleportToOffsetLateral"?lateral:vertical;}}gSavedSettings;
struct Region{LLVector3d getOriginGlobal()const{return {256,512,0};}float getWidth()const{return 256;}float getHeight()const{return 256;}};
bool located=true,regionKnown=true;LLVector3d position(384,640,0);
bool vs_avatar_nearby_location(int,LLVector3d&p){p=position;return located;}
std::optional<Region>vs_region_context(LLVector3d){return regionKnown?std::optional<Region>(Region{}):std::nullopt;}
struct LLNotificationsUtil{static int failures;static void add(const char*){++failures;}};int LLNotificationsUtil::failures=0;
struct FSRadar{static FSRadar*getInstance(){static FSRadar r;return &r;}void teleportToAvatar(int){assert(false);}};
struct LLAvatarActions{static void teleportTo(const LLUUID&);};
'''
        fixture += body(source, 'void LLAvatarActions::teleportTo(const LLUUID& id)')
        fixture += r'''
int main(){LLAvatarActions::teleportTo(1);assert(gAgent.sends==1&&gAgent.destination.mdV[0]==384&&gAgent.destination.mdV[1]==643&&gAgent.destination.mdV[2]==2);
 position={394,640,0};LLAvatarActions::teleportTo(1);assert(gAgent.sends==2&&gAgent.destination.mdV[0]==391&&gAgent.destination.mdV[2]==2);
 gSavedSettings.lateral=-2;LLAvatarActions::teleportTo(1);assert(gAgent.sends==3&&gAgent.destination.mdV[0]==394);
 position.mdV[2]=-1;LLAvatarActions::teleportTo(1);assert(gAgent.sends==3&&LLNotificationsUtil::failures==1);
 position.mdV[2]=10;regionKnown=false;LLAvatarActions::teleportTo(1);assert(gAgent.sends==3&&LLNotificationsUtil::failures==2);
 located=false;LLAvatarActions::teleportTo(1);assert(gAgent.sends==3&&LLNotificationsUtil::failures==3);
}
'''
        self.compile(fixture)

if __name__ == '__main__': unittest.main()
