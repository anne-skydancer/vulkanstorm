"""UI consumers must not retire a publication owned by their shared media source."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class MediaOwnership(unittest.TestCase):
    def test_actual_control_destructor_preserves_shared_source(self):
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        body=method((ROOT/'indra/newview/llmediactrl.cpp').read_text(),'LLMediaCtrl::~LLMediaCtrl()')
        fixture=r'''
#include <cassert>
#include <memory>
#include <string>
#define VS_NATIVE_VULKAN 1
struct Resources {std::shared_ptr<int> image=std::make_shared<int>(7);void erase(const std::string&){image.reset();}};
Resources resources;
struct VSUIDrawBridge {static Resources* resources(){return &::resources;}};
struct ID {std::string asString(){return "test";}};
struct Menu {void die(){}};
struct Handle {Menu* get(){return nullptr;}void markDead(){}};
struct Source {void remObserver(void*){}~Source(){resources.erase("media:test");}};
struct LLMediaCtrl {
 ID mMediaTextureID;std::shared_ptr<int> mNativeMediaImage=resources.image;Handle mContextMenuHandle;
 std::shared_ptr<Source> mMediaSource;~LLMediaCtrl();
};
BODY
int main(){
 auto consumer=std::make_shared<Source>();auto queued=resources.image;
 {LLMediaCtrl control;control.mMediaSource=consumer;}
 assert(resources.image==queued && *resources.image==7);
 consumer.reset();assert(!resources.image);assert(*queued==7);
 queued.reset();
}
'''.replace('BODY',body)
        with tempfile.TemporaryDirectory() as temporary:
            source=Path(temporary)/'media_owner.cpp';source.write_text(fixture);exe=Path(temporary)/'media_owner.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(source),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
if __name__=='__main__':unittest.main()
