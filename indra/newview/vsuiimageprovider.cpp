// CPU skin assets published through native UI facades. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "lluiimage.h"
#include "vsuiimageprovider.h"
#include "lldir.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimageworker.h"
#include "lltexturecache.h"
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
#include <map>
#include <stdexcept>
#include <utility>
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
class NativeDecoded final : public LLImageDecodeThread::Responder
{
    std::function<void(bool,LLPointer<LLImageRaw>,LLPointer<LLImageRaw>)> mComplete;
public:
    explicit NativeDecoded(decltype(mComplete) complete):mComplete(std::move(complete)) {}
    void completed(bool success,const std::string&,LLImageRaw* raw,LLImageRaw* auxiliary,U32) override
    {
        LL::WorkQueue::getInstance("mainloop")->post([complete=mComplete,success,raw=LLPointer<LLImageRaw>(raw),auxiliary=LLPointer<LLImageRaw>(auxiliary)]
            {complete(success,raw,auxiliary);});
    }
};
// Use the same encoded UUID cache as LLTextureFetch. Only complete validated
// J2C entries enter this full-resolution subscriber; partial entries remain in
// the shared cache for streaming consumers and fall back to the network here.
struct NativeCacheResult
{
    std::atomic<bool> done{false};
    LLPointer<LLImageFormatted> image;
    S32 imageSize = 0;
    bool success = false;
    std::shared_ptr<LLEventMailDrop> ready = std::make_shared<LLEventMailDrop>("nativeTextureCacheRead", true);
};
class NativeCacheRead final : public LLTextureCache::ReadResponder
{
    std::shared_ptr<NativeCacheResult> mResult;
public:
    explicit NativeCacheRead(std::shared_ptr<NativeCacheResult> result) : mResult(std::move(result)) {}
    void completed(bool success) override
    {
        mResult->image = mFormattedImage;
        mResult->imageSize = mImageSize;
        mResult->success = success;
        mResult->done.store(true, std::memory_order_release);
        LL::WorkQueue::getInstance("mainloop")->post([result = mResult] { result->ready->post(LLSD()); });
    }
};
struct NativeCacheReadHandle
{
    LLTextureCache* cache;
    LLTextureCache::handle_t handle;
    ~NativeCacheReadHandle()
    {
        // Viewer shutdown owns the cache after its pointer is retired. A late
        // fiber must never call an already destroyed worker queue.
        if (cache == LLAppViewer::getTextureCache() && handle != LLWorkerThread::nullHandle())
            cache->readComplete(handle, true);
    }
};
LLPointer<LLImageFormatted> readNativeTexture(const LLUUID& id)
{
    auto* cache = LLAppViewer::getTextureCache();
    if (!cache) return nullptr;
    auto result = std::make_shared<NativeCacheResult>();
    NativeCacheReadHandle read{cache, cache->readFromCache(id, 0, 16 * 1024 * 1024, new NativeCacheRead(result))};
    if (read.handle == LLWorkerThread::nullHandle()) return nullptr;
    llcoro::suspendUntilEventOnWithTimeout(*result->ready, 15, LLSDMap("timeout", true));
    if (!result->done.load(std::memory_order_acquire) || !result->success || !result->image ||
        result->imageSize <= 0 || result->imageSize > 16 * 1024 * 1024 ||
        result->image->getDataSize() != result->imageSize || result->image->getCodec() != IMG_CODEC_J2C)
        return nullptr;
    return result->image;
}
class NativeCacheWrite final : public LLTextureCache::WriteResponder
{
    // The cache worker borrows encoded bytes until writeComplete retires it.
    LLPointer<LLImageFormatted> mImage;
    std::shared_ptr<const VSResourceBudget::Reservation> mResidency;
public:
    NativeCacheWrite(LLPointer<LLImageFormatted> image, std::shared_ptr<const VSResourceBudget::Reservation> residency)
        : mImage(std::move(image)), mResidency(std::move(residency)) {}
    void completed(bool) override {}
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
    struct Remote { LLUIImagePtr facade, pixels; LLPointer<LLImageRaw> auxiliary; U64 generation = 0, readyGeneration = 0, failedGeneration = 0,touched=0; unsigned attempts = 0; double retryAt = 0; bool cacheChecked = false; };
    std::map<LLUUID, Remote> remote;
    std::map<LLUUID, std::shared_ptr<LLCoreHttpUtil::HttpCoroutineAdapter>> requests;
    std::vector<LLTextureCache::handle_t> cacheWrites;
    U64 diskHits = 0, diskMisses = 0, diskWrites = 0, networkRequests = 0;
    bool retired = false;
    U64 regionEpoch=0,clock=0;
    VSResourceBudget masks{64 * 1024 * 1024},decodes{256 * 1024 * 1024};
    bool evict(const LLUUID& preserve)
    {
        auto victim=remote.end();
        for (auto found=remote.begin();found!=remote.end();++found)
        {
            if (found->first==preserve || requests.count(found->first) ||
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
    if (mImpl->retired) return;
    if (auto* cache = LLAppViewer::getTextureCache())
        mImpl->cacheWrites.erase(std::remove_if(mImpl->cacheWrites.begin(), mImpl->cacheWrites.end(),
            [cache](auto handle) { return cache->writeComplete(handle); }), mImpl->cacheWrites.end());
    else mImpl->cacheWrites.clear();
    const auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected) return;
    if (mImpl->regionEpoch!=owner->regionEpoch())
    {
        auto requests=std::exchange(mImpl->requests,{});mImpl->regionEpoch=owner->regionEpoch();
        for (auto& [id,entry]:mImpl->remote)
            if (entry.readyGeneration!=owner->generation())
            {entry.generation=entry.failedGeneration=0;entry.attempts=0;entry.retryAt=0;}
        for (auto& [id,request]:requests) request->cancelSuspendedOperation();
    }
    if (!owner->regionReady()) return;
    std::vector<LLUUID> pending;
    for (auto& [id, entry] : mImpl->remote)
        if (entry.readyGeneration != owner->generation() && !mImpl->requests.count(id) && entry.attempts < 3 &&
            LLTimer::getTotalSeconds() >= entry.retryAt)
        { entry.generation = 0; pending.push_back(id); }
    for (const auto& id : pending)
    {
        if (mImpl->requests.size() >= 32) break;
        getUIImageByID(id);
    }
}
LLSD VSUIImageProvider::textureCacheEvidence() const
{
    LLSD result;
    result["disk_hits"] = ll_sd_from_U64(mImpl->diskHits);
    result["disk_misses"] = ll_sd_from_U64(mImpl->diskMisses);
    result["disk_writes_started"] = ll_sd_from_U64(mImpl->diskWrites);
    result["network_requests"] = ll_sd_from_U64(mImpl->networkRequests);
    result["pending_disk_writes"] = S32(mImpl->cacheWrites.size());
    return result;
}
LLSD vs_ui_texture_cache_evidence()
{ return sActiveProvider ? sActiveProvider->textureCacheEvidence() : LLSD(); }
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
    if (auto request = mImpl->requests.find(id); request != mImpl->requests.end())
    {
        const auto adapter = request->second;
        mImpl->requests.erase(request);
        adapter->cancelSuspendedOperation();
    }
    if (auto image = mImpl->remote.find(id); image != mImpl->remote.end())
        image->second.generation = image->second.readyGeneration = image->second.failedGeneration = 0;
    // Explicit refresh/local reload must not resurrect an older disk image.
    if (auto* cache = LLAppViewer::getTextureCache()) cache->removeFromCache(id);
    mImpl->remote[id].cacheChecked = true;
    mImpl->remote[id].attempts = 0; mImpl->remote[id].retryAt = 0;
    getUIImageByID(id);
}
void VSUIImageProvider::resetAccount()
{
    auto requests = std::move(mImpl->requests);
    for (auto& request : requests) request.second->cancelSuspendedOperation();
    const auto fallback = getUIImage("Generic_Person");
    for (auto& entry : mImpl->remote)
    {
        mImpl->resources.erase("remote:" + entry.first.asString());
        entry.second.pixels = fallback;
        entry.second.auxiliary=nullptr;
        entry.second.facade->setNativeExtent(fallback->getWidth(), fallback->getHeight());
        entry.second.generation = entry.second.readyGeneration = entry.second.failedGeneration = 0;
        entry.second.attempts = 0; entry.second.retryAt = 0; entry.second.cacheChecked = false;
    }
}
void VSUIImageProvider::cleanUp()
{
    mImpl->retired = true;
    if (auto* cache = LLAppViewer::getTextureCache())
        for (auto handle : mImpl->cacheWrites) cache->writeComplete(handle, true);
    mImpl->cacheWrites.clear();
    auto requests = std::move(mImpl->requests);
    for (auto& request : requests) request.second->cancelSuspendedOperation();
    for (const auto& entry : mImpl->remote) mImpl->resources.erase("remote:" + entry.first.asString());
    mImpl->remote.clear();
    for (const auto &entry : mImpl->cache)
        mImpl->resources.erase("skin:" + entry.first);
    mImpl->cache.clear();
}
LLPointer<LLUIImage> VSUIImageProvider::getUIImageByID(const LLUUID &id, S32)
{
    if (id.isNull())
        return nullptr;
    if (!mImpl->remote.count(id) && mImpl->remote.size()>=16384 && !mImpl->evict(id)) return getUIImage("Generic_Person");
    auto& entry = mImpl->remote[id];entry.touched=++mImpl->clock;
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
    // Match the viewer texture fetcher: ViewerAsset is the current asset
    // endpoint; GetTexture is only the legacy endpoint when it is absent.
    const std::string capability = owner ? (owner->capability("ViewerAsset").empty()
        ? owner->capability("GetTexture") : owner->capability("ViewerAsset")) : std::string();
    if (owner && owner->phase() == VSNativeSession::Phase::Connected && capability.empty())
        entry.failedGeneration = generation;
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected || !owner->regionReady() ||
        capability.empty() || entry.generation == generation || entry.attempts >= 3 || mImpl->requests.size() >= 32)
        return entry.facade;
    const bool checkDisk = !entry.cacheChecked;
    entry.cacheChecked = true;
    entry.generation = generation; ++entry.attempts;
    entry.retryAt = LLTimer::getTotalSeconds() + 5 * entry.attempts;
    const std::weak_ptr<Impl> weak = mImpl;
    const std::weak_ptr<VSNativeSession> session = owner;
    const U64 epoch=owner->regionEpoch();
    const std::string url = capability + (capability.find('?') == std::string::npos ? "?" : "&") + "texture_id=" + id.asString();
    LLCoros::instance().launch("nativeUITexture", [weak, session, generation, epoch, id, url, checkDisk]()
    {
        auto images = weak.lock();
        if (!images || images->retired) return;
        auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeUITexture", LLCore::HttpRequest::DEFAULT_POLICY_ID);
        images->requests[id] = adapter;
        auto readBudget = checkDisk ? images->decodes.reserve(16 * 1024 * 1024) : nullptr;
        images.reset(); // Never retain the resource owner across a suspension.
        LLPointer<LLImageFormatted> cached = readBudget ? readNativeTexture(id) : nullptr;
        readBudget.reset();
        {
        const auto currentOwner = session.lock();
        images = weak.lock();
        if (!images || images->retired || !currentOwner || VSNativeSession::active() != currentOwner ||
            currentOwner->generation() != generation || currentOwner->regionEpoch() != epoch ||
            !currentOwner->regionReady() || !images->requests.count(id) || images->requests.at(id) != adapter) return;
        if (checkDisk) { if (cached) ++images->diskHits; else ++images->diskMisses; }
        if (!cached) ++images->networkRequests;
        }
        images.reset();
        auto request = std::make_shared<LLCore::HttpRequest>();
        auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(15); options->setRetries(0);
        options->setMaximumResponseBodyBytes(16 * 1024 * 1024);
        LLSD result;
        if (!cached) result = adapter->getRawAndSuspend(request, url, options);
        LL::WorkQueue::getInstance("mainloop")->post([weak,session,generation,epoch,id,adapter,result,cached]()
        {
        auto images = weak.lock(); const auto owner = session.lock();
        if (!images || images->retired) return;
        if (!owner || VSNativeSession::active()!=owner || owner->generation()!=generation ||
            owner->regionEpoch()!=epoch || !owner->regionReady()) return;
        const auto pending = images->requests.find(id);
        if (pending == images->requests.end() || pending->second != adapter) return;
        images->requests.erase(pending);
        if (!owner || owner->generation() != generation || owner->phase() != VSNativeSession::Phase::Connected) return;
        auto found = images->remote.find(id);
        if (found == images->remote.end() || found->second.generation != generation) return;
        // A completed final attempt is terminal until explicit refresh or account reset.
        // Mark before decoding so every HTTP/decode failure has the same outcome.
        if (found->second.attempts >= 3) found->second.failedGeneration = generation;
        LLPointer<LLImageFormatted> formatted = cached;
        if (!formatted)
        {
            const auto status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]);
            if (!status)
            {
                if (status == LLCore::HttpStatus(LLCore::HttpStatus::LLCORE, LLCore::HE_RESPONSE_TOO_LARGE))
                    found->second.failedGeneration = generation;
                return;
            }
            const auto& bytes = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
            if (bytes.empty() || bytes.size() > 16 * 1024 * 1024) return;
            formatted = LLImageFormatted::createFromExtension("j2c");
            auto* data = static_cast<U8*>(ll_aligned_malloc_16(bytes.size()));
            if (!data) return;
            std::copy(bytes.begin(), bytes.end(), data); formatted->setData(data, S32(bytes.size()));

        }
        if (!formatted->updateData() || formatted->getWidth() <= 0 || formatted->getHeight() <= 0 ||
            formatted->getWidth() > 4096 || formatted->getHeight() > 4096)
        {
            if (cached)
            {
                if (auto* cache = LLAppViewer::getTextureCache()) cache->removeFromCache(id);
                found->second.generation = 0; found->second.retryAt = 0;
                --found->second.attempts;
            }
            return;
        }
        const bool needsMask=formatted->getComponents()==5;
        auto decodeResidency=images->decodes.reserve(std::size_t(formatted->getDataSize())+std::size_t(formatted->getWidth())*formatted->getHeight()*(needsMask?5:4));
        auto* decoder=LLAppViewer::getImageDecodeThread();
        if(!decodeResidency || !decoder) {found->second.failedGeneration=generation;return;}
        // Retain the request slot through CPU decoding so subsequent pumping
        // cannot turn the bounded HTTP queue into an unbounded decode queue.
        images->requests[id]=adapter;
        LLPointer<LLImageDecodeThread::Responder> decoded=new NativeDecoded(
            [weak,session,generation,epoch,id,adapter,decodeResidency,needsMask,formatted,cached](bool success,LLPointer<LLImageRaw> raw,LLPointer<LLImageRaw> mask)
        {
        auto images=weak.lock();const auto owner=session.lock();
        if(!images || images->retired || !owner || VSNativeSession::active()!=owner || owner->generation()!=generation ||
           owner->regionEpoch()!=epoch || !owner->regionReady())return;
        const auto pending=images->requests.find(id);
        if(pending==images->requests.end() || pending->second!=adapter)return;
        images->requests.erase(pending);
        const auto found=images->remote.find(id);
        if(found==images->remote.end() || found->second.generation!=generation)return;
        if (!success || !raw)
        {
            if (cached)
            {
                if (auto* cache = LLAppViewer::getTextureCache()) cache->removeFromCache(id);
                found->second.generation = 0; found->second.retryAt = 0;
                --found->second.attempts;
            }
            return;
        }
        if (!cached && images->cacheWrites.size() < 16)
            if (auto* cache = LLAppViewer::getTextureCache())
            {
                const auto handle = cache->writeToCache(id, formatted->getData(), formatted->getDataSize(),
                    formatted->getDataSize(), raw, 0, new NativeCacheWrite(formatted, decodeResidency));
                if (handle != LLWorkerThread::nullHandle())
                {
                    images->cacheWrites.push_back(handle);
                    ++images->diskWrites;
                }
            }
        LLPointer<LLImageRaw> auxiliary;
        if (needsMask)
        {
            if(!mask || mask->getComponents()!=1 || mask->getWidth()!=raw->getWidth() || mask->getHeight()!=raw->getHeight())return;
            auto residency=images->masks.reserve(std::size_t(raw->getWidth())*raw->getHeight());
            while (!residency && images->evict(id)) residency=images->masks.reserve(std::size_t(raw->getWidth())*raw->getHeight());
            if (!residency) {found->second.failedGeneration=generation;return;}
            auxiliary=new NativeMaskImage(std::move(residency),mask->getWidth(),mask->getHeight());
            auxiliary->copy(mask);
        }
        auto pixels=images->resources.publish("remote:"+id.asString(),*raw);
        while (!pixels && images->evict(id)) pixels=images->resources.publish("remote:"+id.asString(),*raw);
        if (!pixels) {found->second.failedGeneration=generation;return;}
        found->second.auxiliary=auxiliary;
        found->second.pixels = std::move(pixels);
        found->second.facade->setNativeExtent(raw->getWidth(), raw->getHeight());
        found->second.readyGeneration = generation;
        found->second.failedGeneration = 0;
        found->second.facade->onImageLoaded();
        });
        if(!decoder->decodeImage(formatted,0,needsMask,decoded))
        {images->requests.erase(id);found->second.failedGeneration=generation;}
        });
    });
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
