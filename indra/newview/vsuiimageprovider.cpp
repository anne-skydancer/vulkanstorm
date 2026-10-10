// CPU skin assets published through native UI facades. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "lluiimage.h"
#include "vsuiimageprovider.h"
#include "lldir.h"
#include "llimage.h"
#include "llxmlnode.h"
#include "llxuiparser.h"
#include "vsuiresources.h"
#include "vsnativesession.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "lltimer.h"
#include "lluiimage.h"
#include <filesystem>
#include <map>
#include <stdexcept>
namespace
{
VSUIImageProvider* sActiveProvider = nullptr;
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
    struct Remote { LLUIImagePtr facade, pixels; U64 generation = 0, readyGeneration = 0, failedGeneration = 0; unsigned attempts = 0; double retryAt = 0; };
    std::map<LLUUID, Remote> remote;
    std::map<LLUUID, std::shared_ptr<LLCoreHttpUtil::HttpCoroutineAdapter>> requests;
    bool retired = false;
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
    const auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected) return;
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
        entry.second.facade->setNativeExtent(fallback->getWidth(), fallback->getHeight());
        entry.second.generation = entry.second.readyGeneration = entry.second.failedGeneration = 0;
        entry.second.attempts = 0; entry.second.retryAt = 0;
    }
}
void VSUIImageProvider::cleanUp()
{
    mImpl->retired = true;
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
    auto& entry = mImpl->remote[id];
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
    if (owner && owner->phase() == VSNativeSession::Phase::Connected && owner->capability("GetTexture").empty())
        entry.failedGeneration = generation;
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected ||
        owner->capability("GetTexture").empty() || entry.generation == generation || entry.attempts >= 3 || mImpl->requests.size() >= 32)
        return entry.facade;
    entry.generation = generation; ++entry.attempts;
    entry.retryAt = LLTimer::getTotalSeconds() + 5 * entry.attempts;
    const std::weak_ptr<Impl> weak = mImpl;
    const std::weak_ptr<VSNativeSession> session = owner;
    const std::string capability = owner->capability("GetTexture");
    const std::string url = capability + (capability.find('?') == std::string::npos ? "?" : "&") + "texture_id=" + id.asString();
    LLCoros::instance().launch("nativeUITexture", [weak, session, generation, id, url]()
    {
        auto images = weak.lock();
        if (!images || images->retired) return;
        auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeUITexture", LLCore::HttpRequest::DEFAULT_POLICY_ID);
        images->requests[id] = adapter;
        images.reset(); // Never retain the resource owner across a suspension.
        auto request = std::make_shared<LLCore::HttpRequest>();
        auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(15); options->setRetries(0);
        LLSD result = adapter->getRawAndSuspend(request, url, options);
        images = weak.lock(); const auto owner = session.lock();
        if (!images || images->retired) return;
        const auto pending = images->requests.find(id);
        if (pending == images->requests.end() || pending->second != adapter) return;
        images->requests.erase(pending);
        if (!owner || owner->generation() != generation || owner->phase() != VSNativeSession::Phase::Connected) return;
        auto found = images->remote.find(id);
        if (found == images->remote.end() || found->second.generation != generation) return;
        // A completed final attempt is terminal until explicit refresh or account reset.
        // Mark before decoding so every HTTP/decode failure has the same outcome.
        if (found->second.attempts >= 3) found->second.failedGeneration = generation;
        if (!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"])) return;
        const auto& bytes = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary();
        if (bytes.empty() || bytes.size() > 16 * 1024 * 1024) return;
        LLPointer<LLImageFormatted> formatted = LLImageFormatted::createFromExtension("j2c");
        auto* data = static_cast<U8*>(ll_aligned_malloc_16(bytes.size()));
        if (!data) return;
        std::copy(bytes.begin(), bytes.end(), data); formatted->setData(data, S32(bytes.size()));
        if (!formatted->updateData() || formatted->getWidth() > 4096 || formatted->getHeight() > 4096) return;
        LLPointer<LLImageRaw> raw = new LLImageRaw;
        if (!formatted->decode(raw, 0)) return;
        found->second.pixels = images->resources.publish("remote:" + id.asString(), *raw);
        found->second.facade->setNativeExtent(raw->getWidth(), raw->getHeight());
        found->second.readyGeneration = generation;
        found->second.failedGeneration = 0;
        found->second.facade->onImageLoaded();
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
