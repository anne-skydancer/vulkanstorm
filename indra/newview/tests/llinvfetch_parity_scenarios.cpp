/**
 * @file llinvfetch_parity_scenarios.cpp
 * @brief The inventory fetch wire scenarios (recorder R3b).
 *
 * Each scenario builds a synthetic inventory, hands it to the mock server,
 * drives the real fetch code the way the viewer does (the login steps of
 * llstartup.cpp as frozen copies, the background fetch, the AIS commands,
 * the legacy capability and UDP fallbacks) and records the result through
 * Run (llinvfetch_parity_support.h).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "../llviewerprecompiledheaders.h"

#include "llinvfetch_parity_scenarios.h"

#include "../llinventorymodel.h"
#include "../llinventorymodelbackgroundfetch.h"
#include "../llinventoryobserver.h"
#include "../llviewerinventory.h"
#include "../llaisapi.h"
#include "../llagent.h"
#include "../llstartup.h"
#include "../llviewercontrol.h"
#include "../llappviewer.h"
#include "lldir.h"
#include "llfile.h"

#include <algorithm>
#include <map>
#include <stdexcept>

// R2's frozen copy of the inventory part of LLAppViewer::disconnectViewer()
// (llinventorymodel_parity_generic.cpp).
namespace vks_inv_frozen
{
    void save_inventory_at_logout(bool mSecondInstance);
}

namespace vks_fetch
{
namespace
{
    // ---- ids -------------------------------------------------------------------
    // vks_inv::pid: "<tag8>-0000-4000-8000-<n12>". The first two hex digits
    // are never one the mock scripts (f3 f4 52 53 5e e0) unless a scenario
    // wants that behaviour.
    LLUUID P(U32 tag, U64 n) { return vks_inv::pid(tag, n); }
    const LLUUID ROOT = P(0xc0000000, 1);
    const LLUUID LIBROOT = P(0xd0000000, 1);
    LLUUID SYS(S32 type) { return P(0xc0000001, (U64)(type + 2)); }
    LLUUID USR(U64 n) { return P(0xc0100000, n); }
    LLUUID ITM(U64 n) { return P(0xa0000000, n); }
    LLUUID LNK(U64 n) { return P(0xb0000000, n); }
    LLUUID LIBF(U64 n) { return P(0xd0000000, n); }
    LLUUID LIBI(U64 n) { return P(0xd1000000, n); }
    const LLUUID ORPHAN = P(0xa0f00000, 1);

    S32 ver(U64 n) { return 1 + (S32)((n * 7) % 13); }

    struct SysDef { S32 type; const char* name; };
    const SysDef SYSTEM[] = {
        { LLFolderType::FT_TEXTURE, "Textures" }, { LLFolderType::FT_SOUND, "Sounds" },
        { LLFolderType::FT_CALLINGCARD, "Calling Cards" }, { LLFolderType::FT_LANDMARK, "Landmarks" },
        { LLFolderType::FT_CLOTHING, "Clothing" }, { LLFolderType::FT_OBJECT, "Objects" },
        { LLFolderType::FT_NOTECARD, "Notecards" }, { LLFolderType::FT_LSL_TEXT, "Scripts" },
        { LLFolderType::FT_BODYPART, "Body Parts" }, { LLFolderType::FT_TRASH, "Trash" },
        { LLFolderType::FT_SNAPSHOT_CATEGORY, "Photo Album" }, { LLFolderType::FT_LOST_AND_FOUND, "Lost And Found" },
        { LLFolderType::FT_ANIMATION, "Animations" }, { LLFolderType::FT_GESTURE, "Gestures" },
        { LLFolderType::FT_FAVORITE, "Favorites" }, { LLFolderType::FT_CURRENT_OUTFIT, "Current Outfit" },
        { LLFolderType::FT_MY_OUTFITS, "My Outfits" }, { LLFolderType::FT_INBOX, "Received Items" },
        { LLFolderType::FT_MARKETPLACE_LISTINGS, "Marketplace Listings" }, { LLFolderType::FT_SETTINGS, "Settings" },
        { LLFolderType::FT_MATERIAL, "Materials" } };

    // ---- the synthetic inventories ---------------------------------------------
    struct Ids
    {
        LLUUID shirt, pants, jacket, shape, skin, tex1, obj1, obj2, note1, trash1, trash2, root1, root2;
        LLUUID outfitA, outfitB, cof_shirt_link;
        std::vector<LLUUID> user, user_sub, user_leaf, lib_folders, lib_items, mkt;
        std::map<LLUUID, std::vector<LLUUID> > items_in;   // user folder -> its items
    };

    class Builder
    {
    public:
        World w;
        Ids ids;
        U64 n_user = 1, n_item = 1, n_link = 1, n_lib = 2, n_libitem = 1;

        explicit Builder(bool library)
        {
            w.agent = AGENT;
            w.libowner = LIBRARY_OWNER;
            w.root = ROOT;
            w.libroot = library ? LIBROOT : LLUUID::null;
            w.add_folder(ROOT, LLUUID::null, LLFolderType::FT_ROOT_INVENTORY, 30, "My Inventory");
            for (const SysDef& s : SYSTEM)
            {
                w.add_folder(SYS(s.type), ROOT, s.type, ver(s.type + 2), s.name);
            }
            if (library)
            {
                w.add_folder(LIBROOT, LLUUID::null, LLFolderType::FT_ROOT_INVENTORY, 12, "Library", true);
            }
        }
        LLUUID item(const LLUUID& parent, S32 at, S32 it, const std::string& name, U32 flags = 0)
        {
            LLUUID id = ITM(n_item);
            Item& i = w.add_item(id, parent, at, it, name);
            i.flags = flags;
            i.created = 1700000000 + (S32)n_item;
            ++n_item;
            ids.items_in[parent].push_back(id);
            return id;
        }
        LLUUID libitem(const LLUUID& parent, const std::string& name)
        {
            LLUUID id = LIBI(n_libitem);
            Item& i = w.add_item(id, parent, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, name, true);
            i.created = 1600000000 + (S32)n_libitem;
            i.base = i.owner_mask = 0x7fffffff;
            i.everyone = 0x8000;  // PERM_COPY
            i.next = 0x7fffffff;
            ++n_libitem;
            ids.lib_items.push_back(id);
            return id;
        }
        LLUUID folder(const LLUUID& parent, const std::string& name, S32 type = LLFolderType::FT_NONE)
        {
            LLUUID id = USR(n_user);
            w.add_folder(id, parent, type, ver(n_user), name);
            ++n_user;
            return id;
        }
        LLUUID libfolder(const LLUUID& parent, const std::string& name)
        {
            LLUUID id = LIBF(n_lib);
            w.add_folder(id, parent, LLFolderType::FT_NONE, ver(n_lib), name, true);
            ++n_lib;
            ids.lib_folders.push_back(id);
            return id;
        }
        LLUUID link(const LLUUID& parent, const LLUUID& target, bool folder_link, const std::string& name)
        {
            LLUUID id = LNK(n_link++);
            w.add_link(id, parent, target, folder_link, name);
            return id;
        }
    };

    // The common inventory: every system folder, items in most of them, a
    // few user folders three levels deep, two outfits, the COF, two
    // marketplace listings, a small library, and an orphan (an item the
    // server has with no parent, served by /orphans).
    void basic(Builder& b, int user_folders, bool orphan, int lib_folders = 3)
    {
        Ids& d = b.ids;
        d.tex1 = b.item(SYS(LLFolderType::FT_TEXTURE), LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Texture 1");
        b.item(SYS(LLFolderType::FT_TEXTURE), LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Texture 2");
        d.shirt = b.item(SYS(LLFolderType::FT_CLOTHING), LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Shirt", 4);
        d.pants = b.item(SYS(LLFolderType::FT_CLOTHING), LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Pants", 5);
        d.jacket = b.item(SYS(LLFolderType::FT_CLOTHING), LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Jacket", 8);
        d.shape = b.item(SYS(LLFolderType::FT_BODYPART), LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Shape", 0);
        d.skin = b.item(SYS(LLFolderType::FT_BODYPART), LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Skin", 1);
        d.obj1 = b.item(SYS(LLFolderType::FT_OBJECT), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Object 1");
        d.obj2 = b.item(SYS(LLFolderType::FT_OBJECT), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Object 2");
        d.note1 = b.item(SYS(LLFolderType::FT_NOTECARD), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Notecard 1");
        b.item(SYS(LLFolderType::FT_LSL_TEXT), LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, "Script 1");
        b.item(SYS(LLFolderType::FT_LANDMARK), LLAssetType::AT_LANDMARK, LLInventoryType::IT_LANDMARK, "Landmark 1");
        b.item(SYS(LLFolderType::FT_GESTURE), LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE, "Gesture 1");
        b.item(SYS(LLFolderType::FT_ANIMATION), LLAssetType::AT_ANIMATION, LLInventoryType::IT_ANIMATION, "Animation 1");
        b.item(SYS(LLFolderType::FT_SNAPSHOT_CATEGORY), LLAssetType::AT_TEXTURE, LLInventoryType::IT_SNAPSHOT, "Snapshot 1");
        d.trash1 = b.item(SYS(LLFolderType::FT_TRASH), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Trashed 1");
        d.trash2 = b.item(SYS(LLFolderType::FT_TRASH), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Trashed 2");
        d.root1 = b.item(ROOT, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Root note 1");
        d.root2 = b.item(ROOT, LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Root texture 2");
        for (int u = 1; u <= user_folders; ++u)
        {
            std::string un = "Folder " + std::to_string(u);
            LLUUID U = b.folder(ROOT, un);
            d.user.push_back(U);
            for (int k = 1; k <= 3; ++k)
            {
                b.item(U, k == 2 ? LLAssetType::AT_OBJECT : LLAssetType::AT_NOTECARD,
                       k == 2 ? LLInventoryType::IT_OBJECT : LLInventoryType::IT_NOTECARD, un + " item " + std::to_string(k));
            }
            for (int s = 1; s <= 2; ++s)
            {
                std::string sn = un + "." + std::to_string(s);
                LLUUID S = b.folder(U, sn);
                d.user_sub.push_back(S);
                b.item(S, LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, sn + " item 1");
                b.item(S, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, sn + " item 2");
                LLUUID L = b.folder(S, sn + ".1");
                d.user_leaf.push_back(L);
                b.item(L, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, sn + ".1 item 1");
            }
        }
        d.outfitA = b.folder(SYS(LLFolderType::FT_MY_OUTFITS), "Outfit A", LLFolderType::FT_OUTFIT);
        b.link(d.outfitA, d.shirt, false, "Shirt");
        b.link(d.outfitA, d.pants, false, "Pants");
        b.link(d.outfitA, d.shape, false, "Shape");
        d.outfitB = b.folder(SYS(LLFolderType::FT_MY_OUTFITS), "Outfit B", LLFolderType::FT_OUTFIT);
        b.link(d.outfitB, d.jacket, false, "Jacket");
        const LLUUID cof = SYS(LLFolderType::FT_CURRENT_OUTFIT);
        d.cof_shirt_link = b.link(cof, d.shirt, false, "Shirt");
        b.link(cof, d.pants, false, "Pants");
        b.link(cof, d.shape, false, "Shape");
        b.link(cof, d.skin, false, "Skin");
        b.link(cof, d.outfitA, true, "Outfit A");
        for (int m = 1; m <= 2; ++m)
        {
            LLUUID M = b.folder(SYS(LLFolderType::FT_MARKETPLACE_LISTINGS), "Listing " + std::to_string(m));
            d.mkt.push_back(M);
            b.item(M, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Listed object " + std::to_string(m));
        }
        if (b.w.libroot.notNull())
        {
            b.libitem(LIBROOT, "Library welcome");
            b.libitem(LIBROOT, "Library readme");
            for (int l = 1; l <= lib_folders; ++l)
            {
                std::string ln = "Library " + std::to_string(l);
                LLUUID F = b.libfolder(LIBROOT, ln);
                b.libitem(F, ln + " note 1");
                b.libitem(F, ln + " note 2");
                for (int s = 1; s <= 2; ++s)
                {
                    LLUUID S = b.libfolder(F, ln + "." + std::to_string(s));
                    b.libitem(S, ln + "." + std::to_string(s) + " note");
                    b.libitem(S, ln + "." + std::to_string(s) + " note b");
                }
            }
        }
        if (orphan)
        {
            Item& o = b.w.add_item(ORPHAN, LLUUID::null, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Lost object");
            o.created = 1650000000;
        }
    }

    // The large inventory (survey 3 section 8.2 scenario 3): 5,000 folders,
    // 60,000 items; 300 folders directly under the root (15 subset batches
    // of 20); a chain 60 folders deep (the depth-50 cut and its follow-up).
    void large(Builder& b)
    {
        basic(b, 0, false, 3);
        Ids& d = b.ids;
        const int TOP = 300 - (int)(sizeof(SYSTEM) / sizeof(SYSTEM[0]));   // 279 user folders
        std::vector<LLUUID> all_user;
        for (int u = 1; u <= TOP; ++u)
        {
            std::string un = "Big " + std::to_string(u);
            LLUUID U = b.folder(ROOT, un);
            d.user.push_back(U);
            all_user.push_back(U);
            for (int c = 1; c <= 4; ++c)
            {
                LLUUID C = b.folder(U, un + "." + std::to_string(c));
                all_user.push_back(C);
                for (int g = 1; g <= 3; ++g)
                {
                    all_user.push_back(b.folder(C, un + "." + std::to_string(c) + "." + std::to_string(g)));
                }
            }
        }
        // The chain: 60 levels below the first user folder.
        LLUUID parent = d.user[0];
        for (int k = 1; k <= 60; ++k)
        {
            parent = b.folder(parent, "Chain " + std::to_string(k));
            all_user.push_back(parent);
            d.user_leaf.push_back(parent);
        }
        // Fill to exactly 5,000 folders under user folders 2-5.
        size_t k = 0;
        while (b.w.folders.size() < 5000)
        {
            all_user.push_back(b.folder(d.user[1 + (k % 4)], "Extra " + std::to_string(k)));
            ++k;
        }
        // Items: 12 per user folder, then the rest in Objects, to 60,000.
        static const S32 at[] = { LLAssetType::AT_NOTECARD, LLAssetType::AT_OBJECT, LLAssetType::AT_TEXTURE, LLAssetType::AT_LSL_TEXT };
        static const S32 it[] = { LLInventoryType::IT_NOTECARD, LLInventoryType::IT_OBJECT, LLInventoryType::IT_TEXTURE, LLInventoryType::IT_LSL };
        for (const LLUUID& f : all_user)
        {
            for (int i = 0; i < 12 && b.w.items.size() < 60000; ++i)
            {
                b.item(f, at[i % 4], it[i % 4], "Item " + std::to_string(b.n_item));
            }
        }
        while (b.w.items.size() < 60000)
        {
            b.item(SYS(LLFolderType::FT_OBJECT), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Object " + std::to_string(b.n_item));
        }
    }

    // ---- scenario plumbing -----------------------------------------------------
    struct Ctx
    {
        std::string work;
        const sink_t* sink;
        std::map<std::string, ScenarioInfo> info;
    };

    std::string scenario_dir(Ctx& c, const std::string& id, bool fresh)
    {
        std::string d = c.work + "/" + id;
        if (fresh)
        {
            gDirUtilp->deleteDirAndContents(d);
        }
        LLFile::mkdir(d);
        return d;
    }

    void begin(const std::string& cache_dir, U32 pool, int caps, const World& w, const std::string& mode)
    {
        set_ais_pool(pool);
        reset_scenario(cache_dir);
        set_region(true, caps);
        mock_world(w);
        mock_mode(mode);
    }

    void emit(Ctx& c, Run& r)
    {
        std::string responses;
        Case k = r.finish(responses);
        (*c.sink)(c.info[k.id], k, responses);
    }

    void world_line(Run& r, const World& w)
    {
        size_t server_items = w.items.size(), links = 0;
        for (const Item& i : w.items) if (i.link) ++links;
        r.note("world folders=" + std::to_string(w.folders.size()) + " items=" + std::to_string(server_items)
               + " links=" + std::to_string(links) + " pool=" + std::to_string(gSavedSettings.getU32("PoolSizeAIS"))
               + " batch=" + std::to_string(gSavedSettings.getS32("BatchSizeAIS3")));
    }

    // The inventory steps of login (llstartup.cpp, frozen copies).
    void login(Run& r, const LLSD& response, const LLUUID& root)
    {
        gInventory.setRootFolderID(root);
        vks_inv::set_startup_state(STATE_INVENTORY_SEND);
        startup::inventory_send(response);
        vks_inv::set_startup_state(STATE_INVENTORY_SKEL);
        startup::inventory_skel(response);
        r.snapshot();
        vks_inv::set_startup_state(STATE_INVENTORY_SEND2);
        startup::inventory_send2();
        r.snapshot();
        vks_inv::set_startup_state(STATE_INVENTORY_CALLBACKS);
        bool ready = startup::inventory_callbacks();
        r.note(std::string("startup callbacks ready=") + (ready ? "1" : "0")
               + " usable=" + (gInventory.isInventoryUsable() ? "1" : "0"));
        r.step("login");
    }

    // STATE_WEARABLES_WAIT: the COF fetch (callAfterCOFFetch), pumped with the
    // idle callbacks held so it and the base-outfit follow-up come before the
    // walk's first request (a total order on the wire).
    void cof(Run& r, const LLUUID& base_outfit)
    {
        vks_inv::set_startup_state(STATE_WEARABLES_WAIT);
        vks_inv::config().base_outfit = base_outfit;
        appearance::callAfterCOFFetch(nullary("cof"));
        r.settle(false);
        r.step("cof");
    }

    void walk(Run& r, F64 timeout = 300.0)
    {
        vks_inv::set_startup_state(STATE_STARTED);
        r.settle(true, true, timeout);
        r.step("walk");
    }

    // The model's objects a scenario works on; a missing one ends the
    // scenario with a FAILURE line instead of a crash.
    struct Missing : public std::runtime_error
    {
        explicit Missing(const std::string& w) : std::runtime_error(w) {}
    };
    LLViewerInventoryCategory* cat(const LLUUID& id)
    {
        LLViewerInventoryCategory* c = gInventory.getCategory(id);
        if (!c) throw Missing("category not in the model: " + id.asString());
        return c;
    }
    LLViewerInventoryItem* itm(const LLUUID& id)
    {
        LLViewerInventoryItem* i = gInventory.getItem(id);
        if (!i) throw Missing("item not in the model: " + id.asString());
        return i;
    }
    LLInventoryModelBackgroundFetch& bgf() { return LLInventoryModelBackgroundFetch::instance(); }

    // An item fetch observer in a fixed slot (pointer order), recording done().
    class ItemsObserver : public LLInventoryFetchItemsObserver
    {
    public:
        ItemsObserver(const uuid_vec_t& ids, const std::string& tag) : LLInventoryFetchItemsObserver(ids), mTag(tag) {}
        static void* operator new(size_t size) { return vks_inv::observer_slot(2, size); }
        static void operator delete(void*) {}
        void done() override
        {
            vks_inv::note("observer " + mTag + " done complete=" + std::to_string(mComplete.size())
                          + " incomplete=" + std::to_string(mIncomplete.size()));
            gInventory.removeObserver(this);
            delete this;
        }
        std::string mTag;
    };

    // An item only the viewer has (the server never had it, or deleted it).
    void add_model_only_item(const LLUUID& id, const LLUUID& parent, const std::string& name)
    {
        LLPermissions perm;
        perm.init(AGENT, AGENT, AGENT, LLUUID::null);
        perm.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL);
        LLPointer<LLViewerInventoryItem> it = new LLViewerInventoryItem(id, parent, perm, LLUUID::null,
            LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, name, "", LLSaleInfo::DEFAULT, 0, 1700000000);
        gInventory.updateItem(it);
        gInventory.notifyObservers();
    }

    void add_model_only_folder(const LLUUID& id, const LLUUID& parent, const std::string& name)
    {
        LLPointer<LLViewerInventoryCategory> c = new LLViewerInventoryCategory(id, parent, LLFolderType::FT_NONE, name, AGENT);
        c->setVersion(1);
        c->setDescendentCount(0);
        gInventory.updateCategory(c);
        gInventory.notifyObservers();
    }

    LLSD link_llsd(const LLUUID& target, S32 type, S32 inv_type, const std::string& name)
    {
        LLSD l;
        l["linked_id"] = target;
        l["type"] = type;
        l["inv_type"] = inv_type;
        l["name"] = name;
        l["desc"] = "";
        return l;
    }

    // s01's final folder versions: the skeleton of the warm logins.
    std::map<LLUUID, S32> g_s01_versions;
    World g_s01_world;
    std::string g_s01_cache;

    // ---- 1: login_cold ----------------------------------------------------------
    void s01_login_cold(Ctx& c)
    {
        Builder b(true);
        basic(b, 6, true);
        g_s01_cache = scenario_dir(c, "s01_login_cold", true);
        begin(g_s01_cache, 2, CAPS_ALL, b.w, "");
        Run r("s01_login_cold", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        cof(r, b.ids.outfitA);
        walk(r);
        startup::find_lost_items();
        r.settle(true, true);
        r.step("orphans");
        vks_inv_frozen::save_inventory_at_logout(false);
        r.step("logout_cache");
        g_s01_versions.clear();
        for (const Folder& f : b.w.folders)
        {
            if (LLViewerInventoryCategory* k = cat(f.id)) g_s01_versions[f.id] = k->getVersion();
        }
        g_s01_world = b.w;
        emit(c, r);
    }

    // The server after s01: the orphan moved to Lost And Found (the server
    // acted on the viewer's MoveInventoryItem), every folder at the version
    // the viewer cached.
    World s02_world()
    {
        World w = g_s01_world;
        for (Item& i : w.items)
        {
            if (i.id == ORPHAN) i.parent = SYS(LLFolderType::FT_LOST_AND_FOUND);
        }
        for (Folder& f : w.folders)
        {
            auto it = g_s01_versions.find(f.id);
            if (it != g_s01_versions.end() && it->second >= 0) f.version = it->second;
        }
        return w;
    }

    // ---- 2a: login_warm, everything cached ------------------------------------------
    void s02a_login_warm_full(Ctx& c)
    {
        World w = s02_world();
        begin(g_s01_cache, 2, CAPS_ALL, w, "");
        Run r("s02a_login_warm_full", &w);
        world_line(r, w);
        login(r, w.login_response(), w.root);
        cof(r, LLUUID::null);
        walk(r);
        emit(c, r);
    }

    // ---- 2b: login_warm, two folders changed on the server ------------------------
    void s02b_login_warm_partial(Ctx& c)
    {
        World w = s02_world();
        Builder b(true);
        basic(b, 6, true);   // the same ids as s01 (built the same way)
        const LLUUID changed_tree = b.ids.user[0];      // with sub-folders
        const LLUUID changed_leaf = b.ids.user_leaf[3];
        for (Folder& f : w.folders)
        {
            if (f.id == changed_tree || f.id == changed_leaf) f.version += 1;
        }
        Item& added1 = w.add_item(P(0xa0100000, 1), changed_tree, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Added on the server 1");
        added1.created = 1710000000;
        Item& added2 = w.add_item(P(0xa0100000, 2), changed_leaf, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Added on the server 2");
        added2.created = 1710000001;
        begin(g_s01_cache, 2, CAPS_ALL, w, "");
        Run r("s02b_login_warm_partial", &w);
        world_line(r, w);
        login(r, w.login_response(), w.root);
        cof(r, LLUUID::null);
        walk(r);
        emit(c, r);
    }

    // ---- 3: large ----------------------------------------------------------------
    void s03_large(Ctx& c)
    {
        Builder b(true);
        large(b);
        std::string cache = scenario_dir(c, "s03_large", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s03_large", &b.w, Run::SEQ, true);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        cof(r, b.ids.outfitA);
        walk(r, 900.0);
        emit(c, r);
    }

    // ---- 4: forbidden --------------------------------------------------------------
    void s04_forbidden(Ctx& c)
    {
        Builder b(true);
        basic(b, 2, false);
        // f3: 403 at depth > 0 only; f4: 403 at every depth. A subset batch
        // naming either is refused whole.
        const LLUUID X3 = P(0xf3000000, 1), X4 = P(0xf4000000, 1);
        b.w.add_folder(X3, ROOT, LLFolderType::FT_NONE, 5, "Large three");
        b.item(X3, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Large three item");
        for (int s = 1; s <= 2; ++s)
        {
            LLUUID S = b.folder(X3, "Large three." + std::to_string(s));
            b.item(S, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Large three." + std::to_string(s) + " item");
        }
        b.w.add_folder(X4, ROOT, LLFolderType::FT_NONE, 6, "Large four");
        b.item(X4, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Large four item");
        for (int s = 1; s <= 3; ++s)
        {
            LLUUID S = b.folder(X4, "Large four." + std::to_string(s));
            b.item(S, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Large four." + std::to_string(s) + " item");
        }
        std::string cache = scenario_dir(c, "s04_forbidden", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s04_forbidden", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        emit(c, r);
    }

    // ---- 5: transient 5xx (llcorehttp's retries) ----------------------------------
    void s05_transient_5xx(Ctx& c)
    {
        Builder b(true);
        basic(b, 1, false, 1);
        const LLUUID T52 = P(0x52000000, 1), T5E = P(0x5e000000, 1), W53 = P(0x53000000, 1);
        b.w.add_folder(T52, ROOT, LLFolderType::FT_NONE, 4, "Busy twice");
        b.w.add_folder(T5E, ROOT, LLFolderType::FT_NONE, 4, "Busy at depth");
        for (int s = 1; s <= 2; ++s)
        {
            b.item(b.folder(T52, "Busy twice." + std::to_string(s)), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "bt item " + std::to_string(s));
            b.item(b.folder(T5E, "Busy at depth." + std::to_string(s)), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "bd item " + std::to_string(s));
        }
        Item& w53 = b.w.add_item(W53, SYS(LLFolderType::FT_OBJECT), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Busy once on write");
        w53.created = 1700000500;
        std::string cache = scenario_dir(c, "s05_transient_5xx", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s05_transient_5xx", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        // 503, 503, 200: one logical request.
        cat(T52)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        bgf().start(T52);
        r.settle(true, true);
        r.step("busy_twice");
        // 503 with Retry-After: 1 at depth 50, every time: the retries run
        // out, then the scheduler's fallback (depth 0) succeeds.
        cat(T5E)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        bgf().start(T5E);
        r.settle(true, true, 300.0);
        r.step("busy_at_depth");
        // A write answered 503 once: llcorehttp sends the PATCH again.
        LLSD updates;
        updates["name"] = "Busy once renamed";
        update_inventory_item(W53, updates, cb("w53"));
        r.settle(true, true);
        r.step("write_retry");
        emit(c, r);
    }

    // ---- 6: outage ----------------------------------------------------------------
    void s06_outage(Ctx& c)
    {
        Builder b(true);
        basic(b, 6, false);
        std::string cache = scenario_dir(c, "s06_outage", true);
        // The first four AIS requests fail (404: not retried by llcorehttp,
        // so the scheduler sees each failure once).
        begin(cache, 2, CAPS_ALL, b.w, "fail_first=4\nfail_status=404\n");
        Run r("s06_outage", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        // The root ended at version 0 and FETCH_FAILED (60 s back-off): a
        // forced fetch inside the back-off sends nothing...
        bgf().scheduleFolderFetch(ROOT, true);
        r.settle(true, true);
        r.step("forced_in_backoff");
        // ...and after it, one request.
        clock_advance(61.0);
        bgf().scheduleFolderFetch(ROOT, true);
        r.settle(true, true);
        r.step("forced_after_backoff");
        // A plain start() of a folder with a known version: nothing.
        bgf().start(ROOT, false);
        r.settle(true, true);
        r.step("start_known");
        emit(c, r);
    }

    // ---- 7: forced -----------------------------------------------------------------
    void s07_forced(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, false);
        std::string cache = scenario_dir(c, "s07_forced", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s07_forced", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        const LLUUID F = b.ids.user[0], G = b.ids.user[1], H = b.ids.user[2];
        // A normal fetch in flight, then two forced fetches of the same folder.
        cat(F)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        cat(F)->fetch();
        r.frame();
        r.note(std::string("in flight=") + std::to_string(sched_state().fetch_count));
        bgf().scheduleFolderFetch(F, true);
        bgf().scheduleFolderFetch(F, true);
        r.snapshot();
        r.settle(true, true);
        r.step("forced_twice_during_normal");
        // Forced G, forced H, forced G again (still in the forced set).
        bgf().scheduleFolderFetch(G, true);
        bgf().scheduleFolderFetch(H, true);
        bgf().scheduleFolderFetch(G, true);
        r.snapshot();
        r.settle(true, true);
        r.step("forced_g_h_g");
        // A normal schedule of a folder with a known version: nothing.
        bgf().scheduleFolderFetch(G, false);
        r.settle(true, true);
        r.step("normal_known");
        // A forced item fetch twice, then once more after it went out.
        bgf().scheduleItemFetch(b.ids.obj1, true);
        bgf().scheduleItemFetch(b.ids.obj1, true);
        r.settle(true, true);
        r.step("forced_item_twice");
        bgf().scheduleItemFetch(b.ids.obj1, true);
        r.settle(true, true);
        r.step("forced_item_again");
        emit(c, r);
    }

    // ---- 8: items ------------------------------------------------------------------
    void s08_items(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, false);
        std::string cache = scenario_dir(c, "s08_items", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s08_items", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        cof(r, b.ids.outfitA);   // the COF links (one is fetched below)
        walk(r);
        const std::vector<LLUUID>& u0 = b.ids.items_in[b.ids.user[0]];
        const std::vector<LLUUID>& u1 = b.ids.items_in[b.ids.user[1]];
        const LLUUID A = u0[0], B = u0[1], L = b.ids.cof_shirt_link, LI = b.ids.lib_items[0];
        const LLUUID U = P(0xa0e00000, 1), U2 = P(0xa0e00000, 2);
        itm(A)->setComplete(false);
        itm(B)->setComplete(false);
        itm(L)->setComplete(false);
        itm(LI)->setComplete(false);
        bgf().start(A);                       // incomplete item: scheduleItemFetch
        itm(B)->fetchFromServer();           // AIS: scheduleItemFetch
        bgf().scheduleItemFetch(L);          // a link: the reply's linked_id
        bgf().scheduleItemFetch(LI);         // a library item: LibraryAPIv3
        bgf().scheduleItemFetch(U);          // unknown to the viewer and the server
        r.snapshot();
        r.settle(true, true);
        r.step("item_fetches");
        // The item observer (AIS): two incomplete items of one folder and an
        // id the viewer does not know.
        itm(u1[0])->setComplete(false);
        itm(u1[1])->setComplete(false);
        uuid_vec_t ids;
        ids.push_back(u1[0]);
        ids.push_back(u1[1]);
        ids.push_back(U2);
        ItemsObserver* obs = new ItemsObserver(ids, "items");
        obs->startFetch();
        if (obs->isFinished()) obs->done(); else gInventory.addObserver(obs);
        r.snapshot();
        r.settle(true, true);
        r.step("observer");
        // A forced fetch of a complete item.
        bgf().scheduleItemFetch(b.ids.obj2, true);
        r.settle(true, true);
        r.step("forced_complete_item");
        emit(c, r);
    }

    // ---- 9: writes (AIS) -------------------------------------------------------------
    void s09_writes(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, false);
        std::string cache = scenario_dir(c, "s09_writes", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s09_writes", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        const Ids& d = b.ids;
        auto done = [&](const std::string& name) { r.settle(true, true); r.step(name); };

        gInventory.createNewCategory(d.user[0], LLFolderType::FT_NONE, "New Folder A", ais_cb("create_folder"));
        done("create_folder");
        gInventory.createNewCategory(SYS(LLFolderType::FT_MY_OUTFITS), LLFolderType::FT_OUTFIT, "Outfit C", ais_cb("create_outfit"));
        done("create_outfit");
        // The second created category (the mock numbers what it creates).
        const LLUUID outfitC("c1000000-0000-4000-8000-000000000002");
        {
            LLInventoryObject::const_object_list_t objs;
            objs.push_back(LLConstPointer<LLInventoryObject>(itm(d.shirt)));
            objs.push_back(LLConstPointer<LLInventoryObject>(itm(d.shape)));
            objs.push_back(LLConstPointer<LLInventoryObject>(cat(d.outfitA)));
            link_inventory_array(outfitC, objs, cb("link_array"));
        }
        done("link_array");
        {
            LLSD contents = LLSD::emptyArray();
            contents.append(link_llsd(d.shirt, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Shirt"));
            contents.append(link_llsd(d.jacket, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Jacket"));
            contents.append(link_llsd(d.shape, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Shape"));
            contents.append(link_llsd(d.skin, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Skin"));
            contents.append(link_llsd(d.outfitB, LLAssetType::AT_LINK_FOLDER, LLInventoryType::IT_CATEGORY, "Outfit B"));
            slam_inventory_folder(SYS(LLFolderType::FT_CURRENT_OUTFIT), contents, cb("slam_cof"));
        }
        done("slam_cof");
        {
            LLSD updates;
            updates["name"] = "Renamed by update";
            update_inventory_item(d.items_in.at(d.user[0])[0], updates, cb("update_item_llsd"));
        }
        done("update_item_llsd");
        {
            LLPointer<LLViewerInventoryItem> copy = new LLViewerInventoryItem(itm(d.items_in.at(d.user[0])[1]));
            copy->setDescription("A new description");
            update_inventory_item(copy, cb("update_item_object"));
        }
        done("update_item_object");
        {
            LLSD updates;
            updates["name"] = "Renamed folder";
            update_inventory_category(d.user[1], updates, cb("update_category"));
        }
        done("update_category");
        {
            LLPointer<LLViewerInventoryCategory> copy = new LLViewerInventoryCategory(cat(d.user[2]));
            copy->rename("Renamed via updateServer");
            copy->updateServer(false);
        }
        done("category_update_server");
        cat(d.outfitB)->changeType(LLFolderType::FT_NONE);
        done("change_type");
        {
            LLPointer<LLViewerInventoryItem> copy = new LLViewerInventoryItem(itm(d.note1));
            copy->rename("Notecard renamed");
            copy->updateServer(false);
        }
        done("item_update_server");
        remove_inventory_item(d.obj2, cb("remove_item"));
        done("remove_item");
        remove_inventory_item(d.trash1, cb("remove_item_immediate"), true);
        done("remove_item_immediate");
        const LLUUID gone_item = P(0xa0c00000, 1);
        add_model_only_item(gone_item, d.user[0], "Gone at the server");
        remove_inventory_item(gone_item, cb("remove_item_410"));
        done("remove_item_410");
        remove_inventory_category(d.user_sub[2], cb("remove_category"));
        done("remove_category");
        const LLUUID gone_folder = P(0xc0c00000, 1);
        add_model_only_folder(gone_folder, d.user[0], "Gone folder");
        remove_inventory_category(gone_folder, cb("remove_category_410"));
        done("remove_category_410");
        remove_inventory_category(SYS(LLFolderType::FT_TRASH), cb("remove_protected"));
        done("remove_protected");
        purge_descendents_of(SYS(LLFolderType::FT_TRASH), cb("purge_trash"));
        done("purge_trash");
        AISAPI::CopyLibraryCategory(d.lib_folders[0], d.user[0], false, ais_cb("copy_library_depth0"));
        done("copy_library_depth0");
        AISAPI::CopyLibraryCategory(d.lib_folders[3], d.user[0], true, ais_cb("copy_library_recursive"));
        done("copy_library_recursive");
        emit(c, r);
    }

    // ---- 10a: legacy capabilities on Second Life (no InventoryAPIv3) ------------------
    void s10a_legacy_caps_sl(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, true);
        // A library folder the legacy server refuses (f4: 403), a folder whose
        // reply is a 200-with-error (e0, three times), a folder the server
        // does not have (bad_folders), a folder the server refuses (f4).
        const LLUUID LIB4 = P(0xf4000000, 3), E0 = P(0xe0000000, 1), BAD = P(0xc0500000, 1), X4 = P(0xf4000000, 2);
        b.w.add_folder(LIB4, LIBROOT, LLFolderType::FT_NONE, 3, "Library refused", true);
        b.w.add_folder(E0, ROOT, LLFolderType::FT_NONE, 4, "Error twice");
        b.item(E0, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Error folder item");
        Folder& bad = b.w.add_folder(BAD, ROOT, LLFolderType::FT_NONE, 2, "Not at the server");
        bad.server = false;
        b.w.add_folder(X4, ROOT, LLFolderType::FT_NONE, 5, "Refused");
        std::string cache = scenario_dir(c, "s10a_legacy_caps_sl", true);
        begin(cache, 2, CAPS_LEGACY, b.w, "legacy_error=3\n");
        Run r("s10a_legacy_caps_sl", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        appearance::callAfterCOFFetch(nullary("cof"));
        r.settle(true, true);
        r.step("cof");
        startup::find_lost_items();
        r.settle(true, true);
        r.step("lost_and_found");
        // One batch: three inventory folders (403 for the refused one: split
        // in halves, down to the single folder) and two library folders
        // (one refused: its halves go to the inventory capability).
        gInventory.fetchDescendentsOf(b.ids.user[0]);
        gInventory.fetchDescendentsOf(X4);
        gInventory.fetchDescendentsOf(b.ids.user[1]);
        gInventory.fetchDescendentsOf(b.ids.lib_folders[0]);
        gInventory.fetchDescendentsOf(LIB4);
        r.settle(true, true);
        r.step("forbidden_split");
        gInventory.fetchDescendentsOf(E0);
        r.settle(true, true);
        r.step("error_200");
        gInventory.fetchDescendentsOf(BAD);
        r.settle(true, true);
        r.step("bad_folder");
        // Forced fetches on the legacy path: an unknown folder (requested),
        // then the same again (still in the forced set: ignored), then a
        // folder with a known version (not requested).
        bgf().scheduleFolderFetch(b.ids.user[2], true);
        r.settle(true, true);
        r.step("forced_unknown");
        cat(b.ids.user[2])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        bgf().scheduleFolderFetch(b.ids.user[2], true);
        r.settle(true, true);
        r.step("forced_again");
        bgf().scheduleFolderFetch(ROOT, true);
        r.settle(true, true);
        r.step("forced_known");
        emit(c, r);
    }

    // ---- 10b: legacy capabilities on another grid ---------------------------------------
    void s10b_legacy_caps_other_grid(Ctx& c)
    {
        Builder b(true);
        basic(b, 2, true);
        std::string cache = scenario_dir(c, "s10b_legacy_caps_other_grid", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        // Not Second Life; FSUseAis3Api off: the region's InventoryAPIv3 is
        // ignored (survey 3 section 4).
        vks_inv::config().production_grid = false;
        vks_inv::config().system_grid = false;
        vks_inv::config().grid_id = "other.grid.example";
        gSavedSettings.setBOOL("FSUseAis3Api", false);
        Run r("s10b_legacy_caps_other_grid", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        startup::find_lost_items();
        r.settle(true, true);
        r.step("lost_and_found");
        // Item fetches without FetchInventory2 / FetchLib2 off Second Life:
        // the UDP FetchInventory message.
        set_region(true, CAPS_NONE);
        // Items the legacy walk loaded (it fetched the two roots only).
        itm(b.ids.root1)->setComplete(false);
        itm(b.ids.lib_items[0])->setComplete(false);
        uuid_vec_t ids;
        ids.push_back(b.ids.root1);
        ids.push_back(b.ids.lib_items[0]);
        ItemsObserver* obs = new ItemsObserver(ids, "udp_items");
        obs->startFetch();
        if (obs->isFinished()) obs->done(); else gInventory.addObserver(obs);
        r.settle(true, false);
        r.step("udp_item_fetch");
        emit(c, r);
    }

    // ---- 12: region_change ----------------------------------------------------------
    void s12_region_change(Ctx& c)
    {
        Builder b(true);
        basic(b, 6, false);
        std::string cache = scenario_dir(c, "s12_region_change", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s12_region_change", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        vks_inv::set_startup_state(STATE_STARTED);
        // AIS: the root (depth 0) and the first content batch, then stop.
        r.until([&]() { return r.requests() >= 2 && LLCoprocedureManager::instance().count("AIS") == 0
                               && sched_state().fetch_count == 0; });
        r.step("ais_two_requests");
        // Teleport: no region.
        set_region(false, CAPS_NONE);
        r.frames(30);
        r.step("no_region");
        // A region whose capabilities have not arrived.
        set_region(true, CAPS_NONE, false);
        r.frames(30);
        r.step("caps_not_received");
        // A region with the legacy capabilities only: the legacy scheduler
        // takes the queue as it is; switch back to AIS right after its first
        // batch went out, with the legacy request in flight.
        set_region(true, CAPS_LEGACY);
        r.until([&]() { return sched_state().fetch_count > 0; });
        r.step("legacy_batch_sent");
        set_region(true, CAPS_ALL);
        // AIS never pumps the legacy request's reply: the background fetch's
        // count stays up.
        r.settle(true, false, 300.0, false);
        r.step("ais_with_legacy_reply_unpumped");
        // Legacy again: its reply is processed at last.
        set_region(true, CAPS_LEGACY);
        r.settle(true, false);
        r.step("legacy_again");
        // Capabilities received, neither AIS nor legacy: the legacy scheduler
        // drains the queue without sending anything.
        cat(b.ids.user[5])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        cat(b.ids.user_sub[10])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        bgf().scheduleFolderFetch(b.ids.user[5], false);
        bgf().scheduleFolderFetch(b.ids.user_sub[10], false);
        set_region(true, CAPS_NONE);
        r.frames(30);
        r.step("caps_without_inventory");
        // AIS back: the drained folders are not requested again by themselves.
        set_region(true, CAPS_ALL);
        r.frames(30);
        bgf().start();
        r.settle(true, false);
        r.step("ais_back_start");
        // A UI-style fetch of one of them.
        gInventory.fetchDescendentsOf(b.ids.user[5]);
        r.settle(true, false);
        r.step("ais_back_fetch");
        emit(c, r);
    }

    // ---- 13: Second Life fetch fallbacks (no AIS) ----------------------------------------
    void s13_sl_fetch_fallbacks(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, false);
        std::string cache = scenario_dir(c, "s13_sl_fetch_fallbacks", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s13_sl_fetch_fallbacks", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        const Ids& d = b.ids;
        set_region(true, CAPS_LEGACY);
        // The COF without AIS (callAfterCOFFetch), legacy capabilities. Login
        // marked the COF FETCH_RECURSIVE (180 s with AIS then); setting it back
        // to FETCH_NONE stops the timer without clearing its expiry, so the
        // observer's fetch() is refused until it expires.
        const LLUUID COF = SYS(LLFolderType::FT_CURRENT_OUTFIT);
        cat(COF)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        appearance::callAfterCOFFetch(nullary("cof_legacy"));
        r.settle(true, true);
        r.step("cof_legacy_timer_blocks");
        clock_advance(181.0);
        gInventory.fetchDescendentsOf(COF);
        r.settle(true, true);
        r.step("cof_legacy_after_timer");
        // (The walk's 180 s marks on the other folders have expired too.)
        // Neither InventoryAPIv3 nor FetchInventoryDescendents2: a folder
        // fetch is the UDP FetchInventoryDescendents.
        set_region(true, CAPS_NONE);
        cat(d.user[0])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        gInventory.fetchDescendentsOf(d.user[0]);
        r.settle(true, false);
        r.step("folder_udp");
        // FetchInventoryDescendents2 only: the legacy scheduler.
        set_region(true, CAPS_LEGACY);
        cat(d.user[1])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        gInventory.fetchDescendentsOf(d.user[1]);
        r.settle(true, true);
        r.step("folder_legacy");
        // The COF without AIS and without capabilities: UDP, and the
        // observer waits (no UDP reply here).
        cat(COF)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        clock_advance(11.0);   // past the 10 s guard the last fetch() set
        set_region(true, CAPS_NONE);
        appearance::callAfterCOFFetch(nullary("cof_udp"));
        r.settle(true, false);
        r.step("cof_udp");
        // An incomplete item, legacy capabilities: fetchFromServer posts
        // FetchInventory2 / FetchLib2 itself (foreground, with agent_id).
        set_region(true, CAPS_LEGACY);
        itm(d.obj1)->setComplete(false);
        itm(d.obj1)->fetchFromServer();
        r.settle(true, false);
        r.step("item_fetch_from_server");
        itm(d.lib_items[1])->setComplete(false);
        itm(d.lib_items[1])->fetchFromServer();
        r.settle(true, false);
        r.step("library_item_fetch_from_server");
        // The item observer without AIS: FetchInventory2 and FetchLib2
        // bodies with cap_name.
        itm(d.obj2)->setComplete(false);
        itm(d.lib_items[2])->setComplete(false);
        {
            uuid_vec_t ids;
            ids.push_back(d.obj2);
            ids.push_back(d.lib_items[2]);
            ItemsObserver* obs = new ItemsObserver(ids, "legacy_items");
            obs->startFetch();
            if (obs->isFinished()) obs->done(); else gInventory.addObserver(obs);
        }
        r.settle(true, false);
        r.step("item_observer_legacy");
        // The same on Second Life without capabilities: nothing is sent.
        set_region(true, CAPS_NONE);
        itm(d.note1)->setComplete(false);
        {
            uuid_vec_t ids;
            ids.push_back(d.note1);
            ItemsObserver* obs = new ItemsObserver(ids, "no_caps_items");
            obs->startFetch();
            if (obs->isFinished()) obs->done(); else gInventory.addObserver(obs);
        }
        r.settle(true, false);
        r.step("item_observer_no_caps");
        emit(c, r);
    }

    // ---- 14: Second Life write fallbacks (no AIS) ----------------------------------------
    void s14_sl_write_fallbacks(Ctx& c)
    {
        Builder b(true);
        basic(b, 3, false);
        std::string cache = scenario_dir(c, "s14_sl_write_fallbacks", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s14_sl_write_fallbacks", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        const Ids& d = b.ids;
        set_region(true, CAPS_LEGACY);
        auto done = [&](const std::string& name) { r.settle(true, true); r.step(name); };
        // CreateInventoryCategory (an HTTP capability, its own coroutine).
        const int fired = callbacks_fired();
        gInventory.createNewCategory(d.user[0], LLFolderType::FT_NONE, "Fallback folder", ais_cb("create_cap"));
        r.until([&]() { return callbacks_fired() > fired; });
        done("create_cap");
        set_region(true, CAPS_NONE);
        gInventory.createNewCategory(d.user[0], LLFolderType::FT_NONE, "No capability folder", ais_cb("create_none"));
        done("create_none");
        {
            LLPointer<LLViewerInventoryItem> copy = new LLViewerInventoryItem(itm(d.note1));
            copy->rename("Notecard renamed");
            copy->updateServer(false);
        }
        done("item_update_server");
        {
            LLPointer<LLViewerInventoryCategory> copy = new LLViewerInventoryCategory(cat(d.user[2]));
            copy->rename("Renamed via updateServer");
            copy->updateServer(false);
        }
        done("category_update_server");
        cat(d.outfitB)->changeType(LLFolderType::FT_NONE);
        done("change_type");
        {
            LLInventoryObject::const_object_list_t objs;
            objs.push_back(LLConstPointer<LLInventoryObject>(itm(d.shirt)));
            objs.push_back(LLConstPointer<LLInventoryObject>(cat(d.outfitA)));
            link_inventory_array(d.user[1], objs, cb("link_array"));
        }
        done("link_array");
        {
            LLSD updates;
            updates["name"] = "Renamed by update";
            update_inventory_item(d.items_in.at(d.user[0])[0], updates, cb("update_item_llsd"));
        }
        done("update_item_llsd");
        {
            LLPointer<LLViewerInventoryItem> copy = new LLViewerInventoryItem(itm(d.items_in.at(d.user[0])[1]));
            copy->setDescription("A new description");
            update_inventory_item(copy, cb("update_item_object"));
        }
        done("update_item_object");
        {
            LLSD updates;
            updates["name"] = "Renamed folder";
            update_inventory_category(d.user[1], updates, cb("update_category"));
        }
        done("update_category");
        remove_inventory_item(d.obj2, cb("remove_item"));
        done("remove_item");
        remove_inventory_category(d.user_leaf[0], cb("remove_category_with_item"));
        done("remove_category_with_item");
        const LLUUID empty = P(0xc0d00000, 1);
        add_model_only_folder(empty, d.user[0], "Empty folder");
        remove_inventory_category(empty, cb("remove_empty_category"));
        done("remove_empty_category");
        purge_descendents_of(SYS(LLFolderType::FT_TRASH), cb("purge_trash"));
        done("purge_trash");
        {
            LLSD contents = LLSD::emptyArray();
            contents.append(link_llsd(d.shirt, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Shirt"));
            contents.append(link_llsd(d.jacket, LLAssetType::AT_LINK, LLInventoryType::IT_WEARABLE, "Jacket"));
            slam_inventory_folder(SYS(LLFolderType::FT_CURRENT_OUTFIT), contents, cb("slam_cof"));
        }
        done("slam_cof");
        emit(c, r);
    }


    // ---- 15: the subset batch size (BatchSizeAIS3 = 20) -------------------------
    // Three folders with 19, 20 and 21 child folders. Each refuses depth 50
    // (f3), so the scheduler fetches it at depth 0 and then its children as
    // subset batches: 19 -> one batch; 20 -> one full batch (the folder is
    // queued again and finds nothing left); 21 -> 20 + 1.
    void s15_batch_boundaries(Ctx& c)
    {
        Builder b(true);
        basic(b, 1, false, 1);
        const LLUUID parent = b.ids.user[0];
        std::vector<LLUUID> tops;
        std::map<LLUUID, std::vector<LLUUID> > kids;
        for (int n : { 19, 20, 21 })
        {
            const LLUUID top = P(0xf3000000, (U64)n);
            b.w.add_folder(top, parent, LLFolderType::FT_NONE, 3, "Batch " + std::to_string(n));
            tops.push_back(top);
            for (int k = 1; k <= n; ++k)
            {
                const LLUUID child = b.folder(top, "Batch " + std::to_string(n) + "." + std::to_string(k));
                kids[top].push_back(child);
                b.item(child, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Batch item " + std::to_string(n) + "." + std::to_string(k));
            }
        }
        std::string cache = scenario_dir(c, "s15_batch_boundaries", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s15_batch_boundaries", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        for (const LLUUID& top : tops)
        {
            // As after a server-side change: the folder and its children
            // unknown again.
            cat(top)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
            for (const LLUUID& k : kids[top]) cat(k)->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
            bgf().start(top);
            r.settle(true, true);
            r.step("children_" + std::to_string(kids[top].size()));
        }
        emit(c, r);
    }

    // ---- 16: the depth cut (depth 50) ---------------------------------------------
    // Chains of folders. Under the root (reached through the root's subset
    // batch, whose children are parsed at depth 49): deepest folder at 48,
    // 49 and 50 levels below the chain's top. Under the library root
    // (fetched directly at depth 50): 49, 50 and 51 levels below the
    // library root. A folder parsed at depth -1 is created without a
    // version and fetched again on its own.
    void s16_depth_boundaries(Ctx& c)
    {
        Builder b(true);
        basic(b, 0, false, 0);
        for (int levels : { 48, 49, 50 })
        {
            LLUUID parent = b.folder(ROOT, "Inventory chain " + std::to_string(levels));
            for (int k = 1; k <= levels; ++k)
            {
                parent = b.folder(parent, "Inventory chain " + std::to_string(levels) + " level " + std::to_string(k));
            }
            b.item(parent, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Bottom of inventory chain " + std::to_string(levels));
        }
        for (int levels : { 49, 50, 51 })
        {
            LLUUID parent = LIBROOT;
            for (int k = 1; k <= levels; ++k)
            {
                parent = b.libfolder(parent, "Library chain " + std::to_string(levels) + " level " + std::to_string(k));
            }
            b.libitem(parent, "Bottom of library chain " + std::to_string(levels));
        }
        std::string cache = scenario_dir(c, "s16_depth_boundaries", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        Run r("s16_depth_boundaries", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        emit(c, r);
    }

    // ---- 17: InventoryAPIv3 without LibraryAPIv3; disconnected ---------------------
    // Every library command finds no capability and calls back with null
    // synchronously, inside the scheduler's own loop (the re-entrancy rule:
    // only failure branches, which queue at the back). Then gDisconnected:
    // the scheduler stops, an AIS command calls back null without a request.
    void s17_no_library_cap_disconnected(Ctx& c)
    {
        Builder b(true);
        basic(b, 2, false, 2);
        std::string cache = scenario_dir(c, "s17_no_library_cap_disconnected", true);
        begin(cache, 2, CAPS_ALL, b.w, "");
        vks_inv::config().capabilities.erase("LibraryAPIv3");
        Run r("s17_no_library_cap_disconnected", &b.w);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        cat(b.ids.user[0])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
        clock_advance(181.0);   // past the walk's 180 s mark: fetch() may act
        gDisconnected = true;
        gInventory.fetchDescendentsOf(b.ids.user[0]);
        LLSD updates;
        updates["name"] = "Renamed while disconnected";
        update_inventory_item(b.ids.note1, updates, cb("disconnected_update"));
        r.frames(10);
        r.settle(true, false);
        r.step("disconnected");
        gDisconnected = false;
        r.settle(true, true);
        r.step("reconnected");
        emit(c, r);
    }

    // ---- c04: the AIS concurrency limit (PoolSizeAIS 20 -> 19 fetches) ------------------
    // 18, 19 and 20 folders queued at once while the mock holds every reply
    // for 1.5 s: the first frame sends min(n, 19), the next sends nothing.
    void c04_ais_limit(Ctx& c)
    {
        Builder b(true);
        basic(b, 10, false, 1);
        std::string cache = scenario_dir(c, "c04_ais_limit", true);
        begin(cache, 20, CAPS_ALL, b.w, "");
        Run r("c04_ais_limit", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        mock_mode("delay_ms=1500\n");
        r.step("held_replies");
        const std::vector<LLUUID>& f = b.ids.user_sub;   // 20 folders
        for (size_t n : { (size_t)18, (size_t)19, (size_t)20 })
        {
            for (size_t i = 0; i < n; ++i)
            {
                cat(f[i])->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                bgf().scheduleFolderFetch(f[i]);
            }
            r.frame_note("queued_" + std::to_string(n) + "_first");
            r.frame_note("queued_" + std::to_string(n) + "_second");
            r.settle(true, true);
            r.step("queued_" + std::to_string(n));
        }
        emit(c, r);
    }

    // ---- c11: the legacy limits: batch 10, in-flight threshold 12, 16 handlers -------------
    void c11_legacy_limits(Ctx& c)
    {
        Builder b(true);
        basic(b, 50, false, 8);
        std::string cache = scenario_dir(c, "c11_legacy_limits", true);
        begin(cache, 20, CAPS_LEGACY, b.w, "");
        Run r("c11_legacy_limits", &b.w, Run::PHASED);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);   // the legacy walk does not descend: every user folder stays unknown
        const Ids& d = b.ids;
        std::vector<LLUUID> pool;   // unknown inventory folders, in a fixed order
        for (const LLUUID& f : d.user) pool.push_back(f);
        for (const LLUUID& f : d.user_sub) pool.push_back(f);
        for (const LLUUID& f : d.user_leaf) pool.push_back(f);
        size_t next = 0;
        auto queue_folders = [&](size_t n)
        {
            for (size_t i = 0; i < n; ++i) gInventory.fetchDescendentsOf(pool[next++]);
        };
        mock_mode("delay_ms=3000\n");
        r.step("held_replies");
        // Batch size 10: 9, 10 and 11 folders queued at once.
        for (size_t n : { (size_t)9, (size_t)10, (size_t)11 })
        {
            queue_folders(n);
            r.frame_note("batch_" + std::to_string(n) + "_first");
            r.frame_note("batch_" + std::to_string(n) + "_second");
            r.settle(true, true);
            r.step("batch_" + std::to_string(n));
        }
        // The threshold: one POST per frame until more than 12 are out.
        queue_folders(140);
        for (int k = 1; k <= 15; ++k) r.frame_note("threshold_" + std::string(k < 10 ? "0" : "") + std::to_string(k));
        r.settle(true, true);
        r.step("threshold");
        // Library items into the model (the library sub-folders' parents).
        for (size_t i = 0; i < 8; ++i) gInventory.fetchDescendentsOf(d.lib_folders[i * 3]);
        r.settle(true, true);
        r.step("library_loaded");
        // 16: four POSTs per frame (inventory and library folders, inventory
        // and library items) while 12 or fewer are out.
        std::vector<LLUUID> inv_items, lib_items;
        for (const auto& kv : d.items_in)
        {
            for (const LLUUID& id : kv.second) if (gInventory.getItem(id) && inv_items.size() < 30) inv_items.push_back(id);
        }
        for (const LLUUID& id : d.lib_items) if (gInventory.getItem(id) && lib_items.size() < 30) lib_items.push_back(id);
        std::vector<LLUUID> lib_sub;
        for (size_t i = 0; i < d.lib_folders.size(); ++i) if (i % 3 != 0) lib_sub.push_back(d.lib_folders[i]);
        size_t ni = 0, nl = 0, nf = 0;
        for (int k = 1; k <= 6; ++k)
        {
            gInventory.fetchDescendentsOf(pool[next++]);
            gInventory.fetchDescendentsOf(pool[next++]);
            gInventory.fetchDescendentsOf(lib_sub[nf++]);
            gInventory.fetchDescendentsOf(lib_sub[nf++]);
            for (int j = 0; j < 3; ++j)
            {
                if (ni < inv_items.size()) { itm(inv_items[ni])->setComplete(false); bgf().scheduleItemFetch(inv_items[ni]); ++ni; }
                if (nl < lib_items.size()) { itm(lib_items[nl])->setComplete(false); bgf().scheduleItemFetch(lib_items[nl]); ++nl; }
            }
            r.frame_note("four_posts_" + std::to_string(k));
        }
        r.note("items queued inventory=" + std::to_string(ni) + " library=" + std::to_string(nl));
        r.settle(true, true);
        r.step("sixteen");
        emit(c, r);
    }

    // ---- concurrency ------------------------------------------------------------------
    void c01_login_cold(Ctx& c)
    {
        Builder b(true);
        basic(b, 6, false);
        std::string cache = scenario_dir(c, "c01_login_cold", true);
        begin(cache, 20, CAPS_ALL, b.w, "delay_ms=40\n");
        Run r("c01_login_cold", &b.w, Run::CONC);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        emit(c, r);
    }

    void c02_saturate(Ctx& c)
    {
        Builder b(true);
        basic(b, 40, false, 1);
        std::string cache = scenario_dir(c, "c02_saturate", true);
        // The first content batch fails: its 20 folders are queued one by one.
        begin(cache, 20, CAPS_ALL, b.w, "fail_subset=1\ndelay_ms=300\n");
        Run r("c02_saturate", &b.w, Run::CONC);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        emit(c, r);
    }

    void c03_large(Ctx& c)
    {
        Builder b(true);
        large(b);
        std::string cache = scenario_dir(c, "c03_large", true);
        begin(cache, 20, CAPS_ALL, b.w, "");
        Run r("c03_large", &b.w, Run::CONC, true);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r, 900.0);
        emit(c, r);
    }

    void c10_legacy(Ctx& c)
    {
        Builder b(true);
        basic(b, 30, false, 8);
        std::string cache = scenario_dir(c, "c10_legacy", true);
        begin(cache, 20, CAPS_LEGACY, b.w, "delay_ms=400\n");
        Run r("c10_legacy", &b.w, Run::CONC);
        world_line(r, b.w);
        login(r, b.w.login_response(), b.w.root);
        walk(r);
        // Many unknown folders at once, inventory and library, and incomplete
        // items of both owners.
        const Ids& d = b.ids;
        for (const LLUUID& f : d.user) gInventory.fetchDescendentsOf(f);
        for (const LLUUID& f : d.user_sub) gInventory.fetchDescendentsOf(f);
        for (const LLUUID& f : d.lib_folders) gInventory.fetchDescendentsOf(f);
        // The items the walk loaded (the root's and the library root's).
        for (const LLUUID& id : { d.root1, d.root2, d.lib_items[0], d.lib_items[1] })
        {
            if (LLViewerInventoryItem* it = gInventory.getItem(id)) { it->setComplete(false); bgf().scheduleItemFetch(id); }
        }
        r.settle(true, true);
        r.step("burst");
        emit(c, r);
    }

    typedef void (*scenario_fn)(Ctx&);
    struct Entry { const char* id; const char* title; scenario_fn fn; };
    const Entry ENTRIES[] = {
        { "s01_login_cold", "cold login: skeleton without cache, COF and base outfit, the full AIS walk, orphans, the logout cache", s01_login_cold },
        { "s02a_login_warm_full", "warm login from s01's cache: no folder requested", s02a_login_warm_full },
        { "s02b_login_warm_partial", "warm login, two folders changed on the server: only they are requested", s02b_login_warm_partial },
        { "s03_large", "5,000 folders, 60,000 items, 300 under the root, a 60-deep chain (digests)", s03_large },
        { "s04_forbidden", "403 on depth 50 and on depth 0: the fallback chain, version 0, the notification", s04_forbidden },
        { "s05_transient_5xx", "503 twice then 200; 503 every time at depth 50; a PATCH answered 503 once", s05_transient_5xx },
        { "s06_outage", "the first four requests fail: version 0 for the session, the 60 s back-off", s06_outage },
        { "s07_forced", "forced folder fetches during a normal one, the forced set, forced items", s07_forced },
        { "s08_items", "incomplete items, a link, a library item, an unknown id, the item observer", s08_items },
        { "s09_writes", "every AIS write: create, links, slam, updates, removes (410 too), purge, library copy", s09_writes },
        { "s10a_legacy_caps_sl", "Second Life without InventoryAPIv3: the legacy capabilities, 403 split, 200-with-error, bad_folders", s10a_legacy_caps_sl },
        { "s10b_legacy_caps_other_grid", "another grid with FSUseAis3Api off: legacy capabilities, UDP item fetch", s10b_legacy_caps_other_grid },
        { "s12_region_change", "capabilities lost, delayed, legacy-only and back during the walk", s12_region_change },
        { "s13_sl_fetch_fallbacks", "Second Life fetch fallbacks without AIS: UDP folder fetch, legacy caps, COF, items", s13_sl_fetch_fallbacks },
        { "s14_sl_write_fallbacks", "Second Life write fallbacks without AIS: UDP messages, CreateInventoryCategory", s14_sl_write_fallbacks },
        { "s15_batch_boundaries", "subset batches of 19, 20 and 21 children (BatchSizeAIS3 20)", s15_batch_boundaries },
        { "s16_depth_boundaries", "the depth-50 cut: chains 48/49/50 deep through a subset, 49/50/51 through the library root", s16_depth_boundaries },
        { "s17_no_library_cap_disconnected", "no LibraryAPIv3: synchronous null callbacks inside the scheduler; gDisconnected", s17_no_library_cap_disconnected },
        { "c01_login_cold", "cold login on a 20-coroutine pool: request multiset and bounds", c01_login_cold },
        { "c02_saturate", "a failed content batch queues 20 folders: 19 in flight", c02_saturate },
        { "c03_large", "the large inventory on a 20-coroutine pool (digests)", c03_large },
        { "c04_ais_limit", "18, 19 and 20 folders queued at once: 19 AIS fetches in flight", c04_ais_limit },
        { "c11_legacy_limits", "legacy batch 9/10/11, the in-flight threshold 12, 16 handlers", c11_legacy_limits },
        { "c10_legacy", "the legacy scheduler with many folders and items: up to 16 handlers", c10_legacy },
    };
}

const std::vector<ScenarioInfo>& scenarios()
{
    static std::vector<ScenarioInfo> list;
    if (list.empty())
    {
        for (const Entry& e : ENTRIES) list.push_back(ScenarioInfo{ e.id, e.title });
    }
    return list;
}

void run_scenarios(const std::string& work_dir, const std::set<std::string>& only, const sink_t& sink)
{
    Ctx c;
    c.work = work_dir;
    for (const ScenarioInfo& s : scenarios()) c.info[s.id] = s;
    std::set<std::string> want(only);
    // s02 needs s01's cache.
    if (want.count("s02a_login_warm_full") || want.count("s02b_login_warm_partial")) want.insert("s01_login_cold");
    sink_t filtered = [&](const ScenarioInfo& info, const Case& k, const std::string& responses)
    {
        if (only.empty() || only.count(k.id)) sink(info, k, responses);
    };
    c.sink = &filtered;
    for (const Entry& e : ENTRIES)
    {
        if (!want.empty() && !want.count(e.id)) continue;
        fprintf(stderr, "[r3b] scenario %s\n", e.id);
        try
        {
            e.fn(c);
        }
        catch (const std::exception& ex)
        {
            Case k;
            k.id = e.id;
            k.lines.push_back(std::string("FAILURE exception: ") + ex.what());
            filtered(c.info[e.id], k, std::string());
        }
    }
}
}
