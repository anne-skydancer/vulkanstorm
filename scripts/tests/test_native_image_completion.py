"""Execute the actual native fetch subscriber under delay and upload pressure."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]


class NativeImageCompletionTest(unittest.TestCase):
    def test_pending_and_decoded_workers_survive_subscriber_backpressure(self):
        source=(ROOT/'indra/newview/vsuiimageprovider.cpp').read_text()
        start=source.index('        S32 discard=-1,state=0;')
        end=source.index('    if(!owner->regionReady()) return;',start)
        body=source[start:end].rstrip()
        self.assertTrue(body.endswith('}'))
        body=body[:-1]  # The surrounding remote-entry loop is provided below.
        fixture=r'''
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
using S32=int;
#define LL_WARNS(...) std::cerr
#define LL_ENDL '\n'
namespace LLCore {constexpr int HE_RESPONSE_TOO_LARGE=100;
struct HttpStatus {enum {LLCORE};int value=0;HttpStatus()=default;HttpStatus(int,int code):value(code){}
 bool operator==(HttpStatus rhs)const{return value==rhs.value;}std::string toString()const{return std::to_string(value);}};}
struct LLImageRaw {int getComponents(){return 4;}template<class T>void copy(T){}int getWidth(){return 1024;}int getHeight(){return 1024;}};
template<class T>struct LLPointer:std::shared_ptr<T>{using std::shared_ptr<T>::shared_ptr;
 LLPointer& operator=(T* p){this->reset(p);return *this;}bool notNull()const{return bool(*this);}};
struct NativeMaskImage:LLImageRaw {NativeMaskImage(bool,int,int){}void copy(LLPointer<LLImageRaw>) {}};
struct LLTimer {static double getTotalSeconds(){return 1000.;}};
struct VSWorldContracts {enum class ImageRole {RemoteAsset};};
struct VSUIResources {static int prepareImage(LLImageRaw&,VSWorldContracts::ImageRole,bool){return 1;}};
struct Remote {bool fetching=true,preparing=false,prepared=false,pendingCacheHit=false;
 int generation=7,epoch=2,discard=-1,components=4,fullWidth=0,fullHeight=0,pendingDiscard=-1;
 int attempts=1,failedGeneration=0;double deadline=20;LLPointer<LLImageRaw> pendingAuxiliary;};
struct Fetch {bool done=false,decoded=false;int deletes=0;LLCore::HttpStatus status;
 bool getRequestFinished(int,int& discard,int& state,LLPointer<LLImageRaw>& raw,LLPointer<LLImageRaw>&,LLCore::HttpStatus& out){
 state=9;out=status;if(decoded){raw=new LLImageRaw;discard=0;}return done;}
 bool getRequestDimensions(int,int& w,int& h,int& c){w=h=1024;c=4;return true;}
 bool isFromLocalCache(int){return true;}void deleteRequest(int,bool){++deletes;}
};
struct Impl {struct Budget{bool reserve(std::size_t){return true;}}masks;
 bool accept=false;int preparations=0;
 bool evict(int){return false;}bool compressionWanted(Remote&){return false;}
 template<class Build>bool beginPreparation(int,Remote& entry,int,int,Build){
 if(!accept)return false;entry.preparing=true;++preparations;return true;}
};
struct Provider {Impl state;Impl* mImpl=&state;
 void pump(Remote& entry,Fetch* fetch){int id=1,generation=7,epoch=2;for(int i=0;i<1;++i){
 BODY
 }}
};
int main(){Provider provider;Remote entry;Fetch fetch;
 provider.pump(entry,&fetch); // Well beyond the former arbitrary 20-second limit.
 assert(entry.fetching&&fetch.deletes==0&&entry.attempts==1);
 fetch.done=fetch.decoded=true;
 for(int i=0;i<5;++i)provider.pump(entry,&fetch);
 assert(entry.fetching&&fetch.deletes==0&&entry.attempts==1&&!entry.failedGeneration);
 provider.state.accept=true;provider.pump(entry,&fetch);
 assert(!entry.fetching&&fetch.deletes==1&&provider.state.preparations==1);
 assert(entry.preparing&&entry.pendingDiscard==0&&entry.pendingCacheHit);
 Remote failed;failed.attempts=3;fetch.decoded=false;
 provider.pump(failed,&fetch);assert(!failed.fetching&&failed.failedGeneration==7);
 Remote retired;retired.epoch=1;provider.pump(retired,&fetch);
 assert(!retired.fetching&&fetch.deletes==3); // Circuit cancellation remains decisive.
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++')
        self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as directory:
            cpp=Path(directory)/'image_completion.cpp';exe=cpp.with_suffix('.exe')
            cpp.write_text(fixture)
            result=subprocess.run([compiler,'-std=c++17',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)


if __name__=='__main__':
    unittest.main()
