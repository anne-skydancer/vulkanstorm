/**
 * @file llinventory_task_oracle_rust.cpp
 * @brief The task-inventory oracle's boundary over the viewer's own
 * implementation: the Rust decisions (vks_inv_task_* in vkstorm_ffi.h) and
 * the C++ that carries them out, lltaskinventoryexecutor.inl, the same
 * source llviewerobject.cpp compiles. Included here inside the namespace
 * vks_task_oracle, its names resolve to the oracle's fakes and its
 * LLViewerObject mirror (llinventory_task_oracle.h), as the frozen copies'
 * do; the harness replays the fixtures recorded from the frozen copies
 * against it.
 *
 * The listener bookkeeping, the state accessors and the read-only lookups
 * stay C++ in the viewer, unchanged; here they forward to the oracle
 * object's frozen copies of them (check_r4a_copies.py keeps the live ones
 * identical: KEPT_CPP).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_task_oracle.h"
#include "llinventory_task_oracle_boundary.h"

#include "llfile.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "message_prehash.h"
#include "vkstorm_ffi.h"

#include <cstring>
#include <functional>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace vks_task_oracle
{
#include "../lltaskinventoryexecutor.inl"

    namespace
    {
        typedef LLTaskInventoryExecutor X;

        class RustBoundary : public TaskInventoryBoundary
        {
        public:
            const char* name() const override { return "Rust decisions and lltaskinventoryexecutor.inl"; }

            void registerInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener, void* user_data) override
            {
                obj->registerInventoryListener(listener, user_data);
            }
            void removeInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener) override
            {
                obj->removeInventoryListener(listener);
            }
            void requestInventory(LLViewerObject* obj) override { X::requestInventory(obj); }
            void dirtyInventory(LLViewerObject* obj) override { X::dirtyInventory(obj); }
            void processTaskInv(LLMessageSystem* msg) override { X::processTaskInv(msg); }
            bool loadTaskInvFile(LLViewerObject* obj, const std::string& filename) override
            {
                return X::loadTaskInvFile(obj, filename);
            }
            void updateInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) override
            {
                X::updateInventory(obj, item, key, is_new);
            }
            void updateInventoryLocal(LLViewerObject* obj, LLInventoryItem* item, U8 key) override
            {
                X::updateInventoryLocal(obj, item, key);
            }
            void updateViewerInventoryAsset(LLViewerObject* obj, const LLViewerInventoryItem* item, const LLUUID& new_asset) override
            {
                X::updateViewerInventoryAsset(obj, item, new_asset);
            }
            void updateMaterialInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) override
            {
                X::updateMaterialInventory(obj, item, key, is_new);
            }
            void removeInventory(LLViewerObject* obj, const LLUUID& item_id) override { X::removeInventory(obj, item_id); }
            void moveInventory(LLViewerObject* obj, const LLUUID& folder_id, const LLUUID& item_id) override
            {
                X::moveInventory(obj, folder_id, item_id);
            }
            void saveScript(LLViewerObject* obj, const LLViewerInventoryItem* item, bool active, bool is_new,
                            const LLUUID& template_id) override
            {
                X::saveScript(obj, item, active, is_new, template_id);
            }
            void createInventoryItem(LLViewerObject* obj, LLAssetType::EType asset_type, LLInventoryType::EType inventory_type,
                                     U8 sub_type, const std::string& name, const std::string& description,
                                     const LLPermissions& permissions, const LLSD& params,
                                     std::function<void(bool, const LLSD&)> callback) override
            {
                X::createInventoryItem(obj, asset_type, inventory_type, sub_type, name, description, permissions, params,
                                       callback);
            }
            void deleteInventoryItem(LLViewerObject* obj, const LLUUID& item_id) override
            {
                X::deleteInventoryItem(obj, item_id);
            }
            void clearInventoryListeners(LLViewerObject* obj) override { obj->clearInventoryListeners(); }
            bool hasInventoryListeners(LLViewerObject* obj) override { return obj->hasInventoryListeners(); }
            bool isInventoryDirty(LLViewerObject* obj) override { return obj->isInventoryDirty(); }
            S16 getInventorySerial(LLViewerObject* obj) override { return obj->getInventorySerial(); }
            bool isInventoryPending(LLViewerObject* obj) override { return obj->isInventoryPending(); }
            LLInventoryObject* getInventoryObject(LLViewerObject* obj, const LLUUID& item_id) override
            {
                return obj->getInventoryObject(item_id);
            }
            LLInventoryItem* getInventoryItem(LLViewerObject* obj, const LLUUID& item_id) override
            {
                return obj->getInventoryItem(item_id);
            }
            bool isAssetInInventory(LLViewerObject* obj, LLViewerInventoryItem* item, LLAssetType::EType type) override
            {
                return obj->isAssetInInventory(item, type);
            }
            LLInventoryObject* getInventoryRoot(LLViewerObject* obj) override { return obj->getInventoryRoot(); }
            void getInventoryContents(LLViewerObject* obj, LLInventoryObject::object_list_t& objects) override
            {
                obj->getInventoryContents(objects);
            }
            S32 countInventoryContents(LLViewerObject* obj, LLAssetType::EType type) override
            {
                return obj->countInventoryContents(type);
            }
            LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id) override
            {
                return obj->getInventoryItemByAsset(asset_id);
            }
            LLViewerInventoryItem* getInventoryItemByAsset(LLViewerObject* obj, const LLUUID& asset_id,
                                                           LLAssetType::EType type) override
            {
                return obj->getInventoryItemByAsset(asset_id, type);
            }
        };
    }

    TaskInventoryBoundary& rust_boundary()
    {
        static RustBoundary rust;
        return rust;
    }
}
