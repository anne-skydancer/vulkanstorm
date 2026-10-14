"""Exercise the real standing motion initializer across asynchronous asset states."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class SpecializedMotionLoadingTest(unittest.TestCase):
    def test_pending_asset_does_not_poison_registered_motion_factory(self):
        production = method((ROOT/'indra/llcharacter/llkeyframestandmotion.cpp').read_text(),
                            'LLMotion::LLMotionInitStatus LLKeyframeStandMotion::onInitialize')
        fixture = r'''
#include <cassert>
#include <sstream>
struct LLCharacter {};
struct LLMotion { enum LLMotionInitStatus { STATUS_FAILURE, STATUS_HOLD, STATUS_SUCCESS }; };
struct LLPose { bool complete=true; int lookups=0; int joint=0;
 int* findJointState(const char*) { ++lookups; return complete?&joint:nullptr; } };
struct LLKeyframeMotion:LLMotion { LLMotionInitStatus status=STATUS_HOLD; LLPose pose;
 LLMotionInitStatus onInitialize(LLCharacter*) { return status; }
 LLPose* getPose() { return &pose; } };
std::ostringstream sink;
#define LL_INFOS() sink
#define LL_ENDL "\n"
struct LLKeyframeStandMotion:LLKeyframeMotion { LLCharacter* mCharacter=nullptr; bool mFlipFeet=true;
 int *mPelvisState=nullptr,*mHipLeftState=nullptr,*mKneeLeftState=nullptr,*mAnkleLeftState=nullptr,
 *mHipRightState=nullptr,*mKneeRightState=nullptr,*mAnkleRightState=nullptr;
 const char* getName() { return "stand"; }
 LLMotionInitStatus onInitialize(LLCharacter*);
};
PRODUCTION
int main() {
 LLCharacter avatar; LLKeyframeStandMotion motion;
 assert(motion.onInitialize(&avatar)==LLMotion::STATUS_HOLD);
 assert(motion.pose.lookups==0 && motion.mCharacter==&avatar);
 motion.status=LLMotion::STATUS_FAILURE;
 assert(motion.onInitialize(&avatar)==LLMotion::STATUS_FAILURE && motion.pose.lookups==0);
 motion.status=LLMotion::STATUS_SUCCESS;
 assert(motion.onInitialize(&avatar)==LLMotion::STATUS_SUCCESS && motion.pose.lookups==7);
 assert(motion.mAnkleRightState && motion.mPelvisState);
 motion.pose.complete=false;
 assert(motion.onInitialize(&avatar)==LLMotion::STATUS_FAILURE);
}
'''.replace('PRODUCTION', production)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp = Path(directory)/'stand_loading.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(run.returncode, 0, run.stdout+run.stderr)


if __name__ == '__main__':
    unittest.main()
