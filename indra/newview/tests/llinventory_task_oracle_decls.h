/**
 * @file llinventory_task_oracle_decls.h
 * @brief Live text the task-inventory oracle depends on without copying it
 * as code: LLViewerObject's task-inventory declarations, state members, and
 * the constructor and destructor lines that set and release that state. The
 * oracle's own LLViewerObject (llinventory_task_oracle.h) mirrors them by
 * hand; tests/check_r4a_copies.py checks every region below against the live
 * source like the copied code, so a change to them is caught. Never compiled
 * (everything is under #if 0).
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#if 0
// >>> VKS-R4A-COPY C1 indra/newview/llviewerobject.cpp lines 303-308 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// LLViewerObject constructor: the task-inventory member initialisers
// conditionals:
    mInventory(NULL),
    mInventorySerialNum(0),
    mExpectedInventorySerialNum(0),
    mInvRequestState(INVENTORY_REQUEST_STOPPED),
    mInvRequestXFerId(0),
    mInventoryDirty(false),
// <<< VKS-R4A-COPY C1

// >>> VKS-R4A-COPY C2 indra/newview/llviewerobject.cpp lines 370-375 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// LLViewerObject destructor: mInventory released
// conditionals:
    if(mInventory)
    {
        mInventory->clear();  // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }
// <<< VKS-R4A-COPY C2

// >>> VKS-R4A-COPY C3 indra/newview/llviewerobject.cpp lines 404-414 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// LLViewerObject destructor, end: clearInventoryListeners()
// conditionals:
    llassert(mChildList.size() == 0);
    llassert(mControlAvatar.isNull()); // Should have been cleaned by now
    if (mControlAvatar.notNull())
    {
        mControlAvatar->markForDeath();
        mControlAvatar = NULL;
        LL_WARNS() << "Dead object owned a live control avatar" << LL_ENDL;
    }

    clearInventoryListeners();
}
// <<< VKS-R4A-COPY C3

// >>> VKS-R4A-COPY P1 indra/newview/llviewerobject.cpp lines 7129-7151 blob 0ade67da75e4367e1fcae9e53e8e6388ddb612bf
// LLViewerObject::permYouOwner, an input of the owner guess (faked in the oracle)
// conditionals:
// Owned by this viewer?
bool LLViewerObject::permYouOwner() const
{
    if (isRootEdit())
    {
#ifdef HACKED_GODLIKE_VIEWER
        return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
        if (LLGridManager::getInstance()->isInSLBeta()
            && (gAgent.getGodLevel() >= GOD_MAINTENANCE))
        {
            return true;
        }
# endif
        return flagObjectYouOwner();
#endif
    }
    else
    {
        return ((LLViewerObject*)getParent())->permYouOwner();
    }
}
// <<< VKS-R4A-COPY P1

// >>> VKS-R4A-COPY H1 indra/newview/llviewerobject.h lines 546-603 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject task-inventory declarations (public)
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    void registerInventoryListener(LLVOInventoryListener* listener, void* user_data);
    void removeInventoryListener(LLVOInventoryListener* listener);
    bool isInventoryPending();
    void clearInventoryListeners();
    bool hasInventoryListeners();
    void requestInventory();
    static void processTaskInv(LLMessageSystem* msg, void** user_data);
    void removeInventory(const LLUUID& item_id);

    // The updateInventory() call potentially calls into the selection
    // manager, so do no call updateInventory() from the selection
    // manager until we have better iterators.
    void updateInventory(LLViewerInventoryItem* item, U8 key, bool is_new);
    void updateInventoryLocal(LLInventoryItem* item, U8 key); // Update without messaging.
    void updateMaterialInventory(LLViewerInventoryItem* item, U8 key, bool is_new);
    LLInventoryObject* getInventoryObject(const LLUUID& item_id);
    LLInventoryItem* getInventoryItem(const LLUUID& item_id);

    // Get content except for root category
    void getInventoryContents(LLInventoryObject::object_list_t& objects);
    LLInventoryObject* getInventoryRoot();
    LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID& asset_id);
    LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID& asset_id, LLAssetType::EType type);
    S16 getInventorySerial() const { return mInventorySerialNum; }

    // These functions does viewer-side only object inventory modifications
    void updateViewerInventoryAsset(
        const LLViewerInventoryItem* item,
        const LLUUID& new_asset);

    // This function will make sure that we refresh the inventory.
    void dirtyInventory();
    bool isInventoryDirty() { return mInventoryDirty; }

    // save a script, which involves removing the old one, and rezzing
    // in the new one. This method should be called with the asset id
    // of the new and old script AFTER the bytecode has been saved.
    void saveScript(const LLViewerInventoryItem* item, bool active, bool is_new, const LLUUID& template_id = LLUUID::null);

    void createInventoryItem(
        LLAssetType::EType asset_type,
        LLInventoryType::EType inventory_type,
        U8 sub_type,
        const std::string& name,
        const std::string& description,
        const LLPermissions& permissions,
        const LLSD& params,
        std::function<void(bool, const LLSD&)> callback);



    // move an inventory item out of the task and into agent
    // inventory. This operation is based on messaging. No permissions
    // checks are made on the viewer - the server will double check.
    void moveInventory(const LLUUID& agent_folder, const LLUUID& item_id);

    // Find the number of instances of this object's inventory that are of the given type
    S32 countInventoryContents( LLAssetType::EType type );
// <<< VKS-R4A-COPY H1

// >>> VKS-R4A-COPY H3 indra/newview/llviewerobject.h lines 729-730 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject::isAssetInInventory declaration (private)
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    void setObjectCostStale();
    bool isAssetInInventory(LLViewerInventoryItem* item, LLAssetType::EType type);
// <<< VKS-R4A-COPY H3

// >>> VKS-R4A-COPY H4 indra/newview/llviewerobject.h lines 769-776 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject fetch and coroutine declarations (private)
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    // forms task inventory request if none are pending, marks request as pending
    void fetchInventoryFromServer();

    // forms task inventory request after some time passed, marks request as pending
    void fetchInventoryDelayed(const F64 &time_seconds);
    static void fetchInventoryDelayedCoro(const LLUUID task_inv, const F64 time_seconds);
    static void fetchInventoryFromCapCoro(const LLUUID task_inv);
    static void createInventoryItemCoro(const std::string cap_url, const LLSD body, std::function<void(bool, const LLSD&)> callback);
// <<< VKS-R4A-COPY H4

// >>> VKS-R4A-COPY H5 indra/newview/llviewerobject.h lines 915-919 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject::deleteInventoryItem, doUpdateInventory declarations
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    void deleteInventoryItem(const LLUUID& item_id);

    // do the update/caching logic. called by saveScript and
    // updateInventory.
    void doUpdateInventory(LLPointer<LLViewerInventoryItem>& item, U8 key, bool is_new);
// <<< VKS-R4A-COPY H5

// >>> VKS-R4A-COPY H6 indra/newview/llviewerobject.h lines 931-934 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject task inventory file/LLSD loader and callback declarations
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    static void processTaskInvFile(void** user_data, S32 error_code, LLExtStat ext_status);
    bool loadTaskInvFile(const std::string& filename);
    void loadTaskInvLLSD(const LLSD &inv_result);
    void doInventoryCallback();
// <<< VKS-R4A-COPY H6

// >>> VKS-R4A-COPY H2 indra/newview/llviewerobject.h lines 972-997 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject task-inventory state members
// conditionals: #ifndef LL_LLVIEWEROBJECT_H
    std::list<LLUUID> mPendingInventoryItemsIDs;

    // This is the object's inventory from the viewer's perspective.
    LLInventoryObject::object_list_t* mInventory;
    class LLInventoryCallbackInfo
    {
    public:
        ~LLInventoryCallbackInfo();
        LLVOInventoryListener* mListener;
        void* mInventoryData;
    };
    typedef std::list<LLInventoryCallbackInfo*> callback_list_t;
    callback_list_t mInventoryCallbacks;
    S16 mInventorySerialNum;
    S16 mExpectedInventorySerialNum;

    enum EInventoryRequestState
    {
        INVENTORY_REQUEST_STOPPED,
        INVENTORY_REQUEST_WAIT,    // delay before requesting
        INVENTORY_REQUEST_PENDING, // just did fetchInventoryFromServer()
        INVENTORY_XFER             // processed response from 'fetch', now doing an xfer
    };
    EInventoryRequestState  mInvRequestState;
    U64                     mInvRequestXFerId;
    bool                    mInventoryDirty;
// <<< VKS-R4A-COPY H2

#endif // never compiled
