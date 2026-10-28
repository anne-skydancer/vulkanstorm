/**
 * @file llinventorymodel_parity_stubs.h
 * @brief The viewer environment of the inventory model, stubbed for tests.
 *
 * llinventorymodel.cpp, llviewerinventory.cpp and llinventoryobserver.cpp are
 * compiled unchanged into the test executable. Everything they call outside
 * llcommon, llinventory, llmessage, llfilesystem, llxml, llmath, llcorehttp
 * and llwindow is defined in llinventorymodel_parity_stubs.cpp.
 *
 * Every outward call that has an effect (a message sent, an AIS request, a
 * background-fetch request, a marketplace or appearance call, a notification,
 * a preview closed, the new-inventory observer installed, ...) is appended,
 * in call order, to ONE journal through vks_inv::note(). Tests read it with
 * vks_inv::take_journal(). Journal lines start with a fixed tag:
 *
 *   msg <name> <hex bytes>         gAgent.sendReliableMessage(): the pending
 *                                  gMessageSystem message, built by the real
 *                                  template builder (header bytes zero, not
 *                                  zero-coded: the builder's own output)
 *   ais <call> <id> [<notation body>]   AISAPI::CreateInventory/SlamFolder/
 *                                  RemoveCategory/RemoveItem/PurgeDescendents/
 *                                  UpdateCategory/UpdateItem (completion kept,
 *                                  see ais_completions())
 *   fetch.start <id> recursive=<0|1>
 *   fetch.item <id> force=<0|1>    scheduleItemFetch
 *   fetch.folder <id> force=<0|1>  scheduleFolderFetch
 *   fetch.completion_callback      setFetchCompletionCallback (connected to
 *                                  the stub's signal, see fire_fetch_completion())
 *   market.update <id> <consistency> <skip_clear>   update_marketplace_category
 *   market.listing_id <id>  /  market.get_listing <listing id>
 *   appearance.wear <id> <do_update> <replace>  /  appearance.remove <id> ...
 *   gesture.activate|deactivate <id>
 *   preview.hide <id> <no_saving>
 *   notify <name> [<notation args>]   LLNotificationsUtil::add
 *   trash_preview.show
 *   observer.start_new_inventory
 *   rlv.saved_asset <id>
 *   avatar_name.lookup <id>        (only when the name is not cached; the
 *                                  real LLAvatarNameCache is linked, the stub
 *                                  records nothing for it otherwise)
 *
 * The test adds its own lines (notification batches, callbacks) through the
 * same vks_inv::note(), so the journal holds the complete ordered side-effect
 * stream of a step.
 *
 * Configuration: vks_inv::config() holds every answer the stubbed
 * environment gives (AIS availability, fetch activity, grid, second instance,
 * first login, marketplace depths and listing ids, worn items, active
 * gestures, protected FS folders, ...). vks_inv::reset_config() restores the
 * defaults (Second Life main grid, AIS available, nothing active).
 *
 * How a test uses it:
 *   1. vks_inv::init_environment(template_file, work_dir) once: starts the
 *      real message system on the real template, creates the settings the
 *      sources read, the headless window, the application, agent and viewer
 *      window objects, and points LLDir's cache directory at work_dir.
 *   2. Per case: vks_inv::reset_model() (a freshly constructed gInventory,
 *      as at process start), vks_inv::reset_config(), set config fields,
 *      vks_inv::set_cache_dir(dir), then drive gInventory.
 *   3. vks_inv::take_journal() after each step.
 *
 * Real fetch code (VKS_INVMODEL_STUB_FETCH=0). By default (1) this file
 * stubs AISAPI (isAvailable() from the configuration; the calls journaled as
 * "ais ...", their completions kept in ais_completions()) and
 * LLInventoryModelBackgroundFetch ("fetch.*" journal lines, folder fetch
 * activity from the configuration). A test that drives the real
 * llaisapi.cpp and llinventorymodelbackgroundfetch.cpp adds both sources to
 * its target and sets the compile definition VKS_INVMODEL_STUB_FETCH=0 for
 * the whole target; the stubs, harness and generic files then compile
 * unchanged next to them. What the real fetch code finds here:
 *   - the agent's region: config().region_present, config().capabilities
 *     (name -> URL; "InventoryAPIv3", "LibraryAPIv3", "FetchInventory2",
 *     "FetchLib2", "FetchInventoryDescendents2", "FetchLibDescendents2", ...),
 *     config().capabilities_received. The region is a shim: only
 *     getCapability(), isCapabilityAvailable(), capabilitiesReceived() and
 *     gAgent.getRegionCapability() may be used on it.
 *   - set_startup_state(): LLStartUp::getStartupState() (STATE_STARTED at
 *     process start).
 *   - use_http_inventory(): config().http_inventory (true, as without OPENSIM).
 *   - LLAppearanceMgr::getBaseOutfitUUID(): config().base_outfit.
 *   - settings FSUseAis3Api (true), BatchSizeAIS3 (20), PoolSizeAIS (20),
 *     DebugAvatarAppearanceMessage (false), UseHTTPInventory (true), at the
 *     app_settings defaults; tests may change them with gSavedSettings.
 *   - gAgentAvatarp is null; dump_sequential_xml() does nothing.
 *   - LLAppViewer::getAppCoreHttp() returns a zero-filled, never-constructed
 *     LLAppCoreHttp: every policy class reads as DEFAULT_POLICY_ID (0).
 *   - LLCore's request queue exists (init_environment() calls
 *     HttpRequest::createService()); its worker thread is not started:
 *     a test that issues HTTP requests starts it and pumps the responses.
 *   Everything else the real fetch code reaches (coroutines, the
 *   coprocedure pool, HTTP) is the real library code.
 *
 * vks_inv::model:: gives read and write access to LLInventoryModel's private
 * state (maps, child trees, backlinks, change batch, broken-link lists, locks,
 * usability flag) so tests can snapshot it; nothing in the sources is changed.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLINVENTORYMODEL_PARITY_STUBS_H
#define LL_LLINVENTORYMODEL_PARITY_STUBS_H

// 1 (default): AISAPI and LLInventoryModelBackgroundFetch are stubbed here.
// 0: the test target links the real llaisapi.cpp and
// llinventorymodelbackgroundfetch.cpp (see "Real fetch code" below).
#ifndef VKS_INVMODEL_STUB_FETCH
#define VKS_INVMODEL_STUB_FETCH 1
#endif

#include "lluuid.h"
#include "llsd.h"
#include "llpointer.h"

#include <functional>
#include <map>
#include <unordered_map>
#include <set>
#include <string>
#include <utility>
#include <vector>

class LLInventoryModel;
class LLViewerInventoryCategory;
class LLViewerInventoryItem;
class LLMessageSystem;
class LLWindow;

// The agent's ids (llagent.cpp); set by the tests.
extern LLUUID gAgentID;
extern LLUUID gAgentSessionID;

namespace vks_inv
{
    // ---- the journal ------------------------------------------------------
    void note(const std::string& line);
    std::vector<std::string> take_journal();
    void clear_journal();

    // ---- configuration ----------------------------------------------------
    struct Config
    {
        bool ais_available = true;          // AISAPI::isAvailable()
        bool folder_fetch_active = false;   // LLInventoryModelBackgroundFetch::folderFetchActive()
        bool everything_fetched = false;    // ...::isEverythingFetched()
        bool second_instance = false;       // LLAppViewer::isSecondInstance()
        bool production_grid = true;        // LLGridManager::isInProductionGrid() (isInSLMain)
        bool system_grid = true;            // LLGridManager::isSystemGrid()
        std::string grid_id = "util.agni.lindenlab.com"; // LLGridManager::getGridId(grid)
        bool first_login = false;           // gAgent.isFirstLogin()
        bool godlike = false;               // gAgent.isGodlikeWithoutAdminMenuFakery()
        std::set<LLUUID> agent_groups;      // gAgent.isInGroup()
        LLUUID ao_folder;                   // AOEngine::getAOFolder()
        LLUUID favorites_folder;            // FSFloaterWearableFavorites::getFavoritesFolder()
        std::map<LLUUID, S32> marketplace_depth; // depth_nesting_in_marketplace(), default -1
        std::map<LLUUID, S32> listing_ids;  // LLMarketplaceData::getListingID(), default 0
        std::set<LLUUID> worn_items;        // get_is_item_worn()
        std::set<LLUUID> active_gestures;   // LLGestureMgr::isGestureActive()
        bool category_removable = true;     // get_is_category_removable()
        bool trash_preview_visible = false; // LLFloaterPreviewTrash::isVisible()
        std::map<std::string, std::string> translations; // LLTrans::getString(), default: the key
        // The agent's region (gAgent.getRegion(), getRegionCapability()):
        // absent by default. When present, a capability is available when it
        // has a non-empty URL here.
        bool region_present = false;
        std::map<std::string, std::string> capabilities;
        bool capabilities_received = true;  // LLViewerRegion::capabilitiesReceived()
        bool http_inventory = true;         // use_http_inventory() (non-OPENSIM: true)
        LLUUID base_outfit;                 // LLAppearanceMgr::getBaseOutfitUUID()
    };
    Config& config();
    void reset_config();

    // ---- environment ------------------------------------------------------
    void init_environment(const std::string& template_file, const std::string& work_dir);
    void set_cache_dir(const std::string& dir);
    // LLDir's temp directory (gDirUtilp->getTempFilename() names
    // "<dir>/<random uuid>.tmp" there); init_environment() sets <work>/temp.
    void set_temp_dir(const std::string& dir);
    LLWindow* window();     // the headless window behind gViewerWindow
    void set_busy_count(S32 n);
    // LLAvatarNameCache answers immediate lookups only once running (its
    // first idle() call, never made here).
    void set_avatar_name_cache_running(bool running);
    // gInventoryCallbacks: clears its map and restarts its ids at 1; lists
    // the ids still registered (never fired).
    void callbacks_reset();
    std::vector<U32> callbacks_pending();
    // RlvHandler::isEnabled().
    void set_rlv_enabled(bool enabled);

    // LLStartUp::getStartupState() (an EStartupState value; STATE_STARTED
    // at process start). Not reset by reset_config().
    void set_startup_state(S32 state);

    // ---- AIS completions kept by the stubs, in call order -----------------
    // (VKS_INVMODEL_STUB_FETCH=1 only; with 0 it stays empty and
    // fire_fetch_completion() does nothing.)
    typedef std::function<void(const LLUUID&)> completion_t;
    std::vector<std::pair<std::string, completion_t> >& ais_completions();

    // Fires the signal handed out by setFetchCompletionCallback().
    void fire_fetch_completion();

    // ---- messages -----------------------------------------------------------
    // Builds the message pending in gMessageSystem (newMessage...add...) and
    // clears it; returns its name and the builder's bytes.
    std::vector<U8> build_pending_message(std::string& name);
    // Reads a built packet through gMessageSystem's template reader, which
    // calls the registered handler (as LLMessageSystem::checkMessages does).
    // Returns false when the reader rejected the packet.
    bool dispatch_packet(const std::vector<U8>& packet, bool trusted = true);
    std::string hex(const std::vector<U8>& bytes);

    // ---- the model's private state --------------------------------------------
    namespace model
    {
        void reset();   // destroys and reconstructs gInventory in place
        std::unordered_map<LLUUID, LLPointer<LLViewerInventoryCategory> >& categories();
        std::unordered_map<LLUUID, LLPointer<LLViewerInventoryItem> >& items();
        std::unordered_map<LLUUID, std::vector<LLPointer<LLViewerInventoryCategory> >*>& cat_tree();
        std::unordered_map<LLUUID, std::vector<LLPointer<LLViewerInventoryItem> >*>& item_tree();
        std::multimap<LLUUID, LLUUID>& backlinks();
        std::map<LLUUID, std::set<LLUUID> >& possibly_broken_links();
        std::set<LLUUID>& links_rebuild_list();
        std::map<LLUUID, bool>& category_locks();
        std::map<LLUUID, bool>& item_locks();
        bool& agent_inv_usable();
        U32& modify_mask();
        U32& modify_mask_backlog();
        std::set<LLUUID>& changed_backlog();
        std::set<LLUUID>& added_backlog();
        bool& first_time_in_viewer2();
        S32& pending_system_folders();
        bool load_from_file(const std::string& filename,
                            std::vector<LLPointer<LLViewerInventoryCategory> >& categories,
                            std::vector<LLPointer<LLViewerInventoryItem> >& items,
                            std::set<LLUUID>& cats_to_update,
                            bool& is_cache_obsolete);
        bool save_to_file(const std::string& filename,
                          const std::vector<LLPointer<LLViewerInventoryCategory> >& categories,
                          const std::vector<LLPointer<LLViewerInventoryItem> >& items);
        // Registers the GenericMessage BulkUpdateInventory dispatcher, as the
        // first LLInventoryModel::requestPost() does.
        void register_generic_dispatch();
        // LLViewerInventoryCategory state the sources keep protected.
        S32 fetching_state(const LLViewerInventoryCategory* cat); // mFetching, without getFetching()'s side effect
        std::string fetch_timer_state(const LLViewerInventoryCategory* cat);
    }
}

#endif
