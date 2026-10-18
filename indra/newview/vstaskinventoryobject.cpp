// Existing task controllers consume actual owners, never a fabricated LLVO.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vstaskinventoryobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llagent.h"
#include "llviewernetwork.h"
#include "vsnativeim.h"
#include "vsregioncontext.h"
#include "rlvhandler.h"
#include "rlvlocks.h"
#include "llvoavatarself.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsnativeappearance.h"
#include "vsworldavatar.h"
#include "vsvulkancontext.h"
#endif

VSTaskInventoryObject VSTaskInventoryObject::find(const LLUUID& id)
{

#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        auto* context = gViewerWindow->nativeContext();
        const auto token = context ? context->world().object(id) : std::nullopt;
        return token ? find(*token) : VSTaskInventoryObject{};
    }
#endif
    VSTaskInventoryObject result;
    const auto context = VSObjectContext::resolve(id);
    if (!context) return result;
    result.mContext = *context;

    result.mLegacy = gObjectList.findObject(id);
    return result;
}
VSTaskInventoryObject::operator bool() const
{
    return mContext.current && mContext.current();
}
#if VS_NATIVE_VULKAN
VSTaskInventory* VSTaskInventoryObject::nativeInventory() const
{
    const auto owner = mOwner.lock();
    return mNative && *this && owner ? owner->taskInventory() : nullptr;
}
#endif
LLInventoryObject* VSTaskInventoryObject::getInventoryObject(const LLUUID& id) const
{
    if (!*this) return nullptr;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto* inventory = nativeInventory();
        return inventory ? inventory->object(mObject, id).get() : nullptr;
    }
#endif
    return mLegacy.get()->getInventoryObject(id);
}
void VSTaskInventoryObject::updateInventory(LLViewerInventoryItem* item, U8 key, bool is_new) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative) { if (auto* inventory = nativeInventory()) inventory->update(mObject, item, key, is_new); return; }
#endif
    mLegacy.get()->updateInventory(item, key, is_new);
}
void VSTaskInventoryObject::updateInventoryLocal(LLInventoryItem* item, U8 key) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        if (auto* inventory = nativeInventory())
            inventory->update(mObject, dynamic_cast<LLViewerInventoryItem*>(item), key, false, true);
        return;
    }
#endif
    mLegacy.get()->updateInventoryLocal(item, key);
}
void VSTaskInventoryObject::updateViewerInventoryAsset(const LLViewerInventoryItem* item, const LLUUID& asset) const
{
    if (!*this || !item) return;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        if (auto* inventory = nativeInventory())
            inventory->completeAsset(mObject, item->getUUID(), asset, itemGuard(item->getUUID()));
        return;
    }
#endif
    mLegacy.get()->updateViewerInventoryAsset(item, asset);
}
void VSTaskInventoryObject::saveScript(const LLViewerInventoryItem* item, bool running, bool is_new) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative) { if (auto* inventory = nativeInventory()) inventory->saveScript(mObject, item, running, is_new); return; }
#endif
    mLegacy.get()->saveScript(item, running, is_new);
}
void VSTaskInventoryObject::moveInventory(const LLUUID& folder, const LLUUID& id) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative) { if (auto* inventory = nativeInventory()) inventory->move(mObject, folder, id); return; }
#endif
    mLegacy.get()->moveInventory(folder, id);
}
void VSTaskInventoryObject::removeInventory(const LLUUID& id) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative) { if (auto* inventory = nativeInventory()) inventory->remove(mObject, id); return; }
#endif
    mLegacy.get()->removeInventory(id);
}
bool VSTaskInventoryObject::permModify() const
{
    if (!*this) return false;
    if (!mNative) return mLegacy.get()->permModify();
#ifdef HACKED_GODLIKE_VIEWER
    return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (LLGridManager::instance().isInSLBeta() && gAgent.getGodLevel() >= GOD_MAINTENANCE) return true;
# endif
    return (mContext.flags & FLAGS_OBJECT_MODIFY) != 0;
#endif
}
bool VSTaskInventoryObject::permAnyOwner() const
{
    return *this && (mNative ? (mContext.flags & FLAGS_OBJECT_ANY_OWNER) != 0 : mLegacy.get()->permAnyOwner());
}
bool VSTaskInventoryObject::attachmentLocked() const
{
    if (!*this) return false;
    if (!mNative) return gRlvAttachmentLocks.isLockedAttachment(mLegacy.get()->getRootEdit());
#if VS_NATIVE_VULKAN
    auto* renderer = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
    if (!renderer) return false;
    const auto root = renderer->world().rootObject(mObject, true);
    const auto attachment = root ? renderer->world().objectAttachment(*root) : std::nullopt;
    return attachment && gRlvAttachmentLocks.isLockedAttachment(
        attachment->object.id, attachment->point, attachment->item);
#else
    return false;
#endif
}
std::string VSTaskInventoryObject::getCapability(const std::string& name) const
{
    if (!*this) return {};
    const auto region = vs_region_context(getHost());
    return region ? region->getCapability(name) : std::string();
}
std::function<bool()> VSTaskInventoryObject::objectGuard() const
{
    return mContext.current ? mContext.current : std::function<bool()>([] { return false; });
}
std::function<bool()> VSTaskInventoryObject::itemGuard(const LLUUID& id) const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto* inventory = nativeInventory();
        const auto object = objectGuard();
        const auto item = inventory ? inventory->guard(mObject, id) : std::function<bool()>([] { return false; });
        return [object, item] { return object() && item(); };
    }
#endif
    return objectGuard();
}


void VSTaskInventoryObject::getInventoryContents(LLInventoryObject::object_list_t& contents) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto* inventory = nativeInventory();
        const auto snapshot = inventory ? inventory->snapshot(mObject) : std::nullopt;
        if (snapshot && snapshot->success)
            for (const auto& object : snapshot->contents)
                if (object->getType() != LLAssetType::AT_CATEGORY) contents.push_back(object);
        return;
    }
#endif
    mLegacy.get()->getInventoryContents(contents);
}
bool VSTaskInventoryObject::permYouOwner() const { return *this && mContext.youOwner; }
bool VSTaskInventoryObject::flagScripted() const { return *this && (objectFlags() & FLAGS_SCRIPTED) != 0; }
bool VSTaskInventoryObject::flagAllowInventoryAdd() const { return *this && (objectFlags() & FLAGS_ALLOW_INVENTORY_DROP) != 0; }
U8 VSTaskInventoryObject::getPCode() const { return *this ? mContext.pcode : 0; }
bool VSTaskInventoryObject::isAttachment() const { return *this && mContext.attachment; }
bool VSTaskInventoryObject::isAvatar() const { return *this && mContext.pcode == LL_PCODE_LEGACY_AVATAR; }
VSTaskInventoryObject VSTaskInventoryObject::getRootEdit() const
{
    if (!*this) return {};
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
        const auto root = context ? context->world().rootObject(mObject, true) : std::nullopt;
        return root ? find(*root) : VSTaskInventoryObject{};
    }
#endif
    return find(mContext.rootEditID);
}
VSTaskInventoryObject VSTaskInventoryObject::getParent() const
{
    if (!*this) return {};
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
        const auto parent = context ? context->world().parentObject(mObject) : std::nullopt;
        return parent ? find(*parent) : VSTaskInventoryObject{};
    }
#endif
    const auto* parent = static_cast<const LLViewerObject*>(mLegacy.get()->getParent());
    return parent ? find(parent->getID()) : VSTaskInventoryObject{};
}
bool VSTaskInventoryObject::agentSittingOnRoot() const
{
    if (!*this) return false;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
        auto* avatar = context ? context->world().selfAvatar() : nullptr;
        const auto self = find(gAgent.getID());
        return avatar && avatar->sitting() && self && self.mContext.rootID == getRootEdit().getID();
    }
#endif
    return isAgentAvatarValid() && gAgentAvatarp->isSitting() && gAgentAvatarp->getRoot() == mLegacy.get()->getRootEdit();
}
bool VSTaskInventoryObject::wearing(const LLUUID& item) const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto owner = mOwner.lock();
        return *this && owner && owner->appearance() && owner->appearance()->isWorn(item);
    }
#endif
    return isAgentAvatarValid() && gAgentAvatarp->isWearingAttachment(item);
}

bool VSTaskInventoryObject::isInventoryPending() const
{
    if (!*this) return false;
#if VS_NATIVE_VULKAN
    if (mNative) { const auto* inventory = nativeInventory(); return inventory && inventory->state(mObject).pending; }
#endif
    return mLegacy.get()->isInventoryPending();
}
bool VSTaskInventoryObject::isInventoryDirty() const
{
    if (!*this) return false;
#if VS_NATIVE_VULKAN
    if (mNative) { const auto* inventory = nativeInventory(); return inventory && inventory->state(mObject).dirty; }
#endif
    return mLegacy.get()->isInventoryDirty();
}
S16 VSTaskInventoryObject::getInventorySerial() const
{
    if (!*this) return 0;
#if VS_NATIVE_VULKAN
    if (mNative) { const auto* inventory = nativeInventory(); return inventory ? inventory->state(mObject).serial : 0; }
#endif
    return mLegacy.get()->getInventorySerial();
}
void VSTaskInventoryObject::requestInventory() const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        if (auto* inventory = nativeInventory())
        {
            const auto state = inventory->state(mObject);
            if (state.pending) return;
            if (!state.dirty && inventory->snapshot(mObject)) inventory->notify(mObject);
            else inventory->request(mObject, [](const VSTaskInventory::Result&) {});
        }
        return;
    }
#endif
    mLegacy.get()->requestInventory();
}
void VSTaskInventoryObject::dirtyInventory() const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (mNative) { if (auto* inventory = nativeInventory()) inventory->dirty(mObject); return; }
#endif
    mLegacy.get()->dirtyInventory();
}
LLInventoryObject* VSTaskInventoryObject::getInventoryRoot() const
{
    if (!*this) return nullptr;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto* inventory = nativeInventory();
        const auto snapshot = inventory ? inventory->snapshot(mObject) : std::nullopt;
        return snapshot && !snapshot->contents.empty() ? snapshot->contents.back().get() : nullptr;
    }
#endif
    return mLegacy.get()->getInventoryRoot();
}

U32 VSTaskInventoryObject::objectFlags() const
{
    if (!*this) return 0;
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
        const auto admission = context ? context->world().objectAdmission(mObject) : std::nullopt;
        return admission ? admission->updateFlags : 0;
    }
#endif
    return mLegacy.get()->getFlags();
}
bool VSTaskInventoryObject::isPermanentEnforced() const
{
    if (!*this) return false;
    if (!mNative) return mLegacy.get()->isPermanentEnforced();
    const auto region = vs_current_region_context();
    return (objectFlags() & FLAGS_AFFECTS_NAVMESH) && (!region || region->getHost() != getHost()) && !gAgent.isGodlike();
}
LLUUID VSTaskInventoryObject::getAttachmentItemID() const
{
    if (!*this) return {};
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* context = gViewerWindow ? gViewerWindow->nativeContext() : nullptr;
        return context ? context->world().objectInventoryItem(mObject).value_or(LLUUID::null) : LLUUID::null;
    }
#endif
    return mLegacy.get()->getAttachmentItemID();
}

#if VS_NATIVE_VULKAN
bool VSTaskInventoryObject::requestInventory(VSTaskInventory::Callback callback) const
{
    auto* inventory = nativeInventory();
    if (!*this || !inventory || !callback) return false;
    const auto state = inventory->state(mObject);
    if (!state.pending && !state.dirty)
        if (const auto cached = inventory->snapshot(mObject))
        {
            callback(*cached);
            return true;
        }
    // A one-shot controller appends exactly one continuation to an existing
    // operation, including its real failure/timeout result. The no-argument
    // refresh overload never appends repeated per-frame no-op continuations.
    return inventory->request(mObject, std::move(callback));
}
#endif


bool VSTaskInventoryObject::requiresInventoryFetch() const
{
    if(!*this || !isInventoryDirty())return false;
    // Native task inventory owns and publishes its cache independently of UI
    // listeners. The reference fetch requires its original listener owner.
    return mNative || (mLegacy.get() && mLegacy.get()->hasInventoryListeners());
}
LLViewerInventoryItem* VSTaskInventoryObject::getInventoryItemByAsset(const LLUUID& asset,LLAssetType::EType type) const
{
    if(!*this)return nullptr;
    if(!mNative)return dynamic_cast<LLViewerInventoryItem*>(mLegacy.get()->getInventoryItemByAsset(asset,type));
    LLInventoryObject::object_list_t contents;getInventoryContents(contents);
    for(const auto& entry:contents)
        if(auto* item=dynamic_cast<LLViewerInventoryItem*>(entry.get());item && item->getAssetUUID()==asset && item->getType()==type)return item;
    return nullptr;
}
void VSTaskInventoryObject::updateMaterialInventory(LLViewerInventoryItem* item,U8 key,bool isNew) const
{
    if(!*this || !item)return;
    if(!mNative){mLegacy.get()->updateMaterialInventory(item,key,isNew);return;}
    if(item->getType()!=LLAssetType::AT_TEXTURE && item->getType()!=LLAssetType::AT_MATERIAL)return;
    if(getInventoryItemByAsset(item->getAssetUUID(),item->getType()))return;
    // The genuine native update performs the same local cache insertion and
    // network operation; that cache, rather than a UI-only stand-in, suppresses duplicates.
    updateInventory(item,key,isNew);
}

bool VSTaskInventoryObject::sameSource(const VSTaskInventoryObject& other) const
{
    if (mNative != other.mNative) return false;
#if VS_NATIVE_VULKAN
    if (mNative)
        return mOwner.lock() == other.mOwner.lock() && mObject.host == other.mObject.host &&
            mObject.id == other.mObject.id && mObject.local == other.mObject.local &&
            mObject.region == other.mObject.region && mObject.generation == other.mObject.generation &&
            mObject.kind == other.mObject.kind && mObject.account == other.mObject.account &&
            mObject.accountGeneration == other.mObject.accountGeneration;
#endif
    return mLegacy == other.mLegacy;
}
#if VS_NATIVE_VULKAN
VSTaskInventoryObject VSTaskInventoryObject::find(const VSWorldScene::ObjectHandle& token)
{
    const auto context = VSObjectContext::resolve(token);
    const auto owner = VSNativeSession::active();
    if (!context || !owner) return {};
    VSTaskInventoryObject result;
    result.mNative = true;
    result.mOwner = owner;
    result.mObject = token;
    result.mContext = *context;
    return result;
}
#endif
