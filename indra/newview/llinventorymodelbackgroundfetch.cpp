/**
 * @file llinventorymodelbackgroundfetch.cpp
 * @brief Implementation of background fetching of inventory.
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2014, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"
#include "llinventorymodelbackgroundfetch.h"

#include "llaisapi.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llappearancemgr.h"
#include "llcallbacklist.h"
#include "llinventorymodel.h"
#include "llinventorypanel.h"
#include "llnotificationsutil.h"
#include "llstartup.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermessage.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llhttpconstants.h"
#include "bufferarray.h"
#include "bufferstream.h"
#include "llcorehttputil.h"
#include "llviewermenu.h"
#include "llviewernetwork.h"

// History (may be apocryphal)
//
// Around V2, an HTTP inventory download mechanism was added
// along with inventory LINK items referencing other inventory
// items.  As part of this, at login, the entire inventory
// structure is downloaded 'in the background' using the
// backgroundFetch()/bulkFetch() methods.  The UDP path can
// still be used and is found in the 'DEPRECATED OLD CODE'
// section.
//
// The old UDP path implemented a throttle that adapted
// itself during running.  The mechanism survived info HTTP
// somewhat but was pinned to poll the HTTP plumbing at
// 0.5S intervals.  The reasons for this particular value
// have been lost.  It's possible to switch between UDP
// and HTTP while this is happening but there may be
// surprises in what happens in that case.
//
// Conversion to llcorehttp reduced the number of connections
// used but batches more data and queues more requests (but
// doesn't due pipelining due to libcurl restrictions).  The
// poll interval above was re-examined and reduced to get
// inventory into the viewer more quickly.
//
// Possible future work:
//
// * Don't download the entire heirarchy in one go (which
//   might have been how V1 worked).  Implications for
//   links (which may not have a valid target) and search
//   which would then be missing data.
//
// * Review the download rate throttling.  Slow then fast?
//   Detect bandwidth usage and speed up when it drops?
//
// * An error on a fetch could be due to one item in the batch.
//   If the batch were broken up, perhaps more of the inventory
//   would download.  (Handwave here, not certain this is an
//   issue in practice.)
//
// * Conversion to AISv3.
//

// <Vulkanstorm>
// The scheduler is Rust since vkstorm-rust rung seven (sub-rung R3b):
// vkstorm-inventory's fetch module through vkstorm_ffi.h (vks_inv_fetch_*).
// Rust holds the folder and item queues, the counts and flags, the forced
// set, the expected folders, the open AIS commands and the legacy handlers'
// requests, and decides what to fetch, in what order and batches, within the
// concurrency limit and the frame budget (bulkFetchViaAis, bulkFetch, the AIS
// completions onAISFolderCalback and onAISContentCalback, the legacy 403
// split and re-queue). Each entry point below calls it and carries out the
// operations it returns, in order (runOp): the AISAPI fetch calls, the legacy
// POSTs, the callbacks, the completion signal, the timers. The model stays
// C++ and is read through the host table below; the legacy reply applier
// (BGFolderHttpHandler::processData's model writes), the HTTP transport and
// the OPENSIM UDP loop stay C++ as well.
#include "vkstorm_ffi.h"
// </Vulkanstorm>

namespace
{

///----------------------------------------------------------------------------
/// Class <anonymous>::BGItemHttpHandler
///----------------------------------------------------------------------------

//
// Http request handler class for single inventory item requests.
//
// We'll use a handler-per-request pattern here rather than
// a shared handler.  Mainly convenient as this was converted
// from a Responder class model.
//
// Derives from and is identical to the normal FetchItemHttpHandler
// except that:  1) it uses the background request object which is
// updated more slowly than the foreground and 2) keeps a count of
// active requests on the LLInventoryModelBackgroundFetch object
// to indicate outstanding operations are in-flight.
//
class BGItemHttpHandler : public LLInventoryModel::FetchItemHttpHandler
{
    LOG_CLASS(BGItemHttpHandler);

public:
    // <Vulkanstorm> The scheduler counted the request when it asked for it;
    // the handler releases it (FetchItemHttpHandler keeps no use of the
    // request body).
    BGItemHttpHandler(U64 handler)
        : LLInventoryModel::FetchItemHttpHandler(LLSD()),
          mHandler(handler)
        {
        }

    virtual ~BGItemHttpHandler()
        {
            LLInventoryModelBackgroundFetch::instance().legacyReleased(mHandler);
        }
    // </Vulkanstorm>

    BGItemHttpHandler(const BGItemHttpHandler&) = delete;
    BGItemHttpHandler& operator=(const BGItemHttpHandler&) = delete;

private:
    const U64 mHandler;
};


///----------------------------------------------------------------------------
/// Class <anonymous>::BGFolderHttpHandler
///----------------------------------------------------------------------------

// Http request handler class for folders.
//
// Handler for FetchInventoryDescendents2 and FetchLibDescendents2
// caps requests for folders.
//
class BGFolderHttpHandler : public LLCore::HttpHandler
{
    LOG_CLASS(BGFolderHttpHandler);

public:
    // <Vulkanstorm> The request (its folders, which of them are recursive)
    // is the scheduler's, under the handler's id.
    BGFolderHttpHandler(U64 handler)
        : LLCore::HttpHandler(),
          mHandler(handler)
        {
        }

    virtual ~BGFolderHttpHandler()
        {
            LLInventoryModelBackgroundFetch::instance().legacyReleased(mHandler);
        }
    // </Vulkanstorm>

protected:
    BGFolderHttpHandler(const BGFolderHttpHandler&) = delete;
    BGFolderHttpHandler& operator=(const BGFolderHttpHandler&) = delete;

public:
    virtual void onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse* response);

    bool getIsRecursive(const LLUUID& cat_id) const;

private:
    void processData(LLSD& body, LLCore::HttpResponse* response);
    void processFailure(LLCore::HttpStatus status, LLCore::HttpResponse* response);
    void processFailure(const char* const reason, LLCore::HttpResponse* response);

private:
    const U64 mHandler; // <Vulkanstorm/>
};


const S32 MAX_FETCH_RETRIES = 10; // <FS:ND/> For legacy inventory

const char* const LOG_INV("Inventory");

// <Vulkanstorm>
// ---- The scheduler's view of the model (VksInvFetchHost) --------------------

LLUUID vks_id(const uint8_t* p)
{
    LLUUID id;
    if (p)
    {
        memcpy(id.mData, p, UUID_BYTES);
    }
    return id;
}

void vks_host_model_id(void*, int32_t which, uint8_t* out)
{
    LLUUID id;
    switch (which)
    {
    case VKS_INV_FETCH_ROOT:           id = gInventory.getRootFolderID(); break;
    case VKS_INV_FETCH_LIBRARY_ROOT:   id = gInventory.getLibraryRootFolderID(); break;
    case VKS_INV_FETCH_LIBRARY_OWNER:  id = gInventory.getLibraryOwnerID(); break;
    case VKS_INV_FETCH_MARKETPLACE:    id = gInventory.getMarketplaceListingsUUID(); break;
    case VKS_INV_FETCH_CURRENT_OUTFIT: id = gInventory.findCategoryUUIDForType(LLFolderType::FT_CURRENT_OUTFIT); break;
    case VKS_INV_FETCH_BASE_OUTFIT:    id = LLAppearanceMgr::getInstance()->getBaseOutfitUUID(); break;
    default: break;
    }
    memcpy(out, id.mData, UUID_BYTES);
}

bool vks_host_category(void*, const uint8_t* id, VksInvFetchCategory* out)
{
    const LLViewerInventoryCategory* cat = gInventory.getCategory(vks_id(id));
    if (!cat)
    {
        return false;
    }
    out->version = cat->getVersion();
    out->preferred_type = (int32_t)cat->getPreferredType();
    memcpy(out->owner_id, cat->getOwnerID().mData, UUID_BYTES);
    return true;
}

// getDirectDescendentsOf's categories, in the model's order (valid until the
// next call; the scheduler copies them at once).
std::vector<LLUUID> vks_host_children;

ptrdiff_t vks_host_child_categories(void*, const uint8_t* id, const uint8_t** ids)
{
    LLInventoryModel::cat_array_t* categories(NULL);
    LLInventoryModel::item_array_t* items(NULL);
    gInventory.getDirectDescendentsOf(vks_id(id), categories, items);
    if (!categories)
    {
        *ids = nullptr;
        return -1;
    }
    vks_host_children.clear();
    vks_host_children.reserve(categories->size());
    for (const auto& cat : *categories)
    {
        vks_host_children.push_back(cat->getUUID());
    }
    static_assert(sizeof(LLUUID) == UUID_BYTES, "LLUUID is its 16 bytes");
    *ids = vks_host_children.empty() ? nullptr : vks_host_children.front().mData;
    return (ptrdiff_t)vks_host_children.size();
}

bool vks_host_item(void*, const uint8_t* id, VksInvFetchItem* out)
{
    const LLViewerInventoryItem* itemp = gInventory.getItem(vks_id(id));
    if (!itemp)
    {
        return false;
    }
    out->complete = itemp->isFinished();
    memcpy(out->owner_id, itemp->getPermissions().getOwner().mData, UUID_BYTES);
    return true;
}

bool vks_host_is_object_descendent_of(void*, const uint8_t* obj, const uint8_t* cat)
{
    return gInventory.isObjectDescendentOf(vks_id(obj), vks_id(cat));
}

int32_t vks_host_get_fetching(void*, const uint8_t* id)
{
    LLViewerInventoryCategory* cat = gInventory.getCategory(vks_id(id));
    return cat ? (int32_t)cat->getFetching() : (int32_t)LLViewerInventoryCategory::FETCH_NONE;
}

void vks_host_set_fetching(void*, const uint8_t* id, int32_t state)
{
    if (LLViewerInventoryCategory* cat = gInventory.getCategory(vks_id(id)))
    {
        cat->setFetching((LLViewerInventoryCategory::EFetchType)state);
    }
}

void vks_host_set_version(void*, const uint8_t* id, int32_t version)
{
    if (LLViewerInventoryCategory* cat = gInventory.getCategory(vks_id(id)))
    {
        cat->setVersion(version);
    }
}

// The frame budget's clock: read where bulkFetchViaAis read it, before the
// dispatch loops and after each entry.
double vks_host_total_seconds(void*)
{
    return LLTimer::getTotalSeconds();
}

const VksInvFetchHost VKS_HOST = {
    nullptr,
    &vks_host_model_id,
    &vks_host_category,
    &vks_host_child_categories,
    &vks_host_item,
    &vks_host_is_object_descendent_of,
    &vks_host_get_fetching,
    &vks_host_set_fetching,
    &vks_host_set_version,
    &vks_host_total_seconds,
};

// The C++ values the scheduler's numbers stand for.
static_assert(LLViewerInventoryCategory::FETCH_NONE == 0 && LLViewerInventoryCategory::FETCH_NORMAL == 1 &&
              LLViewerInventoryCategory::FETCH_RECURSIVE == 2 && LLViewerInventoryCategory::FETCH_FAILED == 3,
              "LLViewerInventoryCategory::EFetchType");
static_assert(AISAPI::FETCHITEM == 9 && AISAPI::FETCHCATEGORYCHILDREN == 10 && AISAPI::FETCHCATEGORYSUBSET == 12 &&
              AISAPI::FETCHCOF == 13 && AISAPI::FETCHORPHANS == 14 && AISAPI::FETCHCATEGORYLINKS == 15,
              "AISAPI::COMMAND_TYPE");
static_assert(sizeof(void*) != 8 || (sizeof(VksInvFetchOp) == 104 && sizeof(VksInvFetchEnv) == 104 &&
                                     sizeof(VksInvFetchCategory) == 24 && sizeof(VksInvFetchItem) == 17 &&
                                     sizeof(VksInvFetchEntry) == 24),
              "vks_inv_fetch layouts");

// ---- The environment of a call (VksInvFetchEnv) ------------------------------

// AISAPI's capability names (private there).
const std::string VKS_AIS_INVENTORY_CAP("InventoryAPIv3");
const std::string VKS_AIS_LIBRARY_CAP("LibraryAPIv3");

VksInvBytes vks_bytes(const std::string& s)
{
    VksInvBytes b;
    b.ptr = reinterpret_cast<const uint8_t*>(s.data());
    b.len = s.size();
    return b;
}

// What bulkFetchViaAis, bulkFetch and the legacy handlers read of the
// session, the settings and the region, read once per call. The legacy
// capability URLs and the sort order are read only on the legacy path, the
// AIS capabilities (whether a command would be refused at once) only when AIS
// is available and the call may issue a command (not start()).
struct FetchEnv
{
    enum Need { AIS_COMMANDS, NO_COMMANDS, LEGACY };
    VksInvFetchEnv env{};
    std::string fid2, fld2, fi2, fl2;

    explicit FetchEnv(Need need = AIS_COMMANDS)
    {
        const bool legacy = need == LEGACY;
        memcpy(env.agent_id, gAgent.getID().mData, UUID_BYTES);
        // <FS:Ansariel> Force HTTP features on SL
        env.use_http_inventory = use_http_inventory();
        env.ais_available = AISAPI::isAvailable();
        LLViewerRegion* region = gAgent.getRegion();
        env.region = region != nullptr;
        env.capabilities_received = region && region->capabilitiesReceived();
        env.disconnected = gDisconnected;
        env.exiting = LLApp::isExiting();
        env.in_second_life = LLGridManager::getInstance()->isInSecondLife();
        env.past_wearables_wait = LLStartUp::getStartupState() > STATE_WEARABLES_WAIT;
        if (need == AIS_COMMANDS && env.ais_available)
        {
            env.inventory_cap = !region->getCapability(VKS_AIS_INVENTORY_CAP).empty();
            env.library_cap = !region->getCapability(VKS_AIS_LIBRARY_CAP).empty();
        }
        static LLCachedControl<U32> ais_pool(gSavedSettings, "PoolSizeAIS", 20);
        env.pool_size_ais = ais_pool;
        static LLCachedControl<S32> ais_batch(gSavedSettings, "BatchSizeAIS3", 20);
        env.batch_size_ais3 = ais_batch;
        if (region && (legacy || (env.use_http_inventory && !env.ais_available && env.capabilities_received)))
        {
            env.sort_order = gSavedSettings.getU32(LLInventoryPanel::DEFAULT_SORT_ORDER);
            fid2 = region->getCapability("FetchInventoryDescendents2");
            fld2 = region->getCapability("FetchLibDescendents2");
            fi2 = region->getCapability("FetchInventory2");
            fl2 = region->getCapability("FetchLib2");
            env.cap_fid2 = vks_bytes(fid2);
            env.cap_fld2 = vks_bytes(fld2);
            env.cap_fi2 = vks_bytes(fi2);
            env.cap_fl2 = vks_bytes(fl2);
        }
    }
    const VksInvFetchEnv* get() const { return &env; }
};
// </Vulkanstorm>

} // end of namespace anonymous


///----------------------------------------------------------------------------
/// Class LLInventoryModelBackgroundFetch
///----------------------------------------------------------------------------

LLInventoryModelBackgroundFetch::LLInventoryModelBackgroundFetch():
    // <Vulkanstorm>
    mFetch(vks_inv_fetch_new()),
    mNextCallback(1),
    // </Vulkanstorm>
    mInitialFetchDuration(0.f),
    mMinTimeBetweenFetches(0.3f)
{}

LLInventoryModelBackgroundFetch::~LLInventoryModelBackgroundFetch()
{
    gIdleCallbacks.deleteFunction(&LLInventoryModelBackgroundFetch::backgroundFetchCB, nullptr);
    vks_inv_fetch_free(mFetch); // <Vulkanstorm/>
}

// <Vulkanstorm>
// ---- Carrying out the scheduler's operations --------------------------------

void LLInventoryModelBackgroundFetch::runOps(VksInvFetchOps* ops)
{
    if (!ops)
    {
        return;
    }
    size_t count = 0;
    const VksInvFetchOp* op = vks_inv_fetch_ops_view(ops, &count);
    for (size_t i = 0; i < count; ++i)
    {
        runOp(op[i]);
    }
    vks_inv_fetch_ops_free(ops);
}

void LLInventoryModelBackgroundFetch::runOp(const VksInvFetchOp& op)
{
    const LLUUID id(vks_id(op.id));
    switch (op.kind)
    {
    case VKS_INV_FETCH_OP_ISSUE_AIS:
    {
        const U64 ticket = op.ticket;
        AISAPI::completion_t cb = [ticket](const LLUUID& response_id)
        {
            LLInventoryModelBackgroundFetch::instance().onAISComplete(ticket, response_id);
        };
        const AISAPI::ITEM_TYPE item_type = op.library ? AISAPI::LIBRARY : AISAPI::INVENTORY;
        switch (op.code)
        {
        case AISAPI::FETCHITEM:
            AISAPI::FetchItem(id, item_type, cb);
            break;
        case AISAPI::FETCHCATEGORYCHILDREN:
            AISAPI::FetchCategoryChildren(id, item_type, op.recursive, cb, op.depth);
            break;
        case AISAPI::FETCHCATEGORYSUBSET:
        {
            uuid_vec_t children(op.children.len / UUID_BYTES);
            for (size_t i = 0; i < children.size(); ++i)
            {
                memcpy(children[i].mData, op.children.ptr + i * UUID_BYTES, UUID_BYTES);
            }
            AISAPI::FetchCategorySubset(id, children, item_type, op.recursive, cb, op.depth);
            break;
        }
        case AISAPI::FETCHCOF:
            AISAPI::FetchCOF(cb);
            break;
        case AISAPI::FETCHORPHANS:
            AISAPI::FetchOrphans(cb);
            break;
        case AISAPI::FETCHCATEGORYLINKS:
            AISAPI::FetchCategoryLinks(id, cb);
            break;
        default:
            LL_WARNS(LOG_INV) << "Unknown AIS fetch command " << op.code << LL_ENDL;
            break;
        }
        break;
    }
    case VKS_INV_FETCH_OP_CALLBACK:
    {
        // The completion lambda's callback(), then the rest of the completion.
        std::map<U64, nullary_func_t>::iterator it = mCallbacks.find(op.ticket);
        if (it != mCallbacks.end())
        {
            nullary_func_t callback = std::move(it->second);
            mCallbacks.erase(it);
            callback();
        }
        FetchEnv env;
        runOps(vks_inv_fetch_resume(mFetch, &VKS_HOST, env.get()));
        break;
    }
    case VKS_INV_FETCH_OP_NOTIFY:
        LLNotificationsUtil::add(std::string(reinterpret_cast<const char*>(op.text.ptr), op.text.len));
        break;
    case VKS_INV_FETCH_OP_FETCH_COMPLETED:
        // For now only informs about initial fetch being done
        mFoldersFetchedSignal();

        LL_INFOS(LOG_INV) << "Inventory background fetch completed after: " << mFetchStartTimer.getElapsedTimeF32() << LL_ENDL;
        break;
    case VKS_INV_FETCH_OP_ADD_IDLE:
        gIdleCallbacks.addFunction(&LLInventoryModelBackgroundFetch::backgroundFetchCB, nullptr);
        break;
    case VKS_INV_FETCH_OP_LEGACY_POST:
    {
        const std::string url(reinterpret_cast<const char*>(op.text.ptr), op.text.len);
        const std::string label(reinterpret_cast<const char*>(op.label.ptr), op.label.len);
        LLCore::HttpHandler::ptr_t handler;
        if (op.code)
        {
            handler = std::make_shared<BGFolderHttpHandler>(op.ticket);
        }
        else
        {
            handler = std::make_shared<BGItemHttpHandler>(op.ticket);
        }
        gInventory.requestPostRaw(false, url, reinterpret_cast<const char*>(op.body.ptr), op.body.len, handler, label.c_str());
        break;
    }
    case VKS_INV_FETCH_OP_LEGACY_PUMP:
    {
        // Process completed background HTTP requests
        gInventory.handleResponses(false);
        // Just processed a bunch of items.
        // Note: do we really need notifyObservers() here?
        // OnIdle it will be called anyway due to Add flag for processed item.
        // It seems like in some cases we are updating on fail (no flag),
        // but is there anything to update?
        // <FS:Ansariel> FIRE-21376: Inventory not loading properly on OpenSim
        if (op.code)
        {
            gInventory.notifyObservers();
        }
        // </FS:Ansariel>
        FetchEnv env;
        runOps(vks_inv_fetch_after_pump(mFetch, &VKS_HOST, env.get()));
        break;
    }
    case VKS_INV_FETCH_OP_NOTIFY_OBSERVERS:
        gInventory.notifyObservers();
        break;
    case VKS_INV_FETCH_OP_FETCH_STARTED:
        mFetchStartTimer.reset();
        break;
    case VKS_INV_FETCH_OP_INITIAL_FETCH_DURATION:
        mInitialFetchDuration = mFetchStartTimer.getElapsedTimeF32();
        break;
    case VKS_INV_FETCH_OP_FETCH_TIMER_RESET:
        mFetchTimer.reset();
        break;
    case VKS_INV_FETCH_OP_WARN:
        switch (op.code)
        {
        case VKS_INV_FETCH_WARN_FETCH_COUNT_BELOW_ZERO:
            LL_WARNS_ONCE(LOG_INV) << "Inventory fetch count fell below zero (0)." << LL_ENDL;
            break;
        case VKS_INV_FETCH_WARN_UNEXPECTED_FOLDER:
            // ais shouldn't respond twice
            LL_WARNS() << "Unexpected folder response for " << id << LL_ENDL;
            break;
        case VKS_INV_FETCH_WARN_FOLDER_FAILED:
            LL_WARNS() << "Failed to download folder: " << id << " Requesting known content separately" << LL_ENDL;
            break;
        case VKS_INV_FETCH_WARN_CATEGORY_LINKS_FAILED:
            LL_WARNS() << "Failed to fetch category links " << id << LL_ENDL;
            break;
        case VKS_INV_FETCH_WARN_NO_AIS2_CAP:
            LL_WARNS(LOG_INV) << "Failed to get AIS2 cap" << LL_ENDL;
            break;
        default:
            break;
        }
        break;
    default:
        LL_WARNS(LOG_INV) << "Unknown background fetch operation " << op.kind << LL_ENDL;
        break;
    }
}

U64 LLInventoryModelBackgroundFetch::addCallback(const nullary_func_t& callback)
{
    if (!callback)
    {
        return 0;
    }
    const U64 token = mNextCallback++;
    mCallbacks[token] = callback;
    return token;
}

void LLInventoryModelBackgroundFetch::onAISComplete(U64 ticket, const LLUUID& response_id)
{
    FetchEnv env;
    runOps(vks_inv_fetch_ais_complete(mFetch, &VKS_HOST, env.get(), ticket, response_id.mData));
}

bool LLInventoryModelBackgroundFetch::legacyIsRecursive(U64 handler, const LLUUID& cat_id) const
{
    return vks_inv_fetch_legacy_is_recursive(mFetch, handler, cat_id.mData);
}

void LLInventoryModelBackgroundFetch::legacyDataDone()
{
    FetchEnv env(FetchEnv::LEGACY);
    runOps(vks_inv_fetch_legacy_data_done(mFetch, &VKS_HOST, env.get()));
}

void LLInventoryModelBackgroundFetch::legacyHttpFailure(U64 handler, bool forbidden)
{
    FetchEnv env(FetchEnv::LEGACY);
    runOps(vks_inv_fetch_legacy_http_failure(mFetch, &VKS_HOST, env.get(), handler, forbidden ? HTTP_FORBIDDEN : 0));
}

void LLInventoryModelBackgroundFetch::legacyReplyFailure(U64 handler)
{
    FetchEnv env(FetchEnv::LEGACY);
    runOps(vks_inv_fetch_legacy_reply_failure(mFetch, &VKS_HOST, env.get(), handler));
}

void LLInventoryModelBackgroundFetch::legacyReleased(U64 handler)
{
    runOps(vks_inv_fetch_legacy_released(mFetch, handler));
}
// </Vulkanstorm>

bool LLInventoryModelBackgroundFetch::isBulkFetchProcessingComplete() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_BULK_COMPLETE) != 0; // <Vulkanstorm/>
}

bool LLInventoryModelBackgroundFetch::isFolderFetchProcessingComplete() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_FOLDER_COMPLETE) != 0; // <Vulkanstorm/>
}

bool LLInventoryModelBackgroundFetch::libraryFetchStarted() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_LIB_STARTED) != 0; // <Vulkanstorm/>
}

bool LLInventoryModelBackgroundFetch::libraryFetchCompleted() const
{
    return libraryFetchStarted() && fetchQueueContainsNoDescendentsOf(gInventory.getLibraryRootFolderID());
}

bool LLInventoryModelBackgroundFetch::libraryFetchInProgress() const
{
    return libraryFetchStarted() && !libraryFetchCompleted();
}

bool LLInventoryModelBackgroundFetch::inventoryFetchStarted() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_INV_STARTED) != 0; // <Vulkanstorm/>
}

bool LLInventoryModelBackgroundFetch::inventoryFetchCompleted() const
{
    return inventoryFetchStarted() && fetchQueueContainsNoDescendentsOf(gInventory.getRootFolderID());
}

bool LLInventoryModelBackgroundFetch::inventoryFetchInProgress() const
{
    return inventoryFetchStarted() && !inventoryFetchCompleted();
}

bool LLInventoryModelBackgroundFetch::isEverythingFetched() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_EVERYTHING) != 0; // <Vulkanstorm/>
}

bool LLInventoryModelBackgroundFetch::folderFetchActive() const
{
    return (vks_inv_fetch_flags(mFetch) & VKS_INV_FETCH_FOLDER_ACTIVE) != 0; // <Vulkanstorm/>
}

// <Vulkanstorm> The entry points: the scheduler decides, runOps carries it out.
void LLInventoryModelBackgroundFetch::addRequestAtFront(const LLUUID& id, bool recursive, bool is_category)
{
    vks_inv_fetch_add_request(mFetch, id.mData, recursive, is_category, true);
}

void LLInventoryModelBackgroundFetch::addRequestAtBack(const LLUUID& id, bool recursive, bool is_category)
{
    vks_inv_fetch_add_request(mFetch, id.mData, recursive, is_category, false);
}

void LLInventoryModelBackgroundFetch::start(const LLUUID& id, bool recursive)
{
    LL_DEBUGS(LOG_INV) << "Start fetching: " << id << ", recursive: " << recursive << LL_ENDL;
    FetchEnv env(FetchEnv::NO_COMMANDS);
    runOps(vks_inv_fetch_start(mFetch, &VKS_HOST, env.get(), id.mData, recursive));
}

void LLInventoryModelBackgroundFetch::scheduleFolderFetch(const LLUUID& cat_id, bool forced)
{
    runOps(vks_inv_fetch_schedule_folder(mFetch, cat_id.mData, forced));
}

void LLInventoryModelBackgroundFetch::scheduleItemFetch(const LLUUID& item_id, bool forced)
{
    runOps(vks_inv_fetch_schedule_item(mFetch, item_id.mData, forced));
}

void LLInventoryModelBackgroundFetch::fetchFolderAndLinks(const LLUUID& cat_id, nullary_func_t callback)
{
    // Assume that we have no relevant cache. Fetch folder, and items folder's links point to.
    const U64 token = addCallback(callback);
    FetchEnv env;
    runOps(vks_inv_fetch_folder_and_links(mFetch, &VKS_HOST, env.get(), cat_id.mData, token));
}

void LLInventoryModelBackgroundFetch::fetchCOF(nullary_func_t callback)
{
    // For reliability assume that we have no relevant cache, so
    // fetch cof along with items cof's links point to.
    const U64 token = addCallback(callback);
    FetchEnv env;
    runOps(vks_inv_fetch_cof(mFetch, &VKS_HOST, env.get(), token));
}

void LLInventoryModelBackgroundFetch::findLostItems()
{
    runOps(vks_inv_fetch_find_lost_items(mFetch));
}

void LLInventoryModelBackgroundFetch::setAllFoldersFetched()
{
    runOps(vks_inv_fetch_set_all_folders_fetched(mFetch));
}
// </Vulkanstorm>

boost::signals2::connection LLInventoryModelBackgroundFetch::setFetchCompletionCallback(folders_fetched_callback_t cb)
{
    return mFoldersFetchedSignal.connect(cb);
}

void LLInventoryModelBackgroundFetch::backgroundFetchCB(void*)
{
    LLInventoryModelBackgroundFetch::instance().backgroundFetch();
}

static LLTrace::BlockTimerStatHandle FTM_BULK_FETCH("Bulk Fetch");

void LLInventoryModelBackgroundFetch::backgroundFetch()
{
    // <Vulkanstorm> mBackgroundFetchActive, then bulkFetchViaAis / bulkFetch in Rust
    if (vks_inv_fetch_active(mFetch))
    // </Vulkanstorm>
    {
        // If we'll be using the capability, we'll be sending batches and the background thing isn't as important.
        // <FS:Ansariel> Force HTTP features on SL
        //if (gSavedSettings.getBOOL("UseHTTPInventory"))
        if (use_http_inventory())
        // </FS:Ansariel>
        {
            // <Vulkanstorm> AIS when available, else the legacy capabilities
            // once the region has them (the scheduler decides).
            LL_RECORD_BLOCK_TIME(FTM_BULK_FETCH);
            FetchEnv env;
            runOps(vks_inv_fetch_idle(mFetch, &VKS_HOST, env.get()));
            // </Vulkanstorm>
            return;
        }

#ifdef OPENSIM // Only OpenSim still might support old UDP fetch
        //--------------------------------------------------------------------------------
        // DEPRECATED OLD CODE
        //
        // <Vulkanstorm> The folder queue is the scheduler's.
        VksInvFetchEntry info;

        // No more categories to fetch, stop fetch process.
        if (!vks_inv_fetch_folder_front(mFetch, &info))
        {
            setAllFoldersFetched();
            return;
        }

        F32 fast_fetch_time = lerp(mMinTimeBetweenFetches, mMaxTimeBetweenFetches, 0.1f);
        F32 slow_fetch_time = lerp(mMinTimeBetweenFetches, mMaxTimeBetweenFetches, 0.5f);
        if (mTimelyFetchPending && mFetchTimer.getElapsedTimeF32() > slow_fetch_time)
        {
            // Double timeouts on failure.
            mMinTimeBetweenFetches = llmin(mMinTimeBetweenFetches * 2.f, 10.f);
            mMaxTimeBetweenFetches = llmin(mMaxTimeBetweenFetches * 2.f, 120.f);
            LL_DEBUGS(LOG_INV) << "Inventory fetch times grown to (" << mMinTimeBetweenFetches << ", " << mMaxTimeBetweenFetches << ")" << LL_ENDL;
            // fetch is no longer considered "timely" although we will wait for full time-out.
            mTimelyFetchPending = false;
        }

        while(1)
        {
            if (!vks_inv_fetch_folder_front(mFetch, &info))
            {
                break;
            }

            if (gDisconnected)
            {
                // Just bail if we are disconnected.
                break;
            }

            const LLUUID info_id(vks_id(info.id));

            if (info.is_category)
            {

                LLViewerInventoryCategory* cat = gInventory.getCategory(info_id);

                // Category has been deleted, remove from queue.
                if (!cat)
                {
                    vks_inv_fetch_folder_pop_front(mFetch);
                    continue;
                }

                if (mFetchTimer.getElapsedTimeF32() > mMinTimeBetweenFetches &&
                    LLViewerInventoryCategory::VERSION_UNKNOWN == cat->getVersion())
                {
                    // Category exists but has no children yet, fetch the descendants
                    // for now, just request every time and rely on retry timer to throttle.
                    if (cat->fetch())
                    {
                        mFetchTimer.reset();
                        mTimelyFetchPending = true;
                    }
                    else
                    {
                        //  The catagory also tracks if it has expired and here it says it hasn't
                        //  yet.  Get out of here because nothing is going to happen until we
                        //  update the timers.
                        break;
                    }
                }
                // Do I have all my children?
                else if (gInventory.isCategoryComplete(info_id))
                {
                    // Finished with this category, remove from queue.
                    vks_inv_fetch_folder_pop_front(mFetch);

                    // Add all children to queue.
                    LLInventoryModel::cat_array_t* categories;
                    LLInventoryModel::item_array_t* items;
                    gInventory.getDirectDescendentsOf(cat->getUUID(), categories, items);
                    for (LLInventoryModel::cat_array_t::const_iterator it = categories->begin();
                         it != categories->end();
                         ++it)
                    {
                        VksInvFetchEntry child = info;
                        memcpy(child.id, (*it)->getUUID().mData, UUID_BYTES);
                        child.is_category = true;
                        vks_inv_fetch_folder_push_back(mFetch, &child);
                    }

                    // We received a response in less than the fast time.
                    if (mTimelyFetchPending && mFetchTimer.getElapsedTimeF32() < fast_fetch_time)
                    {
                        // Shrink timeouts based on success.
                        mMinTimeBetweenFetches = llmax(mMinTimeBetweenFetches * 0.8f, 0.3f);
                        mMaxTimeBetweenFetches = llmax(mMaxTimeBetweenFetches * 0.8f, 10.f);
                        LL_DEBUGS(LOG_INV) << "Inventory fetch times shrunk to (" << mMinTimeBetweenFetches << ", " << mMaxTimeBetweenFetches << ")" << LL_ENDL;
                    }

                    mTimelyFetchPending = false;
                    continue;
                }
                else if (mFetchTimer.getElapsedTimeF32() > mMaxTimeBetweenFetches)
                {
                    // Received first packet, but our num descendants does not match db's num descendants
                    // so try again later.
                    vks_inv_fetch_folder_pop_front(mFetch);

                    if (mNumFetchRetries++ < MAX_FETCH_RETRIES)
                    {
                        // push on back of queue
                        vks_inv_fetch_folder_push_back(mFetch, &info);
                    }
                    mTimelyFetchPending = false;
                    mFetchTimer.reset();
                    break;
                }

                // Not enough time has elapsed to do a new fetch
                break;
            }
            else
            {
                LLViewerInventoryItem* itemp = gInventory.getItem(info_id);

                vks_inv_fetch_folder_pop_front(mFetch);
                if (!itemp)
                {
                    continue;
                }

                if (mFetchTimer.getElapsedTimeF32() > mMinTimeBetweenFetches)
                {
                    itemp->fetchFromServer();
                    mFetchTimer.reset();
                    mTimelyFetchPending = true;
                }
                else if (itemp->mIsComplete)
                {
                    mTimelyFetchPending = false;
                }
                else if (mFetchTimer.getElapsedTimeF32() > mMaxTimeBetweenFetches)
                {
                    vks_inv_fetch_folder_push_back(mFetch, &info);
                    mFetchTimer.reset();
                    mTimelyFetchPending = false;
                }
                // Not enough time has elapsed to do a new fetch
                break;
            }
        }
        // </Vulkanstorm>

        //
        // DEPRECATED OLD CODE
        //--------------------------------------------------------------------------------
#endif
    }
}

// <Vulkanstorm>
void LLInventoryModelBackgroundFetch::incrFetchCount(S32 fetching)
{
    runOps(vks_inv_fetch_incr_count(mFetch, fetching, false));
}
void LLInventoryModelBackgroundFetch::incrFetchFolderCount(S32 fetching)
{
    runOps(vks_inv_fetch_incr_count(mFetch, fetching, true));
}

bool LLInventoryModelBackgroundFetch::fetchQueueContainsNoDescendentsOf(const LLUUID& cat_id) const
{
    return vks_inv_fetch_queue_has_no_descendents_of(mFetch, &VKS_HOST, cat_id.mData);
}
// </Vulkanstorm>


namespace
{

///----------------------------------------------------------------------------
/// Class <anonymous>::BGFolderHttpHandler
///----------------------------------------------------------------------------

void BGFolderHttpHandler::onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse* response)
{
    do      // Single-pass do-while used for common exit handling
    {
        LLCore::HttpStatus status(response->getStatus());
        // status = LLCore::HttpStatus(404);                // Dev tool to force error handling
        if (! status)
        {
            processFailure(status, response);
            break;          // Goto common exit
        }

        // Response body should be present.
        LLCore::BufferArray* body(response->getBody());
        // body = NULL;                                 // Dev tool to force error handling
        if (! body || ! body->size())
        {
            LL_WARNS(LOG_INV) << "Missing data in inventory folder query." << LL_ENDL;
            processFailure("HTTP response missing expected body", response);
            break;          // Goto common exit
        }

        // Could test 'Content-Type' header but probably unreliable.

        // Convert response to LLSD
        // body->write(0, "Garbage Response", 16);      // Dev tool to force error handling
        LLSD body_llsd;
        if (! LLCoreHttpUtil::responseToLLSD(response, true, body_llsd))
        {
            // INFOS-level logging will occur on the parsed failure
            processFailure("HTTP response contained malformed LLSD", response);
            break;          // goto common exit
        }

        // Expect top-level structure to be a map
        // body_llsd = LLSD::emptyArray();              // Dev tool to force error handling
        if (! body_llsd.isMap())
        {
            processFailure("LLSD response not a map", response);
            break;          // goto common exit
        }

        // Check for 200-with-error failures
        //
        // See comments in llinventorymodel.cpp about this mode of error.
        //
        // body_llsd["error"] = LLSD::emptyMap();       // Dev tool to force error handling
        // body_llsd["error"]["identifier"] = "Development";
        // body_llsd["error"]["message"] = "You left development code in the viewer";
        if (body_llsd.has("error"))
        {
            processFailure("Inventory application error (200-with-error)", response);
            break;          // goto common exit
        }

        // Okay, process data if possible
        processData(body_llsd, response);
    }
    while (false);
}


void BGFolderHttpHandler::processData(LLSD& content, LLCore::HttpResponse* response)
{
    LLInventoryModelBackgroundFetch* fetcher(LLInventoryModelBackgroundFetch::getInstance());

    // API V2 and earlier should probably be testing for "error" map
    // in response as an application-level error.

    // Instead, we assume success and attempt to extract information.
    if (content.has("folders"))
    {
        LLSD folders(content["folders"]);

        for (LLSD::array_const_iterator folder_it = folders.beginArray();
            folder_it != folders.endArray();
            ++folder_it)
        {
            LLSD folder_sd(*folder_it);

            //LLUUID agent_id = folder_sd["agent_id"];

            //if (agent_id != gAgent.getID())    //This should never happen.
            //{
            //  LL_WARNS(LOG_INV) << "Got a UpdateInventoryItem for the wrong agent."
            //          << LL_ENDL;
            //  break;
            //}

            LLUUID parent_id(folder_sd["folder_id"].asUUID());
            LLUUID owner_id(folder_sd["owner_id"].asUUID());
            S32    version(folder_sd["version"].asInteger());
            S32    descendents(folder_sd["descendents"].asInteger());
            LLPointer<LLViewerInventoryCategory> tcategory = new LLViewerInventoryCategory(owner_id);

            if (parent_id.isNull())
            {
                LLSD items(folder_sd["items"]);
                LLPointer<LLViewerInventoryItem> titem = new LLViewerInventoryItem;

                for (LLSD::array_const_iterator item_it = items.beginArray();
                    item_it != items.endArray();
                    ++item_it)
                {
                    const LLUUID lost_uuid(gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND));

                    if (lost_uuid.notNull())
                    {
                        LLSD item(*item_it);

                        titem->unpackMessage(item);

                        LLInventoryModel::update_list_t update;
                        LLInventoryModel::LLCategoryUpdate new_folder(lost_uuid, 1);
                        update.emplace_back(new_folder);
                        gInventory.accountForUpdate(update);

                        titem->setParent(lost_uuid);
                        titem->updateParentOnServer(false);
                        gInventory.updateItem(titem);
                        // <FS:Ansariel> FIRE-21376: Inventory not loading properly on OpenSim
                        if (!LLGridManager::getInstance()->isInSecondLife())
                        {
                            gInventory.notifyObservers();
                        }
                        // </FS:Ansariel>
                    }
                }
            }

            LLViewerInventoryCategory* pcat(gInventory.getCategory(parent_id));
            if (! pcat)
            {
                continue;
            }

            LLSD categories(folder_sd["categories"]);
            for (LLSD::array_const_iterator category_it = categories.beginArray();
                category_it != categories.endArray();
                ++category_it)
            {
                LLSD category(*category_it);
                tcategory->fromLLSD(category);

                const bool recursive(getIsRecursive(tcategory->getUUID()));
                if (recursive)
                {
                    fetcher->addRequestAtBack(tcategory->getUUID(), recursive, true);
                }
                else if (! gInventory.isCategoryComplete(tcategory->getUUID()))
                {
                    gInventory.updateCategory(tcategory);
                }
            }

            LLSD items(folder_sd["items"]);
            LLPointer<LLViewerInventoryItem> titem = new LLViewerInventoryItem;
            for (LLSD::array_const_iterator item_it = items.beginArray();
                 item_it != items.endArray();
                 ++item_it)
            {
                LLSD item(*item_it);
                titem->unpackMessage(item);

                gInventory.updateItem(titem);
            }

            // Set version and descendentcount according to message.
            LLViewerInventoryCategory* cat(gInventory.getCategory(parent_id));
            if (cat)
            {
                cat->setVersion(version);
                cat->setDescendentCount(descendents);
                cat->determineFolderType();
            }
        }
    }

    if (content.has("bad_folders"))
    {
        LLSD bad_folders(content["bad_folders"]);
        for (LLSD::array_const_iterator folder_it = bad_folders.beginArray();
             folder_it != bad_folders.endArray();
             ++folder_it)
        {
            // *TODO: Stop copying data [ed:  this isn't copying data]
            LLSD folder_sd(*folder_it);

            // These folders failed on the dataserver.  We probably don't want to retry them.
            LL_WARNS(LOG_INV) << "Folder " << folder_sd["folder_id"].asString()
                              << "Error: " << folder_sd["error"].asString() << LL_ENDL;
        }
    }

    // <Vulkanstorm> setAllFoldersFetched() when the bulk fetch is complete,
    // notifyObservers() off Second Life (<FS:Ansariel> FIRE-21376)
    fetcher->legacyDataDone();
    // </Vulkanstorm>
}


void BGFolderHttpHandler::processFailure(LLCore::HttpStatus status, LLCore::HttpResponse* response)
{
    const std::string& ct(response->getContentType());
    LL_WARNS(LOG_INV) << "Inventory folder fetch failure\n"
                      << "[Status: " << status.toTerseString() << "]\n"
                      << "[Reason: " << status.toString() << "]\n"
                      << "[Content-type: " << ct << "]\n"
                      << "[Content (abridged): "
                      << LLCoreHttpUtil::responseToString(response) << "]" << LL_ENDL;

    // Could use a 404 test here to try to detect revoked caps...

    // <Vulkanstorm> The scheduler splits a 403'd request in two when it can
    // (FetchInventoryDescendents2, "assume that this isn't the library"),
    // raises InventoryLimitReachedAIS when it cannot, and otherwise checks
    // for completion; off Second Life, notifyObservers() (<FS:Ansariel>
    // FIRE-21376). The original retry logic for HTTP_INTERNAL_ERROR (an
    // unbounded re-queue) stays disabled, as it was ("if (false)").
    LLInventoryModelBackgroundFetch::getInstance()->legacyHttpFailure(mHandler, status == LLCore::HttpStatus(HTTP_FORBIDDEN));
    // </Vulkanstorm>
}


void BGFolderHttpHandler::processFailure(const char* const reason, LLCore::HttpResponse* response)
{
    LL_WARNS(LOG_INV) << "Inventory folder fetch failure\n"
                      << "[Status: internal error]\n"
                      << "[Reason: " << reason << "]\n"
                      << "[Content (abridged): "
                      << LLCoreHttpUtil::responseToString(response) << "]" << LL_ENDL;

    // Reverse of previous processFailure() method, this is invoked
    // when response structure is found to be invalid.  Original
    // always re-issued the request (without limit).  This does
    // the same but be aware that this may be a source of problems.
    // Philosophy is that inventory folders are so essential to
    // operation that this is a reasonable action.
    // <Vulkanstorm> The scheduler queues every folder of the request again at
    // the front (recursive ones as recursive); off Second Life,
    // notifyObservers() (<FS:Ansariel> FIRE-21376).
    LLInventoryModelBackgroundFetch::getInstance()->legacyReplyFailure(mHandler);
    // </Vulkanstorm>
}


bool BGFolderHttpHandler::getIsRecursive(const LLUUID& cat_id) const
{
    return LLInventoryModelBackgroundFetch::getInstance()->legacyIsRecursive(mHandler, cat_id); // <Vulkanstorm/>
}

///----------------------------------------------------------------------------
/// Class <anonymous>::BGItemHttpHandler
///----------------------------------------------------------------------------

// Nothing to implement here.  All ctor/dtor changes.

} // end namespace anonymous
