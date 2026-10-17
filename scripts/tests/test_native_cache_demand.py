"""Execute the actual cache-post state after demand grows during a disk read."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeCacheDemand(unittest.TestCase):
    def test_complete_short_and_superseded_disk_reads(self):
        source=(ROOT/'indra/newview/lltexturefetch.cpp').read_text()
        body=method(source,'if (mState == CACHE_POST)')
        fixture=r'''
#include <cassert>
#include <sstream>
#include <string>
using S32=int;
#define LL_PROFILE_ZONE_NAMED_CATEGORY_TEXTURE(...)
#define LL_DEBUGS(...) nativeLog
#define LL_INFOS(...) nativeLog
#define LL_WARNS(...) nativeLog
#define LL_ENDL std::endl
#define llassert_always assert
std::ostringstream nativeLog;
std::string llformat(const char*,int,int){return {};}
struct LLUnits{struct Ratio{static int fromValue(int v){return v;}};};
struct LLTextureFetch{static constexpr int sCacheHitRate=0;bool mNativeConsumer=true,cache=true;bool canLoadFromCache(){return cache;}};
void record(int,int){}
struct Formatted{int bytes=600;bool notNull(){return true;}Formatted* operator->(){return this;}int getDataSize(){return bytes;}int getWidth(){return 32;}int getHeight(){return 32;}};
enum{CACHE_POST,LOAD_FROM_TEXTURE_CACHE,LOAD_FROM_NETWORK,DECODE_IMAGE,NOT_WRITE};
struct Worker {LLTextureFetch* mFetcher;int mState=CACHE_POST,mCachedSize=0,mNativeCacheReadEnd=600,mDesiredSize=16777216,mLoadedDiscard=0,mDesiredDiscard=0,mWriteToCacheState=0,mFileSize=1206,mID=7;
bool mHaveAllData=false,mInCache=false;Formatted mFormattedImage;std::string mUrl="http://asset";
explicit Worker(LLTextureFetch* fetch):mFetcher(fetch){} void setState(int state){mState=state;}bool step(){BODY return false;}};
int main(){LLTextureFetch f;Worker w{&f};w.step();assert(w.mState==LOAD_FROM_TEXTURE_CACHE);
w.mState=CACHE_POST;w.mNativeCacheReadEnd=w.mDesiredSize;w.mFormattedImage.bytes=1206;w.mHaveAllData=true;w.step();assert(w.mState==DECODE_IMAGE&&w.mInCache);
w.mHaveAllData=false;w.mState=CACHE_POST;w.mFormattedImage.bytes=600;w.step();assert(w.mState==LOAD_FROM_NETWORK);
w.mState=CACHE_POST;w.mNativeCacheReadEnd=600;f.mNativeConsumer=false;w.step();assert(w.mState==LOAD_FROM_NETWORK);
f.mNativeConsumer=true;f.cache=false;w.mState=CACHE_POST;w.step();assert(w.mState==LOAD_FROM_NETWORK);
f.cache=true;w.mState=CACHE_POST;w.mNativeCacheReadEnd=1200;w.step();assert(w.mState==LOAD_FROM_NETWORK);
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'cache_demand.cpp';path.write_text(fixture);exe=Path(temp)/'cache_demand.exe'
            result=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
