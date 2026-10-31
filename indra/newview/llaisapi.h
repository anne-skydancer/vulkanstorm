/**
 * @file llaisapi.h
 * @brief classes and functions for interfacing with the v3+ ais inventory service.
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLAISAPI_H
#define LL_LLAISAPI_H

#include "lluuid.h"
#include <map>
#include <set>
#include <string>
#include "llviewerinventory.h"
#include "llcorehttputil.h"
#include "llcoproceduremanager.h"

class AISReply; // <Vulkanstorm/> llaisreply.h

class AISAPI
{
public:
    static const S32 HTTP_TIMEOUT;
    typedef enum {
        INVENTORY,
        LIBRARY
    } ITEM_TYPE;

    typedef std::function<void(const LLUUID& invItem)> completion_t;

    static bool isAvailable();
    static void getCapNames(LLSD& capNames);

    static void CreateInventory(const LLUUID& parentId, const LLSD& newInventory, completion_t callback = completion_t());
    static void SlamFolder(const LLUUID& folderId, const LLSD& newInventory, completion_t callback = completion_t());
    static void RemoveCategory(const LLUUID &categoryId, completion_t callback = completion_t());
    static void RemoveItem(const LLUUID &itemId, completion_t callback = completion_t());
    static void PurgeDescendents(const LLUUID &categoryId, completion_t callback = completion_t());
    static void UpdateCategory(const LLUUID &categoryId, const LLSD &updates, completion_t callback = completion_t());
    static void UpdateItem(const LLUUID &itemId, const LLSD &updates, completion_t callback = completion_t());
    static void FetchItem(const LLUUID &itemId, ITEM_TYPE type, completion_t callback = completion_t());
    static void FetchCategoryChildren(const LLUUID &catId, ITEM_TYPE type = AISAPI::ITEM_TYPE::INVENTORY, bool recursive = false, completion_t callback = completion_t(), S32 depth = 0);
    static void FetchCategoryChildren(const std::string &identifier, bool recursive = false, completion_t callback = completion_t(), S32 depth = 0);
    static void FetchCategoryCategories(const LLUUID &catId, ITEM_TYPE type = AISAPI::ITEM_TYPE::INVENTORY, bool recursive = false, completion_t callback = completion_t(), S32 depth = 0);
    static void FetchCategorySubset(const LLUUID& catId, const uuid_vec_t specificChildren, ITEM_TYPE type = AISAPI::ITEM_TYPE::INVENTORY, bool recursive = false, completion_t callback = completion_t(), S32 depth = 0);
    static void FetchCOF(completion_t callback = completion_t());
    static void FetchCategoryLinks(const LLUUID &catId, completion_t callback = completion_t());
    static void FetchOrphans(completion_t callback = completion_t() );
    static void CopyLibraryCategory(const LLUUID& sourceId, const LLUUID& destId, bool copySubfolders, completion_t callback = completion_t());

    typedef enum {
        COPYINVENTORY,
        SLAMFOLDER,
        REMOVECATEGORY,
        REMOVEITEM,
        PURGEDESCENDENTS,
        UPDATECATEGORY,
        UPDATEITEM,
        COPYLIBRARYCATEGORY,
        CREATEINVENTORY,
        FETCHITEM,
        FETCHCATEGORYCHILDREN,
        FETCHCATEGORYCATEGORIES,
        FETCHCATEGORYSUBSET,
        FETCHCOF,
        FETCHORPHANS,
        FETCHCATEGORYLINKS
    } COMMAND_TYPE;

private:
    static const std::string INVENTORY_CAP_NAME;
    static const std::string LIBRARY_CAP_NAME;

    typedef std::function<LLSD(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, LLCore::HttpRequest::ptr_t,
        const std::string, LLSD, LLCore::HttpOptions::ptr_t, LLCore::HttpHeaders::ptr_t) > invokationFn_t;

    static void EnqueueAISCommand(const std::string &procName, LLCoprocedureManager::CoProcedure_t proc);
    static void onIdle(void *userdata); // launches postponed AIS commands
    // <Vulkanstorm> R3a: the reply is a Rust value; the callback ids come out.
    static void onUpdateReceived(const AISReply& update, COMMAND_TYPE type, const LLSD& request_body,
                                 uuid_vec_t& callback_ids);
    // </Vulkanstorm>

    static std::string getInvCap();
    static std::string getLibCap();

    static void InvokeAISCommandCoro(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t httpAdapter,
        invokationFn_t invoke, std::string url, LLUUID targetId, LLSD body,
        completion_t callback, COMMAND_TYPE type);

    typedef std::pair<std::string, LLCoprocedureManager::CoProcedure_t> ais_query_item_t;
    static std::list<ais_query_item_t> sPostponedQuery;
};

class AISUpdate
{
public:
    // <Vulkanstorm> Inventory rung R3a: the reply is a Rust value and the
    // parse is vkstorm-inventory's (vks_inv_ais_parse); the parse* members
    // are gone. doUpdate() applies its results as before.
    AISUpdate(const AISReply& update, AISAPI::COMMAND_TYPE type, const LLSD& request_body);
    void doUpdate();
    // The completion callback ids of the reply, in order.
    const uuid_vec_t& getCallbackIds() const { return mCallbackIds; }
private:
    friend struct AISUpdateHost;
    void parseUpdate(const AISReply& update, const LLSD& request_body);
    // </Vulkanstorm>
    void clearParseResults();
    void checkTimeout();

    // Fetches can return large packets of data,
    // throttle them individually to not get stuck
    // on a single large task. And throttle sum total
    // to not cause lags when multiple large fetches
    // returned results.
    const F32 AIS_TASK_EXPIRY_SECONDS = 0.008f;
    const F32 AIS_BATCH_EXPIRY_SECONDS = 0.010f;

    typedef std::map<LLUUID,size_t> uuid_int_map_t;
    uuid_int_map_t mCatDescendentDeltas;
    uuid_int_map_t mCatVersionsUpdated;

    typedef std::map<LLUUID,LLPointer<LLViewerInventoryItem> > deferred_item_map_t;
    deferred_item_map_t mItemsCreated;
    deferred_item_map_t mItemsLost;
    deferred_item_map_t mItemsUpdated;
    typedef std::map<LLUUID,LLPointer<LLViewerInventoryCategory> > deferred_category_map_t;
    deferred_category_map_t mCategoriesCreated;
    deferred_category_map_t mCategoriesUpdated;

    uuid_list_t mObjectsDeletedIds;
    uuid_vec_t mCallbackIds; // <Vulkanstorm/> was mItemIds, mCategoryIds (the parse's own)
    bool mFetch;
    S32 mFetchDepth;
    LLTimer mTaskTimer;
    static LLTimer sBatchTimer;
    static U32 sBatchFrameCount;
    AISAPI::COMMAND_TYPE mType;
};

#endif
