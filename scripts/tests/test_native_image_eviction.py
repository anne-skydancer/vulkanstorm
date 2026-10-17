"""Exercise production image eviction with hidden facades and live GPU owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeImageEvictionTests(unittest.TestCase):
    def test_hidden_controls_release_payload_without_losing_facades(self):
        text = (ROOT / 'indra/newview/vsuiimageprovider.cpp').read_text()
        eviction = method(text, 'bool evict(const LLUUID& preserve,bool metadata=false)')
        pump = method(text, 'void VSUIImageProvider::pumpImages()')
        pending = pump[pump.index('std::vector<LLUUID> pending;'):pump.rindex('}')]
        source = r'''
#include <cassert>
#include <map>
#include <memory>
#include <string>
#include <vector>
struct LLUUID {
 int value=0; std::string asString()const{return std::to_string(value);}
 bool operator<(LLUUID other)const{return value<other.value;}
 bool operator==(LLUUID other)const{return value==other.value;}
};
struct LLTimer {static double getTotalSeconds(){return 100.;}};
struct Image {int refs=1;int getNumRefs()const{return refs;}};
using LLUIImagePtr=std::shared_ptr<Image>;
struct Remote {
 bool local=false,fetching=false,preparing=false,prepared=false,uiDemand=true,hasUploadPolicy=true;
 int discard=0,generation=7,readyGeneration=7,failedGeneration=7,attempts=2;
 double lastUsed=90,retryAt=0;int touched=0;
 LLUIImagePtr facade=std::make_shared<Image>(),pixels=std::make_shared<Image>();
 std::shared_ptr<int> auxiliary=std::make_shared<int>(1);
};
struct Resources {
 std::map<std::string,std::shared_ptr<int>> images;
 std::shared_ptr<int> image(const std::string& key){return images[key];}
 void erase(const std::string& key){images.erase(key);}
};
struct Impl {
 std::map<LLUUID,Remote> remote;Resources resources;LLUIImagePtr fallback=std::make_shared<Image>();
 EVICTION
};
struct Provider {
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();std::vector<LLUUID> requests;
 void getUIImageByID(LLUUID id,int){requests.push_back(id);}
 void pump(){PENDING}
};
int main(){
 Provider provider;auto& impl=*provider.mImpl;
 auto& entry=impl.remote[{1}];entry.facade->refs=2;
 auto facade=entry.facade;
 impl.resources.images["remote:1"]=std::make_shared<int>(10);
 std::weak_ptr<int> retired=impl.resources.images["remote:1"];
 assert(impl.evict({99}));
 assert(retired.expired() && impl.remote.size()==1 && entry.facade==facade);
 assert(entry.pixels==impl.fallback && !entry.auxiliary && entry.discard==-1);
 assert(!entry.generation && !entry.readyGeneration && !entry.failedGeneration);
 assert(!entry.attempts && !entry.retryAt && !entry.hasUploadPolicy);
 assert(!impl.evict({99})); // No payload: no spurious successful reclamation.
 provider.pump();assert(provider.requests.empty()); // Retained hidden control is not demand.
 entry.lastUsed=99.9;provider.pump();assert(provider.requests.size()==1);
 impl.resources.images["remote:1"]=std::make_shared<int>(20);
 assert(!impl.evict({99})); // Recently drawn controls remain resident.
 entry.lastUsed=90;
 auto queued=impl.resources.images["remote:1"];
 assert(!impl.evict({99})); // Submission/scene ownership pins the generation.
 queued.reset();
 entry.fetching=true;assert(!impl.evict({99}));entry.fetching=false;
 entry.preparing=true;assert(!impl.evict({99}));entry.preparing=false;
 entry.prepared=true;assert(!impl.evict({99}));entry.prepared=false;
 entry.local=true;assert(!impl.evict({99}));entry.local=false;
 assert(!impl.evict({1}));
 assert(!impl.evict({99},true)); // Metadata cannot detach a held facade.
 entry.facade->refs=1;
 assert(impl.evict({99},true) && impl.remote.empty());
 auto& empty=impl.remote[{2}];empty.auxiliary.reset();empty.lastUsed=99.9;
 assert(!impl.evict({99}));
 assert(impl.evict({99},true) && impl.remote.empty()); // Metadata capacity is distinct.
}
'''.replace('EVICTION', eviction).replace('PENDING', pending)
        compiler = shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); cpp = root / 'eviction.cpp'; cpp.write_text(source)
            exe = root / 'eviction.exe'
            built = subprocess.run([compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', str(cpp), '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
