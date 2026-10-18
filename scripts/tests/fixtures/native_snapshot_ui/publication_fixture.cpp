#include <cassert>
#include <memory>
#include <string>
#include <map>
#include <utility>
using U32=unsigned;
#define VS_NATIVE_VULKAN 1
template<class T>using LLPointer=std::shared_ptr<T>;
struct LLImageRaw {int generation;};
struct LLUIImage {int generation;};
struct VSUIRenderer {enum class Sampling {Nearest,Linear,Anisotropic};};
struct LLSnapshotModel {static constexpr int SNAPSHOT_TEXTURE=1;};
struct LLUUID {static LLUUID generateNewID(){return{};}std::string asString()const{return"unique-preview";}};
using Image=std::shared_ptr<LLUIImage>;
std::map<std::string,Image> cache;bool refuse=false;unsigned uploads=0,erases=0;VSUIRenderer::Sampling received;
Image vs_publish_ui_image(const std::string& key,const LLImageRaw& raw,VSUIRenderer::Sampling sampling){++uploads;received=sampling;if(refuse)return{};auto image=std::make_shared<LLUIImage>(LLUIImage{raw.generation});cache[key]=image;return image;}
void vs_erase_ui_image(const std::string& key){++erases;cache.erase(key);}
struct Charge {static inline unsigned live=0;Charge(){++live;}~Charge(){--live;}};
struct LLSnapshotLivePreview {
 struct Slot{Image image;LLPointer<LLImageRaw> pending;std::shared_ptr<const void> residency;std::string key;bool requested=false;};Slot mNativeImages[4];int type=0;
 int getSnapshotType()const{return type;}
 bool publishNativeImage(U32,LLPointer<LLImageRaw>,std::shared_ptr<const void>);bool retryNativeImage(U32);void clearNativeImage(U32);
};
PUBLISH
RETRY
CLEAR
int main(){
 LLSnapshotLivePreview preview;auto first=std::make_shared<LLImageRaw>(LLImageRaw{1});
 assert(preview.publishNativeImage(0,first,std::make_shared<Charge>()));assert(Charge::live==0&&received==VSUIRenderer::Sampling::Nearest);auto queued=preview.mNativeImages[0].image;
 auto second=std::make_shared<LLImageRaw>(LLImageRaw{2});std::weak_ptr<LLImageRaw> pending=second;refuse=true;
 assert(!preview.publishNativeImage(0,second,std::make_shared<Charge>()));second.reset();assert(!pending.expired()&&preview.mNativeImages[0].image==queued&&preview.mNativeImages[0].requested&&Charge::live==1);
 assert(!preview.retryNativeImage(0)&&preview.mNativeImages[0].image==queued);assert(queued->generation==1);
 refuse=false;preview.type=LLSnapshotModel::SNAPSHOT_TEXTURE;assert(preview.retryNativeImage(0));assert(pending.expired()&&Charge::live==0&&!preview.mNativeImages[0].requested&&preview.mNativeImages[0].image->generation==2&&queued->generation==1&&received==VSUIRenderer::Sampling::Anisotropic);
 // Destruction clears both accepted and refused-only keys, without disturbing
 // generations still owned by an in-flight renderer submission.
 refuse=true;assert(!preview.publishNativeImage(2,first,std::make_shared<Charge>()));assert(Charge::live==1);preview.clearNativeImage(2);assert(Charge::live==0);
 preview.clearNativeImage(0);assert(cache.empty()&&queued->generation==1&&erases==2);
 assert(preview.retryNativeImage(0)); // empty retired slot cannot recapture or upload
}
