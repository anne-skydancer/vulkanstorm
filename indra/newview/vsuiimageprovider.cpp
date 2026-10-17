// CPU skin assets published through native UI facades. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "lluiimage.h"
#include "vsuiimageprovider.h"
#include "lldir.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimageworker.h"
#include "lltexturecache.h"
#include "lltexturefetch.h"
#include "llviewercontrol.h"
#include "lleventcoro.h"
#include "llsdutil.h"
#include <atomic>
#include "llappviewer.h"
#include "llxmlnode.h"
#include "llxuiparser.h"
#include "vsuiresources.h"
#include "vsnativesession.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "workqueue.h"
#include "vsresourcebudget.h"
#include "lltimer.h"
#include "lluiimage.h"
#include <filesystem>
#include <fstream>
#include <map>
#include <stdexcept>
#include <utility>
#include <openssl/evp.h>
namespace
{
VSUIImageProvider* sActiveProvider = nullptr;
class NativeMaskImage final : public LLImageRaw
{
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;
public:
    explicit NativeMaskImage(std::shared_ptr<const VSResourceBudget::Reservation> residency):mResidency(std::move(residency)) {}
    NativeMaskImage(std::shared_ptr<const VSResourceBudget::Reservation> residency,U16 width,U16 height)
        :LLImageRaw(width,height,1),mResidency(std::move(residency)) {}
};
struct Image : LLInitParam::Block<Image>
{
    Mandatory<std::string> name;
    Optional<std::string> file_name;
    Optional<bool> preload, use_mips;
    Optional<LLRect> scale, clip;
    Optional<std::string> scale_type;
    Image()
        : name("name"), file_name("file_name"), preload("preload", false), use_mips("use_mips", false), scale("scale"),
          clip("clip"), scale_type("scale_type", "scale_inner")
    {
    }
};
struct Images : LLInitParam::Block<Images>
{
    Mandatory<S32> version;
    Multiple<Image> textures;
    Images() : version("version"), textures("texture")
    {
    }
};
} // namespace
struct VSUIImageProvider::Impl
{
    VSUIResources &resources;
    std::map<std::string, Image> declarations;
    std::map<std::string, LLUIImagePtr> cache;
    struct Remote
    {
        LLUIImagePtr facade,pixels;
        LLPointer<LLImageRaw> auxiliary;
        U64 generation=0,readyGeneration=0,failedGeneration=0,touched=0,epoch=0;
        unsigned attempts=0,target=64;
        S32 discard=-1,fullWidth=0,fullHeight=0,components=4;
        U32 resolutionLimit=0;
        double retryAt=0,deadline=0,lastUsed=0;
        U64 demandFrame=0;
        bool fetching=false,uiDemand=false,cacheChecked=false,fullRes=false;
    };
    std::map<LLUUID,Remote> remote;
    U64 diskHits=0,diskMisses=0;
    bool retired = false;
    U64 regionEpoch=0,clock=0,demandFrame=0;
    VSResourceBudget masks{64 * 1024 * 1024};
    bool evict(const LLUUID& preserve)
    {
        auto victim=remote.end();
        for (auto found=remote.begin();found!=remote.end();++found)
        {
            if (found->first==preserve || found->second.fetching ||
                (found->second.facade && found->second.facade->getNumRefs()>1)) continue;
            if (victim==remote.end() || found->second.touched<victim->second.touched) victim=found;
        }
        if (victim==remote.end()) return false;
        resources.erase("remote:"+victim->first.asString());remote.erase(victim);return true;
    }
    explicit Impl(VSUIResources &r) : resources(r)
    {
        const auto paths = gDirUtilp->findSkinnedFilenames(LLDir::TEXTURES, "textures.xml", LLDir::ALL_SKINS);
        if (paths.empty())
            throw std::runtime_error("Native UI skin image definitions are missing");
        Images images;
        LLXUIParser parser;
        for (const auto &path : paths)
        {
            LLXMLNodePtr root;
            if (!LLXMLNode::parseFile(path, root, nullptr))
                throw std::runtime_error("Native UI skin image definitions cannot be parsed: " + path);
            parser.readXUI(root, images, path);
        }
        if (!images.validateBlock())
            throw std::runtime_error("Invalid native UI skin image definitions");
        for (const auto &image : images.textures)
            declarations[image.name()].overwriteFrom(image);
    }
};
VSUIImageProvider::VSUIImageProvider(VSUIResources &resources) : mImpl(std::make_shared<Impl>(resources))
{
    sActiveProvider = this;
}
VSUIImageProvider::~VSUIImageProvider()
{
    if (sActiveProvider == this) sActiveProvider = nullptr;
    cleanUp();
}
void vs_pump_ui_images() { if (sActiveProvider) sActiveProvider->pumpImages(); }
void VSUIImageProvider::pumpImages()
{
    if(mImpl->retired) return;
    const auto owner=VSNativeSession::active();
    auto* fetch=LLAppViewer::getTextureFetch();
    if(!owner || owner->phase()!=VSNativeSession::Phase::Connected || !fetch) return;
    ++mImpl->demandFrame;
    const auto generation=owner->generation(),epoch=owner->regionEpoch();
    if(mImpl->regionEpoch!=epoch)
    {
        mImpl->regionEpoch=epoch;
        for(auto& [id,entry]:mImpl->remote)
        {
            if(entry.fetching) fetch->deleteRequest(id,true);
            entry.fetching=false;
            if(entry.readyGeneration!=generation)
            {entry.generation=entry.failedGeneration=0;entry.attempts=0;entry.retryAt=0;}
        }
    }
    for(auto& [id,entry]:mImpl->remote)
    {
        if(!entry.fetching) continue;
        if(entry.generation!=generation || entry.epoch!=epoch)
        {fetch->deleteRequest(id,true);entry.fetching=false;continue;}
        S32 discard=-1,state=0;LLPointer<LLImageRaw> raw,auxiliary;LLCore::HttpStatus status;
        const bool done=fetch->getRequestFinished(id,discard,state,raw,auxiliary,status);
        if(raw && discard>=0 && discard!=entry.discard)
        {
            if(!fetch->getRequestDimensions(id,entry.fullWidth,entry.fullHeight,entry.components))
            {entry.fullWidth=raw->getWidth()<<discard;entry.fullHeight=raw->getHeight()<<discard;}
            LLPointer<LLImageRaw> mask;
            bool admitted=entry.components!=5 || auxiliary.notNull();
            if(auxiliary)
            {
                auto residency=mImpl->masks.reserve(std::size_t(auxiliary->getWidth())*auxiliary->getHeight());
                while(!residency && mImpl->evict(id)) residency=mImpl->masks.reserve(std::size_t(auxiliary->getWidth())*auxiliary->getHeight());
                if(residency)
                {mask=new NativeMaskImage(std::move(residency),auxiliary->getWidth(),auxiliary->getHeight());mask->copy(auxiliary);}
                else admitted=false;
            }
            LLUIImagePtr image;
            if(admitted)
            {
                image=mImpl->resources.publish("remote:"+id.asString(),*raw);
                while(!image && mImpl->evict(id)) image=mImpl->resources.publish("remote:"+id.asString(),*raw);
            }
            if(image)
            {
                if(!entry.cacheChecked)
                {if(fetch->isFromLocalCache(id)) ++mImpl->diskHits;else ++mImpl->diskMisses;entry.cacheChecked=true;}
                entry.pixels=image;entry.auxiliary=mask;entry.discard=discard;
                entry.readyGeneration=generation;entry.failedGeneration=0;entry.attempts=0;entry.retryAt=0;
                entry.facade->setNativeExtent(raw->getWidth(),raw->getHeight());entry.facade->onImageLoaded();
            }
        }
        if(done || LLTimer::getTotalSeconds()>=entry.deadline)
        {
            fetch->deleteRequest(id,true);entry.fetching=false;
            if(status==LLCore::HttpStatus(LLCore::HttpStatus::LLCORE,LLCore::HE_RESPONSE_TOO_LARGE))
            {entry.failedGeneration=generation;entry.attempts=3;}
            if(!raw && entry.attempts>=3) entry.failedGeneration=generation;
        }
    }
    if(!owner->regionReady()) return;
    std::vector<LLUUID> pending;
    for(const auto& [id,entry]:mImpl->remote)
        if(!entry.fetching && entry.attempts<3 && LLTimer::getTotalSeconds()>=entry.retryAt &&
            ((entry.uiDemand && entry.facade && entry.facade->getNumRefs()>1) || LLTimer::getTotalSeconds()-entry.lastUsed<1.0))
            pending.push_back(id);
    for(const auto& id:pending) getUIImageByID(id,-2);
}
LLSD VSUIImageProvider::textureCacheEvidence(bool includePixels) const
{
    LLSD result;
    static const LLUUID processInstance=LLUUID::generateNewID();
    result["process_id"]=LLApp::getPid();result["process_instance"]=processInstance;
    result["disk_hits"]=ll_sd_from_U64(mImpl->diskHits);result["disk_misses"]=ll_sd_from_U64(mImpl->diskMisses);
    U32 reads=0,writes=0,waits=0;
    if(auto* fetch=LLAppViewer::getTextureFetch())
    {
        fetch->getStateStats(&reads,&writes,&waits);
        result["network_requests"]=ll_sd_from_U64(fetch->getTotalNumHTTPRequests());
        result["pending_fetches"]=fetch->getNumRequests();
    }
    result["disk_read_attempts"]=ll_sd_from_U64(reads);
    result["disk_writes_started"]=ll_sd_from_U64(writes);
    result["cache_policy"]="shared_LLTextureFetch";
    result["cache_path"]=gDirUtilp->getExpandedFilename(LL_PATH_CACHE,"");
    result["startup_policy"]["LocalCacheVersion"]=gSavedSettings.getS32("LocalCacheVersion");
    result["startup_policy"]["DiskCacheVersion"]=gSavedSettings.getS32("DiskCacheVersion");
    result["startup_policy"]["LastJ2CVersion"]=gSavedSettings.getString("LastJ2CVersion");
    result["configured_cache_size_mb"]=S32(gSavedSettings.getU32("CacheSize"));
    if(auto* cache=LLAppViewer::getTextureCache())
    {
        result["read_only"]=cache->isReadOnly();
        result["maximum_body_bytes"]=ll_sd_from_U64(U64(cache->getMaxUsage().value()));
        result["maximum_entries"]=S32(cache->getMaxEntries());
    }
#if defined(VS_VULKAN_DIAGNOSTICS) && VS_VULKAN_DIAGNOSTICS
    if(includePixels)
    {
        result["images"]=LLSD::emptyArray();
        const auto owner=VSNativeSession::active();
        for(const auto& [id,entry]:mImpl->remote)
        {
            if(!owner || entry.readyGeneration!=owner->generation()) continue;
            const auto image=mImpl->resources.image("remote:"+id.asString());
            if(!image) continue;
            const auto gpu=mImpl->resources.readbackImage("remote:"+id.asString());
            if(gpu!=VSUIRenderer::imagePixels(image)) throw std::runtime_error("Cached texture GPU bytes differ from decoded publication");
            unsigned char digest[EVP_MAX_MD_SIZE];unsigned digestSize=0;
            if(EVP_Digest(gpu.data(),gpu.size(),digest,&digestSize,EVP_sha256(),nullptr)!=1 || digestSize!=32)
                throw std::runtime_error("Cached texture digest failed");
            std::string hex;hex.reserve(64);
            for(unsigned i=0;i<digestSize;++i) {hex+="0123456789abcdef"[digest[i]>>4];hex+="0123456789abcdef"[digest[i]&15];}
            const auto extent=VSUIRenderer::imageExtent(image);
            LLSD row;row["id"]=id;row["width"]=S32(extent[0]);row["height"]=S32(extent[1]);
            row["discard"]=entry.discard;row["gpu_rgba_sha256"]=hex;row["gpu_matches_cpu"]=true;
            const auto* destination=std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY");
            if(!destination) throw std::runtime_error("Missing cached texture evidence destination");
            const auto root=std::filesystem::path(destination);
            const auto relative=std::filesystem::path("r3")/"readbacks"/(id.asString()+"-"+std::to_string(entry.discard)+"-"+hex+".rgba");
            std::filesystem::create_directories((root/relative).parent_path());
            std::ofstream artifact(root/relative,std::ios::binary);
            artifact.write(reinterpret_cast<const char*>(gpu.data()),std::streamsize(gpu.size()));
            if(!artifact.good()) throw std::runtime_error("Cached texture readback artifact write failed");
            row["gpu_rgba_file"]=relative.generic_string();
            result["images"].append(row);
        }
    }
#else
    (void)includePixels;
#endif
    return result;
}
LLSD vs_ui_texture_cache_evidence(bool includePixels)
{ return sActiveProvider ? sActiveProvider->textureCacheEvidence(includePixels) : LLSD(); }
void vs_refresh_ui_image(const LLUUID& id)
{
    if (sActiveProvider) sActiveProvider->refreshImage(id);
}
LLPointer<LLUIImage> vs_publish_ui_image(const std::string& key, const LLImageRaw& raw)
{
    return sActiveProvider ? sActiveProvider->publishImage(key, raw) : nullptr;
}
void vs_erase_ui_image(const std::string& key)
{
    if (sActiveProvider) sActiveProvider->eraseImage(key);
}
LLPointer<LLUIImage> VSUIImageProvider::publishImage(const std::string& key, const LLImageRaw& raw)
{
    return mImpl->resources.publish("control:" + key, raw);
}
void VSUIImageProvider::eraseImage(const std::string& key) { mImpl->resources.erase("control:" + key); }
LLPointer<LLImageRaw> vs_ui_image_raw(const LLUUID& id)
{ return sActiveProvider ? sActiveProvider->rawImage(id) : nullptr; }
LLPointer<LLImageRaw> vs_ui_image_aux(const LLUUID& id)
{ return sActiveProvider ? sActiveProvider->auxiliaryImage(id) : nullptr; }
LLPointer<LLImageRaw> VSUIImageProvider::auxiliaryImage(const LLUUID& id) const
{
    const auto found=mImpl->remote.find(id);
    return found!=mImpl->remote.end() && imageReady(id) ? found->second.auxiliary : nullptr;
}
LLPointer<LLImageRaw> VSUIImageProvider::rawImage(const LLUUID& id) const
{ return imageReady(id) ? mImpl->resources.rawImage("remote:" + id.asString()) : nullptr; }
bool vs_ui_image_ready(const LLUUID& id) { return sActiveProvider && sActiveProvider->imageReady(id); }
bool vs_ui_image_failed(const LLUUID& id) { return sActiveProvider && sActiveProvider->imageFailed(id); }
S32 vs_ui_image_discard(const LLUUID& id) {return sActiveProvider?sActiveProvider->imageDiscard(id):-1;}
S32 VSUIImageProvider::imageDiscard(const LLUUID& id) const
{
    const auto found=mImpl->remote.find(id);
    return found!=mImpl->remote.end() && imageReady(id)?found->second.discard:-1;
}
bool VSUIImageProvider::imageFailed(const LLUUID& id) const
{
    const auto owner = VSNativeSession::active();
    const auto image = mImpl->remote.find(id);
    return owner && owner->phase() == VSNativeSession::Phase::Connected && image != mImpl->remote.end() &&
        image->second.failedGeneration == owner->generation();
}
bool VSUIImageProvider::imageReady(const LLUUID& id) const
{
    const auto owner = VSNativeSession::active();
    const auto image = mImpl->remote.find(id);
    return owner && owner->phase() == VSNativeSession::Phase::Connected && image != mImpl->remote.end() &&
        image->second.readyGeneration == owner->generation();
}
void VSUIImageProvider::refreshImage(const LLUUID& id)
{
    if(auto* fetch=LLAppViewer::getTextureFetch()) fetch->deleteRequest(id,true);
    if(auto* cache=LLAppViewer::getTextureCache()) cache->removeFromCache(id);
    auto& entry=mImpl->remote[id];entry.fetching=false;entry.generation=entry.readyGeneration=entry.failedGeneration=0;
    entry.attempts=0;entry.retryAt=0;entry.discard=-1;entry.cacheChecked=false;
    getUIImageByID(id,-1);
}
void VSUIImageProvider::resetAccount()
{
    const auto fallback=getUIImage("Generic_Person");
    for(auto& [id,entry]:mImpl->remote)
    {
        if(entry.fetching) if(auto* fetch=LLAppViewer::getTextureFetch()) fetch->deleteRequest(id,true);
        mImpl->resources.erase("remote:"+id.asString());
        entry.pixels=fallback;entry.auxiliary=nullptr;entry.fetching=false;
        entry.facade->setNativeExtent(fallback->getWidth(),fallback->getHeight());
        entry.generation=entry.readyGeneration=entry.failedGeneration=0;
        entry.attempts=0;entry.retryAt=0;entry.discard=-1;entry.cacheChecked=false;
    }
}
void VSUIImageProvider::cleanUp()
{
    mImpl->retired=true;
    for(const auto& [id,entry]:mImpl->remote)
    {
        if(entry.fetching) if(auto* fetch=LLAppViewer::getTextureFetch()) fetch->deleteRequest(id,true);
        mImpl->resources.erase("remote:"+id.asString());
    }
    mImpl->remote.clear();
    for(const auto& entry:mImpl->cache) mImpl->resources.erase("skin:"+entry.first);
    mImpl->cache.clear();
}
LLUIImagePtr VSUIImageProvider::requestWorldImage(const LLUUID& id,unsigned dimension)
{
    if(id.isNull()) return nullptr;
    if(!mImpl->remote.count(id) && mImpl->remote.size()>=16384 && !mImpl->evict(id)) return nullptr;
    auto& entry=mImpl->remote[id];
    const unsigned target=std::clamp(dimension,1u,4096u);
    entry.target=entry.demandFrame==mImpl->demandFrame?std::max(entry.target,target):target;
    entry.demandFrame=mImpl->demandFrame;
    return getUIImageByID(id,-1);
}
LLUIImagePtr vs_request_world_image(const LLUUID& id,unsigned dimension)
{return sActiveProvider?sActiveProvider->requestWorldImage(id,dimension):nullptr;}
LLPointer<LLUIImage> VSUIImageProvider::getUIImageByID(const LLUUID &id, S32 priority)
{
    if (id.isNull())
        return nullptr;
    if (!mImpl->remote.count(id) && mImpl->remote.size()>=16384 && !mImpl->evict(id)) return getUIImage("Generic_Person");
    auto& entry = mImpl->remote[id];
    if(priority!=-2) {entry.touched=++mImpl->clock;entry.lastUsed=LLTimer::getTotalSeconds();}
    const auto owner = VSNativeSession::active();
    const U64 generation = owner ? owner->generation() : 0;
    if (!entry.facade)
    {
        entry.pixels = getUIImage("Generic_Person");
        const std::weak_ptr<Impl> weak = mImpl;
        entry.facade = new LLUIImage(id.asString(), entry.pixels->getWidth(), entry.pixels->getHeight(),
            [weak, id](S32 x, S32 y, S32 w, S32 h, const LLColor4& color, bool solid,
                       const LLRectf&, const LLRectf&, bool)
            {
                if (const auto images = weak.lock(); images && !images->retired)
                    if (const auto found = images->remote.find(id); found != images->remote.end())
                    {
                        if (solid) found->second.pixels->drawSolid(x, y, w, h, color);
                        else found->second.pixels->draw(x, y, w, h, color);
                    }
            });
    }
    if(priority>=0) entry.uiDemand=true;
    auto* fetch=LLAppViewer::getTextureFetch();
    if(!owner || owner->phase()!=VSNativeSession::Phase::Connected || !owner->regionReady() || !fetch || entry.fetching)
        return entry.facade;
    const U32 limit=llclamp(gSavedSettings.getU32("RenderMaxTextureResolution"),512u,4096u);
    const bool fullRes=gSavedSettings.getBOOL("TextureLoadFullRes");
    const bool changedLimit=entry.resolutionLimit && (entry.resolutionLimit!=limit || entry.fullRes!=fullRes);
    entry.resolutionLimit=limit;entry.fullRes=fullRes;
    const unsigned target=entry.uiDemand?4096u:std::min(limit,entry.target);
    S32 desired=0;
    if(!entry.uiDemand && !fullRes)
    {
        if(!entry.fullWidth) desired=MAX_DISCARD_LEVEL;
        else while(desired<MAX_DISCARD_LEVEL && (unsigned(entry.fullWidth>>desired)>target || unsigned(entry.fullHeight>>desired)>target)) ++desired;
    }
    // Keep a one-level hysteresis for projected demand; explicit preference
    // changes also replace an unnecessarily large resident image generation.
    if(!changedLimit && entry.readyGeneration==generation && entry.discard>=0 &&
        (entry.discard==desired || desired==entry.discard+1)) return entry.facade;
    if(changedLimit) {entry.attempts=0;entry.failedGeneration=0;}
    if(entry.attempts>=3 || LLTimer::getTotalSeconds()<entry.retryAt) return entry.facade;
    const std::string capability=owner->capability("ViewerAsset").empty()?owner->capability("GetTexture"):owner->capability("ViewerAsset");
    const std::string url=capability.empty()?std::string():capability+(capability.find('?')==std::string::npos?"?":"&")+"texture_id="+id.asString();
    const S32 queued=fetch->createRequest(FTT_DEFAULT,url,id,LLHost(),entry.uiDemand?100000.f:float(target*target),
        entry.fullWidth,entry.fullHeight,entry.components,desired,true,true);
    if(queued>=0)
    {
        entry.fetching=true;entry.generation=generation;entry.epoch=owner->regionEpoch();
        ++entry.attempts;entry.retryAt=LLTimer::getTotalSeconds()+5*entry.attempts;
        entry.deadline=LLTimer::getTotalSeconds()+20;
    }
    return entry.facade;
}
LLPointer<LLUIImage> VSUIImageProvider::getUIImage(const std::string &name, S32)
{
    if (name.empty() || name == "none")
        return nullptr;
    auto &c = *mImpl;
    if (auto found = c.cache.find(name); found != c.cache.end())
        return found->second;
    Image declaration;
    if (auto found = c.declarations.find(name); found != c.declarations.end())
        declaration = found->second;
    const std::string file = declaration.file_name.isProvided() ? declaration.file_name() : name;
    const std::filesystem::path relative(file);
    if (relative.is_absolute() || relative.has_root_name())
        throw std::runtime_error("Absolute UI skin asset path");
    for (const auto &part : relative)
        if (part == "..")
            throw std::runtime_error("Parent traversal in UI skin asset path");
    const auto path = gDirUtilp->findSkinnedFilename(LLDir::TEXTURES, file);
    LLPointer<LLImageFormatted> formatted = LLImageFormatted::createFromExtension(file);
    LLPointer<LLImageRaw> raw = new LLImageRaw;
    if (path.empty() || !formatted || !formatted->load(path) || !formatted->decode(raw, 0))
        throw std::runtime_error("Native UI skin image decode failed: " + file);
    c.resources.publish("skin:" + name, *raw);
    // Controls may resolve an image again by getName(); expose the original
    // skin declaration name while keeping GPU ownership keys private.
    auto image = c.resources.region("skin:" + name, name, LLRectf(0.f, 1.f, 1.f, 0.f));
    auto region = [&](const LLRect &r, S32 width, S32 height) {
        return LLRectf(llclamp(static_cast<F32>(r.mLeft) / width, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mTop) / height, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mRight) / width, 0.f, 1.f),
                       llclamp(static_cast<F32>(r.mBottom) / height, 0.f, 1.f));
    };
    if (declaration.clip.isProvided())
        image->setClipRegion(region(declaration.clip(), raw->getWidth(), raw->getHeight()));
    if (declaration.scale.isProvided())
        image->setScaleRegion(region(declaration.scale(), image->getWidth(), image->getHeight()));
    if (declaration.scale_type() != "scale_inner" && declaration.scale_type() != "scale_outer")
        throw std::runtime_error("Invalid native UI image scale style: " + name);
    image->setScaleStyle(declaration.scale_type() == "scale_outer" ? LLUIImage::SCALE_OUTER : LLUIImage::SCALE_INNER);
    c.cache.emplace(name, image);
    image->onImageLoaded();
    LL_INFOS("NativeUI") << "CPU skin asset: " << path << LL_ENDL;
    return image;
}
