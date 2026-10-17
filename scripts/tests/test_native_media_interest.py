"""Run the production native media-interest bridge without graphics or plugins."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeMediaInterest(unittest.TestCase):
    def test_visibility_expiry_mute_proximity_and_owner_retirement(self):
        source=(ROOT/'indra/newview/llviewermedia.cpp').read_text()
        bridge=source[source.index('    if (!mNativeSurfaces.empty())'):source.index('#endif',source.index('    if (!mNativeSurfaces.empty())'))]
        methods='\n'.join(method(source,name) for name in ('void LLViewerMediaImpl::setNativeSurface(', 'void LLViewerMediaImpl::removeNativeSurface('))
        fixture=r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <map>
#include <set>
using LLUUID=int;using F64=double;
struct LLFrameTimer{static inline double now=10;static double getTotalSeconds(){return now;}};
struct LLMuteList{
 std::set<int> muted;static LLMuteList* getInstance(){static LLMuteList m;return &m;}
 bool isMuted(int object){return muted.count(object)!=0;}
};
struct LLViewerMediaImpl{
 struct NativeSurface{F64 pixels=0,distance=0,updated=0;};std::map<int,NativeSurface> mNativeSurfaces;
 F64 mInterest=0,mProximityDistance=0,mProximityCamera=0;
 bool mIsMuted=false,mNeedsMuteCheck=true;unsigned legacyCalls=0;
 void setNativeSurface(const LLUUID&,F64,F64);void removeNativeSurface(const LLUUID&);
 void calculateInterest(){BRIDGE ++legacyCalls;}
};
METHODS
int main(){
 LLViewerMediaImpl m;m.calculateInterest();assert(m.legacyCalls==1);
 m.setNativeSurface(1,400,10);m.setNativeSurface(2,900,20);m.calculateInterest();
 assert(m.mInterest==900 && m.mProximityDistance==100 && m.mProximityCamera==10 && !m.mIsMuted);
 LLMuteList::getInstance()->muted.insert(2);m.calculateInterest();assert(m.mInterest==400 && !m.mIsMuted);
 LLMuteList::getInstance()->muted.insert(1);m.calculateInterest();assert(m.mInterest==0 && m.mIsMuted);
 LLFrameTimer::now=12;m.calculateInterest();assert(m.mInterest==0 && !m.mIsMuted);
 m.setNativeSurface(3,100,5);m.calculateInterest();assert(m.mInterest==100 && m.mProximityDistance==25);
 m.setNativeSurface(4,std::numeric_limits<double>::quiet_NaN(),1);assert(m.mNativeSurfaces.size()==3);
 m.removeNativeSurface(3);m.calculateInterest();assert(m.mInterest==0);
 m.removeNativeSurface(1);m.removeNativeSurface(2);m.calculateInterest();assert(m.legacyCalls==2);
}
'''.replace('BRIDGE',bridge).replace('METHODS',methods)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            source=Path(temp)/'media_interest.cpp';source.write_text(fixture);exe=Path(temp)/'media_interest.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
