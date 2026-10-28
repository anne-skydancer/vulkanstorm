/**
 * @file llinventorymodel_parity_stubs.cpp
 * @brief The viewer environment of the inventory model, stubbed for tests.
 *
 * See llinventorymodel_parity_stubs.h for what each stub records and how a
 * test configures it. The definitions below are those the three inventory
 * sources (llinventorymodel.cpp, llviewerinventory.cpp,
 * llinventoryobserver.cpp) reference outside the libraries the test links.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "../llinventorymodel.h"
#include "../llviewerinventory.h"
#include "../llinventoryobserver.h"
#include "../llinventorymodelbackgroundfetch.h"
#include "../llinventoryfunctions.h"
#include "../llinventorybridge.h"
#include "../llinventorypanel.h"
#include "../llaisapi.h"
#include "../llagent.h"
#include "../llagentwearables.h"
#include "../llappearancemgr.h"
#include "../llappviewer.h"
#include "../llviewerwindow.h"
#include "../llviewerregion.h"
#include "../llcapabilityprovider.h"
#include "../llvoavatarself.h"
#include "../llviewernetwork.h"
#include "../llviewercontrol.h"
#include "../llviewerfoldertype.h"
#include "../llviewerassettype.h"
#include "../llviewerobject.h"
#include "../llviewerobjectlist.h"
#include "../llviewermessage.h"
#include "../llgesturemgr.h"
#include "../llmarketplacefunctions.h"
#include "../llfloaterpreviewtrash.h"
#include "../llfloatersidepanelcontainer.h"
#include "../llfloaterperms.h"
#include "../llpreview.h"
#include "../llpreviewgesture.h"
#include "../llpanelmaininventory.h"
#include "../llfavoritesbar.h"
#include "llfolderview.h"
#include "../llsettingsvo.h"
#include "../llinventorylistener.h"
#include "../llcommandhandler.h"
#include "../llstartup.h"
#include "../aoengine.h"
#include "../fslslbridge.h"
#include "../fsfloaterwearablefavorites.h"
#include "../rlvhandler.h"
#include "../rlvlocks.h"
#include "../rlvcommon.h"
#include "../rlvhelper.h"
#include "llclipboard.h"
#include "llnotificationsutil.h"
#include "lltrans.h"
#include "llfloater.h"
#include "llfloaterreg.h"
#include "lldispatcher.h"
#include "llwindowheadless.h"
#include "llavatarnamecache.h"
#include "httprequest.h"
#include "llwearabledata.h"
#include "llwearabletype.h"
#include "llsdserialize.h"
#include "lltemplatemessagebuilder.h"
#include "lltemplatemessagereader.h"
#include "message.h"

#include "llinventorymodel_parity_stubs.h"

#include <cstdio>
#include <cstring>
#include <new>
#include <sstream>

// ---- access to private state ---------------------------------------------
// An explicit template instantiation may name a private member; the friend
// it defines hands the pointer out (as in lltexturefetch_parity_stubs.cpp).
namespace
{
    template<typename Tag, typename Tag::type M>
    struct Steal
    {
        friend typename Tag::type steal(Tag) { return M; }
    };

    typedef std::unordered_map<LLUUID, LLPointer<LLViewerInventoryCategory> > cat_map_t;
    typedef std::unordered_map<LLUUID, LLPointer<LLViewerInventoryItem> > item_map_t;
    typedef std::unordered_map<LLUUID, LLInventoryModel::cat_array_t*> parent_cat_map_t;
    typedef std::unordered_map<LLUUID, LLInventoryModel::item_array_t*> parent_item_map_t;
    typedef std::map<LLUUID, std::set<LLUUID> > broken_links_t;
    typedef std::multimap<LLUUID, LLUUID> backlink_mmap_t;
    typedef std::map<LLUUID, bool> lock_map_t;
    typedef void (LLInventoryModel::*init_http_fn_t)();

#define VKS_STEAL(TagName, Type) \
    struct TagName { typedef Type type; friend type steal(TagName); }

    VKS_STEAL(ModelCatMap, cat_map_t LLInventoryModel::*);
    VKS_STEAL(ModelItemMap, item_map_t LLInventoryModel::*);
    VKS_STEAL(ModelCatTree, parent_cat_map_t LLInventoryModel::*);
    VKS_STEAL(ModelItemTree, parent_item_map_t LLInventoryModel::*);
    VKS_STEAL(ModelBacklinks, backlink_mmap_t LLInventoryModel::*);
    VKS_STEAL(ModelBroken, broken_links_t LLInventoryModel::*);
    VKS_STEAL(ModelRebuild, std::set<LLUUID> LLInventoryModel::*);
    VKS_STEAL(ModelCatLock, lock_map_t LLInventoryModel::*);
    VKS_STEAL(ModelItemLock, lock_map_t LLInventoryModel::*);
    VKS_STEAL(ModelUsable, bool LLInventoryModel::*);
    VKS_STEAL(ModelMask, U32 LLInventoryModel::*);
    VKS_STEAL(ModelMaskBacklog, U32 LLInventoryModel::*);
    VKS_STEAL(ModelChangedBacklog, std::set<LLUUID> LLInventoryModel::*);
    VKS_STEAL(ModelAddedBacklog, std::set<LLUUID> LLInventoryModel::*);
    VKS_STEAL(ModelHttpFG, LLCore::HttpRequest* LLInventoryModel::*);
    VKS_STEAL(ModelInitHttp, init_http_fn_t);
    VKS_STEAL(ModelFirstTime, bool*);
    VKS_STEAL(ModelPendingSys, S32*);
    VKS_STEAL(MsgTemplateBuilder, LLTemplateMessageBuilder* LLMessageSystem::*);
    VKS_STEAL(MsgTemplateReader, LLTemplateMessageReader* LLMessageSystem::*);
    VKS_STEAL(MsgReader, LLMessageReaderPointer LLMessageSystem::*);
    VKS_STEAL(MsgBuilder, LLMessageBuilder* LLMessageSystem::*);
    VKS_STEAL(AgentFirstLogin, bool LLAgent::*);
    VKS_STEAL(AppSecondInstance, bool LLAppViewer::*);
    VKS_STEAL(AppInstance, LLAppViewer**);
    VKS_STEAL(ViewerWindowWindow, LLWindow* LLViewerWindow::*);
    VKS_STEAL(DirTemp, std::string LLDir::*);
    VKS_STEAL(NameCacheRunning, bool LLAvatarNameCache::*);
    typedef std::map<U32, LLPointer<LLInventoryCallback> > callback_map_t;
    VKS_STEAL(CallbackMap, callback_map_t LLInventoryCallbackManager::*);
    VKS_STEAL(CallbackLast, U32 LLInventoryCallbackManager::*);
    VKS_STEAL(RlvEnabled, bool*);
    VKS_STEAL(StartupState, EStartupState*);
#undef VKS_STEAL
}
template struct Steal<ModelCatMap, &LLInventoryModel::mCategoryMap>;
template struct Steal<ModelItemMap, &LLInventoryModel::mItemMap>;
template struct Steal<ModelCatTree, &LLInventoryModel::mParentChildCategoryTree>;
template struct Steal<ModelItemTree, &LLInventoryModel::mParentChildItemTree>;
template struct Steal<ModelBacklinks, &LLInventoryModel::mBacklinkMMap>;
template struct Steal<ModelBroken, &LLInventoryModel::mPossiblyBrockenLinks>;
template struct Steal<ModelRebuild, &LLInventoryModel::mLinksRebuildList>;
template struct Steal<ModelCatLock, &LLInventoryModel::mCategoryLock>;
template struct Steal<ModelItemLock, &LLInventoryModel::mItemLock>;
template struct Steal<ModelUsable, &LLInventoryModel::mIsAgentInvUsable>;
template struct Steal<ModelMask, &LLInventoryModel::mModifyMask>;
template struct Steal<ModelMaskBacklog, &LLInventoryModel::mModifyMaskBacklog>;
template struct Steal<ModelChangedBacklog, &LLInventoryModel::mChangedItemIDsBacklog>;
template struct Steal<ModelAddedBacklog, &LLInventoryModel::mAddedItemIDsBacklog>;
template struct Steal<ModelHttpFG, &LLInventoryModel::mHttpRequestFG>;
template struct Steal<ModelInitHttp, &LLInventoryModel::initHttpRequest>;
template struct Steal<ModelFirstTime, &LLInventoryModel::sFirstTimeInViewer2>;
template struct Steal<ModelPendingSys, &LLInventoryModel::sPendingSystemFolders>;
template struct Steal<MsgTemplateBuilder, &LLMessageSystem::mTemplateMessageBuilder>;
template struct Steal<MsgTemplateReader, &LLMessageSystem::mTemplateMessageReader>;
template struct Steal<MsgReader, &LLMessageSystem::mMessageReader>;
template struct Steal<MsgBuilder, &LLMessageSystem::mMessageBuilder>;
template struct Steal<AgentFirstLogin, &LLAgent::mFirstLogin>;
template struct Steal<AppSecondInstance, &LLAppViewer::mSecondInstance>;
template struct Steal<AppInstance, &LLAppViewer::sInstance>;
template struct Steal<ViewerWindowWindow, &LLViewerWindow::mWindow>;
template struct Steal<DirTemp, &LLDir::mTempDir>;
template struct Steal<NameCacheRunning, &LLAvatarNameCache::mRunning>;
template struct Steal<CallbackMap, &LLInventoryCallbackManager::mMap>;
template struct Steal<CallbackLast, &LLInventoryCallbackManager::mLastCallback>;
template struct Steal<RlvEnabled, &RlvHandler::m_fEnabled>;
template struct Steal<StartupState, &LLStartUp::gStartupState>;

namespace
{
    // Protected members reached through a derived class.
    struct CatAccess : public LLViewerInventoryCategory
    {
        static EFetchType LLViewerInventoryCategory::* fetching() { return &CatAccess::mFetching; }
        static LLFrameTimer LLViewerInventoryCategory::* timer() { return &CatAccess::mDescendentsRequested; }
    };
    struct ModelAccess : public LLInventoryModel
    {
        using LLInventoryModel::loadFromFile;
        using LLInventoryModel::saveToFile;
    };
}

// ---- the journal and the configuration --------------------------------------

namespace
{
    std::vector<std::string> g_journal;
    vks_inv::Config g_config;
    std::vector<std::pair<std::string, vks_inv::completion_t> > g_ais;
    boost::signals2::signal<void()> g_fetch_done;

    std::string id_str(const LLUUID& id) { return id.asString(); }

    std::string notation(const LLSD& sd)
    {
        std::ostringstream s;
        // One line; binary as b16"..." (no raw bytes in text fixtures).
        LLPointer<LLSDNotationFormatter> f = new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_PRETTY_BINARY);
        f->format(sd, s);
        return s.str();
    }
}

namespace vks_inv
{
    void note(const std::string& line) { g_journal.push_back(line); }
    std::vector<std::string> take_journal() { std::vector<std::string> j; j.swap(g_journal); return j; }
    void clear_journal() { g_journal.clear(); }
    Config& config() { return g_config; }
    void reset_config() { g_config = Config(); }
    std::vector<std::pair<std::string, completion_t> >& ais_completions() { return g_ais; }
    void fire_fetch_completion() { g_fetch_done(); }

    std::string hex(const std::vector<U8>& bytes)
    {
        static const char* digits = "0123456789abcdef";
        std::string out;
        out.reserve(bytes.size() * 2);
        for (U8 b : bytes)
        {
            out.push_back(digits[b >> 4]);
            out.push_back(digits[b & 15]);
        }
        return out;
    }

    std::vector<U8> build_pending_message(std::string& name)
    {
        LLMessageSystem* msg = gMessageSystem;
        LLTemplateMessageBuilder* builder = msg->*steal(MsgTemplateBuilder());
        name = builder->getMessageName() ? builder->getMessageName() : "";
        std::vector<U8> buffer(MAX_BUFFER_SIZE, 0);
        U32 size = builder->buildMessage(buffer.data(), MAX_BUFFER_SIZE, 0);
        buffer.resize(size);
        msg->clearMessage();
        return buffer;
    }

    bool dispatch_packet(const std::vector<U8>& packet, bool trusted)
    {
        LLMessageSystem* msg = gMessageSystem;
        LLTemplateMessageReader* reader = msg->*steal(MsgTemplateReader());
        std::vector<U8> buffer(packet);
        buffer.resize(MAX_BUFFER_SIZE, 0);
        LockMessageReader lock(msg->*steal(MsgReader()), reader);
        if (!reader->validateMessage(buffer.data(), (S32)packet.size(), LLHost(), trusted))
        {
            return false;
        }
        return reader->readMessage(buffer.data(), LLHost());
    }
}

// ---- settings ---------------------------------------------------------------

LLControlGroup gSavedSettings("Global");
LLControlGroup gSavedPerAccountSettings("PerAccount");
// gDisconnected is defined in llcommon (llapp.cpp) since upstream #5241 (147e864246).
LLUUID gAgentID;
LLUUID gAgentSessionID;
LLDispatcher gGenericDispatcher;
LLFloaterView* gFloaterView = NULL;

// ---- application, agent, window -----------------------------------------
// LLAgent, LLAgentWearables, LLViewerObjectList, LLAppViewer and
// LLViewerWindow are never constructed: the sources only call their inline
// accessors (which read the members set below) and the functions stubbed
// here. On Windows the linker aliases each object's symbol to raw storage;
// on the Itanium ABI a global object's symbol is its plain name.
#if LL_WINDOWS
extern "C" unsigned char gAgentStorage[1 << 16];
extern "C" unsigned char gAgentStorage[1 << 16] = { 0 };
#pragma comment(linker, "/alternatename:?gAgent@@3VLLAgent@@A=gAgentStorage")
extern "C" unsigned char gAgentWearablesStorage[1 << 14];
extern "C" unsigned char gAgentWearablesStorage[1 << 14] = { 0 };
#pragma comment(linker, "/alternatename:?gAgentWearables@@3VLLAgentWearables@@A=gAgentWearablesStorage")
extern "C" unsigned char gObjectListStorage[1 << 16];
extern "C" unsigned char gObjectListStorage[1 << 16] = { 0 };
#pragma comment(linker, "/alternatename:?gObjectList@@3VLLViewerObjectList@@A=gObjectListStorage")
#else
alignas(64) unsigned char gAgentStorage[1 << 16] __asm__("gAgent") = { 0 };
alignas(64) unsigned char gAgentWearablesStorage[1 << 14] __asm__("gAgentWearables") = { 0 };
alignas(64) unsigned char gObjectListStorage[1 << 16] __asm__("gObjectList") = { 0 };
#endif
static_assert(sizeof(LLAgent) <= (1 << 16), "agent storage");
static_assert(sizeof(LLAgentWearables) <= (1 << 14), "wearables storage");
static_assert(sizeof(LLViewerObjectList) <= (1 << 16), "object list storage");

LLAppViewer* LLAppViewer::sInstance = NULL;
LLViewerWindow* gViewerWindow = NULL;

namespace
{
    alignas(64) unsigned char g_app_storage[sizeof(LLAppViewer)];
    alignas(64) unsigned char g_viewer_window_storage[sizeof(LLViewerWindow)];
    LLWindow* g_window = NULL;

    LLAgent& agent() { return *reinterpret_cast<LLAgent*>(gAgentStorage); }
    LLAppViewer* app() { return reinterpret_cast<LLAppViewer*>(g_app_storage); }

    // Pushes the configuration into the members the sources read inline.
    void apply_config()
    {
        agent().*steal(AgentFirstLogin()) = g_config.first_login;
        app()->*steal(AppSecondInstance()) = g_config.second_instance;
    }
}

namespace vks_inv
{
    void init_environment(const std::string& template_file, const std::string& work_dir)
    {
        ll_init_apr();
        // LLCore's request queue (LLAvatarNameCache constructs an HttpRequest);
        // the worker thread is never started.
        LLCore::HttpRequest::createService();
        // The real message system on the real template; no circuits are used.
        start_messaging_system(template_file, 0, 1, 0, 0, false, "notasharedsecret", NULL, false, 5.f, 100.f);

        // Settings the sources read, at their app_settings defaults.
        gSavedSettings.declareU32("InventoryTrashMaxCapacity", 5000, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("QAModeFakeSystemFolderIssues", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("FSDontNagWhenPurging", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareU32("InventorySortOrder", 7, "", LLControlVariable::PERSIST_NO);
        // Read only by the real llaisapi.cpp and llinventorymodelbackgroundfetch.cpp
        // (VKS_INVMODEL_STUB_FETCH=0).
        gSavedSettings.declareBOOL("FSUseAis3Api", true, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareS32("BatchSizeAIS3", 20, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareU32("PoolSizeAIS", 20, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("DebugAvatarAppearanceMessage", false, "", LLControlVariable::PERSIST_NO);
        gSavedSettings.declareBOOL("UseHTTPInventory", true, "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareBOOL("LockAOFolders", true, "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareBOOL("LockBridgeFolder", true, "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareBOOL("LockWearableFavoritesFolders", true, "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareLLSD("FSProtectedFolders", LLSD::emptyArray(), "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareString("ModelUploadFolder", "", "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareString("TextureUploadFolder", "", "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareString("SoundUploadFolder", "", "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareString("AnimationUploadFolder", "", "", LLControlVariable::PERSIST_NO);
        gSavedPerAccountSettings.declareString("PBRUploadFolder", "", "", LLControlVariable::PERSIST_NO);

        memset(g_app_storage, 0, sizeof(g_app_storage));
        *steal(AppInstance()) = app();
        memset(g_viewer_window_storage, 0, sizeof(g_viewer_window_storage));
        gViewerWindow = reinterpret_cast<LLViewerWindow*>(g_viewer_window_storage);
        g_window = new LLWindowHeadless(NULL, "inventory parity", "inventory parity", 0, 0, 64, 64, 0, false, false, false, false, false);
        gViewerWindow->*steal(ViewerWindowWindow()) = g_window;

        LLFile::mkdir(work_dir);
        std::string temp_dir = gDirUtilp->add(work_dir, "temp");
        LLFile::mkdir(temp_dir);
        (*gDirUtilp).*steal(DirTemp()) = temp_dir;
        set_cache_dir(work_dir);
        apply_config();
    }

    void set_temp_dir(const std::string& dir)
    {
        (*gDirUtilp).*steal(DirTemp()) = dir;
    }

    void set_cache_dir(const std::string& dir)
    {
        LLFile::mkdir(dir);
        const_cast<LLDir*>(gDirUtilp)->setCacheDir(dir);
        apply_config();
    }

    LLWindow* window() { return g_window; }

    void callbacks_reset()
    {
        (gInventoryCallbacks.*steal(CallbackMap())).clear();
        gInventoryCallbacks.*steal(CallbackLast()) = 0;
    }

    std::vector<U32> callbacks_pending()
    {
        std::vector<U32> ids;
        for (const auto& kv : gInventoryCallbacks.*steal(CallbackMap())) ids.push_back(kv.first);
        return ids;
    }

    void set_rlv_enabled(bool enabled) { *steal(RlvEnabled()) = enabled; }

    void set_avatar_name_cache_running(bool running)
    {
        LLAvatarNameCache::getInstance()->*steal(NameCacheRunning()) = running;
    }

    void set_busy_count(S32 n)
    {
        g_window->resetBusyCount();
        for (S32 i = 0; i < n; ++i) g_window->incBusyCount();
    }

    namespace model
    {
        void reset()
        {
            gInventory.~LLInventoryModel();
            new (&gInventory) LLInventoryModel();
            *steal(ModelFirstTime()) = true;
            *steal(ModelPendingSys()) = 0;
            LLInventoryState::sWearNewClothing = false;
            LLInventoryState::sWearNewClothingTransactionID.setNull();
            g_ais.clear();
            g_fetch_done.disconnect_all_slots();
            apply_config();
        }
        std::unordered_map<LLUUID, LLPointer<LLViewerInventoryCategory> >& categories() { return gInventory.*steal(ModelCatMap()); }
        std::unordered_map<LLUUID, LLPointer<LLViewerInventoryItem> >& items() { return gInventory.*steal(ModelItemMap()); }
        std::unordered_map<LLUUID, std::vector<LLPointer<LLViewerInventoryCategory> >*>& cat_tree() { return gInventory.*steal(ModelCatTree()); }
        std::unordered_map<LLUUID, std::vector<LLPointer<LLViewerInventoryItem> >*>& item_tree() { return gInventory.*steal(ModelItemTree()); }
        std::multimap<LLUUID, LLUUID>& backlinks() { return gInventory.*steal(ModelBacklinks()); }
        std::map<LLUUID, std::set<LLUUID> >& possibly_broken_links() { return gInventory.*steal(ModelBroken()); }
        std::set<LLUUID>& links_rebuild_list() { return gInventory.*steal(ModelRebuild()); }
        std::map<LLUUID, bool>& category_locks() { return gInventory.*steal(ModelCatLock()); }
        std::map<LLUUID, bool>& item_locks() { return gInventory.*steal(ModelItemLock()); }
        bool& agent_inv_usable() { return gInventory.*steal(ModelUsable()); }
        U32& modify_mask() { return gInventory.*steal(ModelMask()); }
        U32& modify_mask_backlog() { return gInventory.*steal(ModelMaskBacklog()); }
        std::set<LLUUID>& changed_backlog() { return gInventory.*steal(ModelChangedBacklog()); }
        std::set<LLUUID>& added_backlog() { return gInventory.*steal(ModelAddedBacklog()); }
        bool& first_time_in_viewer2() { return *steal(ModelFirstTime()); }
        S32& pending_system_folders() { return *steal(ModelPendingSys()); }

        bool load_from_file(const std::string& filename,
                            std::vector<LLPointer<LLViewerInventoryCategory> >& categories,
                            std::vector<LLPointer<LLViewerInventoryItem> >& items,
                            std::set<LLUUID>& cats_to_update,
                            bool& is_cache_obsolete)
        {
            return ModelAccess::loadFromFile(filename, categories, items, cats_to_update, is_cache_obsolete);
        }

        bool save_to_file(const std::string& filename,
                          const std::vector<LLPointer<LLViewerInventoryCategory> >& categories,
                          const std::vector<LLPointer<LLViewerInventoryItem> >& items)
        {
            return ModelAccess::saveToFile(filename, categories, items);
        }

        void register_generic_dispatch()
        {
            // initHttpRequest() allocates the HTTP requests only when none
            // exist, then registers the dispatcher; a placeholder request
            // pointer skips the allocation and is cleared again at once.
            static unsigned char placeholder[64];
            LLCore::HttpRequest*& fg = gInventory.*steal(ModelHttpFG());
            fg = reinterpret_cast<LLCore::HttpRequest*>(placeholder);
            (gInventory.*steal(ModelInitHttp()))();
            fg = NULL;
        }

        S32 fetching_state(const LLViewerInventoryCategory* cat)
        {
            return (S32)(cat->*CatAccess::fetching());
        }

        std::string fetch_timer_state(const LLViewerInventoryCategory* cat)
        {
            const LLFrameTimer& t = cat->*CatAccess::timer();
            char buf[96];
            snprintf(buf, sizeof(buf), "started=%d expired=%d to_expire=%.3f",
                     t.getStarted() ? 1 : 0, t.hasExpired() ? 1 : 0, (double)t.getTimeToExpireF32());
            return buf;
        }
    }
}

// ---- the agent's region ------------------------------------------------------
// LLViewerRegion cannot be constructed without llviewerregion.cpp. The
// region is raw storage whose first word is the vtable pointer of
// RegionShim, an LLCapabilityProvider (LLViewerRegion's primary base, at
// offset 0): the virtual getCapability()/getHost()/getDescription() calls
// made on the region land in RegionShim and answer from the configuration;
// the non-virtual capability queries are defined below. Nothing else of the
// region may be used.
namespace
{
    class RegionShim : public LLCapabilityProvider
    {
    public:
        std::string getCapability(std::string_view name) const override
        {
            std::map<std::string, std::string>::const_iterator it = g_config.capabilities.find(std::string(name));
            return it == g_config.capabilities.end() ? std::string() : it->second;
        }
        const LLHost& getHost() const override { static LLHost host; return host; }
        std::string getDescription() const override { return "inventory parity region"; }
    };

    alignas(64) unsigned char g_region_storage[sizeof(LLViewerRegion)];

    LLViewerRegion* region()
    {
        static bool made = false;
        if (!made)
        {
            memset(g_region_storage, 0, sizeof(g_region_storage));
            RegionShim shim;
            memcpy(g_region_storage, (const void*)&shim, sizeof(void*));
            made = true;
        }
        return reinterpret_cast<LLViewerRegion*>(g_region_storage);
    }
}

bool LLViewerRegion::isCapabilityAvailable(std::string_view name) const
{
    std::map<std::string, std::string>::const_iterator it = g_config.capabilities.find(std::string(name));
    return it != g_config.capabilities.end() && !it->second.empty();
}
bool LLViewerRegion::capabilitiesReceived() const { return g_config.capabilities_received; }

// ---- start-up state, HTTP inventory, outfit (used by the real fetch code) -------

EStartupState LLStartUp::gStartupState = STATE_STARTED;
bool use_http_inventory() { return g_config.http_inventory; }
LLPointer<LLVOAvatarSelf> gAgentAvatarp;
void dump_sequential_xml(const std::string, const LLSD&) {}
const LLUUID LLAppearanceMgr::getBaseOutfitUUID() { return g_config.base_outfit; }

namespace vks_inv
{
    void set_startup_state(S32 state) { *steal(StartupState()) = (EStartupState)state; }
}

// ---- LLAgent ------------------------------------------------------------------

LLViewerRegion* LLAgent::getRegion() const { return g_config.region_present ? region() : NULL; }
std::string LLAgent::getRegionCapability(const std::string& name)
{
    return g_config.region_present ? region()->getCapability(name) : std::string();
}
bool LLAgent::isGodlikeWithoutAdminMenuFakery() const { return g_config.godlike; }
bool LLAgent::isInGroup(const LLUUID& group_id, bool) const { return g_config.agent_groups.count(group_id) > 0; }
bool LLAgent::requestPostCapability(const std::string& cap, LLSD& body, httpCallback_t, httpCallback_t)
{
    vks_inv::note("cap.post " + cap + " " + notation(body));
    return false;
}
void LLAgent::sendReliableMessage()
{
    std::string name;
    std::vector<U8> bytes = vks_inv::build_pending_message(name);
    vks_inv::note("msg " + name + " " + vks_inv::hex(bytes));
}

LLViewerWearable* LLAgentWearables::getViewerWearable(const LLWearableType::EType, U32) { return NULL; }
void LLAgentWearables::createWearable(LLWearableType::EType type, bool wear, const LLUUID& parent_id, std::function<void(const LLUUID&)>)
{
    vks_inv::note(llformat("wearable.create %d %d ", (S32)type, wear ? 1 : 0) + id_str(parent_id));
}
U32 LLWearableData::getWearableCount(const LLWearableType::EType) const { return 0; }

LLWearableType::EType LLWearableType::inventoryFlagsToWearableType(U32 flags)
{
    // As llappearance/llwearabletype.cpp.
    return (LLWearableType::EType)(flags & LLInventoryItemFlags::II_FLAGS_SUBTYPE_MASK);
}
LLWearableType::EType LLWearableType::typeNameToType(const std::string&) { return WT_INVALID; }

// ---- grid -------------------------------------------------------------------

LLGridManager::LLGridManager() {}
LLGridManager::~LLGridManager() {}
bool LLGridManager::isInProductionGrid() const { return g_config.production_grid; }
bool LLGridManager::isSystemGrid(const std::string&) const { return g_config.system_grid; }
std::string LLGridManager::getGridId(const std::string&) { return g_config.grid_id; }

#if VKS_INVMODEL_STUB_FETCH
// The stubbed AIS and background fetch (VKS_INVMODEL_STUB_FETCH=1, the
// default). With 0 the test links the real llaisapi.cpp and
// llinventorymodelbackgroundfetch.cpp instead (see the header).
// ---- AIS ----------------------------------------------------------------------

const S32 AISAPI::HTTP_TIMEOUT = 180;
bool AISAPI::isAvailable() { return g_config.ais_available; }

namespace
{
    void ais_call(const char* what, const LLUUID& id, const LLSD* body, AISAPI::completion_t cb)
    {
        std::string line = std::string("ais ") + what + " " + id_str(id);
        if (body)
        {
            line += " " + notation(*body);
        }
        vks_inv::note(line);
        g_ais.push_back(std::make_pair(std::string(what), cb));
    }
}

void AISAPI::CreateInventory(const LLUUID& parent, const LLSD& body, completion_t cb) { ais_call("CreateInventory", parent, &body, cb); }
void AISAPI::SlamFolder(const LLUUID& id, const LLSD& body, completion_t cb) { ais_call("SlamFolder", id, &body, cb); }
void AISAPI::RemoveCategory(const LLUUID& id, completion_t cb) { ais_call("RemoveCategory", id, NULL, cb); }
void AISAPI::RemoveItem(const LLUUID& id, completion_t cb) { ais_call("RemoveItem", id, NULL, cb); }
void AISAPI::PurgeDescendents(const LLUUID& id, completion_t cb) { ais_call("PurgeDescendents", id, NULL, cb); }
void AISAPI::UpdateCategory(const LLUUID& id, const LLSD& body, completion_t cb) { ais_call("UpdateCategory", id, &body, cb); }
void AISAPI::UpdateItem(const LLUUID& id, const LLSD& body, completion_t cb) { ais_call("UpdateItem", id, &body, cb); }

// ---- background fetch ----------------------------------------------------------

LLInventoryModelBackgroundFetch::LLInventoryModelBackgroundFetch() {}
LLInventoryModelBackgroundFetch::~LLInventoryModelBackgroundFetch() {}
bool LLInventoryModelBackgroundFetch::folderFetchActive() const { return g_config.folder_fetch_active; }
bool LLInventoryModelBackgroundFetch::isEverythingFetched() const { return g_config.everything_fetched; }
void LLInventoryModelBackgroundFetch::start(const LLUUID& id, bool recursive)
{
    vks_inv::note("fetch.start " + id_str(id) + (recursive ? " recursive=1" : " recursive=0"));
}
void LLInventoryModelBackgroundFetch::scheduleItemFetch(const LLUUID& id, bool force)
{
    vks_inv::note("fetch.item " + id_str(id) + (force ? " force=1" : " force=0"));
}
void LLInventoryModelBackgroundFetch::scheduleFolderFetch(const LLUUID& id, bool force)
{
    vks_inv::note("fetch.folder " + id_str(id) + (force ? " force=1" : " force=0"));
}
boost::signals2::connection LLInventoryModelBackgroundFetch::setFetchCompletionCallback(folders_fetched_callback_t cb)
{
    vks_inv::note("fetch.completion_callback");
    return g_fetch_done.connect(cb);
}
#endif // VKS_INVMODEL_STUB_FETCH

// ---- marketplace -----------------------------------------------------------------

S32 depth_nesting_in_marketplace(LLUUID id)
{
    std::map<LLUUID, S32>::const_iterator it = g_config.marketplace_depth.find(id);
    return it == g_config.marketplace_depth.end() ? -1 : it->second;
}
void update_marketplace_category(const LLUUID& id, bool consistency, bool skip_clear)
{
    vks_inv::note(llformat("market.update %s %d %d", id.asString().c_str(), consistency ? 1 : 0, skip_clear ? 1 : 0));
}
LLMarketplaceData::LLMarketplaceData() {}
LLMarketplaceData::~LLMarketplaceData() {}
S32 LLMarketplaceData::getListingID(const LLUUID& folder_id)
{
    vks_inv::note("market.listing_id " + id_str(folder_id));
    std::map<LLUUID, S32>::const_iterator it = g_config.listing_ids.find(folder_id);
    return it == g_config.listing_ids.end() ? 0 : it->second;
}
bool LLMarketplaceData::getListing(S32 listing_id)
{
    vks_inv::note(llformat("market.get_listing %d", listing_id));
    return true;
}

// ---- inventory functions -----------------------------------------------------------

bool get_is_item_worn(const LLUUID& id) { return g_config.worn_items.count(id) > 0; }
bool get_is_category_removable(const LLInventoryModel*, const LLUUID&) { return g_config.category_removable; }
std::string make_path(const LLInventoryObject* object) { return object ? object->getUUID().asString() : std::string("(null)"); }
std::string make_inventory_path(const LLUUID& id) { return id.asString(); }
std::string make_info(const LLInventoryObject* object) { return make_path(object); }
std::string make_inventory_info(const LLUUID& id) { return id.asString(); }
void rez_attachment(LLViewerInventoryItem* item, LLViewerJointAttachment*, bool replace)
{
    vks_inv::note("rez_attachment " + (item ? id_str(item->getUUID()) : std::string("null")) + (replace ? " 1" : " 0"));
}
void start_new_inventory_observer() { vks_inv::note("observer.start_new_inventory"); }

bool LLIsType::operator()(LLInventoryCategory* cat, LLInventoryItem* item)
{
    // As llinventoryfunctions.cpp.
    if (mType == LLAssetType::AT_CATEGORY)
    {
        if (cat) return true;
    }
    if (item)
    {
        if (item->getType() == mType) return true;
    }
    return false;
}
bool LLAssetIDMatches::operator()(LLInventoryCategory*, LLInventoryItem* item)
{
    return (item && item->getAssetUUID() == mAssetID);
}
bool LLLinkedItemIDMatches::operator()(LLInventoryCategory*, LLInventoryItem* item)
{
    return (item &&
            (item->getIsLinkType()) &&
            (item->getLinkedUUID() == mBaseItemID));
}
bool LLFindWearables::operator()(LLInventoryCategory*, LLInventoryItem* item)
{
    if (item)
    {
        if ((item->getType() == LLAssetType::AT_CLOTHING)
            || (item->getType() == LLAssetType::AT_BODYPART))
        {
            return true;
        }
    }
    return false;
}

// ---- appearance, gestures, previews, notifications -------------------------------

// Only held by unique_ptr in LLAppearanceMgr; defined in llappearancemgr.cpp,
// which is not linked. The stub object never creates one.
class LLOutfitUnLockTimer {};
LLAppearanceMgr::LLAppearanceMgr() {}
LLAppearanceMgr::~LLAppearanceMgr() {}
void LLAppearanceMgr::wearItemOnAvatar(const LLUUID& id, bool do_update, bool replace, LLPointer<LLInventoryCallback>)
{
    vks_inv::note(llformat("appearance.wear %s %d %d", id.asString().c_str(), do_update ? 1 : 0, replace ? 1 : 0));
}
void LLAppearanceMgr::removeItemFromAvatar(const LLUUID& id, nullary_func_t, LLPointer<LLInventoryCallback>, bool immediate)
{
    vks_inv::note(llformat("appearance.remove %s %d", id.asString().c_str(), immediate ? 1 : 0));
}
void LLAppearanceMgr::removeItemsFromAvatar(const uuid_vec_t& ids, nullary_func_t, LLPointer<LLInventoryCallback>, bool immediate)
{
    for (const LLUUID& id : ids)
    {
        vks_inv::note(llformat("appearance.remove %s %d", id.asString().c_str(), immediate ? 1 : 0));
    }
}

LLGestureMgr::LLGestureMgr() {}
LLGestureMgr::~LLGestureMgr() {}
void LLGestureMgr::done() {}
void LLGestureMgr::changed(U32) {}
bool LLGestureMgr::isGestureActive(const LLUUID& id) { return g_config.active_gestures.count(id) > 0; }
void LLGestureMgr::activateGesture(const LLUUID& id) { vks_inv::note("gesture.activate " + id_str(id)); }
void LLGestureMgr::deactivateGesture(const LLUUID& id) { vks_inv::note("gesture.deactivate " + id_str(id)); }

void LLPreview::hide(const LLUUID& id, bool no_saving)
{
    vks_inv::note("preview.hide " + id_str(id) + (no_saving ? " 1" : " 0"));
}
LLPreviewGesture* LLPreviewGesture::show(const LLUUID& item_id, const LLUUID&)
{
    vks_inv::note("preview_gesture.show " + id_str(item_id));
    return NULL;
}

LLNotificationPtr LLNotificationsUtil::add(const std::string& name)
{
    vks_inv::note("notify " + name);
    return LLNotificationPtr();
}
LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& args, const LLSD&, const std::string&)
{
    vks_inv::note("notify " + name + " " + notation(args));
    return LLNotificationPtr();
}
LLNotificationPtr LLNotificationsUtil::add(const std::string& name, const LLSD& args, const LLSD&, LLNotificationFunctorRegistry::ResponseFunctor)
{
    vks_inv::note("notify " + name + " " + notation(args));
    return LLNotificationPtr();
}
S32 LLNotificationsUtil::getSelectedOption(const LLSD&, const LLSD& response)
{
    return response.asInteger();
}

bool LLFloaterPreviewTrash::isVisible() { return g_config.trash_preview_visible; }
void LLFloaterPreviewTrash::show() { vks_inv::note("trash_preview.show"); }

std::string LLTrans::getString(std::string_view key, const LLStringUtil::format_map_t&, bool)
{
    std::string k(key);
    std::map<std::string, std::string>::const_iterator it = g_config.translations.find(k);
    return it == g_config.translations.end() ? k : it->second;
}

void LLViewerAssetType::generateDescriptionFor(LLAssetType::EType, std::string& description)
{
    description = "(stub description)";
}

// ---- FS, RLVa --------------------------------------------------------------------

AOSitCancelTimer::AOSitCancelTimer() : LLEventTimer(0.1f), mTickCount(0) { mEventTimer.stop(); }
bool AOSitCancelTimer::tick() { return false; }
AOTimerCollection::AOTimerCollection() : LLEventTimer(10.f), mInventoryTimer(false), mSettingsTimer(false), mReloadTimer(false), mImportTimer(false) { mEventTimer.stop(); }
bool AOTimerCollection::tick() { return false; }
AOEngine::AOEngine() {}
AOEngine::~AOEngine() {}
const LLUUID& AOEngine::getAOFolder() const { return g_config.ao_folder; }
FSLSLBridge::FSLSLBridge() {}
FSLSLBridge::~FSLSLBridge() {}
void FSLSLBridge::inventoryChanged(LLViewerObject*, LLInventoryObject::object_list_t*, S32, void*) {}
void LLVOInventoryListener::removeVOInventoryListener() {}
LLUUID FSFloaterWearableFavorites::getFavoritesFolder() { return g_config.favorites_folder; }
bool RlvHandler::m_fEnabled = false;
void RlvAttachmentLockWatchdog::onSavedAssetIntoInventory(const LLUUID& id) { vks_inv::note("rlv.saved_asset " + id_str(id)); }
bool RlvAttachmentLockWatchdog::onTimer() { return false; }
bool RlvPredIsEqualOrLinkedItem::operator()(const LLViewerInventoryItem* item) const
{
    return (item) && (item->getLinkedUUID() == m_pItem->getLinkedUUID());
}

// ---- UI the sources reach only from paths the tests do not drive ------------------

LLClipboard::LLClipboard() {}
LLClipboard::~LLClipboard() {}
bool LLClipboard::hasContents() const { return false; }
bool LLClipboard::isOnClipboard(const LLUUID&) const { return false; }
void LLClipboard::reset() {}
LLCommandHandler::LLCommandHandler(const char*, EUntrustedAccess) {}
LLCommandHandler::~LLCommandHandler() {}
const std::string LLCommandHandler::NAV_TYPE_CLICKED = "clicked";
const std::string LLCommandHandler::NAV_TYPE_EXTERNAL = "external";
LLInventoryListener::LLInventoryListener() : LLEventAPI("LLInventory", "inventory parity stub") {}
const std::string LLInventoryPanel::DEFAULT_SORT_ORDER = std::string("InventorySortOrder");
void LLInventoryPanel::openInventoryPanelAndSetSelection(bool, const LLUUID& id, bool, bool, bool)
{
    vks_inv::note("ui.open_inventory_selection " + id_str(id));
}
void LLInventoryPanel::setSelectionByID(const LLUUID&, bool) {}
void LLPanelMainInventory::toggleFindOptions() {}
const std::string LLFloaterSidePanelContainer::sMainPanelName = "main_panel";
LLPanel* LLFloaterSidePanelContainer::getPanel(std::string_view, std::string_view) { return NULL; }
void LLFloaterSidePanelContainer::showPanel(std::string_view floater_name, const LLSD&)
{
    vks_inv::note("ui.show_panel " + std::string(floater_name));
}
void LLFloaterView::adjustToFitScreen(LLFloater*, bool, bool) {}
void LLFolderView::scrollToShowSelection() {}
U32 LLFloaterPerms::getEveryonePerms(std::string) { return PERM_NONE; }
U32 LLFloaterPerms::getGroupPerms(std::string) { return PERM_NONE; }
U32 LLFloaterPerms::getNextOwnerPerms(std::string) { return PERM_MOVE | PERM_TRANSFER; }
void LLFavoritesOrderStorage::load() {}
void LLFavoritesOrderStorage::destroyClass() {}
S32 LLFavoritesOrderStorage::getSortIndex(const LLUUID&) { return -1; }
void LLFavoritesOrderStorage::getSLURL(const LLUUID&) {}
void LLSettingsVOBase::createNewInventoryItem(LLSettingsType::type_e type, const LLUUID& parent_id, std::function<void(const LLUUID&)>)
{
    vks_inv::note(llformat("settings.create %d ", (S32)type) + id_str(parent_id));
}
bool LLViewerRegion::simulatorFeaturesReceived() const { return false; }
void LLViewerRegion::getSimulatorFeatures(LLSD& features) const { features = LLSD::emptyMap(); }
const LLUUID& LLViewerObject::getAttachmentItemID() const { return LLUUID::null; }

// LLInventoryState statics (llinventorybridge.cpp).
bool LLInventoryState::sWearNewClothing = false;
LLUUID LLInventoryState::sWearNewClothingTransactionID;
