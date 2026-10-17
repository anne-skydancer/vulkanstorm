"""Execute actual glTF worker decoding, admission and retirement without a GPU."""
from pathlib import Path
import shutil,subprocess,tempfile,unittest
from test_native_simulator_features import method
ROOT=Path(__file__).resolve().parents[2]
class NativeGLTFDecodeTests(unittest.TestCase):
    def test_async_decode_discard_refusal_and_retirement(self):
        source=(ROOT/'indra/newview/vsworldgltf.cpp').read_text()
        fixture=r'''
#include "vsresourcebudget.h"
#include <cassert>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <stdexcept>
using U64=unsigned long long;
template<class T>struct LLPointer:std::shared_ptr<T>{
 using std::shared_ptr<T>::shared_ptr;using std::shared_ptr<T>::operator=;
 LLPointer()=default;LLPointer(T* value):std::shared_ptr<T>(value){}
};
using U8=unsigned char;using U32=unsigned;using S32=int;
constexpr int INVALID_INDEX=-1,MAX_DISCARD_LEVEL=5;
struct LLImageRaw {int tag=0,width=0,height=0;LLImageRaw()=default;LLImageRaw(int t,int w,int h):tag(t),width(w),height(h){}
 int getWidth()const{return width;}int getHeight()const{return height;}
 bool scale(int w,int h){width=w;height=h;return true;}};
struct LLImageFormatted {unsigned width=0,height=0;
 static LLImageFormatted* loadFromMemory(const U8* bytes,U32 size,const std::string&){if(size<4)return nullptr;return new LLImageFormatted{unsigned(bytes[0])*256+bytes[1],unsigned(bytes[2])*256+bytes[3]};}
 unsigned getWidth(){return width;}unsigned getHeight(){return height;}int getComponents(){return 4;}
 bool decode(LLPointer<LLImageRaw>& raw,int){raw->width=width;raw->height=height;raw->tag=7;return true;}
};
using llifstream=std::ifstream;
struct Dir {std::string getDirName(const std::string&){return {};};std::string getDirDelimiter(){return "/";};std::string getExtension(const std::string&){return "png";}}dir;
auto* gDirUtilp=&dir;
struct LLURI {static std::string unescape(const std::string& s){return s;}};
struct LLBase64 {static std::string decodeBinary(const std::string& s){return s;}};
struct LLFrameTimer {static double getTotalSeconds(){return 1;}};
struct Asset {
 struct Image {int mBufferView=0;std::string mUri,mMimeType="image/png";};
 struct View {int mBuffer=0;unsigned mByteOffset=0,mByteLength=4;};
 struct Buffer {std::vector<U8> mData{0,8,0,8};};
 std::vector<Image> mImages{{}};std::vector<View> mBufferViews{{}};std::vector<Buffer> mBuffers{{}};std::string mFilename;
};
VSResourceBudget gltfEncodedImageBytes{1024*1024},gltfDecodedImageBytes{256*1024*1024};
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
  UPDATE;
  std::shared_ptr<Asset> asset=std::make_shared<Asset>();
  VSUIResources resources;U64 imageEpoch=1;std::string error;bool publicationKnown=true;
  std::map<unsigned,ImageUpdate> imageUpdates;std::map<unsigned,std::string> images;
  std::map<unsigned,std::shared_ptr<int>> publications;std::vector<std::shared_ptr<int>> publishedPackets;
  bool current()const{return true;}
RESULT;
DISCARD
DECODE
PREPARE
 };
 std::shared_ptr<Impl> mImpl=std::make_shared<Impl>();
 void releaseRenderingResources();
};
RELEASE
int main(){
 VSWorldGLTF owner;auto state=owner.mImpl;auto queue=LL::WorkQueue::getInstance("mainloop");
 auto retained=std::make_shared<int>(91);state->publications[0]=retained;state->publishedPackets.push_back(retained);state->images[0]="old";
 LLPointer<LLImageRaw> old=new LLImageRaw{11,2,2};
 assert(state->prepareImage(0,old,false));const auto oldBytes=state->resources.budget.used();assert(oldBytes>0);
 owner.releaseRenderingResources();assert(state->imageEpoch==2 && !state->publicationKnown && state->imageUpdates.empty());
 assert(state->images.empty() && state->publications.empty() && state->publishedPackets.empty() && *retained==91 && retained.use_count()==1);
 assert(state->resources.erased==std::vector<std::string>{"old"});
 owner.releaseRenderingResources();assert(state->imageEpoch==2);
 LLPointer<LLImageRaw> next=new LLImageRaw{22,3,3};assert(state->prepareImage(0,next,true));
 const auto combined=state->resources.budget.used();assert(combined>oldBytes);
 queue->runOne();assert(state->imageUpdates[0].preparing && !state->imageUpdates[0].prepared);
 assert(state->resources.budget.used()==combined-oldBytes);
 queue->runOne();assert(!state->imageUpdates[0].preparing && *state->imageUpdates[0].prepared==22);
 owner.releaseRenderingResources();assert(state->resources.budget.used()==0 && state->imageEpoch==3);
 assert(state->decodeImage(0,2));assert(state->imageUpdates[0].decoding&&!state->imageUpdates[0].decoded);
 assert(gltfEncodedImageBytes.used()>0);queue->runOne();
 assert(!state->imageUpdates[0].decoding&&state->imageUpdates[0].decoded->width==2);
 assert(state->imageUpdates[0].fullWidth==8&&state->imageUpdates[0].decodedDiscard==2);
 assert(gltfEncodedImageBytes.used()==0&&gltfDecodedImageBytes.used()>0);
 owner.releaseRenderingResources();assert(gltfDecodedImageBytes.used()==0);
 assert(state->decodeImage(0,2));owner.releaseRenderingResources();assert(state->decodeImage(0,4));
 queue->runOne();assert(state->imageUpdates[0].decoding&&!state->imageUpdates[0].decoded);
 queue->runOne();assert(!state->imageUpdates[0].decoding&&state->imageUpdates[0].decoded->width==4);
 owner.releaseRenderingResources();assert(gltfDecodedImageBytes.used()==0);
 auto held=gltfDecodedImageBytes.reserve(gltfDecodedImageBytes.limit());assert(held);
 assert(state->decodeImage(0,2));queue->runOne();assert(!state->imageUpdates[0].decoded&&state->imageUpdates[0].retryAt>1);
 held.reset();owner.releaseRenderingResources();
 state->asset->mBuffers[0].mData={32,0,0,8};assert(state->decodeImage(0,2));queue->runOne();
 assert(!state->error.empty()&&!state->imageUpdates[0].decoded);
 assert(gltfDecodedImageBytes.used()==0&&gltfEncodedImageBytes.used()==0);

}
'''
        fixture=fixture.replace('UPDATE',method(source,'struct ImageUpdate'))
        fixture=fixture.replace('RESULT',method(source,'struct DecodeResult'))
        fixture=fixture.replace('DISCARD'+chr(10),method(source,'static S32 imageDiscard(')+chr(10))
        fixture=fixture.replace('DECODE',method(source,'bool decodeImage(unsigned index,'))
        fixture=fixture.replace('PREPARE',method(source,'bool prepareImage(unsigned index,'))
        fixture=fixture.replace('RELEASE',method(source,'void VSWorldGLTF::releaseRenderingResources()'))
        compiler=shutil.which('clang++') or shutil.which('g++');self.assertIsNotNone(compiler)
        with tempfile.TemporaryDirectory() as temp:
            path=Path(temp)/'gltf_decode.cpp';path.write_text(fixture);exe=Path(temp)/'gltf_decode.exe'
            result=subprocess.run([compiler,'-std=c++20','-Wall','-Wextra','-Werror','-I',str(ROOT/'indra/newview'),str(path),'-o',str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(exe)],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
if __name__=='__main__':unittest.main()
