/**
 * @file llinventory_task_oracle_boundary.h
 * @brief The one boundary between the task-inventory oracle's harness and the
 * task-inventory decision code. The harness calls the decision code only
 * through TaskInventoryBoundary; the implementation in use today is the
 * frozen copies of llviewerobject.cpp (frozen_boundary()). At W5 of the
 * inventory rung the harness swaps in an implementation that calls the Rust
 * decisions over FFI plus the C++ that carries out their actions, with
 * set_boundary(), and replays the same fixtures under the same fakes.
 *
 * Inputs: the calls below (the viewer's own entry points into
 * LLViewerObject's task inventory) and three asynchronous deliveries, which
 * the harness makes through the fakes and which reach whatever the
 * implementation registered:
 *   - an xfer completion: the callback and user data the implementation
 *     handed to gXferManager->requestFile (vks_task_oracle::FakeXfer);
 *   - an HTTP response: complete_http() resumes the coroutine suspended in
 *     HttpCoroutineAdapter::getAndSuspend/postAndSuspend;
 *   - a timer: fire_timer() resumes the coroutine suspended in
 *     llcoro::suspendUntilTimeout.
 * The incoming ReplyTaskInventory reaches processTaskInv as a message read
 * by the real template reader (LLMessageSystem getters).
 *
 * Outputs:
 *   - the task-inventory state of the LLViewerObject, which stays a C++
 *     object: mInventorySerialNum, mExpectedInventorySerialNum,
 *     mInvRequestState, mInvRequestXFerId, mInventoryDirty, mInventory (the
 *     entries, every field, in list order), mPendingInventoryItemsIDs,
 *     mInventoryCallbacks;
 *   - the side effects, in order, as recorded by the fakes (recorder()):
 *     msg.send <message> (and, in capture mode, the datagram itself),
 *     xfer.request/xfer.requested/xfer.abort, coro.launch, http.get/http.post
 *     (URL, and the POST body), timer.start, callback L<n> (listener
 *     notifications with serial and entries), floater.hide, and the
 *     create-item callback;
 *   - the return values of the calls below.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_TASK_ORACLE_BOUNDARY_H
#define LL_LLINVENTORY_TASK_ORACLE_BOUNDARY_H

#include "llinventory_task_oracle.h"

namespace vks_task_oracle
{
    class TaskInventoryBoundary
    {
    public:
        virtual ~TaskInventoryBoundary() {}
        virtual const char* name() const = 0;

        // listeners
        virtual void registerInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener, void* user_data) = 0;
        virtual void removeInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener) = 0;

        // requesting and receiving the contents
        virtual void requestInventory(LLViewerObject* obj) = 0;
        virtual void dirtyInventory(LLViewerObject* obj) = 0;
        // ReplyTaskInventory, read from msg's current message
        virtual void processTaskInv(LLMessageSystem* msg) = 0;
        virtual bool loadTaskInvFile(LLViewerObject* obj, const std::string& filename) = 0;

        // local mutations and the messages they send
        virtual void updateInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) = 0;
        virtual void updateInventoryLocal(LLViewerObject* obj, LLInventoryItem* item, U8 key) = 0;
        virtual void updateViewerInventoryAsset(LLViewerObject* obj, const LLViewerInventoryItem* item, const LLUUID& new_asset) = 0;
        virtual void updateMaterialInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) = 0;
        virtual void removeInventory(LLViewerObject* obj, const LLUUID& item_id) = 0;
        virtual void moveInventory(LLViewerObject* obj, const LLUUID& folder_id, const LLUUID& item_id) = 0;
        virtual void saveScript(LLViewerObject* obj, const LLViewerInventoryItem* item, bool active, bool is_new,
                                const LLUUID& template_id) = 0;
        virtual void createInventoryItem(LLViewerObject* obj,
                                         LLAssetType::EType asset_type,
                                         LLInventoryType::EType inventory_type,
                                         U8 sub_type,
                                         const std::string& name,
                                         const std::string& description,
                                         const LLPermissions& permissions,
                                         const LLSD& params,
                                         std::function<void(bool, const LLSD&)> callback) = 0;
        virtual void deleteInventoryItem(LLViewerObject* obj, const LLUUID& item_id) = 0;

        // listener and state accessors (object bookkeeping, not decisions:
        // W5 may forward them to the C++ object unchanged)
        virtual void clearInventoryListeners(LLViewerObject* obj) = 0;
        virtual bool hasInventoryListeners(LLViewerObject* obj) = 0;
        virtual bool isInventoryDirty(LLViewerObject* obj) = 0;
        virtual S16 getInventorySerial(LLViewerObject* obj) = 0;

        // request state and lookups over the contents (decision logic: the
        // W5 implementation answers them)
        virtual bool isInventoryPending(LLViewerObject* obj) = 0;
        virtual LLInventoryObject* getInventoryObject(LLViewerObject* obj, const LLUUID& item_id) = 0;
        virtual LLInventoryItem* getInventoryItem(LLViewerObject* obj, const LLUUID& item_id) = 0;
        virtual bool isAssetInInventory(LLViewerObject* obj, LLViewerInventoryItem* item, LLAssetType::EType type) = 0;

        // read helpers over the contents
        virtual LLInventoryObject* getInventoryRoot(LLViewerObject* obj) = 0;
        virtual void getInventoryContents(LLViewerObject* obj, LLInventoryObject::object_list_t& objects) = 0;
        virtual S32 countInventoryContents(LLViewerObject* obj, LLAssetType::EType type) = 0;
        virtual LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id) = 0;
        virtual LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id,
                                                               LLAssetType::EType type) = 0;
    };

    // The frozen copies (llinventory_task_oracle.cpp).
    TaskInventoryBoundary& frozen_boundary();
    // The implementation the harness uses: frozen_boundary() unless replaced.
    TaskInventoryBoundary& boundary();
    void set_boundary(TaskInventoryBoundary* implementation); // NULL: back to the frozen copies
}

#endif // LL_LLINVENTORY_TASK_ORACLE_BOUNDARY_H
