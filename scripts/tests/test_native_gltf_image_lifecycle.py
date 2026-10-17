"""Execute actual glTF preparation/release with a late completion and index reuse."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeGLTFImageLifecycleTests(unittest.TestCase):
    def test_completed_probe_restores_settings_before_other_contracts(self):
        text=(ROOT/'indra/newview/vsr3gltfreplay.cpp').read_text()
        restore=method(text,'void restoreSettings()')
        complete=method(text,'if(s.step==8)')
        source=r"""
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct LLSD {std::map<std::string,LLSD> children;bool value=false;
 LLSD& operator[](const char* key){return children[key];}LLSD& operator=(bool v){value=v;return *this;}};
struct Settings {std::map<std::string,double> values;
 void setF32(const char* k,double v){values[k]=v;}void setU32(const char* k,unsigned v){values[k]=v;}
 void setBOOL(const char* k,bool v){values[k]=v;}}gSavedSettings;
struct Window{bool minimized=true;void restore(){minimized=false;}};
struct Viewer{Window win;Window* getWindow(){return &win;}}viewer,*gViewerWindow=&viewer;
struct State {unsigned step=8,limit=2048;double minimizedTime=10;bool full=false,compression=true,anisotropy=false;
 std::vector<std::weak_ptr<int>> images;LLSD evidence;RESTORE};
struct World{bool present=true;bool object(int){return present;}};
void require(bool ok,const char*){assert(ok);}
bool completed(State& s,World& world,LLSD& report){const int objectID=1;COMPLETE;return false;}
int main(){State state;World world;LLSD report;
 gSavedSettings.values["RenderMaxTextureResolution"]=512;
 assert(!completed(state,world,report));assert(gSavedSettings.values["RenderMaxTextureResolution"]==512);
 world.present=false;auto queued=std::make_shared<int>(1);state.images.push_back(queued);
 assert(!completed(state,world,report));assert(gSavedSettings.values["RenderMaxTextureResolution"]==512);
 queued.reset();assert(completed(state,world,report));
 assert(gSavedSettings.values["RenderMaxTextureResolution"]==2048 && !viewer.win.minimized);
 assert(gSavedSettings.values["TextureDiscardMinimizedTime"]==10);
 assert(gSavedSettings.values["RenderCompressTextures"]==1 && gSavedSettings.values["RenderAnisotropic"]==0);
 assert(report["r3_gltf_images"]["object_retired"].value);
}
""".replace('RESTORE',restore).replace('COMPLETE',complete)
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'completion.cpp';path.write_text(source);exe=Path(temp)/'completion.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)

    def test_inactive_images_release_and_late_work_cannot_replace_new_preparation(self):
        text = (ROOT / 'indra/newview/vsworldgltf.cpp').read_text()
        prepare = method(text, 'bool prepareImage(unsigned index,')
        release = method(text, 'void VSWorldGLTF::releaseRenderingResources()')
        source = r'''
#include "vsresourcebudget.h"
#include <cassert>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
using U64=unsigned long long;
template<class T>using LLPointer=std::shared_ptr<T>;
struct LLImageRaw {int tag,width,height;int getWidth()const{return width;}int getHeight()const{return height;}};
namespace VSWorldContracts {enum class ImageRole{RemoteAsset};}
namespace LL {
struct WorkQueue {
 using ptr_t=std::shared_ptr<WorkQueue>;
 static ptr_t getInstance(const char*){static auto queue=std::make_shared<WorkQueue>();return queue;}
 std::deque<std::function<void()>> jobs;
 template<class Work,class Done>bool postTo(ptr_t,Work work,Done done){jobs.push_back([work,done]{done(work());});return true;}
 void runOne(){auto job=std::move(jobs.front());jobs.pop_front();job();}
};
}
struct VSUIResources {
 using PreparedImage=std::shared_ptr<const int>;
 VSResourceBudget budget{40000};std::vector<std::string> erased;
 auto reserveImagePreparation(std::size_t bytes){return budget.reserve(bytes);}
 static PreparedImage prepareImage(const LLImageRaw& raw,VSWorldContracts::ImageRole,bool){return std::make_shared<int>(raw.tag);}
 void erase(const std::string& key){erased.push_back(key);}
};
struct VSWorldGLTF {
 struct Impl:std::enable_shared_from_this<Impl> {
  struct ImageUpdate {bool preparing=false;VSUIResources::PreparedImage prepared;std::shared_ptr<const VSResourceBudget::Reservation> residency;};
  VSUIResources resources;U64 imageEpoch=1;std::string error;bool publicationKnown=true;
  std::map<unsigned,ImageUpdate> imageUpdates;std::map<unsigned,std::string> images;
  std::map<unsigned,std::shared_ptr<int>> publications;std::vector<std::shared_ptr<int>> publishedPackets;
  bool current()const{return true;}
PREPARE
 };
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 void releaseRenderingResources();
};
RELEASE
int main(){
 VSWorldGLTF owner;auto state=owner.mImpl;auto queue=LL::WorkQueue::getInstance("mainloop");
 auto retained=std::make_shared<int>(91);state->publications[0]=retained;state->publishedPackets.push_back(retained);state->images[0]="old";
 auto old=std::make_shared<LLImageRaw>(LLImageRaw{11,2,2});
 assert(state->prepareImage(0,old,false));const auto oldBytes=state->resources.budget.used();assert(oldBytes>0);
 owner.releaseRenderingResources();assert(state->imageEpoch==2 && !state->publicationKnown && state->imageUpdates.empty());
 assert(state->images.empty() && state->publications.empty() && state->publishedPackets.empty() && *retained==91 && retained.use_count()==1);
 assert(state->resources.erased==std::vector<std::string>{"old"});
 owner.releaseRenderingResources();assert(state->imageEpoch==2);
 auto next=std::make_shared<LLImageRaw>(LLImageRaw{22,3,3});assert(state->prepareImage(0,next,true));
 const auto combined=state->resources.budget.used();assert(combined>oldBytes);
 queue->runOne();assert(state->imageUpdates[0].preparing && !state->imageUpdates[0].prepared);
 assert(state->resources.budget.used()==combined-oldBytes);
 queue->runOne();assert(!state->imageUpdates[0].preparing && *state->imageUpdates[0].prepared==22);
 owner.releaseRenderingResources();assert(state->resources.budget.used()==0 && state->imageEpoch==3);
}
'''.replace('PREPARE', prepare).replace('RELEASE', release)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / 'gltf_images.cpp'
            cpp.write_text(source)
            result = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror',
                '-I', str(ROOT / 'indra/newview'), str(cpp), '-o', str(directory / 'images.exe')],
                capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(directory / 'images.exe')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
