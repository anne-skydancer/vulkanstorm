#include "llviewerprecompiledheaders.h"
#include "vsworldsound.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsregioncontext.h"
#include "llviewerwindow.h"
#include "llaudiosourcevo.h"
#include "vsobjectsoundcontroller.h"
#include "llmutelist.h"
#include "llframetimer.h"
#include "llviewerobjectlist.h"
#include "fsassetblacklist.h"
#include <boost/signals2/connection.hpp>

struct VSWorldSound::Impl : std::enable_shared_from_this<VSWorldSound::Impl>
{
    std::weak_ptr<VSNativeSession> owner;
    std::function<bool()> current;
    bool retired = false;
    boost::signals2::scoped_connection objectsChanged, blacklistChanged;
    struct Source
    {
        VSWorldScene::ObjectHandle object;
        LLAudioSourceVO* source = nullptr; // Engine owns it; validate by actual ID before every use.
        LLUUID logID; // Engine incarnation, including allocator address reuse.
        F32 gain = 0;
    };
    std::map<LLUUID, Source> sources;
    struct Pending
    {
        LLUUID asset, owner;
        F32 gain = 0;
        U8 flags = 0;
        F64 deadline = 0;
        VSRegionContext region;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::map<LLUUID, Pending> postponed;
    std::set<LLUUID> pendingBlacklistRetirements;
    VSWorldScene* scene() const
    {
        if (retired || !current || !current() || !gViewerWindow || !gViewerWindow->nativeContext()) return nullptr;
        return &gViewerWindow->nativeContext()->world();
    }
    static bool same(const VSWorldScene::ObjectHandle& a, const VSWorldScene::ObjectHandle& b)
    {
        return a.host == b.host && a.id == b.id && a.local == b.local && a.region == b.region &&
            a.generation == b.generation && a.account == b.account && a.accountGeneration == b.accountGeneration;
    }
    static bool live(const Source& record)
    {
        const auto* source = gAudiop ? gAudiop->findAudioSource(record.object.id) : nullptr;
        return source && source == record.source && source->getLogID() == record.logID;
    }
    void cleanup(Source& record)
    {
        if (live(record))
            gAudiop->cleanupAudioSource(record.source);
        record.source = nullptr;
    }
    void reset()
    {
        retired = true;
        objectsChanged.disconnect(); blacklistChanged.disconnect();
        postponed.clear(); pendingBlacklistRetirements.clear();
        for (auto& [id, source] : sources) cleanup(source);
        sources.clear();
    }
    LLAudioSourceVO* ensure(Source& record, const LLUUID& ownerID)
    {
        auto* world = scene();
        if (!gAudiop || !world || !world->current(record.object)) return nullptr;
        if (live(record)) return record.source;
        record.source = nullptr;
        const auto weak = weak_from_this(); const auto token = record.object;
        auto* source = new LLAudioSourceVO(token.id, ownerID, .01f, [weak, token]() -> std::optional<VSAudioObjectState>
        {
            const auto self = weak.lock(); auto* world = self ? self->scene() : nullptr;
            if (!world) return {};
            const auto object = world->objectSound(token);
            return object ? std::optional<VSAudioObjectState>(object->state) : std::nullopt;
        });
        if (scene() != world || !world->current(token)) { delete source; return nullptr; }
        gAudiop->addAudioSource(source); record.source = source; record.logID = source->getLogID();
        return source;
    }
    void apply(const VSWorldScene::ObjectSound& object, const LLUUID& asset, const LLUUID& ownerID, F32 gain, U8 flags)
    {
        auto* world = scene(); if (!world || !world->current(object.object)) return;
        auto found = sources.find(object.object.id);
        if (found != sources.end() && !same(found->second.object, object.object))
        {
            cleanup(found->second); sources.erase(found); found = sources.end();
        }
        // Null one-shot updates never allocate an otherwise nonexistent source.
        if (found == sources.end() && asset.isNull()) return;
        if (found == sources.end()) found = sources.emplace(object.object.id, Source{object.object}).first;
        auto& record = found->second;
        if (record.source && !live(record)) record.source = nullptr;
        vs_set_attached_sound(record.source, record.gain, asset, ownerID, gain, flags,
            [&] { return object.state.attachment; }, [&] { return object.state.position; },
            [&](const LLUUID& id) { ensure(record, id); });
    }
    void changed(const VSWorldScene::ObjectHandle& token)
    {
        auto* world = scene(); if (!world) return;
        const auto object = world->objectSound(token);
        if (!object)
        {
            if (auto found = sources.find(token.id); found != sources.end() && same(found->second.object, token))
            { cleanup(found->second); sources.erase(found); }
            return;
        }
        // As with LLViewerObject::processUpdateMessage, full/compressed object
        // sound data uses the original controller directly; standalone packet
        // muting/antispam is performed by the shared message handler.
        apply(*object, object->asset, object->owner, object->gain, object->flags);
        if (auto pending = postponed.find(token.id); pending != postponed.end())
        {
            const auto data = pending->second;
            postponed.erase(pending);
            if (data.region.current() && data.region.getHost() == token.host &&
                LLFrameTimer::getTotalSeconds() <= data.deadline)
                attached(token.id, data.asset, data.owner, data.gain, data.flags, token.host);
        }
    }
    void attached(const LLUUID& id, const LLUUID& asset, const LLUUID& ownerID, F32 gain, U8 flags, const LLHost& host)
    {
        auto* world = scene(); if (!world) return;
        const auto token = world->object(id);
        if (token && token->host == host)
        {
            const auto object = world->objectSound(*token); if (!object) return;
            if (LLMuteList::getInstance()->isMuted(id) ||
                LLMuteList::getInstance()->isMuted(ownerID, LLMute::flagObjectSounds) ||
                !gAgent.canAccessMaturityAtGlobal(object->state.position)) return;
            apply(*object, asset, ownerID, gain, flags);
        }
        else if (asset.notNull())
        {
            const auto region = vs_region_context(host); if (!region || !region->current()) return;
            auto lease = world->reserveSpatialBytes(sizeof(Pending) + sizeof(LLUUID));
            if (!lease) return;
            postponed[id] = {asset, ownerID, gain, flags, LLFrameTimer::getTotalSeconds() + 15., *region, std::move(lease)};
        }
        else postponed.erase(id);
    }
};

VSWorldSound::VSWorldSound(std::weak_ptr<VSNativeSession> owner) : mImpl(std::make_shared<Impl>())
{
    mImpl->owner = owner;
    if (const auto account = owner.lock()) mImpl->current = account->accountGuard(true);
    if (auto* world = mImpl->scene())
    {
        const auto weak = std::weak_ptr<Impl>(mImpl);
        mImpl->objectsChanged = world->observeObjectSounds([weak](const VSWorldScene::ObjectHandle& object)
        { if (auto self = weak.lock()) self->changed(object); });
        mImpl->blacklistChanged = FSAssetBlacklist::instance().setBlacklistChangedCallback(
            [weak](const FSAssetBlacklist::changed_signal_data_t& entries, FSAssetBlacklist::eBlacklistOperation op)
            {
                const auto self = weak.lock(); auto* scene = self ? self->scene() : nullptr;
                if (!scene || op != FSAssetBlacklist::eBlacklistOperation::BLACKLIST_ADD) return;
                for (const auto& [id, data] : entries)
                    if (data && (data->type == LLAssetType::AT_OBJECT || data->type == LLAssetType::AT_PERSON))
                        self->pendingBlacklistRetirements.insert(id);
            });
    }
}
VSWorldSound::~VSWorldSound() { reset(); }
void VSWorldSound::reset() { if (mImpl) mImpl->reset(); }
void VSWorldSound::attached(const LLUUID& object, const LLUUID& asset, const LLUUID& owner, F32 gain, U8 flags, const LLHost& sender)
{ mImpl->attached(object, asset, owner, gain, flags, sender); }
void VSWorldSound::preload(const LLUUID& id, const LLUUID& asset, const LLUUID& owner, const LLHost& sender)
{
    auto* scene = mImpl->scene(); if (!gAudiop || !scene) return;
    const auto token = scene->object(id); if (!token || token->host != sender) return;
    const auto object = scene->objectSound(*token); if (!object) return;
    if (LLMuteList::getInstance()->isMuted(id) || LLMuteList::getInstance()->isMuted(owner, LLMute::flagObjectSounds)) return;
    auto& record = mImpl->sources[id];
    if (record.source && !Impl::same(record.object, *token)) mImpl->cleanup(record);
    record.object = *token;
    auto* source = mImpl->ensure(record, owner); if (!source) return;
    auto* data = gAudiop->getAudioData(asset);
    if (gAgent.canAccessMaturityAtGlobal(object->state.position)) source->addAudioData(data, false);
}
void VSWorldSound::gain(const LLUUID& id, F32 gain, const LLHost& sender)
{
    auto* scene = mImpl->scene(); if (!scene) return;
    const auto token = scene->object(id); if (!token || token->host != sender) return;
    const auto found = mImpl->sources.find(id);
    if (found == mImpl->sources.end() || !Impl::same(found->second.object, *token) || !Impl::live(found->second)) return;
    found->second.gain = gain; found->second.source->setGain(gain);
}
void VSWorldSound::tick()
{
    auto* scene = mImpl->scene(); if (!scene) { reset(); return; }
    // Original derender controllers must finish ObjectDeselect/publication
    // ordering before a blacklist observer retires current scene identities.
    auto retirements = std::move(mImpl->pendingBlacklistRetirements);
    mImpl->pendingBlacklistRetirements.clear();
    for (const auto& id : retirements)
        if (const auto object = scene->object(id)) scene->derender(*object);
    for (auto found = mImpl->sources.begin(); found != mImpl->sources.end();)
        if (!scene->current(found->second.object)) { mImpl->cleanup(found->second); found = mImpl->sources.erase(found); }
        else ++found;
    for (auto found = mImpl->postponed.begin(); found != mImpl->postponed.end();)
        if (!found->second.region.current() || LLFrameTimer::getTotalSeconds() > found->second.deadline) found = mImpl->postponed.erase(found);
        else ++found;
}
bool VSWorldSound::isAttachment(const LLUUID& id) const
{
    auto* scene = mImpl->scene(); if (!scene) return false;
    const auto token = scene->object(id); const auto snapshot = token ? scene->objectSnapshot(*token) : std::nullopt;
    return snapshot && snapshot->attachment;
}
