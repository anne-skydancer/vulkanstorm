"""Exercise production upload publication against a deterministic fake GL driver."""
from pathlib import Path
import subprocess
import tempfile
ROOT = Path(__file__).resolve().parents[2]

# Platform recommendations can override settings.xml during first startup.
# Keep the feature enabled without requiring an existing user to find a debug flag.
import re
import xml.etree.ElementTree as ET
settings = list(ET.parse(ROOT/'indra/newview/app_settings/settings.xml').getroot().find('map'))
setting = next(settings[i+1] for i in range(0, len(settings), 2)
               if settings[i].text == 'RenderGLMultiThreadedTextures')
fields = list(setting)
assert next(fields[i+1].text for i in range(0, len(fields), 2) if fields[i].text == 'Value') == '1'
for table in ('featuretable.txt', 'featuretable_linux.txt'):
    default = re.search(r'^RenderGLMultiThreadedTextures\s+(\d+)\s+(\d+)',
                        (ROOT/'indra/newview'/table).read_text(), re.M)
    assert default and default.groups() == ('1', '1'), f'{table} disables asynchronous delivery by default'
def method(path, signature):
    text = (ROOT/path).read_text()
    begin = text.index(signature)
    brace = text.index('{', begin)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[begin:end]
fixture = r'''
#include <atomic>
#include <algorithm>
#include <bit>
#include <cassert>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "lltexturedeliverybudget.h"
using U64 = uint64_t; using S32 = int; using GLenum = unsigned;
using U32 = unsigned;
using F32 = float; using F64 = double;
template<class T> T llmax(T a,T b) { return std::max(a,b); }
float testDrawDistance=128.f;
int gSavedSettings=0;
template<class T> struct LLCachedControl {
    LLCachedControl(int&,const char*,T) {}
    operator T() const { return T(testDrawDistance); }
};
struct Position {
    float x=0,y=0,z=0;
    Position operator-(const Position& other) const { return {x-other.x,y-other.y,z-other.z}; }
    float lengthSquared() const { return x*x+y*y+z*z; }
};
struct Agent { Position position; Position getPositionAgent() const { return position; } } gAgent;
struct LLTimer { static inline double now=0; static double getTotalSeconds() { return now; } };
namespace LLRender { constexpr U32 NUM_TEXTURE_CHANNELS=4; }
struct LLVOAvatar {
    bool self=false,dead=false;
    Position position{1000,0,0};
    bool isSelf() const { return self; }
    bool isDead() const { return dead; }
    Position getPositionAgent() const { return position; }
};
struct LLViewerObject {
    bool dead=false, attachment=false;
    LLViewerObject* root=this;
    LLVOAvatar* avatar=nullptr;
    LLVOAvatar* directAvatar=nullptr;
    LLVOAvatar* asAvatar() { return directAvatar; }
    bool isDead() const { return dead; }
    bool isAttachment() const { return attachment; }
    LLViewerObject* getRootEdit() { return root; }
    LLVOAvatar* getAvatarAncestor() { return avatar; }
};
struct LLFace { LLViewerObject* object=nullptr; LLViewerObject* getViewerObject() { return object; } };
using GLsync = int*;
constexpr GLenum GL_ALREADY_SIGNALED=1, GL_TIMEOUT_EXPIRED=2, GL_WAIT_FAILED=3;
#define LL_IMAGEGL_THREAD_CHECK 0
#define llassert(x) assert(x)
#define LL_WARNS(x) std::cerr
#define LL_INFOS(x) std::cout
#define LL_ENDL std::endl
#define LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE
bool on_main_thread() { return true; }
std::deque<GLenum> responses;
std::vector<U64> waits;
int deleted_fences=0;
GLenum glClientWaitSync(GLsync, unsigned, U64 timeout) {
    waits.push_back(timeout); assert(!responses.empty());
    auto value=responses.front(); responses.pop_front(); return value;
}
void glDeleteSync(GLsync) { ++deleted_fences; }
template<class T> using LLPointer = std::shared_ptr<T>;
struct LLImageRaw {
    int width=1,height=1,components=4;
    LLTextureDeliveryBudget::Lease mDeliveryReservation;
    int getWidth() const { return width; }
    int getHeight() const { return height; }
    int getComponents() const { return components; }
    int getDataSize() const { return width*height*components; }
};
constexpr int MAX_DISCARD_LEVEL=5;
constexpr U64 MAX_IMAGE_SIZE=4096, MAX_IMAGE_COMPONENTS=8;
struct LLImageFormatted {
    U64 width=0,height=0,components=0;
    bool valid=true;
    int parses=0;
    U64 getWidth() const { return width; }
    U64 getHeight() const { return height; }
    U64 getComponents() const { return components; }
    bool updateData() { ++parses; if(valid) { width=height=2048; components=4; } return valid; }
};
struct LLImageDataLock { explicit LLImageDataLock(LLImageFormatted*) {} };
struct LLImageDecodeThread {
    static LLTextureDeliveryBudget::Lease reserveDelivery(LLImageFormatted* image,
        bool needs_aux, bool first_visible, bool& invalid_image);
    static U64 decodedDeliveryBytes(const LLImageRaw* raw, const LLImageRaw* aux);
};
struct DecodeReply {
    bool success=false;
    U64 reserved=0;
    void completed(bool ok, const std::string&, const LLPointer<LLImageRaw>& raw,
                   const LLPointer<LLImageRaw>& aux, U32) {
        success=ok; reserved=raw->mDeliveryReservation->bytes();
        if (aux) assert(aux->mDeliveryReservation==raw->mDeliveryReservation);
    }
};
struct ImageRequest {
    bool mDecodedRaw=true, mDecodedAux=false, mNeedsAux=false;
    LLTextureDeliveryBudget::Lease mReservation;
    LLPointer<LLImageRaw> mDecodedImageRaw, mDecodedImageAux;
    std::string mErrorString;
    U32 mRequestId=1;
    struct Responder {
        DecodeReply* value=nullptr;
        bool notNull() const { return value!=nullptr; }
        DecodeReply* operator->() { return value; }
    } mResponder;
    void finishRequest(bool completed);
};
struct LLImageGL {
    bool mDetachedUpload = false;
    int mTextureMemory=0, mPickMask=0, mPickMaskWidth=0, mPickMaskHeight=0;
    int mIsMask=0, mAlphaStride=0, mAlphaOffset=0, mGLTextureCreated=0;
    int mTexName=0, mWidth=0, mHeight=0, mCurrentDiscardLevel=0;
    int mHasMipMaps=0, mMipLevels=0, mComponents=0, mMaxDiscardLevel=0;
    int mFormatInternal=0, mFormatPrimary=0, mFormatType=0, mFormatSwapBytes=0;
    int mAutoGenMips=0, mLastBindTime=0;
    bool mTexOptionsDirty=false;
    U64 mContentRevision=1;
    static constexpr int sLastFrameTime=42;
    void adoptUploadImage(LLImageGL& image);
    int getDiscardLevel() const { return mCurrentDiscardLevel; }
};
struct LLViewerFetchedTexture {
    std::vector<LLFace*> faces[LLRender::NUM_TEXTURE_CHANNELS];
    bool lowMemory=false;
    bool isSystemMemoryLow() const { return lowMemory; }
    int getNumFaces(U32 channel) const { return int(faces[channel].size()); }
    const std::vector<LLFace*>* getFaceList(U32 channel) const { return &faces[channel]; }
    bool retainAvatarDetail();
    F64 mAvatarDetailRetentionStarted=-1.;
    LLImageGL image;
    bool mNeedsCreateTexture=true, mCreatePending=true;
    int posts=0, destroyed_raw=0, desired=0;
    bool hasGLTexture() const { return image.mTexName != 0; }
    int getDiscardLevel() const { return image.mCurrentDiscardLevel; }
    int getDesiredDiscardLevel() const { return desired; }
    LLImageGL* getGLTexture() { return &image; }
    void postCreateTexture() { ++posts; mNeedsCreateTexture=false; }
    void destroyRawImage() { ++destroyed_raw; }
};
class LLViewerTextureList {
public:
    struct PendingUpload;
    std::deque<std::unique_ptr<PendingUpload>> mPendingUploads;
    U64 mPendingUploadBytes=0;
    void completeTextureUploads(bool drain=false);
};
'''
fixture += method('indra/llimage/llimageworker.cpp', 'LLTextureDeliveryBudget::Lease LLImageDecodeThread::reserveDelivery') + '\n'
fixture += method('indra/llimage/llimageworker.cpp', 'U64 LLImageDecodeThread::decodedDeliveryBytes') + '\n'
fixture += method('indra/llimage/llimageworker.cpp', 'void ImageRequest::finishRequest') + '\n'
fixture += method('indra/newview/llviewertexturelist.cpp', 'static S32 deliveryDiscardForRaw') + '\n'
fixture += method('indra/newview/llviewertexturelist.cpp', 'struct LLViewerTextureList::PendingUpload') + ';\n'
fixture += method('indra/llrender/llimagegl.cpp', 'void LLImageGL::adoptUploadImage') + '\n'
fixture += method('indra/newview/llviewertexturelist.cpp', 'void LLViewerTextureList::completeTextureUploads') + '\n'
fixture += method('indra/newview/llviewertexture.cpp', 'bool LLViewerFetchedTexture::retainAvatarDetail') + '\n'
fixture += r'''
int main() {
    LLVOAvatar self{true}, other{false};
    LLViewerObject attachment; attachment.attachment=true; attachment.avatar=&self;
    LLViewerObject child; child.root=&attachment; child.avatar=&self;
    LLFace face{&child};
    LLViewerFetchedTexture worn; worn.image.mTexName=10;
    worn.faces[3].push_back(&face); // child prim's material channel
    assert(worn.retainAvatarDetail());
    worn.lowMemory=true; assert(!worn.retainAvatarDetail()); worn.lowMemory=false;
    child.avatar=&other; assert(!worn.retainAvatarDetail()); child.avatar=&self;
    attachment.attachment=false; assert(!worn.retainAvatarDetail()); attachment.attachment=true;
    child.dead=true; assert(!worn.retainAvatarDetail()); child.dead=false;
    worn.image.mTexName=0; assert(!worn.retainAvatarDetail());
    worn.image.mTexName=10; child.avatar=&other;
    other.position={10,0,0}; LLTimer::now=100;
    assert(worn.retainAvatarDetail());
    LLTimer::now=159; assert(worn.retainAvatarDetail());
    LLTimer::now=160; assert(!worn.retainAvatarDetail()); // retries do not renew
    worn.mAvatarDetailRetentionStarted=-1.; other.position={15,0,0}; LLTimer::now=200;
    assert(worn.retainAvatarDetail()); LLTimer::now=230; assert(!worn.retainAvatarDetail());
    other.position={20,0,0}; assert(!worn.retainAvatarDetail());
    other.position={9,0,0}; LLTimer::now=300; assert(worn.retainAvatarDetail());
    other.position={15,0,0}; LLTimer::now=331; assert(!worn.retainAvatarDetail()); // band changes share start
    testDrawDistance=8; other.position={9,0,0}; assert(!worn.retainAvatarDetail());
    testDrawDistance=128; assert(worn.retainAvatarDetail());
    worn.lowMemory=true; assert(!worn.retainAvatarDetail()); worn.lowMemory=false;
    other.dead=true; assert(!worn.retainAvatarDetail()); other.dead=false;
    gAgent.position={100,0,0}; other.position={109,0,0}; assert(worn.retainAvatarDetail());
    gAgent.position={0,0,0}; assert(!worn.retainAvatarDetail()); // avatar-relative, not camera-relative
    LLViewerObject body; body.directAvatar=&other; LLFace bodyFace{&body};
    other.position={5,0,0}; worn.faces[0].push_back(&bodyFace);
    assert(worn.retainAvatarDetail()); // avatar body, not only attachments
    LLTimer::now+=45; assert(worn.retainAvatarDetail());
    worn.faces[0].clear(); other.position={15,0,0}; assert(!worn.retainAvatarDetail());
    bool invalid=false;
    auto formatted=std::make_shared<LLImageFormatted>();
    auto reserved=LLImageDecodeThread::reserveDelivery(formatted.get(), true, true, invalid);
    assert(reserved && !invalid && formatted->parses==1);
    assert(reserved->bytes()==U64(2048)*2048*9);
    reserved.reset();
    LLImageRaw small{256,256,4}, auxiliary{256,256,1}, nonpot{17,20,3};
    assert(LLImageDecodeThread::decodedDeliveryBytes(&small,nullptr)==524288);
    assert(LLImageDecodeThread::decodedDeliveryBytes(&small,&auxiliary)==589824);
    assert(LLImageDecodeThread::decodedDeliveryBytes(&nonpot,nullptr)==6144);
    {
        ImageRequest request; DecodeReply reply; request.mResponder.value=&reply;
        request.mDecodedImageRaw=std::make_shared<LLImageRaw>(small);
        request.mReservation=LLTextureDeliveryBudget::reserve(32*1048576,false);
        request.finishRequest(false);
        assert(!reply.success && reply.reserved==32*1048576); // no premature trim
        request.finishRequest(true);
        assert(reply.success && reply.reserved==524288); // trim before publication
    }
    assert(LLTextureDeliveryBudget::used()==0);
    // Completing many reduced decodes must leave admission available, while
    // every result and its preparation allowance remain charged.
    std::vector<LLTextureDeliveryBudget::Lease> decoded_results;
    for (int i=0;i<100;++i) {
        auto lease=LLImageDecodeThread::reserveDelivery(formatted.get(),false,false,invalid);
        assert(lease); lease->shrinkTo(LLImageDecodeThread::decodedDeliveryBytes(&small,nullptr));
        decoded_results.push_back(lease);
    }
    assert(LLTextureDeliveryBudget::used()==100*524288);
    decoded_results.clear(); assert(LLTextureDeliveryBudget::used()==0);
    auto malformed=std::make_shared<LLImageFormatted>(); malformed->valid=false;
    assert(!LLImageDecodeThread::reserveDelivery(malformed.get(), false, true, invalid) && invalid);
    auto all=LLTextureDeliveryBudget::reserve(LLTextureDeliveryBudget::Ceiling,true);
    assert(!LLImageDecodeThread::reserveDelivery(formatted.get(),false,true,invalid) && !invalid);
    assert(formatted->parses==1); // deferral does not repeat header parsing
    all.reset(); formatted->width=8192;
    assert(!LLImageDecodeThread::reserveDelivery(formatted.get(),false,true,invalid) && invalid);
    assert(LLTextureDeliveryBudget::used()==0);
    formatted->width=17; formatted->height=20; formatted->components=4;
    reserved=LLImageDecodeThread::reserveDelivery(formatted.get(),false,true,invalid);
    assert(reserved && reserved->bytes()==32*32*8); reserved.reset();
    assert(deliveryDiscardForRaw(LLImageRaw{2048,1},0,5)==0);
    assert(deliveryDiscardForRaw(LLImageRaw{64,16},2,5)==5);
    assert(deliveryDiscardForRaw(LLImageRaw{16,64},0,5)==4);
    assert(deliveryDiscardForRaw(LLImageRaw{32,32},5,0)==5);
    LLViewerTextureList list;
    int fence_storage=0;
    auto add = [&](bool submitted=true) {
        auto job=std::make_unique<LLViewerTextureList::PendingUpload>();
        job->texture=std::make_shared<LLViewerFetchedTexture>();
        job->texture->image.mTexName=10;
        job->texture->image.mCurrentDiscardLevel=4;
        job->texture->image.mPickMask=11;
        job->image=std::make_shared<LLImageGL>();
        job->image->mDetachedUpload=true;
        job->image->mTexName=20;
        job->image->mCurrentDiscardLevel=0;
        job->image->mPickMask=21;
        job->image->mWidth=2048;
        job->image->mComponents=4;
        job->bytes=1024;
        job->reservation=LLTextureDeliveryBudget::reserve(1024, true);
        job->fence=&fence_storage;
        job->success=true;
        job->submitted=submitted;
        auto texture=job->texture;
        list.mPendingUploadBytes+=1024;
        list.mPendingUploads.push_back(std::move(job));
        return texture;
    };
    auto texture=add(false);
    list.completeTextureUploads();
    assert(waits.empty() && list.mPendingUploadBytes==1024);
    list.mPendingUploads.front()->submitted=true;
    responses={GL_TIMEOUT_EXPIRED};
    list.completeTextureUploads();
    assert(texture->image.mTexName==10 && texture->image.mPickMask==11);
    assert(texture->posts==0 && LLTextureDeliveryBudget::used()==1024);
    auto upload_image=list.mPendingUploads.front()->image;
    responses={GL_ALREADY_SIGNALED};
    list.completeTextureUploads();
    assert(texture->image.mTexName==20 && texture->image.mPickMask==21);
    assert(texture->image.mWidth==2048 && texture->image.mComponents==4);
    assert(texture->image.mContentRevision==2 && texture->image.mTexOptionsDirty);
    assert(upload_image->mTexName==10 && upload_image->mPickMask==11);
    assert(texture->posts==1 && !texture->mCreatePending);
    assert(list.mPendingUploadBytes==0 && LLTextureDeliveryBudget::used()==0);
    assert(deleted_fences==1);

    auto redundant=add(); redundant->desired=4;
    responses={GL_ALREADY_SIGNALED}; list.completeTextureUploads();
    assert(redundant->image.mTexName==10 && redundant->posts==1);
    auto poorer=add(); poorer->image.mCurrentDiscardLevel=1;
    list.mPendingUploads.front()->image->mCurrentDiscardLevel=2;
    responses={GL_ALREADY_SIGNALED}; list.completeTextureUploads();
    assert(poorer->image.mTexName==10 && poorer->getDiscardLevel()==1);
    auto failure=add(); responses={GL_WAIT_FAILED}; list.completeTextureUploads();
    assert(failure->image.mTexName==10 && failure->posts==1);
    auto prepare_failure=add(); list.mPendingUploads.front()->success=false;
    list.mPendingUploads.front()->fence=nullptr;
    list.completeTextureUploads();
    assert(prepare_failure->image.mTexName==10 && prepare_failure->posts==1);
    for (auto timeout: waits) assert(timeout==0); // never wait in normal frames

    // A slow oldest upload must not block publication of a later completed one.
    auto slow=add(); auto fast=add();
    responses={GL_TIMEOUT_EXPIRED,GL_ALREADY_SIGNALED}; list.completeTextureUploads();
    assert(slow->posts==0 && fast->posts==1 && list.mPendingUploads.size()==1);
    responses={GL_TIMEOUT_EXPIRED,GL_ALREADY_SIGNALED};
    list.completeTextureUploads(true);
    assert(slow->posts==0 && slow->destroyed_raw==1 && !slow->mNeedsCreateTexture);
    assert(slow->image.mTexName==10 && !slow->mCreatePending);
    assert(list.mPendingUploadBytes==0 && LLTextureDeliveryBudget::used()==0);
    std::cout << "Texture publication: ready, pending, out-of-order, failure, stale demand and shutdown passed\n";
}
'''
with tempfile.TemporaryDirectory(prefix='texture-publication-') as temp:
    root=Path(temp)
    (root/'test.cpp').write_text(fixture)
    (root/'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(texture_publication LANGUAGES CXX)
find_package(Threads REQUIRED)
add_executable(test test.cpp)
add_executable(budget "{(ROOT/'indra/llimage/tests/lltexturedeliverybudget_test.cpp').as_posix()}")
foreach(target test budget)
  target_compile_features(${{target}} PRIVATE cxx_std_20)
  target_include_directories(${{target}} PRIVATE "{(ROOT/'indra/llimage').as_posix()}")
  target_link_libraries(${{target}} PRIVATE Threads::Threads)
endforeach()
''')
    subprocess.run(['cmake','-S',temp,'-B',str(root/'build')],check=True,stdout=subprocess.DEVNULL)
    subprocess.run(['cmake','--build',str(root/'build'),'--config','Debug'],check=True,stdout=subprocess.DEVNULL)
    exe=root/'build/Debug/test.exe' if __import__('os').name=='nt' else root/'build/test'
    subprocess.run([str(exe)],check=True)
    budget=exe.with_name('budget.exe' if exe.suffix=='.exe' else 'budget')
    subprocess.run([str(budget)],check=True)
