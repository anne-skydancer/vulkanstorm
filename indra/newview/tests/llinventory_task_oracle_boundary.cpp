/**
 * @file llinventory_task_oracle_boundary.cpp
 * @brief The frozen implementation of the task-inventory boundary: every
 * call goes to the frozen copy of the LLViewerObject member of the same name
 * (llinventory_task_oracle.cpp), unchanged. See llinventory_task_oracle_boundary.h.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_task_oracle_boundary.h"

namespace vks_task_oracle
{
    namespace
    {
        class FrozenBoundary : public TaskInventoryBoundary
        {
        public:
            const char* name() const override { return "frozen copies of llviewerobject.cpp"; }

            void registerInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener, void* user_data) override
            {
                obj->registerInventoryListener(listener, user_data);
            }
            void removeInventoryListener(LLViewerObject* obj, LLVOInventoryListener* listener) override
            {
                obj->removeInventoryListener(listener);
            }
            void requestInventory(LLViewerObject* obj) override { obj->requestInventory(); }
            void dirtyInventory(LLViewerObject* obj) override { obj->dirtyInventory(); }
            void processTaskInv(LLMessageSystem* msg) override { LLViewerObject::processTaskInv(msg, NULL); }
            bool loadTaskInvFile(LLViewerObject* obj, const std::string& filename) override { return obj->loadTaskInvFile(filename); }
            void updateInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) override
            {
                obj->updateInventory(item, key, is_new);
            }
            void updateInventoryLocal(LLViewerObject* obj, LLInventoryItem* item, U8 key) override
            {
                obj->updateInventoryLocal(item, key);
            }
            void updateViewerInventoryAsset(LLViewerObject* obj, const LLViewerInventoryItem* item, const LLUUID& new_asset) override
            {
                obj->updateViewerInventoryAsset(item, new_asset);
            }
            void updateMaterialInventory(LLViewerObject* obj, LLViewerInventoryItem* item, U8 key, bool is_new) override
            {
                obj->updateMaterialInventory(item, key, is_new);
            }
            void removeInventory(LLViewerObject* obj, const LLUUID& item_id) override { obj->removeInventory(item_id); }
            void moveInventory(LLViewerObject* obj, const LLUUID& folder_id, const LLUUID& item_id) override
            {
                obj->moveInventory(folder_id, item_id);
            }
            void saveScript(LLViewerObject* obj, const LLViewerInventoryItem* item, bool active, bool is_new,
                            const LLUUID& template_id) override
            {
                obj->saveScript(item, active, is_new, template_id);
            }
            void createInventoryItem(LLViewerObject* obj, LLAssetType::EType asset_type, LLInventoryType::EType inventory_type,
                                     U8 sub_type, const std::string& name, const std::string& description,
                                     const LLPermissions& permissions, const LLSD& params,
                                     std::function<void(bool, const LLSD&)> callback) override
            {
                obj->createInventoryItem(asset_type, inventory_type, sub_type, name, description, permissions, params, callback);
            }
            void deleteInventoryItem(LLViewerObject* obj, const LLUUID& item_id) override { obj->deleteInventoryItem(item_id); }
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

        TaskInventoryBoundary* g_boundary = NULL;
    }

    TaskInventoryBoundary& frozen_boundary()
    {
        static FrozenBoundary frozen;
        return frozen;
    }

    TaskInventoryBoundary& boundary()
    {
        return g_boundary ? *g_boundary : frozen_boundary();
    }

    void set_boundary(TaskInventoryBoundary* implementation)
    {
        g_boundary = implementation;
    }
}
