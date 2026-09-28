"""Exercise production deferred mesh failure routing and queue/accounting behavior."""
from pathlib import Path
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
s=(ROOT/'indra/newview/llmeshrepository.cpp').read_text()
a=s.index('bool deferMeshFailure(')
b=s.index('\n}\n}',a)+2
helper=s[a:b]
a=s.index('        const F64 retry_now =')
b=s.index("        // Match upstream's worker priority",a)
drain=s[a:b]
classes=['LLMeshHeaderHandler','LLMeshLODHandler','LLMeshSkinInfoHandler','LLMeshDecompositionHandler','LLMeshPhysicsShapeHandler']
fixture=r"""
#include <cassert>
#include <map>
#include <queue>
#include <deque>
#include <set>
#include <functional>
#include <iostream>
using F64=double;
struct LLTimer { static inline double now=0; static double getTotalSeconds(){return now;} };
namespace LLCore {struct HttpStatus {
 int value; explicit HttpStatus(int v):value(v){}
 bool isRetryable()const{return value==-28 || (value>=500 && value<=599);}
 bool operator!=(HttpStatus r)const{return value!=r.value;}
 int toTerseString()const{return value;}
};}
struct LLMutexLock{explicit LLMutexLock(int*){}};
#define LL_INFOS(x) std::cout
#define LL_ENDL std::endl
struct LLMeshRepository {static inline int sHTTPRetryCount=0,sLODProcessing=0;};
struct Thread {
 std::multimap<double,std::function<void()>> mDeferredRequests;
 std::queue<int> mHeaderReqQ;
 std::queue<std::pair<int,int>> mLODReqQ;
 std::deque<int> mSkinRequests;
 std::set<int> mDecompositionRequests,mPhysicsShapeRequests;
 int* mMutex=nullptr;
 void drain(){
"""+drain+r"""
 }
} thread;
struct Repo {Thread* mThread=&thread;} gMeshRepo;
int unavailable=0;
"""+helper
for cls in classes:
    fixture+=f'\nstruct {cls} {{int mMeshParams=7,mMeshID=9,mLOD=2;double mRetryDelay=30;void processFailure(LLCore::HttpStatus status);}};\n'
    a=s.index(f'void {cls}::processFailure(')
    b=s.index('    LL_WARNS',a)
    fixture+=s[a:b]+'    ++unavailable;\n}\n'
fixture+=r"""
int main(){
 LLMeshHeaderHandler header; LLMeshLODHandler lod; LLMeshSkinInfoHandler skin;
 LLMeshDecompositionHandler decomp; LLMeshPhysicsShapeHandler physics;
 header.processFailure(LLCore::HttpStatus(-28));
 lod.processFailure(LLCore::HttpStatus(503));
 skin.processFailure(LLCore::HttpStatus(429));
 decomp.processFailure(LLCore::HttpStatus(408));
 physics.processFailure(LLCore::HttpStatus(-28));
 assert(unavailable==0 && thread.mDeferredRequests.size()==5);
 assert(thread.mHeaderReqQ.empty() && thread.mLODReqQ.empty() && thread.mSkinRequests.empty());
 thread.mHeaderReqQ.push(1);thread.mLODReqQ.emplace(1,0);thread.mSkinRequests.push_back(1);
 LLMeshRepository::sLODProcessing=1;
 LLTimer::now=29;thread.drain();assert(thread.mDeferredRequests.size()==5);
 LLTimer::now=30;thread.drain();assert(thread.mDeferredRequests.empty());
 assert(thread.mHeaderReqQ.front()==1 && thread.mHeaderReqQ.back()==7);
 assert(thread.mLODReqQ.front().first==1 && thread.mLODReqQ.back().first==7);
 assert(LLMeshRepository::sLODProcessing==2);
 assert(thread.mSkinRequests.front()==1 && thread.mSkinRequests.back()==9);
 assert(thread.mDecompositionRequests.count(9) && thread.mPhysicsShapeRequests.count(9));
 for(int i=0;i<20;++i){lod.processFailure(LLCore::HttpStatus(-28));LLTimer::now+=30;thread.drain();}
 assert(unavailable==0); // Retry exhaustion never marks a transient failure missing.
 header.processFailure(LLCore::HttpStatus(404));lod.processFailure(LLCore::HttpStatus(410));
 assert(unavailable==2 && thread.mDeferredRequests.empty());
 skin.mRetryDelay=120;skin.processFailure(LLCore::HttpStatus(429));
 LLTimer::now+=30;thread.drain();assert(thread.mDeferredRequests.size()==1);
 LLTimer::now+=90;thread.drain();assert(thread.mDeferredRequests.empty());
}
"""
with tempfile.TemporaryDirectory(prefix='mesh-deferred-') as directory:
    p=Path(directory)
    (p/'test.cpp').write_text(fixture)
    (p/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(mesh_deferred LANGUAGES CXX)\nadd_executable(mesh_deferred test.cpp)\ntarget_compile_features(mesh_deferred PRIVATE cxx_std_17)\n')
    subprocess.run(['cmake','-S',str(p),'-B',str(p/'build')],check=True)
    subprocess.run(['cmake','--build',str(p/'build'),'--config','Debug'],check=True)
    exe=p/'build'/('Debug/mesh_deferred.exe' if sys.platform=='win32' else 'mesh_deferred')
    subprocess.run([str(exe)],check=True)
