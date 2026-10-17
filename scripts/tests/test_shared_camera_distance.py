"""Compare renderer-peer camera and interest-list policy with the original viewer."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
from test_native_camera_publication import method


class SharedCameraDistance(unittest.TestCase):
    def test_settings_update_actual_camera_owner_without_entering_gl_world(self):
        current = method((ROOT / 'indra/newview/llviewercontrol.cpp').read_text(encoding='utf-8'),
                         'static bool handleRenderFarClipChanged(')
        source = r'''
#include <cassert>
using F32=float;
#define VS_NATIVE_VULKAN 1
constexpr int STATE_STARTED=10;
struct LLSD {double value;double asReal()const{return value;}};
struct LLStartUp {inline static int state=0;static int getStartupState(){return state;}};
struct AgentCamera {F32 mDrawDistance=128;}gAgentCamera;
struct Window {bool native=false;bool isNativeVulkan()const{return native;}}window,*gViewerWindow=&window;
struct LLAppViewer {
 bool native=false;static LLAppViewer* instance(){static LLAppViewer app;return &app;}
 bool isNativeVulkanLogin()const{return native;}
};
struct LLWorld {
 int calls=0;F32 farClip=0;static LLWorld* getInstance(){static LLWorld w;return &w;}
 void setLandFarClip(F32 distance){farClip=distance;++calls;}
};
CURRENT
int main(){
 assert(!handleRenderFarClipChanged({512}));assert(gAgentCamera.mDrawDistance==128&&LLWorld::getInstance()->calls==0);
 LLStartUp::state=STATE_STARTED;window.native=true;
 assert(handleRenderFarClipChanged({2048}));assert(gAgentCamera.mDrawDistance==2048&&LLWorld::getInstance()->calls==0);
 // Settings callbacks remain renderer-scoped after the native window retires.
 gViewerWindow=nullptr;LLAppViewer::instance()->native=true;
 assert(handleRenderFarClipChanged({4096}));assert(gAgentCamera.mDrawDistance==4096&&LLWorld::getInstance()->calls==0);
 gViewerWindow=&window;window.native=false;LLAppViewer::instance()->native=false;
 assert(handleRenderFarClipChanged({256}));assert(gAgentCamera.mDrawDistance==256&&LLWorld::getInstance()->calls==1);
 assert(LLWorld::getInstance()->farClip==256);
}
'''.replace('CURRENT', current)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'settings.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(source, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_draw_distance_locks_customization_memory_and_interest_ramp(self):
        current = (ROOT / 'indra/newview/vscameraview.cpp').read_text(encoding='utf-8')
        current = '\n'.join(line for line in current.splitlines() if not line.startswith('#include'))
        source = (HERE / 'camera_distance_fixture.cpp').read_text(encoding='utf-8')
        source = source.replace('REFERENCE', (HERE / 'camera_far_reference.cpp').read_text(encoding='utf-8'))
        source = source.replace('CURRENT', current)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'camera.cpp'
            exe = cpp.with_suffix('.exe')
            cpp.write_text(source, encoding='utf-8')
            built = subprocess.run([compiler, '-std=c++17', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == '__main__':
    unittest.main()
