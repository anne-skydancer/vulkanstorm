// Typed access to genuine reference/native task inventory owners. LGPL-2.1.
#pragma once
#include "llviewerobject.h"
#include "vsobjectcontext.h"
#include <functional>
#include <string>
#if VS_NATIVE_VULKAN
#include "vstaskinventory.h"
#endif
class VSTaskInventoryObject
{
public:
    static VSTaskInventoryObject find(const LLUUID&);
    bool sameSource(const VSTaskInventoryObject&) const;
#if VS_NATIVE_VULKAN
    static VSTaskInventoryObject find(const VSWorldScene::ObjectHandle&);
#endif
    explicit operator bool() const;
    const VSTaskInventoryObject* operator->() const { return this; }
    VSTaskInventoryObject* operator->() { return this; }
    LLInventoryObject* getInventoryObject(const LLUUID&) const;
    void updateInventory(LLViewerInventoryItem*, U8, bool) const;
    void updateMaterialInventory(LLViewerInventoryItem*, U8, bool) const;
    LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID&,LLAssetType::EType) const;
    bool requiresInventoryFetch() const;
    void updateInventoryLocal(LLInventoryItem*, U8) const;
    void updateViewerInventoryAsset(const LLViewerInventoryItem*, const LLUUID&) const;
    void saveScript(const LLViewerInventoryItem*, bool running, bool is_new) const;
    void moveInventory(const LLUUID& folder, const LLUUID& item) const;
    void removeInventory(const LLUUID&) const;
    bool isInventoryPending() const;
    bool isInventoryDirty() const;
    S16 getInventorySerial() const;
    void requestInventory() const;
    void dirtyInventory() const;
    LLInventoryObject* getInventoryRoot() const;
    void getInventoryContents(LLInventoryObject::object_list_t&) const;
    bool permYouOwner() const;
    bool flagScripted() const;
    U32 objectFlags() const;
    bool isPermanentEnforced() const;
    LLUUID getAttachmentItemID() const;
    bool flagAllowInventoryAdd() const;
    U8 getPCode() const;
    bool isAttachment() const;
    bool isAvatar() const;
    VSTaskInventoryObject getRootEdit() const;
    VSTaskInventoryObject getParent() const;
    bool agentSittingOnRoot() const;
    bool wearing(const LLUUID&) const;
    bool permModify() const;
    bool permAnyOwner() const;
    bool attachmentLocked() const;
    const LLUUID& getID() const { return mContext.id; }
    U32 getLocalID() const { return mContext.localID; }
    const LLHost& getHost() const { return mContext.host; }
    std::string getCapability(const std::string&) const;
    std::function<bool()> itemGuard(const LLUUID&) const;
    std::function<bool()> objectGuard() const;
    bool native() const { return mNative; }
    LLViewerObject* legacy() const { return *this && !mNative ? mLegacy.get() : nullptr; }
#if VS_NATIVE_VULKAN
    VSTaskInventory* nativeInventory() const;
    bool requestInventory(VSTaskInventory::Callback) const;
    const VSWorldScene::ObjectHandle& nativeObject() const { return mObject; }
#endif
private:
    VSObjectContext mContext;
    LLPointer<LLViewerObject> mLegacy;
    bool mNative = false;
#if VS_NATIVE_VULKAN
    VSWorldScene::ObjectHandle mObject;
    std::weak_ptr<VSNativeSession> mOwner;
#endif
};
