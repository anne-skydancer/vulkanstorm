/**
 * @file lltaskinventoryexecutor.inl
 * @brief The C++ half of LLViewerObject's task inventory ("object
 * contents") on vkstorm-rust rung seven, sub-rung R4a: it reads the object's
 * task-inventory state for the Rust decisions (vks_inv_task_* in
 * vkstorm_ffi.h) and carries out the actions they return.
 *
 * One source for the viewer and the oracle test. llviewerobject.cpp includes
 * it at namespace scope; INTEGRATION_TEST_llinventory_task_oracle includes it
 * inside the namespace vks_task_oracle, where the viewer singletons and
 * classes it names (LLViewerObject, gObjectList, gXferManager, gMessageSystem,
 * LLCoros, the HTTP adapter, LLFloaterReg, LLSelectMgr, gAgent ...) resolve to
 * the test's fakes by ordinary name lookup, so the test cannot pass against
 * an executor other than the one the viewer runs. Hence: no #include here;
 * the including file provides vkstorm_ffi.h, llsdserialize.h, llsdutil.h,
 * llfile.h, lldir.h, message_prehash.h and the classes above.
 *
 * LLViewerObject declares LLTaskInventoryExecutor a friend: the executor
 * reads and writes the task-inventory members. What stays C++ in the object:
 * the state itself, the listener bookkeeping (register, remove, clear, has),
 * the accessors, and the I/O the decisions ask for (the message fields, the
 * xfer manager, the coroutines and HTTP adapter, the cache files, the
 * capability reply's LLSD entries).
 *
 * Listeners may call into the object while they are notified (the material
 * editor and the notecard preview register again and request the contents);
 * the decisions are built for it: every action after a notification reads or
 * changes the object as it is then.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

struct LLTaskInventoryExecutor
{
    static_assert(LLViewerObject::INVENTORY_REQUEST_STOPPED == VKS_INV_TASK_REQUEST_STOPPED &&
                      LLViewerObject::INVENTORY_REQUEST_WAIT == VKS_INV_TASK_REQUEST_WAIT &&
                      LLViewerObject::INVENTORY_REQUEST_PENDING == VKS_INV_TASK_REQUEST_PENDING &&
                      LLViewerObject::INVENTORY_XFER == VKS_INV_TASK_XFER,
                  "EInventoryRequestState values");

    //------------------------------------------------------------------
    // Records
    //------------------------------------------------------------------
    static void setUUID(uint8_t* out, const LLUUID& id) { memcpy(out, id.mData, UUID_BYTES); }
    static LLUUID uuid(const uint8_t* in)
    {
        LLUUID id;
        memcpy(id.mData, in, UUID_BYTES);
        return id;
    }
    static VksInvBytes bytesOf(const std::string& s)
    {
        return VksInvBytes{ reinterpret_cast<const uint8_t*>(s.data()), s.size() };
    }
    static std::string text(const VksInvBytes& b)
    {
        return b.len ? std::string(reinterpret_cast<const char*>(b.ptr), b.len) : std::string();
    }

    // The item as its getters return it (the senders slice the caller's
    // item through them; a link's getters follow it). The strings borrow
    // the item's for the call.
    static void itemFromGetters(const LLInventoryItem* item, VksInvItem& out)
    {
        memset(&out, 0, sizeof(out));
        setUUID(out.object.id, item->getUUID());
        setUUID(out.object.parent, item->getParentUUID());
        out.object.asset_type = (int32_t)item->getType();
        out.object.creation_date = (int64_t)item->getCreationDate();
        out.object.name = bytesOf(item->getName());
        out.perms = *reinterpret_cast<const VksInvPerms*>(&item->getPermissions());
        setUUID(out.asset, item->getAssetUUID());
        out.sale = *reinterpret_cast<const VksInvSale*>(&item->getSaleInfo());
        out.inv_type = (int32_t)item->getInventoryType();
        out.flags = item->getFlags();
        out.description = bytesOf(item->getDescription());
    }

    static void viewerItemFromGetters(const LLViewerInventoryItem* item, VksInvTaskItem& out)
    {
        memset(&out, 0, sizeof(out));
        itemFromGetters(item, out.item);
        setUUID(out.transaction, item->getTransactionID());
    }

    // The item's fields (the copy constructor's view of it).
    static void viewerItemFields(const LLViewerInventoryItem* item, VksInvTaskItem& out)
    {
        memset(&out, 0, sizeof(out));
        item->toVks(out.item);
        setUUID(out.transaction, item->mTransactionID);
        out.complete = item->mIsComplete;
        out.running = item->mIsRunning;
        out.faulted = item->mIsFaulted;
    }

    static LLPointer<LLInventoryObject> makeItem(const VksInvTaskItem& rec)
    {
        LLViewerInventoryItem* item = new LLViewerInventoryItem;
        item->fromVks(rec.item);
        item->mIsComplete = rec.complete;
        memcpy(item->mTransactionID.mData, rec.transaction, UUID_BYTES);
        item->mIsRunning = rec.running;
        item->mIsFaulted = rec.faulted;
        return LLPointer<LLInventoryObject>(item);
    }

    static LLPointer<LLInventoryObject> makeObject(const VksInvObject& rec)
    {
        LLInventoryObject* obj = new LLInventoryObject;
        obj->fromVks(rec);
        return LLPointer<LLInventoryObject>(obj);
    }

    //------------------------------------------------------------------
    // The decisions' inputs
    //------------------------------------------------------------------
    enum
    {
        NEED_TASK_CAP = 1,   // the RequestTaskInventory capability (a fetch may follow)
        NEED_CREATE_CAP = 2, // the CreateTaskInventoryItem capability
        NEED_OWNER_GUESS = 4 // permYouOwner() and the selection's permissions
    };

    struct Inputs
    {
        VksInvTaskView view;
        VksInvTaskEnv env;
        std::vector<VksInvTaskEntry> entries;
        std::vector<LLInventoryObject*> objects; // entries' objects, same order
        std::vector<uint8_t> pending;
        std::string cap_task;
        std::string cap_create;
    };

    // One key per entry of mInventory, front to back, as the getters return
    // them.
    static void readEntries(LLViewerObject* obj, Inputs& in)
    {
        in.entries.clear();
        in.objects.clear();
        if (!obj->mInventory)
        {
            return;
        }
        in.entries.reserve(obj->mInventory->size());
        in.objects.reserve(obj->mInventory->size());
        for (const LLPointer<LLInventoryObject>& p : *obj->mInventory)
        {
            LLInventoryObject* o = p.get();
            VksInvTaskEntry k;
            memset(&k, 0, sizeof(k));
            setUUID(k.id, o->getUUID());
            k.asset_type = (int32_t)o->getType();
            if (LLViewerInventoryItem* item = dynamic_cast<LLViewerInventoryItem*>(o))
            {
                k.is_item = true;
                setUUID(k.asset, item->getAssetUUID());
                k.perms = reinterpret_cast<const VksInvPerms*>(&item->getPermissions());
            }
            in.entries.push_back(k);
            in.objects.push_back(o);
        }
    }

    static void readView(LLViewerObject* obj, Inputs& in)
    {
        VksInvTaskView& v = in.view;
        memset(&v, 0, sizeof(v));
        setUUID(v.id, obj->mID);
        v.local_id = obj->mLocalID;
        v.serial = obj->mInventorySerialNum;
        v.expected = obj->mExpectedInventorySerialNum;
        v.request = (int32_t)obj->mInvRequestState;
        v.xfer_id = obj->mInvRequestXFerId;
        v.dirty = obj->mInventoryDirty;
        v.has_inventory = obj->mInventory != NULL;
        v.has_listeners = !obj->mInventoryCallbacks.empty();
        readEntries(obj, in);
        v.entries = in.entries.data();
        v.entry_count = in.entries.size();
        in.pending.clear();
        in.pending.reserve(obj->mPendingInventoryItemsIDs.size() * UUID_BYTES);
        for (const LLUUID& id : obj->mPendingInventoryItemsIDs)
        {
            in.pending.insert(in.pending.end(), id.mData, id.mData + UUID_BYTES);
        }
        v.pending = in.pending.data();
        v.pending_count = obj->mPendingInventoryItemsIDs.size();
    }

    // obj may be NULL (an object gone before a create reply): only the agent.
    static void readEnv(LLViewerObject* obj, Inputs& in, int needs)
    {
        VksInvTaskEnv& e = in.env;
        memset(&e, 0, sizeof(e));
        setUUID(e.agent, gAgent.getID());
        setUUID(e.session, gAgent.getSessionID());
        setUUID(e.group, gAgent.getGroupID());
        e.has_region = obj && obj->mRegionp;
        if (e.has_region)
        {
            if (needs & NEED_TASK_CAP)
            {
                in.cap_task = obj->mRegionp->getCapability("RequestTaskInventory");
            }
            if (needs & NEED_CREATE_CAP)
            {
                in.cap_create = obj->mRegionp->getCapability("CreateTaskInventoryItem");
            }
            e.cap_request_task_inventory = bytesOf(in.cap_task);
            e.cap_create_task_inventory_item = bytesOf(in.cap_create);
        }
        if (obj && (needs & NEED_OWNER_GUESS))
        {
            e.you_owner = obj->permYouOwner();
            if (LLPermissions* perm = LLSelectMgr::getInstance()->findObjectPermissions(obj))
            {
                e.has_selection = true;
                setUUID(e.selection_owner, perm->getOwner());
                setUUID(e.selection_group, perm->getGroup());
            }
        }
    }

    static void read(LLViewerObject* obj, Inputs& in, int needs)
    {
        readView(obj, in);
        readEnv(obj, in, needs);
    }

    //------------------------------------------------------------------
    // Carrying out the actions
    //------------------------------------------------------------------
    struct Context
    {
        const LLSD* reply = NULL;                                 // the capability reply (PUSH_FRONT_REPLY_CONTENTS)
        LLSD* create_result = NULL;                               // the create reply (CREATE_CALLBACK)
        const std::function<void(bool, const LLSD&)>* callback = NULL; // createInventoryItem's
        bool http_get = false;                                    // an HTTP_GET to make, in the coroutine
        std::string url;
    };

    struct ResultFree
    {
        void operator()(VksInvTaskResult* r) const { vks_inv_task_result_free(r); }
    };
    typedef std::unique_ptr<VksInvTaskResult, ResultFree> ResultPtr;

    static void notify(LLViewerObject* obj)
    {
        // doInventoryCallback's loop.
        for (LLViewerObject::callback_list_t::iterator iter = obj->mInventoryCallbacks.begin();
             iter != obj->mInventoryCallbacks.end(); )
        {
            LLViewerObject::callback_list_t::iterator curiter = iter++;
            LLViewerObject::LLInventoryCallbackInfo* info = *curiter;
            if (info->mListener != NULL)
            {
                info->mListener->inventoryChanged(obj, obj->mInventory, obj->mInventorySerialNum, info->mInventoryData);
            }
            else
            {
                LL_INFOS() << "LLViewerObject::doInventoryCallback() deleting bad listener entry." << LL_ENDL;
                delete info;
                obj->mInventoryCallbacks.erase(curiter);
            }
        }
    }

    // LLViewerInventoryItem::packMessage's fields, from the decision's block.
    static void packItem(LLMessageSystem* msg, const VksInvItemBlock& b)
    {
        msg->addUUIDFast(_PREHASH_ItemID, uuid(b.item_id));
        msg->addUUIDFast(_PREHASH_FolderID, uuid(b.folder_id));
        msg->addUUIDFast(_PREHASH_CreatorID, uuid(b.creator_id));
        msg->addUUIDFast(_PREHASH_OwnerID, uuid(b.owner_id));
        msg->addUUIDFast(_PREHASH_GroupID, uuid(b.group_id));
        msg->addU32Fast(_PREHASH_BaseMask, b.base_mask);
        msg->addU32Fast(_PREHASH_OwnerMask, b.owner_mask);
        msg->addU32Fast(_PREHASH_GroupMask, b.group_mask);
        msg->addU32Fast(_PREHASH_EveryoneMask, b.everyone_mask);
        msg->addU32Fast(_PREHASH_NextOwnerMask, b.next_owner_mask);
        msg->addBOOLFast(_PREHASH_GroupOwned, b.group_owned);
        msg->addUUIDFast(_PREHASH_TransactionID, uuid(b.asset));
        msg->addS8Fast(_PREHASH_Type, b.asset_type);
        msg->addS8Fast(_PREHASH_InvType, b.inv_type);
        msg->addU32Fast(_PREHASH_Flags, b.flags);
        msg->addU8Fast(_PREHASH_SaleType, b.sale_type);
        msg->addS32Fast(_PREHASH_SalePrice, b.sale_price);
        msg->addStringFast(_PREHASH_Name, text(b.name));
        msg->addStringFast(_PREHASH_Description, text(b.description));
        msg->addS32Fast(_PREHASH_CreationDate, b.creation_date);
        msg->addU32Fast(_PREHASH_CRC, b.crc);
    }

    static void send(LLViewerObject* obj, const VksInvTaskMessage& m)
    {
        if (!obj->mRegionp)
        {
            return; // the decisions send nothing without a region
        }
        LLMessageSystem* msg = gMessageSystem;
        switch (m.kind)
        {
        case VKS_INV_TASK_MSG_REQUEST_TASK_INVENTORY:
            msg->newMessageFast(_PREHASH_RequestTaskInventory);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, uuid(m.agent));
            msg->addUUIDFast(_PREHASH_SessionID, uuid(m.session));
            msg->nextBlockFast(_PREHASH_InventoryData);
            msg->addU32Fast(_PREHASH_LocalID, m.local_id);
            break;
        case VKS_INV_TASK_MSG_UPDATE_TASK_INVENTORY:
            msg->newMessageFast(_PREHASH_UpdateTaskInventory);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, uuid(m.agent));
            msg->addUUIDFast(_PREHASH_SessionID, uuid(m.session));
            msg->nextBlockFast(_PREHASH_UpdateData);
            msg->addU32Fast(_PREHASH_LocalID, m.local_id);
            msg->addU8Fast(_PREHASH_Key, m.key);
            msg->nextBlockFast(_PREHASH_InventoryData);
            packItem(msg, m.item);
            break;
        case VKS_INV_TASK_MSG_REMOVE_TASK_INVENTORY:
            msg->newMessageFast(_PREHASH_RemoveTaskInventory);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, uuid(m.agent));
            msg->addUUIDFast(_PREHASH_SessionID, uuid(m.session));
            msg->nextBlockFast(_PREHASH_InventoryData);
            msg->addU32Fast(_PREHASH_LocalID, m.local_id);
            msg->addUUIDFast(_PREHASH_ItemID, uuid(m.item_id));
            break;
        case VKS_INV_TASK_MSG_MOVE_TASK_INVENTORY:
            msg->newMessageFast(_PREHASH_MoveTaskInventory);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, uuid(m.agent));
            msg->addUUIDFast(_PREHASH_SessionID, uuid(m.session));
            msg->addUUIDFast(_PREHASH_FolderID, uuid(m.folder_id));
            msg->nextBlockFast(_PREHASH_InventoryData);
            msg->addU32Fast(_PREHASH_LocalID, m.local_id);
            msg->addUUIDFast(_PREHASH_ItemID, uuid(m.item_id));
            break;
        case VKS_INV_TASK_MSG_REZ_SCRIPT:
            msg->newMessageFast(_PREHASH_RezScript);
            msg->nextBlockFast(_PREHASH_AgentData);
            msg->addUUIDFast(_PREHASH_AgentID, uuid(m.agent));
            msg->addUUIDFast(_PREHASH_SessionID, uuid(m.session));
            msg->addUUIDFast(_PREHASH_GroupID, uuid(m.group));
            msg->nextBlockFast(_PREHASH_UpdateBlock);
            msg->addU32Fast(_PREHASH_ObjectLocalID, m.local_id);
            msg->addBOOLFast(_PREHASH_Enabled, m.enabled);
            msg->nextBlockFast(_PREHASH_InventoryBlock);
            packItem(msg, m.item);
            if (m.has_template)
            {
                msg->nextBlock("NewScriptInfo");
                msg->addUUID("TemplateID", uuid(m.template_id));
            }
            break;
        default:
            LL_WARNS() << "Unknown task inventory message " << m.kind << LL_ENDL;
            return;
        }
        msg->sendReliable(obj->mRegionp->getHost());
    }

    // The xfer's user data (LLFilenameAndTask's role): owned by the
    // transfer, deleted by its callback.
    struct XferData
    {
        LLUUID mTaskID;
        std::string mFilename;
        S16 mSerial;
    };

    // The cache file as the loader's text-mode stream reads it (on Windows
    // after CRLF -> LF, ending at a Ctrl-Z); false when it does not open.
    static bool readCacheFile(const std::string& filename, std::string& out)
    {
        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_CACHE, filename);
        llifstream ifs(path.c_str());
        if (!ifs.good())
        {
            return false;
        }
        char buffer[65536];
        for (;;)
        {
            ifs.read(buffer, sizeof(buffer));
            const std::streamsize got = ifs.gcount();
            if (got > 0)
            {
                out.append(buffer, (size_t)got);
            }
            if (!ifs)
            {
                break;
            }
        }
        ifs.close();
        return true;
    }

    static void apply(LLViewerObject* obj, VksInvTaskResult* raw, Context& ctx)
    {
        ResultPtr result(raw);
        if (!result)
        {
            LL_WARNS() << "Task inventory decision failed: " << vks_inv_last_error() << LL_ENDL;
            return;
        }
        const VksInvTaskResultView* r = vks_inv_task_result_view(result.get());
        for (size_t i = 0; i < r->action_count; ++i)
        {
            const VksInvTaskAction& a = r->actions[i];
            switch (a.kind)
            {
            case VKS_INV_TASK_SET_SERIAL:
                obj->mInventorySerialNum = (S16)a.value;
                break;
            case VKS_INV_TASK_SET_EXPECTED:
                obj->mExpectedInventorySerialNum = (S16)a.value;
                break;
            case VKS_INV_TASK_ADD_EXPECTED:
                obj->mExpectedInventorySerialNum = (S16)(obj->mExpectedInventorySerialNum + a.value);
                break;
            case VKS_INV_TASK_SET_REQUEST_STATE:
                obj->mInvRequestState = (LLViewerObject::EInventoryRequestState)a.value;
                break;
            case VKS_INV_TASK_SET_XFER_ID:
                obj->mInvRequestXFerId = a.id;
                break;
            case VKS_INV_TASK_SET_DIRTY:
                obj->mInventoryDirty = a.value != 0;
                break;
            case VKS_INV_TASK_DELETE_INVENTORY:
                if (obj->mInventory)
                {
                    obj->mInventory->clear(); // will deref and delete entries
                    delete obj->mInventory;
                    obj->mInventory = NULL;
                }
                break;
            case VKS_INV_TASK_RESET_INVENTORY:
                if (obj->mInventory)
                {
                    obj->mInventory->clear(); // will deref and delete it
                }
                else
                {
                    obj->mInventory = new LLInventoryObject::object_list_t;
                }
                break;
            case VKS_INV_TASK_PUSH_FRONT_ITEM:
                if (obj->mInventory && a.value >= 0 && (size_t)a.value < r->item_count)
                {
                    obj->mInventory->push_front(makeItem(r->items[a.value]));
                }
                break;
            case VKS_INV_TASK_PUSH_FRONT_OBJECT:
                if (obj->mInventory && a.value >= 0 && (size_t)a.value < r->object_count)
                {
                    obj->mInventory->push_front(makeObject(r->objects[a.value]));
                }
                break;
            case VKS_INV_TASK_PUSH_FRONT_REPLY_CONTENTS:
                if (obj->mInventory && ctx.reply)
                {
                    // The capability reply's entries: an LLSD form, read by
                    // LLViewerInventoryItem::unpackMessage(LLSD).
                    const LLSD& inventory = (*ctx.reply)["contents"];
                    for (const auto& inv_entry : llsd::inArray(inventory))
                    {
                        if (inv_entry.has("item_id"))
                        {
                            LLPointer<LLViewerInventoryItem> inv = new LLViewerInventoryItem;
                            inv->unpackMessage(inv_entry);
                            obj->mInventory->push_front(inv);
                        }
                        else
                        {
                            LL_WARNS_ONCE() << "Unknown inventory entry while reading from inventory file. Entry: '"
                                            << inv_entry << "'" << LL_ENDL;
                        }
                    }
                }
                break;
            case VKS_INV_TASK_ERASE:
                if (obj->mInventory && a.value >= 0 && (size_t)a.value < obj->mInventory->size())
                {
                    LLInventoryObject::object_list_t::iterator it = obj->mInventory->begin();
                    std::advance(it, a.value);
                    obj->mInventory->erase(it); // will deref and delete it
                }
                break;
            case VKS_INV_TASK_PENDING_PUSH:
                obj->mPendingInventoryItemsIDs.push_back(uuid(a.uuid));
                break;
            case VKS_INV_TASK_PENDING_ERASE:
                if (a.value >= 0 && (size_t)a.value < obj->mPendingInventoryItemsIDs.size())
                {
                    std::list<LLUUID>::iterator it = obj->mPendingInventoryItemsIDs.begin();
                    std::advance(it, a.value);
                    obj->mPendingInventoryItemsIDs.erase(it);
                }
                break;
            case VKS_INV_TASK_NOTIFY_LISTENERS:
                notify(obj);
                break;
            case VKS_INV_TASK_NOTIFY_LISTENERS_KEEP_REQUEST:
            {
                // A request or transfer is outstanding: the listeners get the
                // contents, and the request state and xfer id stay.
                const LLViewerObject::EInventoryRequestState request_state = obj->mInvRequestState;
                const U64 request_xfer_id = obj->mInvRequestXFerId;
                notify(obj);
                obj->mInvRequestState = request_state;
                obj->mInvRequestXFerId = request_xfer_id;
                break;
            }
            case VKS_INV_TASK_CLEAR_LOADED_PENDING:
                decide(obj, 0, ctx, [](const VksInvTaskView* v, const VksInvTaskEnv* e) {
                    return vks_inv_task_clear_loaded_pending(v, e);
                });
                break;
            case VKS_INV_TASK_SEND:
                if (a.value >= 0 && (size_t)a.value < r->message_count)
                {
                    send(obj, r->messages[a.value]);
                }
                break;
            case VKS_INV_TASK_LAUNCH_CAP_FETCH:
            {
                const LLUUID task_inv = obj->mID;
                LLCoros::instance().launch("LLViewerObject::fetchInventoryFromCapCoro()",
                                           [task_inv]() { capFetchCoro(task_inv); });
                break;
            }
            case VKS_INV_TASK_HTTP_GET:
                // The last action of vks_inv_task_cap_fetch_begin: the
                // coroutine makes the request once the block is gone.
                ctx.http_get = true;
                ctx.url = text(a.text);
                return;
            case VKS_INV_TASK_LAUNCH_DELAYED_FETCH:
            {
                const LLUUID task_inv = obj->mID;
                const F64 time_seconds = a.seconds;
                LLCoros::instance().launch("LLViewerObject::fetchInventoryDelayedCoro()",
                                           [task_inv, time_seconds]() { delayedFetchCoro(task_inv, time_seconds); });
                break;
            }
            case VKS_INV_TASK_REQUEST_XFER:
            {
                if (!obj->mRegionp)
                {
                    break; // the decisions request nothing without a region
                }
                XferData* ft = new XferData;
                ft->mTaskID = obj->mID;
                ft->mSerial = (S16)a.value;
                ft->mFilename = text(a.text);
                U64 new_id = gXferManager->requestFile(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, ft->mFilename),
                                                       ft->mFilename, LL_PATH_CACHE,
                                                       obj->mRegionp->getHost(),
                                                       true,
                                                       &LLTaskInventoryExecutor::xferCompleted,
                                                       (void**)ft, // This takes ownership of ft
                                                       LLXferManager::HIGH_PRIORITY);
                decide(obj, 0, ctx, [new_id](const VksInvTaskView* v, const VksInvTaskEnv* e) {
                    return vks_inv_task_xfer_requested(v, e, new_id);
                });
                break;
            }
            case VKS_INV_TASK_ABORT_XFER:
                gXferManager->abortRequestById(a.id, -1);
                break;
            case VKS_INV_TASK_REMOVE_CACHE_FILE:
                LLFile::remove(gDirUtilp->getExpandedFilename(LL_PATH_CACHE, text(a.text)));
                break;
            case VKS_INV_TASK_HIDE_FLOATER:
                if (a.value == 1)
                {
                    LLSD params;
                    params["id"] = uuid(a.uuid);
                    params["object"] = uuid(a.uuid2);
                    LLFloaterReg::hideInstance(text(a.text), params);
                }
                else
                {
                    LLFloaterReg::hideInstance(text(a.text), uuid(a.uuid));
                }
                break;
            case VKS_INV_TASK_LAUNCH_CREATE:
            {
                const std::string cap_url = text(a.text);
                LLSD body;
                std::istringstream body_stream(text(a.text2));
                LLSDSerialize::fromBinary(body, body_stream, (S32)a.text2.len);
                std::function<void(bool, const LLSD&)> callback;
                if (ctx.callback)
                {
                    callback = *ctx.callback;
                }
                const LLUUID object_id = obj->mID;
                LLCoros::instance().launch("LLViewerObject::createInventoryItemCoro",
                                           [cap_url, body, callback, object_id]() {
                                               createItemCoro(cap_url, body, callback, object_id);
                                           });
                break;
            }
            case VKS_INV_TASK_CREATE_CALLBACK:
                if (ctx.callback && *ctx.callback)
                {
                    LLSD empty;
                    (*ctx.callback)(a.value != 0, ctx.create_result ? *ctx.create_result : empty);
                }
                break;
            case VKS_INV_TASK_CREATE_FAILED:
                if (ctx.callback && *ctx.callback)
                {
                    (*ctx.callback)(false, LLSD().with("message", text(a.text)));
                }
                break;
            default:
                LL_WARNS() << "Unknown task inventory action " << a.kind << LL_ENDL;
                break;
            }
        }
    }

    // A decision over the object as it is now, carried out at once.
    template <typename Decide>
    static bool decide(LLViewerObject* obj, int needs, Context& ctx, Decide&& call)
    {
        Inputs in;
        read(obj, in, needs);
        VksInvTaskResult* r = call(&in.view, &in.env);
        const bool ok = r && vks_inv_task_result_view(r)->ok;
        apply(obj, r, ctx);
        return ok;
    }

    template <typename Decide>
    static bool decide(LLViewerObject* obj, int needs, Decide&& call)
    {
        Context ctx;
        return decide(obj, needs, ctx, std::forward<Decide>(call));
    }

    //------------------------------------------------------------------
    // The coroutines and the xfer callback
    //------------------------------------------------------------------
    static void capFetchCoro(const LLUUID task_inv)
    {
        LLViewerObject* obj = gObjectList.findObject(task_inv);
        if (!obj)
        {
            return;
        }
        Context begin;
        decide(obj, NEED_TASK_CAP, begin, [](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_cap_fetch_begin(v, e);
        });
        if (!begin.http_get)
        {
            return;
        }
        LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t
            httpAdapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("TaskInventoryRequest", httpPolicy);
        LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();
        LLSD result = httpAdapter->getAndSuspend(httpRequest, begin.url);

        LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

        // Object may have gone away while we were suspended, double-check that it still exists
        obj = gObjectList.findObject(task_inv);
        if (!obj)
        {
            LL_WARNS() << "Object " << task_inv << " went away while fetching inventory, dropping result" << LL_ENDL;
            return;
        }
        if (!status && status.getType() != 304)
        {
            LL_WARNS() << "Error status while requesting task inventory: " << status.toString() << LL_ENDL;
        }
        const LLSD& reply = result;
        const bool success = static_cast<bool>(status);
        const S32 http_status = (S32)status.getType();
        const S32 serial = reply["inventory_serial"].asInteger();
        const bool has_contents = reply.has("contents");
        Context ctx;
        ctx.reply = &reply;
        decide(obj, NEED_TASK_CAP, ctx, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_cap_fetch_reply(v, e, success, http_status, serial, has_contents);
        });
    }

    static void delayedFetchCoro(const LLUUID task_inv, const F64 time_seconds)
    {
        llcoro::suspendUntilTimeout((float)time_seconds);
        LLViewerObject* obj = gObjectList.findObject(task_inv);
        if (obj)
        {
            decide(obj, NEED_TASK_CAP, [](const VksInvTaskView* v, const VksInvTaskEnv* e) {
                return vks_inv_task_delayed_fetch_due(v, e);
            });
        }
    }

    static void createItemCoro(const std::string cap_url, const LLSD body,
                               std::function<void(bool, const LLSD&)> callback, const LLUUID object_id)
    {
        LLCore::HttpRequest::policy_t httpPolicy(LLCore::HttpRequest::DEFAULT_POLICY_ID);
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter =
            std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("CreateTaskInventoryItem", httpPolicy);
        LLCore::HttpRequest::ptr_t httpRequest = std::make_shared<LLCore::HttpRequest>();

        LLSD result = httpAdapter->postAndSuspend(httpRequest, cap_url, body);

        LLSD httpResults = result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS];
        LLCore::HttpStatus status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(httpResults);

        // As the C++ did: result["success"] is read (and so added to the
        // map the callback gets) only after a successful status.
        const bool http_ok = static_cast<bool>(status);
        const bool result_success = http_ok && result["success"].asBoolean();

        LLViewerObject* obj = gObjectList.findObject(object_id);
        Inputs in;
        if (obj)
        {
            read(obj, in, NEED_TASK_CAP);
        }
        else
        {
            readEnv(NULL, in, 0);
        }
        VksInvTaskResult* r = vks_inv_task_create_item_completed(obj ? &in.view : NULL, &in.env, http_ok, result_success);
        Context ctx;
        ctx.callback = &callback;
        ctx.create_result = &result;
        if (obj)
        {
            apply(obj, r, ctx);
        }
        else
        {
            // Only the callback: nothing touches an object.
            ResultPtr owned(r);
            const VksInvTaskResultView* v = r ? vks_inv_task_result_view(r) : NULL;
            for (size_t i = 0; v && i < v->action_count; ++i)
            {
                if (v->actions[i].kind == VKS_INV_TASK_CREATE_CALLBACK && callback)
                {
                    callback(v->actions[i].value != 0, result);
                }
            }
        }
    }

    static void xferCompleted(void** user_data, S32 error_code, LLExtStat ext_status)
    {
        XferData* ft = (XferData*)user_data;
        LLViewerObject* object = ft ? gObjectList.findObject(ft->mTaskID) : NULL;
        if (object)
        {
            std::string file;
            const bool has_file = readCacheFile(ft->mFilename, file);
            decide(object, 0, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
                return vks_inv_task_xfer_completed(v, e, ft->mSerial,
                                                   reinterpret_cast<const uint8_t*>(ft->mFilename.data()),
                                                   ft->mFilename.size(), error_code, has_file,
                                                   reinterpret_cast<const uint8_t*>(file.data()), file.size());
            });
        }
        else
        {
            // This Occurs When two requests were made, and the first one
            // has already handled it.
            LL_DEBUGS() << "Problem loading task inventory. Return code: " << error_code << LL_ENDL;
        }
        delete ft;
    }

    //------------------------------------------------------------------
    // The LLViewerObject members
    //------------------------------------------------------------------
    static void requestInventory(LLViewerObject* obj)
    {
        decide(obj, NEED_TASK_CAP, [](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_request_inventory(v, e);
        });
    }

    static void dirtyInventory(LLViewerObject* obj)
    {
        decide(obj, 0, [](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_dirty_inventory(v, e);
        });
    }

    static void deleteInventoryItem(LLViewerObject* obj, const LLUUID& item_id)
    {
        decide(obj, 0, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_delete_inventory_item(v, e, item_id.mData);
        });
    }

    // ReplyTaskInventory.
    static void processTaskInv(LLMessageSystem* msg)
    {
        LLUUID task_id;
        msg->getUUIDFast(_PREHASH_InventoryData, _PREHASH_TaskID, task_id);
        LLViewerObject* object = gObjectList.findObject(task_id);
        if (!object)
        {
            LL_WARNS() << "LLViewerObject::processTaskInv object "
                << task_id << " does not exist." << LL_ENDL;
            return;
        }
        S16 serial = 0;
        msg->getS16Fast(_PREHASH_InventoryData, _PREHASH_Serial, serial);
        std::string unclean_filename;
        msg->getStringFast(_PREHASH_InventoryData, _PREHASH_Filename, unclean_filename);
        decide(object, NEED_TASK_CAP, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_reply(v, e, serial, reinterpret_cast<const uint8_t*>(unclean_filename.data()),
                                      unclean_filename.size());
        });
    }

    static bool loadTaskInvFile(LLViewerObject* obj, const std::string& filename)
    {
        std::string file;
        const bool has_file = readCacheFile(filename, file);
        if (!has_file)
        {
            LL_WARNS() << "unable to load task inventory: "
                       << gDirUtilp->getExpandedFilename(LL_PATH_CACHE, filename) << LL_ENDL;
        }
        return decide(obj, 0, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_load_file(v, e, reinterpret_cast<const uint8_t*>(filename.data()), filename.size(),
                                          has_file, reinterpret_cast<const uint8_t*>(file.data()), file.size());
        });
    }

    static void updateInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new)
    {
        VksInvTaskItem rec;
        viewerItemFromGetters(item, rec);
        decide(obj, NEED_OWNER_GUESS, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_update_inventory(v, e, &rec, key, is_new);
        });
    }

    static void updateInventoryLocal(LLViewerObject* obj, LLInventoryItem* item, U8 key)
    {
        VksInvItem rec;
        itemFromGetters(item, rec);
        decide(obj, NEED_OWNER_GUESS, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_update_inventory_local(v, e, &rec, key);
        });
    }

    static void updateViewerInventoryAsset(LLViewerObject* obj, const LLViewerInventoryItem* item,
                                           const LLUUID& new_asset)
    {
        VksInvTaskItem rec;
        viewerItemFields(item, rec);
        decide(obj, NEED_OWNER_GUESS, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_update_viewer_inventory_asset(v, e, &rec, new_asset.mData);
        });
    }

    static void updateMaterialInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new)
    {
        if (!item)
        {
            return;
        }
        VksInvTaskItem rec;
        viewerItemFromGetters(item, rec);
        decide(obj, NEED_OWNER_GUESS, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_update_material_inventory(v, e, &rec, key, is_new);
        });
    }

    static void removeInventory(LLViewerObject* obj, const LLUUID& item_id)
    {
        decide(obj, 0, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_remove_inventory(v, e, item_id.mData);
        });
    }

    static void moveInventory(LLViewerObject* obj, const LLUUID& folder_id, const LLUUID& item_id)
    {
        decide(obj, 0, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_move_inventory(v, e, folder_id.mData, item_id.mData);
        });
    }

    static void saveScript(LLViewerObject* obj, const LLViewerInventoryItem* item, bool active, bool is_new,
                           const LLUUID& template_id)
    {
        VksInvTaskItem rec;
        viewerItemFromGetters(item, rec);
        decide(obj, NEED_OWNER_GUESS, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_save_script(v, e, &rec, active, is_new, template_id.mData);
        });
    }

    static void createInventoryItem(LLViewerObject* obj, LLAssetType::EType asset_type,
                                    LLInventoryType::EType inventory_type, U8 sub_type, const std::string& name,
                                    const std::string& description, const LLPermissions& permissions,
                                    const LLSD& params, const std::function<void(bool, const LLSD&)>& callback)
    {
        std::string params_binary;
        if (params.isDefined())
        {
            std::ostringstream os;
            LLSDSerialize::toBinary(params, os);
            params_binary = os.str();
        }
        Context ctx;
        ctx.callback = &callback;
        decide(obj, NEED_CREATE_CAP, ctx, [&](const VksInvTaskView* v, const VksInvTaskEnv* e) {
            return vks_inv_task_create_inventory_item(
                v, e, (int32_t)asset_type, (int32_t)inventory_type, sub_type,
                reinterpret_cast<const uint8_t*>(name.data()), name.size(),
                reinterpret_cast<const uint8_t*>(description.data()), description.size(),
                reinterpret_cast<const VksInvPerms*>(&permissions), params.isDefined(),
                reinterpret_cast<const uint8_t*>(params_binary.data()), params_binary.size());
        });
    }

    //------------------------------------------------------------------
    // The lookups
    //------------------------------------------------------------------
    static LLInventoryObject* find(LLViewerObject* obj, uint32_t kind, const LLUUID& id, S32 type)
    {
        if (!obj->mInventory)
        {
            return NULL;
        }
        Inputs in;
        readEntries(obj, in);
        const int64_t i = vks_inv_task_find(in.entries.data(), in.entries.size(), kind, id.mData, type);
        return i < 0 ? NULL : in.objects[(size_t)i];
    }

    static LLInventoryObject* getInventoryObject(LLViewerObject* obj, const LLUUID& item_id)
    {
        return find(obj, VKS_INV_TASK_FIND_OBJECT, item_id, 0);
    }

    static LLInventoryItem* getInventoryItem(LLViewerObject* obj, const LLUUID& item_id)
    {
        // An entry found is an LLViewerInventoryItem.
        return static_cast<LLViewerInventoryItem*>(find(obj, VKS_INV_TASK_FIND_ITEM, item_id, 0));
    }

    static void getInventoryContents(LLViewerObject* obj, LLInventoryObject::object_list_t& objects)
    {
        if (!obj->mInventory)
        {
            return;
        }
        Inputs in;
        readEntries(obj, in);
        std::vector<size_t> found(in.entries.size());
        const size_t n = vks_inv_task_contents(in.entries.data(), in.entries.size(), found.data());
        for (size_t k = 0; k < n; ++k)
        {
            objects.push_back(LLPointer<LLInventoryObject>(in.objects[found[k]]));
        }
    }

    static LLInventoryObject* getInventoryRoot(LLViewerObject* obj)
    {
        return find(obj, VKS_INV_TASK_FIND_ROOT, LLUUID::null, 0);
    }

    static LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id)
    {
        if (obj->mInventoryDirty)
            LL_WARNS() << "Peforming inventory lookup for object " << obj->mID << " that has dirty inventory!" << LL_ENDL;
        return static_cast<LLViewerInventoryItem*>(find(obj, VKS_INV_TASK_FIND_ITEM_BY_ASSET, asset_id, 0));
    }

    static LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id,
                                                          LLAssetType::EType type)
    {
        if (obj->mInventoryDirty)
            LL_WARNS() << "Peforming inventory lookup for object " << obj->mID << " that has dirty inventory!" << LL_ENDL;
        // Whatever calls this with AT_CATEGORY shouldn't be trying to get a
        // folder by asset: categories don't have assets.
        llassert(type != LLAssetType::AT_CATEGORY);
        return static_cast<LLViewerInventoryItem*>(
            find(obj, VKS_INV_TASK_FIND_ITEM_BY_ASSET_AND_TYPE, asset_id, (S32)type));
    }

    static S32 countInventoryContents(LLViewerObject* obj, LLAssetType::EType type)
    {
        if (!obj->mInventory)
        {
            return 0;
        }
        Inputs in;
        readEntries(obj, in);
        return (S32)vks_inv_task_count_of_type(in.entries.data(), in.entries.size(), (int32_t)type);
    }

    static bool isAssetInInventory(LLViewerObject* obj, LLViewerInventoryItem* item, LLAssetType::EType type)
    {
        if (!item)
        {
            return false;
        }
        Inputs in;
        readView(obj, in);
        return vks_inv_task_is_asset_in_inventory(in.entries.data(), in.entries.size(), in.view.has_inventory,
                                                  in.pending.data(), obj->mPendingInventoryItemsIDs.size(),
                                                  item->getAssetUUID().mData, (int32_t)type);
    }
};
