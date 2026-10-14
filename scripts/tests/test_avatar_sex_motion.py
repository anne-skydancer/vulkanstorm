"""Exercise the actual shape/sex update's active motion remapping."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class AvatarSexMotionTest(unittest.TestCase):
    def test_active_mapped_motions_restart_and_ao_control_owners_are_preserved(self):
        production = method((ROOT/'indra/newview/vsworldavatar.cpp').read_text(), 'void updateVisualParams() override')
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        fixture = r'''
#include <cassert>
#include <map>
#include <set>
#include <vector>
using LLUUID=int;using S32=int;enum ESex{SEX_FEMALE,SEX_MALE};
int vs_remap_avatar_motion(int id,bool female){return female&&(id==1||id==2)?id+10:id;}
struct LLCharacter{std::set<int> active;std::vector<int> started,stopped;int updates=0;
 virtual ~LLCharacter()=default;virtual void updateVisualParams(){++updates;}
 bool isMotionActive(int id)const{return active.count(id)!=0;}
 bool stopMotion(int id,bool){stopped.push_back(id);return active.erase(id)!=0;}
 bool startMotion(int id){started.push_back(id);return active.insert(id).second;}};
struct Root{int updates=0;void updateWorldMatrixChildren(){++updates;}};
struct Avatar:LLCharacter{float male=0;ESex sex=SEX_FEMALE;bool control=false,shapeDirty=false;
 std::map<int,int> animations{{1,1},{2,1},{3,1}};std::vector<int> compositeSignature{1,2};
 unsigned skeletonSerial=2,serial=2;int sizes=0;Root root,*mRoot=&root;
 float getVisualParamWeight(const char*)const{return male;}ESex getSex()const{return sex;}
 void setSex(ESex value){sex=value;}unsigned getSkeletonSerialNum()const{return serial;}
 void computeBodySize(){++sizes;}
PRODUCTION
};
int main(){Avatar avatar;avatar.active={11,12,3};avatar.male=1;
 avatar.updateVisualParams();assert(avatar.sex==SEX_MALE&&avatar.shapeDirty&&avatar.compositeSignature.empty());
 assert((avatar.active==std::set<int>{1,2,3}));assert((avatar.stopped==std::vector<int>{11,12}));
 assert((avatar.started==std::vector<int>{1,2}));avatar.updateVisualParams();assert(avatar.started.size()==2);
 avatar.male=0;avatar.updateVisualParams();assert((avatar.active==std::set<int>{11,12,3}));
 // An AO-replaced source motion is signaled but has no active default instance.
 Avatar ao;ao.active={3};ao.male=1;ao.updateVisualParams();assert(ao.active==std::set<int>{3});assert(ao.started.empty());
 // Control-avatar animation IDs are assets, not resident locomotion aliases.
 Avatar control;control.control=true;control.active={11,12,3};control.male=1;control.updateVisualParams();
 assert((control.active==std::set<int>{11,12,3}));assert(control.started.empty()&&control.stopped.empty());
 // The unchanged sex path still applies shared morphs and skeleton changes.
 Avatar unchanged;unchanged.serial=3;unchanged.updateVisualParams();assert(unchanged.sizes==1&&unchanged.root.updates==1);
 assert(unchanged.compositeSignature.size()==2&&unchanged.updates==1);
}
'''.replace('PRODUCTION', production)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary)/'avatar_sex_motion.cpp'; exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


if __name__ == '__main__':
    unittest.main()
