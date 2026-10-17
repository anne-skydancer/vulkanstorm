"""Execute native shared material publication through refusal and retired account races."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from test_native_simulator_features import method

ROOT=Path(__file__).resolve().parents[2]


class NativeRemoteMaterialTests(unittest.TestCase):
    def test_parse_failure_retry_and_account_retirement(self):
        text=(ROOT/'indra/newview/llgltfmateriallist.cpp').read_text()
        bodies='\n'.join(method(text,signature) for signature in (
            'bool boundedNativeMaterialJSON(',
            'void LLGLTFMaterialList::onNativeAssetLoadComplete(',
            'LLFetchedGLTFMaterial* LLGLTFMaterialList::getNativeMaterial(',
            'void LLGLTFMaterialList::resetNativeMaterials()'))
        source=r'''
#include <cassert>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#define LL_WARNS(x) std::cerr
#define LL_ENDL std::endl
#include <iostream>
using S32=int;
const int LL_ERR_NOERR=0;
struct LLUUID {int value=0;bool operator<(LLUUID other)const{return value<other.value;}};
namespace LLAssetType {enum EType{AT_MATERIAL};}
struct LLTimer {static double now;static double getTotalSeconds(){return now;}};double LLTimer::now=100;
template<class T>struct LLPointer {
 T* value=nullptr;
 LLPointer()=default;LLPointer(T* p):value(p){if(value)++value->refs;}
 LLPointer(const LLPointer& p):LLPointer(p.value){}
 ~LLPointer(){if(value && !--value->refs)delete value;}
 LLPointer& operator=(LLPointer next){std::swap(value,next.value);return *this;}
 T* get()const{return value;}T* operator->()const{return value;}operator T*()const{return value;}
};
struct LLFetchedGLTFMaterial {
 int refs=0,writes=0,completions=0;bool fetching=false,loaded=false;
 int getNumRefs()const{return refs;}bool isFetching()const{return fetching;}bool isLoaded()const{return loaded;}
 void materialBegin(){assert(!fetching);fetching=true;}
 void materialComplete(bool success){assert(fetching);fetching=false;loaded=success;++completions;}
 void setFromModel(int,int){++writes;}
};
struct AssetLoadUserData {
 static int alive;int mModelIn=0;LLPointer<LLFetchedGLTFMaterial> mMaterial;std::function<bool()> current;
 AssetLoadUserData(){++alive;}~AssetLoadUserData(){--alive;}
};int AssetLoadUserData::alive=0;
int epoch=1,activePhase=1;
struct VSNativeSession {
 enum class Phase {Connecting,Connected,Login};Phase phase(){return Phase(activePhase);}
 static VSNativeSession* active(){static VSNativeSession instance;return &instance;}
 auto accountGuard(bool){const int generation=epoch;return [generation]{return generation==epoch;};}
};
namespace LL {
struct WorkQueue {
 using ptr_t=std::shared_ptr<WorkQueue>;
 std::deque<std::function<void()>> jobs;bool refuse=false;
 static ptr_t getInstance(const std::string& name){static auto main=std::make_shared<WorkQueue>(),general=std::make_shared<WorkQueue>();return name=="mainloop"?main:general;}
 template<class Work,class Done>bool postTo(ptr_t other,Work work,Done done){
  if(refuse)return false;
  other->jobs.push_back([this,work,done]{bool result=work();jobs.push_back([done,result]{done(result);});});return true;}
 void run(){while(!jobs.empty()){auto job=std::move(jobs.front());jobs.pop_front();job();}}
};
}
bool decodeResult=true,decodeThrows=false;
bool decodeNativeMaterial(LLUUID,LLAssetType::EType,AssetLoadUserData&){if(decodeThrows)throw std::runtime_error("fixture refusal");return decodeResult;}
struct LLGLTFMaterialList {
 struct NativeMaterial {LLFetchedGLTFMaterial* material=nullptr;unsigned attempts=0;double retryAt=0;};
 std::map<LLUUID,LLPointer<LLFetchedGLTFMaterial>> mList;
 std::map<LLUUID,NativeMaterial> mNativeMaterials;
 static void onNativeAssetLoadComplete(const LLUUID&,LLAssetType::EType,void*,S32);
 static void onAssetLoadComplete(const LLUUID& id,LLAssetType::EType type,void* data,S32 status){onNativeAssetLoadComplete(id,type,data,status);}
 LLFetchedGLTFMaterial* getNativeMaterial(const LLUUID&);
 void resetNativeMaterials();
};
struct Storage {
 struct Request {LLUUID id;LLAssetType::EType type;void(*callback)(const LLUUID&,LLAssetType::EType,void*,S32);void* data;};
 std::deque<Request> pending;int requests=0;
 void getAssetData(LLUUID id,LLAssetType::EType type,decltype(Request::callback) callback,void* data){++requests;pending.push_back({id,type,callback,data});}
 void deliver(int status=0){auto value=pending.front();pending.pop_front();value.callback(value.id,value.type,value.data,status);}
} storage;
Storage* gAssetStorage=&storage;
BODIES
void pump(){LL::WorkQueue::getInstance("General")->run();LL::WorkQueue::getInstance("mainloop")->run();}
int main(){
 assert(boundedNativeMaterialJSON("{\"quoted\":\"[}{]\\\"\"}"));
 assert(!boundedNativeMaterialJSON("") && !boundedNativeMaterialJSON(std::string(1024*1024+1,' ')));
 assert(boundedNativeMaterialJSON(std::string(64,'[')+std::string(64,']')));
 assert(!boundedNativeMaterialJSON(std::string(65,'[')+std::string(65,']')));
 assert(!boundedNativeMaterialJSON("{") && !boundedNativeMaterialJSON("]") && !boundedNativeMaterialJSON("\"unclosed"));
 LLGLTFMaterialList list;
 LLPointer<LLFetchedGLTFMaterial> retired=list.getNativeMaterial({1});
 assert(retired->isFetching() && storage.requests==1);storage.deliver();
 LL::WorkQueue::getInstance("General")->run(); // Decode finished; main publication is pending.
 ++epoch;list.resetNativeMaterials();
 LLPointer<LLFetchedGLTFMaterial> current=list.getNativeMaterial({1});
 assert(current.get()!=retired.get() && current->isFetching());
 LL::WorkQueue::getInstance("mainloop")->run();
 assert(!retired->writes && !retired->completions); // Retired work cannot publish or invoke old subscribers.
 storage.deliver();pump();assert(current->isLoaded() && current->writes==1 && current->completions==1);
 decodeResult=false;
 LLPointer<LLFetchedGLTFMaterial> failed=list.getNativeMaterial({2});storage.deliver();pump();
 assert(!failed->isFetching() && !failed->isLoaded() && !failed->writes && failed->completions==1);
 list.getNativeMaterial({2});assert(storage.requests==3);
 LLTimer::now=105;list.getNativeMaterial({2});assert(storage.requests==4);
 LL::WorkQueue::getInstance("mainloop")->refuse=true;storage.deliver();
 assert(!failed->isFetching() && failed->completions==2 && !AssetLoadUserData::alive);
 LL::WorkQueue::getInstance("mainloop")->refuse=false;
 LLTimer::now=115;list.getNativeMaterial({2});storage.deliver(42);
 assert(failed->completions==3 && !failed->isLoaded());
 LLTimer::now=1000;list.getNativeMaterial({2});assert(storage.requests==5); // Three finite attempts.
 decodeThrows=true;
 LLPointer<LLFetchedGLTFMaterial> threw=list.getNativeMaterial({4});storage.deliver();pump();
 assert(!threw->isLoaded() && !threw->isFetching() && threw->completions==1);
 decodeThrows=false;
 const int requests=storage.requests;activePhase=2;
 auto disconnected=list.getNativeMaterial({8});assert(!disconnected->isFetching() && storage.requests==requests);
 activePhase=1;
 // Registry replacement belongs to its local watcher, not the retired account.
 LLPointer<LLFetchedGLTFMaterial> local=new LLFetchedGLTFMaterial;local->loaded=true;list.mList[{2}]=local;
 assert(list.getNativeMaterial({2})==local.get());list.resetNativeMaterials();
 assert(list.mList.size()==1 && list.mList.find({2})->second.get()==local.get());
 // A shared-cache flush may remove a metadata row before its native marker.
 LLPointer<LLFetchedGLTFMaterial> old=list.getNativeMaterial({3});storage.deliver(42);
 list.mList.erase({3});LLPointer<LLFetchedGLTFMaterial> replacement=list.getNativeMaterial({3});
 assert(replacement.get()!=old.get());storage.deliver(42);
 list.resetNativeMaterials();
 std::vector<LLPointer<LLFetchedGLTFMaterial>> held;
 for(int i=10;i<4106;++i){LLPointer<LLFetchedGLTFMaterial> item=new LLFetchedGLTFMaterial;item->loaded=true;
  list.mList[{i}]=item;list.mNativeMaterials[{i}]={item.get()};held.push_back(item);}
 assert(!list.getNativeMaterial({5000})); // All 4096 entries have real owners.
 held[0]=nullptr;assert(list.getNativeMaterial({5000}));storage.deliver(42);
 assert(list.mNativeMaterials.size()==4096 && !AssetLoadUserData::alive);
 list.resetNativeMaterials();assert(list.mList.size()==1); // Local registry entry survives.
}
'''.replace('BODIES',bodies)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);cpp=root/'materials.cpp';cpp.write_text(source);exe=root/'materials.exe'
            built=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
