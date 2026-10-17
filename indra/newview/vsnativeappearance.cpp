// Connected native appearance transactions and attachment queues. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsnativeappearance.h"
#include "vsnativesession.h"
#include "vsworldavatar.h"
#include "vsavatarservices.h"
#include "vswearable.h"
#include "vsresourcebudget.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llappearancemgr.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llinventoryfunctions.h"
#include "llviewerinventory.h"
#include "llassetstorage.h"
#include "llfilesystem.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llagent.h"
#include "llagentbenefits.h"
#include "llavatarappearance.h"
#include "llappviewer.h"
#include "llframetimer.h"
#include "llcorehttputil.h"
#include "llsdutil.h"
#include "llcoros.h"
#include "llnotificationsutil.h"
#include "llgesturemgr.h"
#include "llviewercontrol.h"
#include "llviewerassetupload.h"
#include "message.h"
#include "rlvactions.h"
#include "rlvhelper.h"
#include "rlvhandler.h"
#include "rlvlocks.h"
#include "lltooldraganddrop.h"
#include <sstream>
namespace
{
VSWorldAvatar* selfAvatar()
{ return gViewerWindow && gViewerWindow->nativeContext() ? gViewerWindow->nativeContext()->world().selfAvatar() : nullptr; }
} // namespace
struct VSNativeAppearance::Impl : std::enable_shared_from_this<Impl>
{
    std::weak_ptr<VSNativeSession> session;
    VSNativeSession::AccountHandle account;
    VSResourceBudget               budget{ 512 * 1024 * 1024 };
    struct Entry
    {
        LLUUID                      item, asset;
        LLWearableType::EType       type = LLWearableType::WT_NONE;
        std::string                 order, error;
        std::shared_ptr<VSWearable> wearable;
        bool                        requested = false;
        double                      deadline  = 0;
    };
    std::map<LLUUID, Entry>                     entries;
    std::map<LLUUID, LLUUID>                    applied;
    LLSD                                        fallback;
    LLUUID                                      cof;
    S32                                         version = -1, requestedBake = -1;
    U64                                         revision = 1, avatarGeneration = 0, bakeRegion = 0;
    bool                                        dirty        = true;
    unsigned                                    bakeAttempts = 0;
    double                                      nextBake     = 0;
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t baker;
    std::string                                 error;
    struct AttachmentRequest
    {
        VSNativeSession::InventoryHandle inventory;
        U8                               point = 0;
        bool                             add = true, attach = true;
        unsigned                         attempts = 0;
        double                           deadline = 0;
        LLPointer<LLInventoryCallback>   callback;
    };
    std::map<LLUUID, AttachmentRequest> attachments;
    struct BakeUpload
    {
        U64      generation = 0;
        LLUUID   asset;
        unsigned attempts = 0;
        bool     pending  = false;
        double   deadline = 0;
    };
    struct UploadRequest
    {
        std::weak_ptr<Impl>                                  owner;
        U64                                                  region = 0, avatar = 0, generation = 0;
        unsigned                                             index   = 0;
        unsigned                                             attempt = 0;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::map<unsigned, BakeUpload>         uploads;
    U32                                    appearanceSerial       = 0;
    U64                                    sentAppearanceRevision = 0;
    double                                 nextLocalBake          = 0;
    S32                                    reconciledCOF          = -1;
    U64                                    reconciledRegion       = 0;
    double                                 reconcileAt            = 0;
    std::optional<std::pair<LLUUID, bool>> categoryRequest;
    struct PendingLink
    {
        VSNativeSession::InventoryHandle inventory;
        LLWearableType::EType            type;
        U64                              serial;
        double                           deadline;
        LLPointer<LLInventoryCallback>   callback;
        bool                             completed = false, failed = false;
    };
    std::map<LLUUID, PendingLink> pendingLinks;
    U64                           nextLinkSerial = 0;
    unsigned                      pendingOrder   = 0;
    U64                           orderSerial    = 0;
    bool                          orderRecovery  = false;
    struct QueuedRemoval
    {
        std::vector<VSNativeSession::InventoryHandle> items;
        LLPointer<LLInventoryCallback>                callback;
    };
    std::vector<QueuedRemoval>              queuedRemovals;
    std::map<LLUUID, std::pair<LLUUID, U8>> observedAttachments;
    U64                                     attachmentRegion = 0;
    struct Request
    {
        std::weak_ptr<Impl> owner;
        U64                 revision = 0, avatar = 0;
        LLUUID              item, asset;
    };
    explicit Impl(const std::shared_ptr<VSNativeSession>& owner) : session(owner), account(owner->account()) {}
    ~Impl()
    {
        if (baker)
            baker->cancelSuspendedOperation();
    }
    bool current() const
    {
        const auto owner = session.lock();
        return owner && owner->current(account) && owner->phase() == VSNativeSession::Phase::Connected;
    }
    static void loaded(const LLUUID& asset, LLAssetType::EType type, void* data, S32 status, LLExtStat)
    {
        const auto request = std::shared_ptr<Request>(static_cast<Request*>(data));
        LLAppViewer::instance()->postToMainCoro(
            [request, asset, type, status]
            {
                const auto owner  = request->owner.lock();
                const auto avatar = selfAvatar();
                if (!owner || !owner->current() || request->revision != owner->revision || !avatar ||
                    avatar->ownerGeneration() != request->avatar)
                    return;
                const auto found = owner->entries.find(request->item);
                if (found == owner->entries.end() || found->second.asset != asset || asset != request->asset)
                    return;
                auto& entry = found->second;
                if (!entry.error.empty() || !entry.requested || LLFrameTimer::getTotalSeconds() > entry.deadline)
                    return;
                const auto session = owner->session.lock();
                if (const auto current = session->inventory(entry.item); current && current->asset != entry.asset)
                {
                    entry.error = "Wearable inventory asset changed during loading";
                    return;
                }
                if (status != LL_ERR_NOERR)
                {
                    entry.error = "Wearable asset fetch failed";
                    return;
                }
                try
                {
                    LLFileSystem file(asset, type, LLFileSystem::READ);
                    const S32    size = file.getSize();
                    if (size <= 0 || size > 1024 * 1024)
                    {
                        entry.error = "Wearable asset exceeds admission limit";
                        return;
                    }
                    // Charge immutable clones, their layer/mask working storage and
                    // source bytes through the last actual wearable owner.
                    auto residency = owner->budget.reserve(std::size_t(size) + 4 * 1024 * 1024);
                    if (!residency)
                    {
                        entry.error = "Wearable CPU budget exhausted";
                        return;
                    }
                    std::string bytes(std::size_t(size), '\0');
                    if (!file.read(reinterpret_cast<U8*>(bytes.data()), size))
                    {
                        entry.error = "Wearable cache read failed";
                        return;
                    }
                    const auto weak     = std::weak_ptr<Impl>(owner);
                    const auto item     = entry.item;
                    auto       wearable = VSWearable::create(
                        item, asset,
                        [weak, item, asset]
                        {
                            const auto state  = weak.lock();
                            const auto avatar = selfAvatar();
                            if (state && state->current() && avatar && state->applied.count(item) && state->applied.at(item) == asset)
                                avatar->invalidateLocalAppearance();
                        },
                        std::move(residency));
                    // Shared import links cross-wearable drivers to the avatar.
                    // Preserve its authoritative values until the whole candidate
                    // outfit is admitted, including on a malformed asset.
                    auto&                                       appearance = avatar->appearanceOwner();
                    std::vector<std::pair<LLVisualParam*, F32>> saved;
                    for (auto* param = appearance.getFirstVisualParam(); param; param = appearance.getNextVisualParam())
                        saved.emplace_back(param, param->getWeight());
                    struct RestoreParameters
                    {
                        LLAvatarAppearance&                                appearance;
                        const std::vector<std::pair<LLVisualParam*, F32>>& saved;
                        ~RestoreParameters()
                        {
                            for (const auto& [param, weight] : saved)
                                appearance.setVisualParamWeight(param, weight, false);
                            appearance.updateVisualParams();
                        }
                    } restore{ appearance, saved };
                    std::istringstream stream(bytes);
                    const auto         imported = wearable->importStream(stream, &appearance);
                    if (imported != LLWearable::SUCCESS || wearable->getType() != entry.type || wearable->getAssetType() != type)
                    {
                        entry.error = "Shared wearable decoder rejected asset or type";
                        return;
                    }
                    entry.wearable = std::move(wearable);
                    owner->dirty   = true;
                }
                catch (const std::exception& exception)
                {
                    entry.error = exception.what();
                }
            });
    }
    void bake()
    {
        const auto owner = session.lock();
        if (!owner || !owner->regionReady() || version < 0 || baker || requestedBake == version || bakeAttempts >= 3 ||
            LLFrameTimer::getTotalSeconds() < nextBake)
            return;
        const auto url = owner->capability("UpdateAvatarAppearance");
        if (url.empty())
            return;
        const U64  epoch = owner->regionEpoch(), generation = revision;
        const S32  cofVersion = version;
        const auto weak       = weak_from_this();
        baker = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeAppearanceBake", LLCore::HttpRequest::DEFAULT_POLICY_ID);
        requestedBake = version;
        ++bakeAttempts;
        const auto adapter = baker;
        LLCoros::instance().launch("nativeAppearanceBake",
                                   [weak, adapter, url, epoch, generation, cofVersion]
                                   {
                                       auto request = std::make_shared<LLCore::HttpRequest>();
                                       auto options = std::make_shared<LLCore::HttpOptions>();
                                       options->setTransferTimeout(30);
                                       options->setRetries(0);
                                       options->setMaximumResponseBodyBytes(4 * 1024 * 1024);
                                       LLSD body;
                                       body["cof_version"] = cofVersion;
                                       const auto result   = adapter->postAndSuspend(request, url, body, options);
                                       LLAppViewer::instance()->postToMainCoro(
                                           [weak, adapter, result, epoch, generation]
                                           {
                                               const auto state = weak.lock();
                                               if (!state || !state->current() || state->baker != adapter)
                                                   return;
                                               state->baker.reset();
                                               const auto session = state->session.lock();
                                               if (state->revision != generation || session->regionEpoch() != epoch)
                                                   return;
                                               if (!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(
                                                       result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]) ||
                                                   !result["success"].asBoolean())
                                               {
                                                   state->requestedBake = -1;
                                                   state->nextBake      = LLFrameTimer::getTotalSeconds() + 5;
                                                   state->error         = "Server appearance bake failed";
                                               }
                                               else
                                                   state->error.clear();
                                           });
                                   });
    }
    static void uploadCompleted(std::shared_ptr<UploadRequest> request, const LLUUID& asset, S32 status)
    {
        LLAppViewer::instance()->postToMainCoro(
            [request, asset, status]
            {
                const auto state   = request->owner.lock();
                const auto avatar  = selfAvatar();
                const auto session = state ? state->session.lock() : nullptr;
                // A COF revision may change geometry or another bake while
                // this immutable bake generation remains current. Its content,
                // avatar, account and region identities decide acceptance.
                if (!state || !state->current() || !avatar || session->regionEpoch() != request->region ||
                    avatar->ownerGeneration() != request->avatar)
                    return;
                const auto found = state->uploads.find(request->index);
                if (found == state->uploads.end() || found->second.generation != request->generation)
                    return;
                auto& upload = found->second;
                if (upload.attempts != request->attempt || LLFrameTimer::getTotalSeconds() > upload.deadline)
                    return;
                if (status == LL_ERR_NOERR)
                {
                    // A new compositor generation may precede this tick's
                    // upload-table refresh. Superseded success is retirement,
                    // not an upload failure or a mutation of the new content.
                    if (!avatar->setLocalBake(request->index, request->generation, asset))
                        return;
                    upload.pending                = false;
                    upload.asset                  = asset;
                    state->sentAppearanceRevision = 0;
                }
                else
                {
                    upload.pending  = false;
                    upload.deadline = LLFrameTimer::getTotalSeconds() + 5;
                    state->error    = "Local appearance bake upload failed";
                }
            });
    }
    static void uploaded(const LLUUID& asset, void* data, S32 status, LLExtStat)
    { uploadCompleted(std::shared_ptr<UploadRequest>(static_cast<UploadRequest*>(data)), asset, status); }
    class BakeCapabilityUpload final : public LLBufferedAssetUploadInfo
    {
        std::shared_ptr<UploadRequest> mRequest;
        void                           complete(const LLUUID& asset, S32 status)
        {
            if (mRequest)
                uploadCompleted(std::move(mRequest), asset, status);
        }

    public:
        BakeCapabilityUpload(const LLUUID& asset, std::string bytes, std::shared_ptr<UploadRequest> request) :
            LLBufferedAssetUploadInfo(LLUUID::null, LLAssetType::AT_TEXTURE, std::move(bytes), nullptr, nullptr),
            mRequest(std::move(request))
        { setAssetId(asset); }
        ~BakeCapabilityUpload() override { complete(LLUUID::null, -1); }
        LLSD   prepareUpload() override { return LLSD().with("success", true); }
        LLUUID finishUpload(LLSD& result) override
        {
            const auto status =
                LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
            const LLUUID asset   = result["new_asset"].asUUID();
            const bool   success = bool(status) && result["state"].asString() == "complete" && asset.notNull();
            complete(asset, success ? LL_ERR_NOERR : -1);
            return success ? asset : LLUUID::null;
        }
        bool failedUpload(LLSD&, std::string&) override
        {
            complete(LLUUID::null, -1);
            return true;
        }
    };
    void legacyBake(VSWorldAvatar& avatar)
    {
        const auto   owner = session.lock();
        const double now   = LLFrameTimer::getTotalSeconds();
        if (!owner || !owner->regionReady() || !owner->capability("UpdateAvatarAppearance").empty() || !gAssetStorage ||
            now < nextLocalBake)
            return;
        nextLocalBake = now + 1;
        // Admit codec scratch before materializing readback copies. Local
        // composition is CPU-owned and never reads an OpenGL framebuffer.
        const auto bakes = avatar.localBakes();
        if (bakes.empty())
            return;
        bool     complete = true;
        unsigned active   = 0;
        for (const auto& [index, upload] : uploads)
            if (upload.pending && now < upload.deadline)
                ++active;
        for (const auto& bake : bakes)
        {
            auto& upload = uploads[bake.index];
            if (upload.generation != bake.generation)
            {
                upload                 = {};
                upload.generation      = bake.generation;
                sentAppearanceRevision = 0;
            }
            if (upload.asset.notNull())
                continue;
            complete = false;
            if (upload.pending && now >= upload.deadline)
            {
                upload.pending = false;
                error          = "Local appearance bake upload timed out";
            }
            if (upload.pending || active >= 2 || now < upload.deadline || upload.attempts >= 3)
                continue;
            auto residency = budget.reserve(64 * 1024 * 1024);
            if (!residency)
                continue;
            ++upload.attempts;
            const auto  snapshot = avatar.localBake(bake.index);
            const auto& color    = snapshot.color;
            if (!color || color->getComponents() != 4 || color->getWidth() > 2048 || color->getHeight() > 2048)
            {
                error = "Invalid local bake image";
                continue;
            }
            LLPointer<LLImageRaw> source = new LLImageRaw(color->getWidth(), color->getHeight(), 5);
            const bool            mask =
                snapshot.mask && snapshot.mask->getWidth() == color->getWidth() && snapshot.mask->getHeight() == color->getHeight();
            for (S32 index = 0; index < color->getWidth() * color->getHeight(); ++index)
            {
                std::copy_n(color->getData() + index * 4, 4, source->getData() + index * 5);
                source->getData()[index * 5 + 4] = mask ? snapshot.mask->getData()[index] : 255;
            }
            LLPointer<LLImageJ2C> encoded = new LLImageJ2C;
            if (!encoded->encode(source, LINDEN_J2C_COMMENT_PREFIX "RGBHM"))
            {
                error = "Local appearance bake encoding failed";
                continue;
            }
            LLTransactionID transaction;
            transaction.generate();
            const auto   asset = transaction.makeAssetID(gAgent.getSecureSessionID());
            LLFileSystem file(asset, LLAssetType::AT_TEXTURE, LLFileSystem::WRITE);
            if (!file.write(encoded->getData(), encoded->getDataSize()))
            {
                error = "Local appearance bake cache write failed";
                continue;
            }
            auto request        = std::make_unique<UploadRequest>();
            request->owner      = weak_from_this();
            request->region     = owner->regionEpoch();
            request->avatar     = avatar.ownerGeneration();
            request->generation = bake.generation;
            request->index      = bake.index;
            request->residency  = residency;
            request->attempt    = upload.attempts;
            upload.pending      = true;
            upload.deadline     = now + 60;
            ++active;
            const auto capability = owner->capability("UploadBakedTexture");
            if (!capability.empty() && !gSavedSettings.getBOOL("UploadBakedTexOld") && upload.attempts < 3)
            {
                // Use the same two-stage capability uploader as the reference
                // renderer. The final attempt retains its asset-store fallback.
                auto                        sharedRequest = std::shared_ptr<UploadRequest>(request.release());
                LLResourceUploadInfo::ptr_t info          = std::make_shared<BakeCapabilityUpload>(
                    asset,
                    std::string(reinterpret_cast<const char*>(encoded->getData()), encoded->getDataSize()),
                    std::move(sharedRequest));
                LLViewerAssetUpload::EnqueueInventoryUpload(capability, info);
            }
            else
            {
                gAssetStorage->storeAssetData(transaction, LLAssetType::AT_TEXTURE, uploaded, request.release(), true, true, true, false,
                                              F64Seconds(60));
            }
        }
        if (complete && sentAppearanceRevision != revision && gMessageSystem)
        {
            LLMessageSystem* message = gMessageSystem;
            if (avatar.sendAppearance(*message, account.session, ++appearanceSerial) && message->sendReliable(owner->host()) > 0)
            {
                sentAppearanceRevision = revision;
                error.clear();
            }
        }
    }
    void tickAttachments()
    {
        const auto owner = session.lock();
        if (!owner || !owner->regionReady() || !gMessageSystem || !gViewerWindow || !gViewerWindow->nativeContext())
            return;
        const auto                              attached = gViewerWindow->nativeContext()->world().attachments();
        std::map<LLUUID, std::pair<LLUUID, U8>> observed;
        for (const auto& attachment : attached)
            if (observed.size() < 256)
                observed.emplace(attachment.object.id, std::make_pair(attachment.item, attachment.point));
        // Region hand-off can temporarily hide parent bindings; refresh labels
        // and folder-lock lookups, without treating it as a COF detach action.
        const bool       moved = attachmentRegion != owner->regionEpoch();
        std::set<LLUUID> changed;
        for (const auto& [id, previous] : observedAttachments)
            if (!observed.count(id) || observed.at(id) != previous)
                changed.insert(previous.first);
        for (const auto& [id, current] : observed)
            if (moved || !observedAttachments.count(id) || observedAttachments.at(id) != current)
                changed.insert(current.first);
        observedAttachments = std::move(observed);
        attachmentRegion    = owner->regionEpoch();
        if (!changed.empty())
        {
            const auto weak     = weak_from_this();
            const auto identity = account;
            LLAppViewer::instance()->postToMainCoro(
                [weak, identity, changed]
                {
                    const auto state   = weak.lock();
                    const auto session = state ? state->session.lock() : nullptr;
                    if (!state || !state->current() || !session->current(identity))
                        return;
                    for (const auto& item : changed)
                        LLAppearanceMgr::instance().notifyAttachmentChange(item);
                    gInventory.notifyObservers();
                });
        }
        const double now = LLFrameTimer::getTotalSeconds();
        for (auto request = attachments.begin(); request != attachments.end();)
        {
            const LLUUID itemID  = request->first;
            auto&        pending = request->second;
            if (!owner->current(pending.inventory))
            {
                request = attachments.erase(request);
                continue;
            }
            const bool present = std::any_of(attached.begin(), attached.end(), [&](const auto& item) { return item.item == itemID; });
            if (present == pending.attach)
            {
                const auto                     weak     = weak_from_this();
                auto                           callback = pending.callback;
                const auto                     token    = pending.inventory;
                LLPointer<LLInventoryCallback> linked   = new LLBoostFuncInventoryCallback(
                    [weak, callback, token](const LLUUID& link) mutable
                    {
                        const auto state = weak.lock();
                        const auto owner = state ? state->session.lock() : nullptr;
                        if (!state || !state->current() || !owner->current(token))
                            return;
                        state->dirty         = true;
                        state->requestedBake = -1;
                        if (callback)
                            callback->fire(link);
                    });
                if (present)
                    LLAppearanceMgr::instance().addCOFItemLink(itemID, linked);
                else
                    LLAppearanceMgr::instance().removeCOFItemLinks(itemID, linked);
                request = attachments.erase(request);
                continue;
            }
            if (pending.deadline && now < pending.deadline)
            {
                ++request;
                continue;
            }
            if (pending.attempts >= 3)
            {
                error   = "Attachment operation timed out";
                request = attachments.erase(request);
                continue;
            }
            const auto* item = gInventory.getItem(itemID);
            if (!item)
            {
                request = attachments.erase(request);
                continue;
            }
            if (pending.attach)
            {
                // Use the reference compound-attachment protocol. A bounded
                // one-object packet also obeys its batching/header contract.
                LLUUID compound;
                compound.generate();
                gMessageSystem->newMessage("RezMultipleAttachmentsFromInv");
                gMessageSystem->nextBlock("AgentData");
                gMessageSystem->addUUID("AgentID", account.agent);
                gMessageSystem->addUUID("SessionID", account.session);
                gMessageSystem->nextBlock("HeaderData");
                gMessageSystem->addUUID("CompoundMsgID", compound);
                gMessageSystem->addU8("TotalObjects", 1);
                gMessageSystem->addBOOL("FirstDetachAll", false);
                gMessageSystem->nextBlock("ObjectData");
                gMessageSystem->addUUID("ItemID", itemID);
                gMessageSystem->addUUID("OwnerID", item->getPermissions().getOwner());
                gMessageSystem->addU8("AttachmentPt", pending.point | (pending.add ? ATTACHMENT_ADD : 0));
                pack_permissions_slam(gMessageSystem, item->getFlags(), item->getPermissions());
                gMessageSystem->addString("Name", item->getName());
                gMessageSystem->addString("Description", item->getDescription());
            }
            else
            {
                gMessageSystem->newMessage("DetachAttachmentIntoInv");
                gMessageSystem->nextBlock("ObjectData");
                gMessageSystem->addUUID("AgentID", account.agent);
                gMessageSystem->addUUID("ItemID", itemID);
            }
            ++pending.attempts;
            if (gMessageSystem->sendReliable(owner->host()) > 0)
            {
                pending.deadline = now + 15;
            }
            else
            {
                error            = "Attachment operation could not be sent";
                pending.deadline = now + 5;
            }
            ++request;
        }
    }
};
VSNativeAppearance::VSNativeAppearance(std::shared_ptr<VSNativeSession> session) : mImpl(std::make_shared<Impl>(session))
{
}
VSNativeAppearance::~VSNativeAppearance() = default;
void VSNativeAppearance::fallback(const LLSD& wearables)
{
    mImpl->fallback = wearables;
    mImpl->dirty    = true;
}
void VSNativeAppearance::refresh()
{
    auto& state         = *mImpl;
    state.dirty         = true;
    state.requestedBake = -1;
    state.bakeAttempts  = 0;
    state.nextBake      = 0;
    if (state.baker)
        state.baker->cancelSuspendedOperation();
    state.baker.reset();
    ++state.revision;
    for (auto& [id, entry] : state.entries)
        if (!entry.wearable)
        {
            entry.requested = false;
            entry.error.clear();
            entry.deadline = 0;
        }
    state.uploads.clear();
    state.sentAppearanceRevision = 0;
}
void VSNativeAppearance::tick()
{
    auto&      state   = *mImpl;
    const auto session = state.session.lock();
    const auto avatar  = selfAvatar();
    if (!state.current() || !session->regionReady() || !avatar)
        return;
    state.tickAttachments();
    if (!state.pendingOrder && state.orderRecovery && gInventory.isCategoryComplete(state.cof))
        state.orderRecovery = false;
    if (!state.pendingOrder && !state.orderRecovery && !state.queuedRemovals.empty())
    {
        auto request = std::move(state.queuedRemovals.front());
        state.queuedRemovals.erase(state.queuedRemovals.begin());
        std::vector<LLUUID> ids;
        for (const auto& item : request.items)
            if (session->current(item.account))
                if (const auto current = session->inventory(item.item); current && current->asset == item.asset)
                    ids.push_back(item.item);
        if (!ids.empty())
            remove(ids, request.callback);
    }
    if (state.categoryRequest && gInventory.isCategoryComplete(state.categoryRequest->first))
    {
        const auto request = *state.categoryRequest;
        state.categoryRequest.reset();
        wearCategory(request.first, request.second);
    }
    if (state.avatarGeneration != avatar->ownerGeneration())
    {
        state.entries.clear();
        state.applied.clear();
        state.uploads.clear();
        state.sentAppearanceRevision = 0;
        state.avatarGeneration       = avatar->ownerGeneration();
        ++state.revision;
        state.dirty = true;
    }
    if (state.bakeRegion != session->regionEpoch())
    {
        if (state.baker)
            state.baker->cancelSuspendedOperation();
        state.baker.reset();
        state.bakeRegion    = session->regionEpoch();
        state.requestedBake = -1;
        state.bakeAttempts  = 0;
        state.uploads.clear();
        state.sentAppearanceRevision = 0;
    }
    auto& manager = LLAppearanceMgr::instance();
    manager.initCOFID();
    const LLUUID                  cof      = manager.getCOF();
    const auto*                   category = gInventory.getCategory(cof);
    const bool                    complete = category && category->getVersion() >= 0 && gInventory.isCategoryComplete(cof);
    std::map<LLUUID, Impl::Entry> candidate;
    std::set<LLUUID>              actualLinks;
    const S32                     version = complete ? category->getVersion() : -1;
    if (complete)
    {
        LLInventoryModel::cat_array_t  categories;
        LLInventoryModel::item_array_t items;
        gInventory.collectDescendents(cof, categories, items, LLInventoryModel::EXCLUDE_TRASH);
        for (const auto& item : items)
        {
            actualLinks.insert(item->getLinkedUUID());
            if (!item->isWearableType())
                continue;
            const auto  linked = item->getLinkedUUID();
            const auto* actual = gInventory.getItem(linked);
            if (!actual || actual->getAssetUUID().isNull())
                return;
            Impl::Entry entry;
            entry.item        = linked;
            entry.asset       = actual->getAssetUUID();
            entry.type        = actual->getWearableType();
            entry.order       = item->getActualDescription();
            candidate[linked] = std::move(entry);
        }
    }
    else
    {
        if (cof.notNull())
            LLInventoryModelBackgroundFetch::instance().start(cof, true);
        for (const auto& row : llsd::inArray(state.fallback))
        {
            if (row["item"].asUUID().isNull() || row["asset"].asUUID().isNull())
                continue;
            const S32 type = row["type"].asInteger();
            if (type < 0 || type >= LLWearableType::WT_COUNT)
                continue;
            Impl::Entry entry;
            entry.item            = row["item"].asUUID();
            entry.asset           = row["asset"].asUUID();
            entry.type            = LLWearableType::EType(type);
            candidate[entry.item] = std::move(entry);
        }
    }
    for (auto pending = state.pendingLinks.begin(); pending != state.pendingLinks.end();)
    {
        if (!session->current(pending->second.inventory))
        {
            pending = state.pendingLinks.erase(pending);
            continue;
        }
        auto& operation = pending->second;
        if (!operation.completed && !operation.failed)
        {
            if (actualLinks.count(pending->first))
            {
                operation.completed = true;
                auto callback       = operation.callback;
                operation.callback  = nullptr;
                if (callback)
                {
                    const auto weak      = std::weak_ptr<Impl>(mImpl);
                    const auto inventory = operation.inventory;
                    const auto id        = pending->first;
                    LLAppViewer::instance()->postToMainCoro(
                        [weak, inventory, id, callback]() mutable
                        {
                            const auto state   = weak.lock();
                            const auto session = state ? state->session.lock() : nullptr;
                            if (state && state->current() && session->current(inventory))
                                callback->fire(id);
                        });
                }
            }
            else if (LLFrameTimer::getTotalSeconds() >= operation.deadline)
            {
                operation.failed   = true;
                operation.callback = nullptr;
                state.error        = "Current outfit link request timed out";
            }
        }
        // Keep the bounded request record until its network callback retires;
        // a late failed operation still needs to roll back its unwanted link.
        ++pending;
    }
    if (candidate.empty() && !complete)
        return;
    if (candidate.size() > 64)
    {
        state.error = "Current outfit exceeds wearable admission limit";
        return;
    }
    bool changed = cof != state.cof || version != state.version || candidate.size() != state.entries.size();
    for (const auto& [id, entry] : candidate)
    {
        const auto old = state.entries.find(id);
        changed |= old == state.entries.end() || old->second.asset != entry.asset || old->second.type != entry.type ||
                   old->second.order != entry.order;
    }
    if (changed)
    {
        for (auto& [id, entry] : candidate)
            if (const auto old = state.entries.find(id);
                old != state.entries.end() && old->second.asset == entry.asset && old->second.type == entry.type)
                entry.wearable = old->second.wearable;
        ++state.revision;
        state.entries      = std::move(candidate);
        state.cof          = cof;
        state.version      = version;
        state.dirty        = true;
        state.bakeAttempts = 0;
    }
    if (complete && (state.reconciledCOF != version || state.reconciledRegion != session->regionEpoch()))
    {
        // Simulator-restored attachments may arrive after the COF response.
        // Reconcile once per authoritative outfit/region, honoring operations
        // already queued by the user instead of continually re-rezzing them.
        if (!state.reconcileAt)
            state.reconcileAt = LLFrameTimer::getTotalSeconds() + 5;
        if (LLFrameTimer::getTotalSeconds() >= state.reconcileAt)
        {
            LLInventoryModel::cat_array_t  categories;
            LLInventoryModel::item_array_t items;
            gInventory.collectDescendents(cof, categories, items, LLInventoryModel::EXCLUDE_TRASH);
            for (const auto& link : items)
            {
                const LLUUID id   = link->getLinkedUUID();
                const auto*  item = gInventory.getItem(id);
                if (!item || state.attachments.count(id))
                    continue;
                if (item->getType() == LLAssetType::AT_OBJECT && !isWorn(id))
                    attach(id, 0, true);
                else if (item->getType() == LLAssetType::AT_GESTURE && !LLGestureMgr::instance().isGestureActive(id))
                    LLGestureMgr::instance().activateGesture(id);
            }
            state.reconciledCOF    = version;
            state.reconciledRegion = session->regionEpoch();
            state.reconcileAt      = 0;
        }
    }
    unsigned pending = 0;
    for (auto& [id, entry] : state.entries)
    {
        if (entry.requested && !entry.wearable && entry.error.empty())
        {
            if (LLFrameTimer::getTotalSeconds() >= entry.deadline)
                entry.error = "Wearable asset fetch timed out";
            else
                ++pending;
        }
    }
    for (auto& [id, entry] : state.entries)
    {
        if (pending >= 4)
            break;
        if (entry.wearable || entry.requested || !entry.error.empty())
            continue;
        const auto type = LLWearableType::getInstance()->getAssetType(entry.type);
        if (!gAssetStorage || (type != LLAssetType::AT_CLOTHING && type != LLAssetType::AT_BODYPART))
        {
            entry.error = "Wearable storage or type unavailable";
            continue;
        }
        auto request      = std::make_unique<Impl::Request>();
        request->owner    = mImpl;
        request->revision = state.revision;
        request->avatar   = state.avatarGeneration;
        request->item     = entry.item;
        request->asset    = entry.asset;
        entry.requested   = true;
        entry.deadline    = LLFrameTimer::getTotalSeconds() + 60;
        ++pending;
        gAssetStorage->getAssetData(entry.asset, type, Impl::loaded, request.release());
    }
    if (state.dirty)
    {
        std::vector<const Impl::Entry*> ordered;
        for (const auto& [id, entry] : state.entries)
        {
            if (!entry.wearable)
                return;
            ordered.push_back(&entry);
        }
        std::stable_sort(ordered.begin(), ordered.end(),
                         [](const auto* a, const auto* b)
                         {
                             return a->type != b->type ? a->type < b->type : a->order != b->order ? a->order < b->order : a->item < b->item;
                         });
        std::vector<std::shared_ptr<LLWearable>> wearables;
        for (const auto* entry : ordered)
            wearables.push_back(entry->wearable);
        if (!avatar->wearables(wearables, state.version))
        {
            state.error = "Native avatar rejected current outfit";
            return;
        }
        state.applied.clear();
        for (const auto* entry : ordered)
            state.applied[entry->item] = entry->asset;
        state.dirty = false;
        state.error.clear();
        for (const auto& [id, asset] : state.applied)
            gInventory.addChangedMask(LLInventoryObserver::LABEL, id);
        gInventory.notifyObservers();
    }
    state.bake();
    state.legacyBake(*avatar);
}
bool VSNativeAppearance::startupReady() const
{
    // Match the reference wearables-loaded condition: a complete authoritative
    // outfit, resolved local wearable assets, and their publication to self.
    // Background bake textures and attachments continue loading after login.
    const auto& state = *mImpl;
    if (!state.current() || state.cof.isNull() || state.version < 0 || state.dirty ||
        !gInventory.isCategoryComplete(state.cof))
        return false;
    const auto* category = gInventory.getCategory(state.cof);
    if (!category || category->getVersion() != state.version)
        return false;
    return std::all_of(state.entries.begin(), state.entries.end(),
        [&state](const auto& row)
        {
            const auto& entry = row.second;
            const auto applied = state.applied.find(entry.item);
            return entry.wearable && applied != state.applied.end() && applied->second == entry.asset;
        });
}

bool VSNativeAppearance::isWorn(const LLUUID& id) const
{
    if (!mImpl->current() || id.isNull())
        return false;
    const auto item = gInventory.getLinkedItemID(id);
    if (mImpl->applied.count(item))
        return true;
    if (gViewerWindow && gViewerWindow->nativeContext())
        for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
            if (attachment.item == item)
                return true;
    return false;
}
bool VSNativeAppearance::removeTop(unsigned type)
{
    if (!mImpl->current())
        return false;
    const auto items = vs_avatar_wearable_items(type);
    for (auto item = items.rbegin(); item != items.rend(); ++item)
        if (remove({ *item }))
            return true;
    return false;
}
bool VSNativeAppearance::moveWearable(const LLUUID& id, bool closerToBody)
{
    if (!mImpl->current() || !mImpl->pendingLinks.empty() || mImpl->pendingOrder || mImpl->orderRecovery)
        return false;
    const auto* item = gInventory.getItem(id);
    if (!item || item->getType() != LLAssetType::AT_CLOTHING || !LLAppearanceMgr::instance().isLinkedInCOF(item->getLinkedUUID()))
        return false;
    const auto type  = item->getWearableType();
    const auto worn  = vs_avatar_wearable_items(unsigned(type));
    const auto found = std::find(worn.begin(), worn.end(), item->getLinkedUUID());
    if (found == worn.end())
        return false;
    const auto position = std::size_t(found - worn.begin());
    if ((closerToBody && !position) || (!closerToBody && position + 1 >= worn.size()))
        return false;
    const auto swap  = closerToBody ? position - 1 : position + 1;
    auto       links = LLAppearanceMgr::instance().findCOFItemLinks(item->getLinkedUUID());
    auto       peers = LLAppearanceMgr::instance().findCOFItemLinks(worn[swap]);
    if (links.empty() || peers.empty())
        return false;
    if (links.size() + peers.size() > 128)
        return false;
    const auto weak     = std::weak_ptr<Impl>(mImpl);
    const auto account  = mImpl->account;
    const auto serial   = ++mImpl->orderSerial;
    mImpl->pendingOrder = unsigned(links.size() + peers.size());
    const auto complete = [weak, account, serial](bool success)
    {
        const auto state = weak.lock();
        const auto owner = state ? state->session.lock() : nullptr;
        if (!state || !state->current() || !owner->current(account) || state->orderSerial != serial || !state->pendingOrder)
            return;
        if (!success)
        {
            state->orderRecovery = true;
            state->error         = "Wearable layer ordering failed";
        }
        if (!--state->pendingOrder && state->orderRecovery)
            if (auto* category = gInventory.getCategory(state->cof))
            {
                category->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                category->setDescendentCount(-1);
                category->setFetching(LLViewerInventoryCategory::FETCH_NONE);
                LLInventoryModelBackgroundFetch::instance().start(state->cof, true);
            }
    };
    const auto callback = [complete]() -> LLPointer<LLInventoryCallback>
    {
        const auto delivered = std::make_shared<bool>(false);
        return new LLBoostFuncInventoryCallback(
            [complete, delivered](const LLUUID& id)
            {
                if (!*delivered)
                {
                    *delivered = true;
                    complete(id.notNull());
                }
            },
            [complete, delivered]
            {
                if (!*delivered)
                    complete(false);
            });
    };
    // Use the reference ordering strings and normal server update path. The
    // next COF snapshot rebuilds the native layer sequence and local bakes.
    for (auto& link : links)
    {
        link->setDescription(build_order_string(type, U32(swap)));
        link->setComplete(true);
        update_inventory_item(link, callback());
        gInventory.updateItem(link);
    }
    for (auto& peer : peers)
    {
        peer->setDescription(build_order_string(type, U32(position)));
        peer->setComplete(true);
        update_inventory_item(peer, callback());
        gInventory.updateItem(peer);
    }
    LLAppearanceMgr::instance().setOutfitDirty(true);
    mImpl->dirty = true;
    gInventory.notifyObservers();
    return true;
}
bool VSNativeAppearance::attach(const LLUUID& id, U8 point, bool add)
{
    const auto  owner = mImpl->session.lock();
    const auto* item  = gInventory.getItem(gInventory.getLinkedItemID(id));
    if (!mImpl->current() || !owner->regionReady() || !item || item->getType() != LLAssetType::AT_OBJECT || !gViewerWindow ||
        !gViewerWindow->nativeContext() || point >= 128 || mImpl->attachments.size() >= 256)
        return false;
    if (rlv_handler_t::isEnabled())
    {
        if (RlvFolderLocks::instance().isLockedFolder(item->getParentUUID(), RLV_LOCK_ADD))
            return false;
        if (!point)
            point = U8(RlvAttachPtLookup::getAttachPointIndex(item, true));
        if (!point)
        {
            const auto* avatar = selfAvatar();
            if (!avatar)
                return false;
            // The server's unspecified default cannot be used to bypass a
            // locked point. Select an actual unlocked native attachment point.
            for (const auto& candidate : avatar->attachmentPoints())
                if (!candidate.hud && !gRlvAttachmentLocks.isLockedAttachmentPoint(S32(candidate.index), RLV_LOCK_ADD))
                {
                    point = U8(candidate.index);
                    break;
                }
        }
        if (!point || gRlvAttachmentLocks.isLockedAttachmentPoint(point, RLV_LOCK_ADD))
            return false;
        if (!add)
            for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
                if (attachment.point == point &&
                    (gRlvAttachmentLocks.isLockedAttachmentPoint(point, RLV_LOCK_REMOVE) ||
                     gRlvAttachmentLocks.getAttachObjLocks().count(attachment.object.id) ||
                     (gInventory.getItem(attachment.item) &&
                      RlvFolderLocks::instance().isLockedFolder(gInventory.getItem(attachment.item)->getParentUUID(), RLV_LOCK_REMOVE))))
                    return false;
    }
    const auto token = owner->inventory(item->getUUID());
    if (!token)
        return false;
    std::set<std::pair<bool, LLUUID>> reserved;
    for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
        reserved.emplace(attachment.item.isNull(), attachment.item.isNull() ? attachment.object.id : attachment.item);
    for (const auto& [item, request] : mImpl->attachments)
        if (request.attach)
            reserved.emplace(false, item);
    const S32 limit = LLAgentBenefitsMgr::current().getAttachmentLimit();
    if (!reserved.count({ false, item->getUUID() }) && (limit <= 0 || reserved.size() >= std::size_t(limit)))
        return false;
    Impl::AttachmentRequest request;
    request.inventory                   = *token;
    request.point                       = point;
    request.add                         = add;
    mImpl->attachments[item->getUUID()] = std::move(request);
    return true;
}
bool VSNativeAppearance::detach(const LLUUID& id)
{
    const auto owner = mImpl->session.lock();
    const auto item  = gInventory.getLinkedItemID(id);
    if (!mImpl->current() || !owner->regionReady() || !gViewerWindow || !gViewerWindow->nativeContext() || mImpl->attachments.size() >= 256)
        return false;
    // Temporary attachments have an object owner but no inventory item. Use
    // their guarded host/local binding, rather than inventing a COF identity.
    for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
        if (attachment.item.isNull() && attachment.object.id == id)
        {
            if (!gViewerWindow->nativeContext()->world().current(attachment.object) || !gMessageSystem)
                return false;
            if (rlv_handler_t::isEnabled() && (gRlvAttachmentLocks.isLockedAttachmentPoint(attachment.point, RLV_LOCK_REMOVE) ||
                                               gRlvAttachmentLocks.getAttachObjLocks().count(attachment.object.id)))
                return false;
            gMessageSystem->newMessage("ObjectDetach");
            gMessageSystem->nextBlock("AgentData");
            gMessageSystem->addUUID("AgentID", owner->account().agent);
            gMessageSystem->addUUID("SessionID", owner->account().session);
            gMessageSystem->nextBlock("ObjectData");
            gMessageSystem->addU32("ObjectLocalID", attachment.object.local);
            return gMessageSystem->sendReliable(attachment.object.host) > 0;
        }
    if (rlv_handler_t::isEnabled())
    {
        const auto* inventory = gInventory.getItem(item);
        if (!inventory || RlvFolderLocks::instance().isLockedFolder(inventory->getParentUUID(), RLV_LOCK_REMOVE))
            return false;
        for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
            if (attachment.item == item && (gRlvAttachmentLocks.isLockedAttachmentPoint(attachment.point, RLV_LOCK_REMOVE) ||
                                            gRlvAttachmentLocks.getAttachObjLocks().count(attachment.object.id)))
                return false;
    }
    const auto token = owner->inventory(item);
    if (!token)
        return false;
    Impl::AttachmentRequest request;
    request.inventory        = *token;
    request.attach           = false;
    mImpl->attachments[item] = std::move(request);
    return true;
}
bool VSNativeAppearance::canWear(const std::vector<LLUUID>& items, bool replace, bool replaceOutfit) const
{
    const auto owner = mImpl->session.lock();
    if (!mImpl->current() || !owner->regionReady() || items.size() > 256 || !gViewerWindow || !gViewerWindow->nativeContext())
        return false;
    if (mImpl->pendingOrder || mImpl->orderRecovery)
        return false;
    const LLUUID cof = LLAppearanceMgr::instance().getCOF();
    if (cof.isNull() || !gInventory.isCategoryComplete(cof))
        return false;
    std::map<LLWearableType::EType, std::set<LLUUID>> layers;
    if (!replaceOutfit)
    {
        LLInventoryModel::cat_array_t  categories;
        LLInventoryModel::item_array_t current;
        gInventory.collectDescendents(cof, categories, current, LLInventoryModel::EXCLUDE_TRASH);
        for (const auto& link : current)
            if (const auto* item = gInventory.getItem(link->getLinkedUUID()); item && item->isWearableType())
                layers[item->getWearableType()].insert(item->getUUID());
    }
    if (replaceOutfit && !mImpl->pendingLinks.empty())
        return false;
    for (const auto& [id, pending] : mImpl->pendingLinks)
        if (!pending.failed && !pending.completed && pending.type >= 0 && pending.type < LLWearableType::WT_COUNT)
            layers[pending.type].insert(id);
    std::set<std::pair<bool, LLUUID>> objects;
    for (const auto& object : gViewerWindow->nativeContext()->world().attachments())
        objects.emplace(object.item.isNull(), object.item.isNull() ? object.object.id : object.item);
    for (const auto& [id, request] : mImpl->attachments)
        if (request.attach)
            objects.emplace(false, id);
    std::set<LLWearableType::EType> replaced;
    std::set<LLUUID>                newLinks;
    for (const auto& id : items)
    {
        const auto* item = gInventory.getItem(gInventory.getLinkedItemID(id));
        if (!item || mImpl->pendingLinks.count(item->getUUID()))
            return false;
        if (item->getType() == LLAssetType::AT_OBJECT)
            objects.emplace(false, item->getUUID());
        else if (item->isWearableType())
        {
            const auto type = item->getWearableType();
            if (type < 0 || type >= LLWearableType::WT_COUNT)
                return false;
            if (mImpl->pendingLinks.count(item->getUUID()))
                return false;
            if (replace || item->getType() == LLAssetType::AT_BODYPART || item->getWearableType() == LLWearableType::WT_PHYSICS)
                for (const auto& [id, pending] : mImpl->pendingLinks)
                    if (pending.type == type)
                        return false;
            const bool single = item->getType() == LLAssetType::AT_BODYPART || type == LLWearableType::WT_PHYSICS ||
                                !LLWearableType::getInstance()->getAllowMultiwear(type);
            if ((replace || single) && replaced.insert(type).second)
                layers[type].clear();
            layers[type].insert(item->getUUID());
            if (single && layers[type].size() > 1)
                return false;
        }
        else if (item->getType() != LLAssetType::AT_GESTURE)
            return false;
        if (item->getType() != LLAssetType::AT_OBJECT && !LLAppearanceMgr::instance().isLinkedInCOF(item->getUUID()))
            newLinks.insert(item->getUUID());
    }
    if (newLinks.size() + mImpl->pendingLinks.size() > 64)
        return false;
    unsigned clothes = 0, total = 0;
    for (const auto& [type, items] : layers)
    {
        total += unsigned(items.size());
        if (LLWearableType::getInstance()->getAssetType(type) == LLAssetType::AT_CLOTHING)
            clothes += unsigned(items.size());
    }
    const S32 attachmentLimit = LLAgentBenefitsMgr::current().getAttachmentLimit();
    return clothes <= LLWearableData::MAX_CLOTHING_LAYERS && total <= 64 && attachmentLimit >= 0 &&
           objects.size() <= std::size_t(attachmentLimit);
}
bool VSNativeAppearance::wear(const std::vector<LLUUID>& items, bool replace, LLPointer<LLInventoryCallback> callback)
{ return wearItems(items, replace, callback, false); }
bool VSNativeAppearance::wearItems(const std::vector<LLUUID>& items, bool replace, LLPointer<LLInventoryCallback> callback,
                                   bool replaceOutfit)
{
    auto&      state = *mImpl;
    const auto owner = state.session.lock();
    if (!canWear(items, replace, replaceOutfit))
        return false;
    bool                            accepted = false;
    std::set<LLWearableType::EType> replaced;
    for (const auto& id : items)
    {
        const auto* source = gInventory.getItem(id);
        if (!source)
            continue;
        if (gInventory.isObjectDescendentOf(id, gInventory.getLibraryRootFolderID()))
        {
            const auto                     weak    = std::weak_ptr<Impl>(mImpl);
            const auto                     account = state.account;
            LLPointer<LLInventoryCallback> copied  = new LLBoostFuncInventoryCallback(
                [weak, account, replace, callback](const LLUUID& item)
                {
                    const auto state = weak.lock();
                    const auto owner = state ? state->session.lock() : nullptr;
                    if (state && state->current() && owner->current(account) && owner->appearance())
                        owner->appearance()->wear({ item }, replace, callback);
                });
            copy_inventory_item(owner->account().agent, source->getPermissions().getOwner(), id, LLUUID::null, "", copied);
            accepted = true;
            continue;
        }
        const auto  linked = source->getLinkedUUID();
        const auto* item   = gInventory.getItem(linked);
        if (!item || !gInventory.isObjectDescendentOf(linked, gInventory.getRootFolderID()) ||
            gInventory.isObjectDescendentOf(linked, gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH)))
            continue;
        if (item->isWearableType() && LLAppearanceMgr::instance().isLinkedInCOF(linked))
        {
            if (callback)
                callback->fire(linked);
            accepted = true;
            continue;
        }
        if (item->getType() != LLAssetType::AT_OBJECT && rlv_handler_t::isEnabled() &&
            !rlvPredCanWearItem(item, (item->getType() == LLAssetType::AT_BODYPART || replace) ? RLV_WEAR_REPLACE : RLV_WEAR_ADD))
            continue;
        if (item->getType() == LLAssetType::AT_OBJECT)
        {
            if (attach(linked, 0, !replace))
            {
                state.attachments[linked].callback = callback;
                accepted                           = true;
            }
            continue;
        }
        if (item->getType() == LLAssetType::AT_GESTURE)
        {
            LLGestureMgr::instance().activateGesture(linked);
            accepted = true;
            if (LLAppearanceMgr::instance().isLinkedInCOF(linked))
            {
                if (callback)
                    callback->fire(linked);
                continue;
            }
        }
        else if (!item->isWearableType())
            continue;
        if (item->isWearableType() &&
            (replace || item->getType() == LLAssetType::AT_BODYPART || item->getWearableType() == LLWearableType::WT_PHYSICS ||
             !LLWearableType::getInstance()->getAllowMultiwear(item->getWearableType())) &&
            replaced.insert(item->getWearableType()).second)
            LLAppearanceMgr::instance().removeCOFLinksOfType(item->getWearableType());
        const auto token = owner->inventory(linked);
        if (!token)
            continue;
        const auto weak = std::weak_ptr<Impl>(mImpl);
        if (state.pendingLinks.size() >= 64)
            continue;
        const U64 serial = ++state.nextLinkSerial;
        state.pendingLinks.emplace(
            linked, Impl::PendingLink{ *token, item->getWearableType(), serial, LLFrameTimer::getTotalSeconds() + 30, callback });
        LLPointer<LLInventoryCallback> linkedCallback = new LLBoostFuncInventoryCallback(
            [weak, token, linked, serial](const LLUUID& id) mutable
            {
                const auto state = weak.lock();
                const auto owner = state ? state->session.lock() : nullptr;
                if (!state || !state->current() || !owner->current(*token))
                    return;
                const auto pending = state->pendingLinks.find(linked);
                if (pending == state->pendingLinks.end() || pending->second.serial != serial)
                    return;
                auto       callback = pending->second.callback;
                const bool expired =
                    pending->second.failed ||
                    (!pending->second.completed && (id.isNull() || LLFrameTimer::getTotalSeconds() >= pending->second.deadline));
                state->pendingLinks.erase(pending);
                if (expired)
                {
                    LLAppearanceMgr::instance().removeCOFItemLinks(linked);
                    state->error = "Late current outfit link rolled back";
                    return;
                }
                state->dirty         = true;
                state->requestedBake = -1;
                if (callback)
                    callback->fire(id);
            });
        LLAppearanceMgr::instance().addCOFItemLink(item, linkedCallback);
        accepted = true;
    }
    return accepted;
}
bool VSNativeAppearance::remove(const std::vector<LLUUID>& items, LLPointer<LLInventoryCallback> callback)
{
    if (!mImpl->current() || items.size() > 256)
        return false;
    const auto owner = mImpl->session.lock();
    if (mImpl->pendingOrder || mImpl->orderRecovery)
    {
        if (mImpl->queuedRemovals.size() >= 64)
            return false;
        Impl::QueuedRemoval queued;
        queued.callback = callback;
        for (const auto& id : items)
            if (const auto token = owner->inventory(gInventory.getLinkedItemID(id)))
                queued.items.push_back(*token);
        if (queued.items.empty())
            return false;
        mImpl->queuedRemovals.push_back(std::move(queued));
        return true;
    }
    bool accepted = false;
    for (const auto& id : items)
    {
        const auto  linked = gInventory.getLinkedItemID(id);
        const auto* item   = gInventory.getItem(linked);
        if (!item || item->getType() == LLAssetType::AT_BODYPART ||
            (item->getType() != LLAssetType::AT_OBJECT && rlv_handler_t::isEnabled() && !rlvPredCanRemoveItem(item)))
            continue;
        if (const auto pending = mImpl->pendingLinks.find(linked); pending != mImpl->pendingLinks.end())
        {
            pending->second.failed   = true;
            pending->second.callback = nullptr;
        }
        if (item->getType() == LLAssetType::AT_OBJECT)
        {
            if (detach(linked))
            {
                mImpl->attachments[linked].callback = callback;
                accepted                            = true;
            }
            continue;
        }
        if (item->getType() == LLAssetType::AT_GESTURE)
            LLGestureMgr::instance().deactivateGesture(linked);
        const auto token = owner->inventory(linked);
        if (!token)
            continue;
        const auto                     weak    = std::weak_ptr<Impl>(mImpl);
        LLPointer<LLInventoryCallback> removed = new LLBoostFuncInventoryCallback(
            [weak, token, callback](const LLUUID& id) mutable
            {
                const auto state = weak.lock();
                const auto owner = state ? state->session.lock() : nullptr;
                if (!state || !state->current() || !owner->current(*token))
                    return;
                state->dirty         = true;
                state->requestedBake = -1;
                if (callback)
                    callback->fire(id);
            });
        LLAppearanceMgr::instance().removeCOFItemLinks(linked, removed);
        accepted = true;
    }
    return accepted;
}
bool VSNativeAppearance::wearCategory(const LLUUID& category, bool append)
{
    if (!mImpl->current() || category.isNull() || !gInventory.getCategory(category))
        return false;
    if (!mImpl->pendingLinks.empty() || mImpl->pendingOrder || mImpl->orderRecovery)
    {
        mImpl->categoryRequest = { { category, append } };
        return true;
    }
    if (!gInventory.isCategoryComplete(category))
    {
        mImpl->categoryRequest = { { category, append } };
        LLInventoryModelBackgroundFetch::instance().start(category, true);
        return true;
    }
    LLInventoryModel::cat_array_t  categories;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendents(category, categories, items, LLInventoryModel::EXCLUDE_TRASH);
    for (const auto& child : categories)
        if (!gInventory.isCategoryComplete(child->getUUID()))
        {
            mImpl->categoryRequest = { { category, append } };
            LLInventoryModelBackgroundFetch::instance().start(category, true);
            return true;
        }
    if (items.size() > 256)
        return false;
    LLInventoryModel::item_array_t body, clothing, other;
    std::set<LLUUID>               seen;
    for (const auto& item : items)
    {
        if (!seen.insert(item->getLinkedUUID()).second)
            continue;
        if (item->getType() == LLAssetType::AT_BODYPART)
            body.push_back(item);
        else if (item->getType() == LLAssetType::AT_CLOTHING)
            clothing.push_back(item);
        else
            other.push_back(item);
    }
    auto& appearance = LLAppearanceMgr::instance();
    appearance.filterWearableItems(body, 1, 0);
    appearance.filterWearableItems(clothing, LLWearableData::MAX_CLOTHING_LAYERS, LLWearableData::MAX_CLOTHING_LAYERS);
    items = body;
    items.insert(items.end(), clothing.begin(), clothing.end());
    items.insert(items.end(), other.begin(), other.end());
    std::vector<LLUUID> additions;
    std::set<LLUUID>    desired;
    for (const auto& item : items)
        if (item->isWearableType() || item->getType() == LLAssetType::AT_OBJECT || item->getType() == LLAssetType::AT_GESTURE)
        {
            additions.push_back(item->getUUID());
            desired.insert(item->getLinkedUUID());
        }
    if (!canWear(additions, false, !append))
        return false;
    if (!append)
    {
        LLInventoryModel::cat_array_t  currentCategories;
        LLInventoryModel::item_array_t current;
        gInventory.collectDescendents(LLAppearanceMgr::instance().getCOF(), currentCategories, current, LLInventoryModel::EXCLUDE_TRASH);
        std::vector<LLUUID> removals;
        for (const auto& item : current)
            if (!desired.count(item->getLinkedUUID()) && item->getType() != LLAssetType::AT_BODYPART)
                removals.push_back(item->getLinkedUUID());
        remove(removals);
    }
    // Replacement already removed only items absent from the desired folder.
    // Preserve all desired multiwear layers rather than replacing their type.
    return wearItems(additions, false, {}, !append);
}
LLSD VSNativeAppearance::evidence() const
{
    LLSD result;
    result["cof"]     = mImpl->cof;
    result["version"] = mImpl->version;
    if (const auto* category = gInventory.getCategory(mImpl->cof))
    {
        result["inventory_cof_version"]            = category->getVersion();
        result["inventory_cof_descendents"]        = category->getDescendentCount();
        result["inventory_cof_actual_descendents"] = category->getViewerDescendentCount();
        result["inventory_cof_complete"]           = gInventory.isCategoryComplete(mImpl->cof);
    }
    result["requested_bake"]                = mImpl->requestedBake;
    result["revision"]                      = ll_sd_from_U64(mImpl->revision);
    result["error"]                         = mImpl->error;
    result["applied_wearables"]             = S32(mImpl->applied.size());
    result["resident_wearable_bytes"]       = ll_sd_from_U64(mImpl->budget.used());
    result["wearable_budget_bytes"]         = ll_sd_from_U64(mImpl->budget.limit());
    result["pending_attachment_operations"] = S32(mImpl->attachments.size());
    result["pending_wearable_links"]        = S32(mImpl->pendingLinks.size());
    result["pending_layer_order_updates"]   = S32(mImpl->pendingOrder);
    result["queued_removals"]               = S32(mImpl->queuedRemovals.size());
    unsigned uploaded = 0, pendingUploads = 0;
    result["bake_uploads"] = LLSD::emptyArray();
    for (const auto& [index, upload] : mImpl->uploads)
    {
        uploaded += upload.asset.notNull();
        pendingUploads += upload.pending;
        LLSD row;
        row["index"]      = S32(index);
        row["asset"]      = upload.asset;
        row["generation"] = ll_sd_from_U64(upload.generation);
        row["pending"]    = upload.pending;
        row["attempts"]   = S32(upload.attempts);
        result["bake_uploads"].append(row);
    }
    result["uploaded_bakes"]           = S32(uploaded);
    result["pending_bake_uploads"]     = S32(pendingUploads);
    result["appearance_sent_revision"] = ll_sd_from_U64(mImpl->sentAppearanceRevision);
    result["entries"]                  = LLSD::emptyArray();
    for (const auto& [id, entry] : mImpl->entries)
    {
        LLSD row;
        row["item"]  = id;
        row["asset"] = entry.asset;
        row["ready"] = bool(entry.wearable);
        row["error"] = entry.error;
        result["entries"].append(row);
    }
    return result;
}
