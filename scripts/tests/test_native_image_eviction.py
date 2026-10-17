"""Exercise production image eviction with hidden facades and live GPU owners."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT = Path(__file__).resolve().parents[2]


class NativeImageEvictionTests(unittest.TestCase):
    def test_shared_world_demand_keeps_peak_until_consumers_finish(self):
        text = (ROOT / 'indra/newview/vsuiimageprovider.cpp').read_text()
        request = method(text, 'LLUIImagePtr VSUIImageProvider::requestWorldImage(const LLUUID& id,unsigned dimension,bool exact)')
        request = request.replace('VSUIImageProvider::', '')
        source = r"""
#include <algorithm>
#include <cassert>
#include <map>
#include <memory>
#include <vector>
struct LLUUID {int id;bool isNull()const{return !id;}bool operator<(LLUUID rhs)const{return id<rhs.id;}};
using LLUIImagePtr=std::shared_ptr<int>;
struct Remote {bool exactWorld=false;unsigned target=64,previousTarget=0,frameTarget=0;unsigned long long demandFrame=0;};
struct Impl {std::map<LLUUID,Remote> remote;unsigned long long demandFrame=0;bool evict(LLUUID,bool){return false;}};
struct Provider {
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();std::vector<unsigned> submitted;
 LLUIImagePtr getUIImageByID(LLUUID id,int){submitted.push_back(mImpl->remote[id].target);return {};}
 REQUEST
};
int main(){
 Provider p;LLUUID id{1};
 p.requestWorldImage(id,1024,false);assert(p.submitted.back()==1024);
 ++p.mImpl->demandFrame;
 p.requestWorldImage(id,32,false);assert(p.submitted.back()==1024);
 p.requestWorldImage(id,256,false);assert(p.submitted.back()==1024);
 p.requestWorldImage(id,1024,false);assert(p.submitted.back()==1024);
 ++p.mImpl->demandFrame;
 p.requestWorldImage(id,32,false);assert(p.submitted.back()==1024);
 ++p.mImpl->demandFrame;
 p.requestWorldImage(id,32,false);assert(p.submitted.back()==32);
 p.requestWorldImage(id,256,true);assert(p.submitted.back()==256 && p.mImpl->remote[id].exactWorld);
 p.requestWorldImage(id,1,false);assert(p.submitted.back()==256 && p.mImpl->remote[id].exactWorld);
 p.requestWorldImage({2},0,false);assert(p.submitted.back()==1);
 p.requestWorldImage({3},9000,false);assert(p.submitted.back()==4096);
 auto size=p.submitted.size();p.requestWorldImage({0},64,false);assert(p.submitted.size()==size);
}
""".replace('REQUEST', request)
        self.compile_and_run(source)

    def test_inactive_world_publications_release_all_image_owners(self):
        text = (ROOT / 'indra/newview/vsworldscene.cpp').read_text()
        release = method(text, 'void releaseRenderingResources()')
        source = r'''
#include <array>
#include <cassert>
#include <memory>
#include <vector>
struct Face {
 std::shared_ptr<int> mesh,publishedImage;
 std::shared_ptr<int> pbrAsset;
 std::array<std::shared_ptr<int>,4> publishedChannels;
 int material=17;
};
struct GLTF {int calls=0;void releaseRenderingResources(){++calls;}};
struct Object {
 std::vector<Face> faces,admittedFaces;
 std::shared_ptr<GLTF> gltf=std::make_shared<GLTF>(),pendingGLTF=std::make_shared<GLTF>();
 std::shared_ptr<int> cpuGeometry=std::make_shared<int>(23);
 bool dirty=false;
 RELEASE
};
int main(){
 Object object;Face face;
 face.mesh=std::make_shared<int>(1);face.publishedImage=std::make_shared<int>(2);
 face.pbrAsset=std::make_shared<int>(4);
 for(auto& channel:face.publishedChannels)channel=std::make_shared<int>(3);
 object.faces.push_back(face);object.admittedFaces.push_back(face);
 const auto cpu=object.cpuGeometry;
 auto queued=face.publishedImage;std::weak_ptr<int> mesh=face.mesh,base=queued,normal=face.publishedChannels[0],material=face.pbrAsset;
 face={};object.releaseRenderingResources();
 assert(object.dirty && object.admittedFaces.empty());
 assert(mesh.expired() && normal.expired() && material.expired() && !base.expired());
 assert(!object.faces[0].mesh && !object.faces[0].publishedImage);
 for(const auto& channel:object.faces[0].publishedChannels)assert(!channel);
 assert(object.faces[0].material==17 && object.cpuGeometry==cpu && *cpu==23);
 assert(object.gltf->calls==1 && object.pendingGLTF->calls==1);
 queued.reset();assert(base.expired()); // The submission owns its real lifetime.
 object.releaseRenderingResources();assert(object.gltf->calls==2);
}
'''.replace('RELEASE', release)
        self.compile_and_run(source)

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
 bool local=false,fetching=false,preparing=false,prepared=false,uiDemand=true,hasUploadPolicy=true,exactWorld=false;
 int discard=0,generation=7,readyGeneration=7,failedGeneration=7,attempts=2;
 double lastUsed=90,retryAt=0;int touched=0;
 LLUIImagePtr facade=std::make_shared<Image>(),pixels=std::make_shared<Image>();
 std::shared_ptr<int> auxiliary=std::make_shared<int>(1);
};
struct Resources {
 struct Policy{double bias=1;} policy;
 const Policy& textureMemoryPolicy()const{return policy;}
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
 void pump(){const unsigned generation=7;PENDING}
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
 // An inactive view still pumps coarsening for a previously admitted world
 // image. UI and explicit full-resolution subscribers retain their policy.
 entry.lastUsed=90;entry.readyGeneration=7;entry.uiDemand=false;
 impl.resources.policy.bias=4;provider.requests.clear();provider.pump();
 assert(provider.requests.size()==1);
 entry.exactWorld=true;provider.requests.clear();provider.pump();assert(provider.requests.empty());
 entry.exactWorld=false;entry.uiDemand=true;provider.pump();assert(provider.requests.empty());
 impl.resources.policy.bias=1;entry.lastUsed=99.9;
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
        self.compile_and_run(source)

    def compile_and_run(self, source):
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
