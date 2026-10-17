"""Compile the production shared fetch-request policy with bounded native consumers."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeTextureCacheTest(unittest.TestCase):
    def test_native_request_discard_ranges_bounds_and_legacy_default(self):
        source=(ROOT/'indra/newview/lltexturefetch.cpp').read_text()
        body=method(source,'S32 LLTextureFetch::createRequest(')
        fixture=r'''
#include <algorithm>
#include <cassert>
#include <map>
#include <sstream>
#include <string>
using S32=int;using F32=float;using LLUUID=int;using LLHost=int;
enum FTType{FTT_DEFAULT,FTT_SERVER_BAKE};
constexpr int MAX_IMAGE_DATA_SIZE=64*1024*1024,TEXTURE_CACHE_ENTRY_SIZE=600,MAX_DISCARD_LEVEL=5,IMG_CODEC_J2C=1;
#define LL_PROFILE_ZONE_SCOPED
#define LL_DEBUGS(x) std::ostringstream()
#define LL_WARNS(x) std::ostringstream()
#define LL_ENDL ""
#define llassert(x) assert(x)
int fttype_to_string(FTType type){return int(type);}
template<class T>T llmax(T a,T b){return std::max(a,b);}template<class T>T llmin(T a,T b){return std::min(a,b);}
struct Dir{std::string getExtension(const std::string& url){return url.find("png")!=std::string::npos?"png":std::string();}}dir;
auto* gDirUtilp=&dir;
struct LLImageBase{static int getCodecFromExtension(const std::string& ext){return ext=="png"?2:IMG_CODEC_J2C;}};
struct LLImageJ2C{static inline int calls=0,lastW=0,lastH=0,lastC=0,lastDiscard=0;
 static int calcDataSizeJ2C(int w,int h,int c,int discard){++calls;lastW=w;lastH=h;lastC=c;lastDiscard=discard;return 1200;}};
struct LLTextureFetch;
struct LLTextureFetchWorker{
 enum{DONE,INIT};int mHost,mState=INIT,mDesiredSize,mDesiredDiscard,mActiveCount=0;bool mNeedsAux=false,aborted=false,work=true,http=false;
 float priority=0;
 LLTextureFetchWorker(LLTextureFetch*,FTType,const std::string&,int,int host,float p,int discard,int size):mHost(host),mDesiredSize(size),mDesiredDiscard(discard),priority(p){}
 bool wasAborted(){return aborted;}void lockWorkMutex(){}void unlockWorkMutex(){}
 void setImagePriority(float p){priority=p;}void setDesiredDiscard(int d,int s){mDesiredDiscard=d;mDesiredSize=s;}
 void setCanUseHTTP(bool b){http=b;}bool haveWork(){return work;}void setState(int state){mState=state;}void addWork(int){work=true;}
};
struct LLTextureFetch{
 bool mNativeConsumer,mDebugPause=false;std::map<int,LLTextureFetchWorker*> mRequestMap;
 enum{CREATE_REQUEST_ERROR_DEFAULT=-1,CREATE_REQUEST_ERROR_MHOSTS=-2,CREATE_REQUEST_ERROR_ABORTED=-3,CREATE_REQUEST_ERROR_TRANSITION=-4};
 explicit LLTextureFetch(bool native):mNativeConsumer(native){}
 ~LLTextureFetch(){for(auto& pair:mRequestMap)delete pair.second;}
 LLTextureFetchWorker* getWorker(int id){auto found=mRequestMap.find(id);return found==mRequestMap.end()?nullptr:found->second;}
 int getNumRequests(){return int(mRequestMap.size());}void lockQueue(){}void unlockQueue(){}
 void removeRequest(LLTextureFetchWorker* worker,bool){for(auto it=mRequestMap.begin();it!=mRequestMap.end();++it)if(it->second==worker){delete worker;mRequestMap.erase(it);break;}}
 S32 createRequest(FTType,const std::string&,const LLUUID&,const LLHost&,F32,S32,S32,S32,S32,bool,bool);
};
BODY
int main(){
 LLTextureFetch native(true),legacy(false);
 assert(native.createRequest(FTT_DEFAULT,"http://texture/",1,0,12,0,0,4,3,true,true)==MAX_DISCARD_LEVEL);
 auto* first=native.getWorker(1);assert(first->mDesiredSize==600 && first->mNeedsAux && first->http);
 assert(native.createRequest(FTT_DEFAULT,"http://texture/",1,0,20,1024,512,4,2,true,true)==2);
 assert(LLImageJ2C::lastW==1024 && LLImageJ2C::lastH==512 && LLImageJ2C::lastC==4 && LLImageJ2C::lastDiscard==2);
 assert(first->mDesiredSize==1200 && first->priority==20);
 assert(native.createRequest(FTT_DEFAULT,"http://texture/",1,0,20,1024,512,4,0,true,true)==0);
 assert(first->mDesiredSize==16*1024*1024);
 assert(legacy.createRequest(FTT_DEFAULT,"http://texture/",1,0,20,1024,512,4,0,true,true)==0);
 assert(legacy.getWorker(1)->mDesiredSize==MAX_IMAGE_DATA_SIZE);
 first->aborted=true;assert(native.createRequest(FTT_DEFAULT,"",1,0,1,0,0,4,0,true,true)==-3);first->aborted=false;
 first->mState=LLTextureFetchWorker::DONE;assert(native.createRequest(FTT_DEFAULT,"",1,0,1,0,0,4,0,true,true)==-4);
 first->mState=LLTextureFetchWorker::INIT;
 for(int id=2;id<=32;++id)assert(native.createRequest(FTT_DEFAULT,"",id,0,1,0,0,4,0,true,true)>=0);
 assert(native.createRequest(FTT_DEFAULT,"",33,0,1,0,0,4,0,true,true)==-1);
 assert(native.createRequest(FTT_DEFAULT,"",1,99,1,0,0,4,0,true,true)==-2);assert(!native.getWorker(1));
}
'''.replace('BODY',body)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/'native_cache.cpp';cpp.write_text(fixture);exe=Path(temp)/'native_cache.exe'
            build=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror',str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
    def test_native_decode_bounds_corruption_retry_and_budget(self):
        source=(ROOT/'indra/newview/lltexturefetch.cpp').read_text()
        source=source[source.index('    if (mState == DECODE_IMAGE)'):]
        guard=method(source,'if (mFetcher->mNativeConsumer)')
        fixture=r'''
#include "vsresourcebudget.h"
#include <algorithm>
#include <cassert>
#include <memory>
constexpr int IMG_CODEC_J2C=1,INIT=1,DONE=2;
struct Cache{unsigned removed=0;void removeFromCache(int){++removed;}};
struct Fetcher{bool mNativeConsumer=true;VSResourceBudget mNativeDecodeBytes{128};Cache cache;Cache* mTextureCache=&cache;};
struct Formatted{
 bool valid=true;int w=2,h=2,bytes=16,codec=IMG_CODEC_J2C,components=5;
 bool updateData(){return valid;}int getCodec(){return codec;}int getWidth(){return w;}int getHeight(){return h;}int getDataSize(){return bytes;}int getComponents(){return components;}
};
struct Worker{
 Fetcher* mFetcher;std::shared_ptr<Formatted> mFormattedImage=std::make_shared<Formatted>();
 std::shared_ptr<const VSResourceBudget::Reservation> mNativeDecodeResidency;
 std::shared_ptr<int> mRawImage,mAuxImage;
 int mCachedSize=0,mRetryAttempt=0,mID=9,state=7,discard=0;bool mNeedsAux=true;
 explicit Worker(Fetcher& f):mFetcher(&f){}void setState(int s){state=s;}
 bool validate(){GUARD return true;}
};
int main(){
 Fetcher fetch;Worker good(fetch);assert(good.validate());assert(fetch.mNativeDecodeBytes.used()==36);
 Worker waiting(fetch);waiting.mFormattedImage->w=5;waiting.mFormattedImage->h=5;
 assert(!waiting.validate() && waiting.state==7);assert(fetch.mNativeDecodeBytes.used()==36);
 good.mNativeDecodeResidency.reset();waiting.mFormattedImage->w=4;waiting.mFormattedImage->h=4;
 assert(waiting.validate() && fetch.mNativeDecodeBytes.used()==96);waiting.mNativeDecodeResidency.reset();
 Worker corrupt(fetch);corrupt.mCachedSize=600;corrupt.mFormattedImage->valid=false;
 assert(!corrupt.validate() && corrupt.state==INIT && corrupt.mRetryAttempt==1 && fetch.cache.removed==1);
 for(int invalid=0;invalid<5;++invalid){Worker bad(fetch);
  if(invalid==0)bad.mFormattedImage->w=4097;
  if(invalid==1)bad.mFormattedImage->h=0;
  if(invalid==2)bad.mFormattedImage->codec=2;
  if(invalid==3)bad.mFormattedImage->bytes=16*1024*1024+1;
  if(invalid==4)bad.mFormattedImage->components=6;
  assert(bad.validate() && bad.state==DONE && !bad.mNativeDecodeResidency);
 }
 Worker rgba(fetch);rgba.mFormattedImage->components=4;assert(rgba.validate() && !rgba.mNeedsAux);
 assert(fetch.mNativeDecodeBytes.used()==32);rgba.mNativeDecodeResidency.reset();
 fetch.mNativeConsumer=false;Worker legacy(fetch);legacy.mFormattedImage->w=9999;
 assert(legacy.validate() && legacy.state==7 && fetch.mNativeDecodeBytes.used()==0);
}
'''.replace('GUARD',guard)
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            cpp=Path(temp)/'native_decode.cpp';cpp.write_text(fixture);exe=Path(temp)/'native_decode.exe'
            build=subprocess.run([compiler,'-std=c++17','-Wall','-Wextra','-Werror','-I',str(ROOT/'indra/newview'),str(cpp),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(exe)],capture_output=True,text=True);self.assertEqual(run.returncode,0,run.stdout+run.stderr)
if __name__=='__main__':unittest.main()
