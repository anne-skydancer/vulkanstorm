import pathlib, shutil, subprocess, tempfile, unittest
from test_native_simulator_features import method
ROOT = pathlib.Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'indra/newview/vsworldassets.cpp'
class MeshAssetBounds(unittest.TestCase):
    def test_real_section_admission(self):
        source = SOURCE.read_text()
        begin = source.index('bool section(')
        end = source.index('\n}\n}', begin) + 2
        function = source[begin:end]
        fixture = r'''
#include <map>
#include <string>
#include <cstddef>
#include <cassert>
using S32=int;
struct LLSD { int number=0; bool map=false, integer=false; std::map<std::string,LLSD> data;
 const LLSD& operator[](const char* key)const { static LLSD missing; auto i=data.find(key); return i==data.end()?missing:i->second; }
 bool has(const char* key)const{return data.count(key)!=0;} bool isMap()const{return map;} bool isInteger()const{return integer;} int asInteger()const{return number;}
};
''' + function + r'''
int main(){LLSD h; h.data["high_lod"].map=true;
 auto& s=h.data["high_lod"]; s.data["offset"].integer=true; s.data["size"].integer=true;
 size_t offset=0,length=0; s.data["size"].number=32;
 assert(section(h,"high_lod",64,96,offset,length)&&offset==64&&length==32);
 s.data["offset"].number=1; assert(!section(h,"high_lod",64,96,offset,length));
 s.data["offset"].number=-1; assert(!section(h,"high_lod",64,96,offset,length));
 s.data["offset"].number=0;s.data["size"].number=0;assert(!section(h,"high_lod",64,96,offset,length));
 s.data["size"].number=32; assert(!section(h,"high_lod",97,96,offset,length));
 assert(!section(h,"skin",64,96,offset,length));
 s.data["offset"].integer=false;assert(!section(h,"high_lod",64,96,offset,length));
 assert(meshVersionAllowed(h)); h.data["version"].number=999;assert(meshVersionAllowed(h));
 h.data["version"].number=1000;assert(!meshVersionAllowed(h));
 s.data["offset"].integer=true;assert(meshLOD(h,0,64,96,offset,length)==3);
 h.data["low_lod"]=s;assert(meshLOD(h,2,64,96,offset,length)==1);
 assert(meshLOD(h,3,64,96,offset,length)==3);
 s.data["offset"].number=2;assert(meshLOD(h,2,64,96,offset,length)==-1);
 s.data["size"].number=0;assert(meshLOD(h,3,64,96,offset,length)==1);
 assert(meshLOD(h,4,64,96,offset,length)==-1);
 h.data["low_lod"].data["size"].number=-1;assert(meshLOD(h,1,64,96,offset,length)==-1);
}
'''
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=pathlib.Path(directory)/'bounds.cpp'; cpp.write_text(fixture)
            exe=pathlib.Path(directory)/'bounds.exe'
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode,0)
    def test_requests_only_queue_and_reconcile_before_insert(self):
        s=SOURCE.read_text(); request=s[s.index('VSWorldAssets::State VSWorldAssets::request'):s.index('std::shared_ptr<const VSWorldAssets::Geometry> VSWorldAssets::geometry')]
        self.assertNotIn('pump();',request)
        self.assertLess(request.index('synchronizeOwner(owner)'),request.index('entries.emplace'))
        self.assertIn('face.mNumIndices % 3 != 0',s)
    def test_publication_rechecks_account_region_and_request_identity(self):
        s=SOURCE.read_text(); resume=s[s.index('auto state = weak.lock();'):]
        for guard in ['VSNativeSession::active() != owner','owner->generation() != generation','owner->regionEpoch() != epoch','pending->second != adapter','found->second.revision != revision','state->budget.reserve(decoded->bytes)']:
            self.assertIn(guard,resume)
        self.assertLess(resume.index('owner->regionEpoch() != epoch'),resume.index('decoded = decode'))
        self.assertIn('WorkQueue::getInstance("mainloop")->post(',s)

    def test_actual_resource_reservation_outlives_cache_and_budget_owner(self):
        header = (ROOT/'indra/newview/vsresourcebudget.h').as_posix()
        self.compile(f'#include "{header}"\n' + r'''
#include <cassert>
#include <limits>
int main(){
 VSResourceBudget budget(32);
 auto a=budget.reserve(20);assert(a&&a->bytes()==20&&budget.used()==20);
 assert(!budget.reserve(13));assert(!budget.reserve(0));
 assert(!budget.reserve(std::numeric_limits<size_t>::max()));
 auto scene=a;a.reset();assert(budget.used()==20&&!budget.reserve(13));
 auto b=budget.reserve(12);assert(b&&budget.used()==32);
 scene.reset();assert(budget.used()==12);
 auto replacement=budget.reserve(20);assert(replacement&&budget.used()==32);
 replacement.reset();b.reset();assert(budget.used()==0);
 std::shared_ptr<const VSResourceBudget::Reservation> submitted;
 {VSResourceBudget retired(8);submitted=retired.reserve(8);assert(submitted);}
 submitted.reset(); // A submitted generation can retire after its cache owner.
}
''')

    def compile(self, fixture):
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=pathlib.Path(directory)/'lifecycle.cpp';cpp.write_text(fixture)
            exe=cpp.with_suffix('.exe')
            built=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_mainloop_asset_delivery_rejects_retired_owners_and_generations(self):
        source=SOURCE.read_text()
        delivery=method(source,'[weak, session, adapter, key, params, generation, epoch, revision, response]()')
        lifecycle='\n'.join(method(source,signature) for signature in (
            'void VSWorldAssets::synchronizeOwner(', 'void VSWorldAssets::replace('))
        header=(ROOT/'indra/newview/vsresourcebudget.h').as_posix()
        fixture=f'#include "{header}"\n'+r'''
#include <cassert>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <utility>
using U64=unsigned long long;
struct LLUUID {int value=0;LLUUID(int id):value(id){};
 operator int()const{return value;}
 bool operator!=(const LLUUID& other)const{return value!=other.value;}};
using Key=std::pair<int,int>;
struct LLSD {const LLSD& operator[](const char*)const{return *this;}
 std::vector<unsigned char> asBinary()const{return {};}};
bool success=true,retryable=true;int decodes=0,cancels=0;
struct Status {operator bool()const{return success;}bool isRetryable()const{return !success&&retryable;}};
struct Adapter {void cancelSuspendedOperation(){++cancels;}};
struct LLFrameTimer {static double getTotalSeconds(){return 10;}};
namespace LLCoreHttpUtil {struct HttpCoroutineAdapter {
 using ptr_t=std::shared_ptr<Adapter>;
 static Status getStatusFromLLSD(const LLSD&){return {};}
 static constexpr const char* HTTP_RESULTS_RAW="raw";};}
struct VSNativeSession {
 enum class Phase {Connected,Logout}; U64 account=1,region=2;Phase current=Phase::Connected;
 U64 generation()const{return account;}U64 regionEpoch()const{return region;}
 Phase phase()const{return current;}
 static std::shared_ptr<VSNativeSession> live;
 static std::shared_ptr<VSNativeSession> active(){return live;}
};
std::shared_ptr<VSNativeSession> VSNativeSession::live;
enum class State {Pending,Ready,Error};
struct Geometry {size_t bytes=20;U64 generation=0;
 std::shared_ptr<const VSResourceBudget::Reservation> residency;};
std::shared_ptr<const Geometry> decode(int,int,const std::vector<unsigned char>&,std::string&){
 ++decodes;return std::make_shared<Geometry>();}
struct Entry {U64 revision=3;State state=State::Pending;std::string error;
 unsigned attempts=1;double retryAt=0;std::shared_ptr<const Geometry> geometry;};
struct Impl {bool retired=false;std::map<Key,std::shared_ptr<Adapter>> requests;
 std::weak_ptr<VSNativeSession> owner;U64 generation=0,epoch=0,nextRevision=10,replacements=0;
 std::map<Key,Entry> entries;VSResourceBudget budget{32};bool evict(const Key*){return false;}};
struct VSWorldAssets {using State=::State;std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 void synchronizeOwner(const std::shared_ptr<VSNativeSession>&);
 void replace(const LLUUID&);
 void reset(){mImpl->entries.clear();mImpl->requests.clear();mImpl->owner.reset();}
};
LIFECYCLE
int main(){
 auto state=std::make_shared<Impl>();auto owner=std::make_shared<VSNativeSession>();
 VSNativeSession::live=owner;
 std::weak_ptr<Impl> weak=state;std::weak_ptr<VSNativeSession> session=owner;
 auto adapter=std::make_shared<Adapter>();Key key{7,0};int params=0;
 U64 generation=1,epoch=2,revision=3;LLSD response;
 auto prepare=[&]{state->requests[key]=adapter;state->entries[key]=Entry{};};
 prepare();std::function<void()> completion=DELIVERY;
 assert(decodes==0); // Delivery is queued; coroutine completion does not publish.
 owner->account=9;completion();assert(decodes==0);owner->account=1;
 owner->region=9;completion();assert(decodes==0);owner->region=2;
 owner->current=VSNativeSession::Phase::Logout;completion();assert(decodes==0);
 owner->current=VSNativeSession::Phase::Connected;
 state->retired=true;completion();assert(decodes==0);state->retired=false;
 VSNativeSession::live=std::make_shared<VSNativeSession>();completion();assert(decodes==0);
 VSNativeSession::live=owner;
 state->entries[key].revision=4;completion();assert(decodes==0);
 prepare();state->requests[key]=std::make_shared<Adapter>();completion();assert(decodes==0);
 prepare();state->entries.erase(key);completion();assert(decodes==0);
 prepare();completion();assert(decodes==1&&state->requests.empty());
 assert(state->entries[key].state==State::Ready&&state->entries[key].geometry->generation==3);
 auto scene=state->entries[key].geometry;assert(state->budget.used()==20);
 prepare();completion();assert(decodes==2&&state->entries[key].state==State::Error);
 assert(!state->entries[key].geometry&&state->budget.used()==20);
 scene.reset();assert(state->budget.used()==0);
 prepare();success=false;completion();assert(decodes==2&&state->entries[key].retryAt==12);
 prepare();state->entries[key].attempts=3;completion();assert(state->entries[key].retryAt==0);
 prepare();retryable=false;completion();assert(state->entries[key].retryAt==0&&decodes==2);
 success=true;prepare();VSNativeSession::live.reset();owner.reset();completion();assert(decodes==2);
 state.reset();completion();assert(decodes==2);
 // Execute the real region reconciliation and local replacement methods.
 VSWorldAssets assets;owner=std::make_shared<VSNativeSession>();assets.synchronizeOwner(owner);
 auto ready=std::make_shared<Geometry>();ready->residency=assets.mImpl->budget.reserve(20);
 assets.mImpl->entries[key].geometry=ready;assets.mImpl->entries[key].state=State::Ready;
 Key loading{8,0};assets.mImpl->entries[loading]=Entry{};
 assets.mImpl->requests[loading]=adapter;
 owner->region++;assets.synchronizeOwner(owner);
 assert(assets.mImpl->entries[key].geometry==ready&&cancels==1);
 assert(assets.mImpl->entries[loading].revision==10&&assets.mImpl->requests.empty());
 assets.mImpl->requests[key]=adapter;assets.replace(LLUUID{7});
 assert(cancels==2&&!assets.mImpl->entries[key].geometry);
 assert(assets.mImpl->entries[key].revision==11&&assets.mImpl->replacements==1);
 assert(assets.mImpl->budget.used()==20); // An old scene still owns ready.
 owner->account++;assets.synchronizeOwner(owner);assert(assets.mImpl->entries.empty());
 assert(assets.mImpl->budget.used()==20);ready.reset();assert(assets.mImpl->budget.used()==0);
}
'''.replace('DELIVERY',delivery).replace('LIFECYCLE',lifecycle)
        self.compile(fixture)
    def test_actual_shared_decoder_and_expansion_admission(self):
        s=SOURCE.read_text()
        self.assertIn('LLSDSerialize::fromBinary',s)
        self.assertIn('unpackVolumeFaces',s)
        self.assertIn('LLUZipHelper::unzip_llsd',s)
        self.assertLess(s.index('inflatedBound(bytes.data() + offset, length)'),s.index('unpackVolumeFaces'))
        self.assertIn('face.mIndices[i] >= face.mNumVertices',s)
if __name__=='__main__': unittest.main()
