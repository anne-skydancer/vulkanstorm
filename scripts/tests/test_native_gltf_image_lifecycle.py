"""Execute actual glTF preparation/release with a late completion and index reuse."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeGLTFImageLifecycleTests(unittest.TestCase):
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
