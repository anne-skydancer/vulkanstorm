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
// >>> VKS-R4A-COPY C1 indra/newview/llviewerobject.cpp lines 303-308 blob c9ac3bdc20301e3e43da73183f18595712912e79
// LLViewerObject constructor: the task-inventory member initialisers
    mInventory(NULL),
    mInventorySerialNum(0),
    mExpectedInventorySerialNum(0),
    mInvRequestState(INVENTORY_REQUEST_STOPPED),
    mInvRequestXFerId(0),
    mInventoryDirty(false),
// <<< VKS-R4A-COPY C1

// >>> VKS-R4A-COPY C2 indra/newview/llviewerobject.cpp lines 370-375 blob c9ac3bdc20301e3e43da73183f18595712912e79
// LLViewerObject destructor: mInventory released
    if(mInventory)
    {
        mInventory->clear();  // will deref and delete entries
        delete mInventory;
        mInventory = NULL;
    }
// <<< VKS-R4A-COPY C2

// >>> VKS-R4A-COPY H1 indra/newview/llviewerobject.h lines 548-578 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject task-inventory declarations (public)
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
// <<< VKS-R4A-COPY H1

// >>> VKS-R4A-COPY H2 indra/newview/llviewerobject.h lines 972-997 blob fe6ce7f6e0ccc5dc8d10f26db5887a8cea2589f9
// LLViewerObject task-inventory state members
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
