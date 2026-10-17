"""Actual cache callbacks must reject replaced native operations, including failure replies."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeCacheOperations(unittest.TestCase):
    def test_actual_read_and_write_completions_are_operation_scoped(self):
        text=(ROOT/'indra/newview/lltexturefetch.cpp').read_text()
        read=method(text,'class CacheReadResponder')+';';write=method(text,'class CacheWriteResponder')+';'
        callbacks='\n'.join(method(text,'void LLTextureFetchWorker::'+name) for name in ('callbackCacheRead','callbackCacheWrite'))
        fixture=r'''
#include <cassert>
#include <cstdint>
#include <sstream>
using U64=std::uint64_t;using S32=int;using LLUUID=int;
#define LL_PROFILE_ZONE_SCOPED
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE
#define llassert_always assert
#define LL_INFOS(...) logOutput
#define LL_ENDL std::endl
std::ostringstream logOutput;
struct LLMutexLock {LLMutexLock(int*){}};
struct LLImageFormatted {int getCodec(){return 1;}int getDataSize(){return 1206;}};
struct LLTextureCache {
 struct ReadResponder {LLImageFormatted* mFormattedImage=nullptr;int mImageSize=1206;bool mImageLocal=false;
  virtual ~ReadResponder()=default;virtual void completed(bool)=0;void setImage(LLImageFormatted* value){mFormattedImage=value;}};
 struct WriteResponder {virtual ~WriteResponder()=default;virtual void completed(bool)=0;};
};
class LLTextureFetchWorker;
struct LLTextureFetch {LLTextureFetchWorker* current=nullptr;LLTextureFetchWorker* getWorker(int){return current;}};
class LLTextureFetchWorker {
public:
 static constexpr int LOAD_FROM_TEXTURE_CACHE=3,WAIT_ON_WRITE=13;
 U64 mNativeCacheReadOperation=0,mNativeCacheWriteOperation=0;
 int mWorkMutex=0,mState=LOAD_FROM_TEXTURE_CACHE,mFileSize=0,mImageCodec=0,mID=7;
 bool mInLocalCache=false,mHaveAllData=false,mLoaded=false,mWritten=false;
 LLImageFormatted* mFormattedImage=nullptr;
 void callbackCacheRead(bool,LLImageFormatted*,S32,bool,U64=0);void callbackCacheWrite(bool,U64=0);
 READ_CLASS
 WRITE_CLASS
};
CALLBACKS
int main(){
 LLTextureFetch fetch;LLTextureFetchWorker first,second;LLImageFormatted image;
 first.mNativeCacheReadOperation=11;first.mNativeCacheWriteOperation=21;
 second.mNativeCacheReadOperation=12;second.mNativeCacheWriteOperation=22;
 LLTextureFetchWorker::CacheReadResponder staleRead(&fetch,7,&image,11),currentRead(&fetch,7,&image,12);
 LLTextureFetchWorker::CacheWriteResponder staleWrite(&fetch,7,21),currentWrite(&fetch,7,22);
 fetch.current=&second;staleRead.completed(false);assert(!second.mLoaded&&!second.mFormattedImage);
 currentRead.completed(true);assert(second.mLoaded&&second.mFormattedImage==&image&&second.mHaveAllData);
 second.mLoaded=false;second.mNativeCacheReadOperation=13;currentRead.completed(false);assert(!second.mLoaded);
 second.mState=LLTextureFetchWorker::WAIT_ON_WRITE;staleWrite.completed(true);assert(!second.mWritten);
 currentWrite.completed(true);assert(second.mWritten);second.mWritten=false;
 second.mNativeCacheWriteOperation=23;currentWrite.completed(false);assert(!second.mWritten);
 fetch.current=nullptr;currentRead.completed(true);currentWrite.completed(true);
 // Existing OpenGL responders have no native operation and preserve their behavior.
 fetch.current=&first;first.mNativeCacheReadOperation=0;first.mNativeCacheWriteOperation=0;
 LLTextureFetchWorker::CacheReadResponder legacyRead(&fetch,7,&image);legacyRead.completed(true);assert(first.mLoaded);
 first.mState=LLTextureFetchWorker::WAIT_ON_WRITE;
 LLTextureFetchWorker::CacheWriteResponder legacyWrite(&fetch,7);legacyWrite.completed(true);assert(first.mWritten);
}
'''.replace('READ_CLASS',read).replace('WRITE_CLASS',write).replace('CALLBACKS',callbacks)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'cache_operations.cpp';path.write_text(fixture);exe=Path(temp)/'cache_operations.exe'
            result=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-Wno-unused-parameter',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
