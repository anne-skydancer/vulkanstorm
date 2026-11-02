/**
 * @file llinventorymodel_parity_test.cpp
 * @brief The inventory model and its cache against the record made before
 *        the port (vkstorm-rust rung seven, recorder R2).
 *
 * The real llinventorymodel.cpp, llviewerinventory.cpp and
 * llinventoryobserver.cpp run against the stubbed viewer environment of
 * llinventorymodel_parity_stubs.cpp (every outward call journaled), on the
 * real llinventory, llmessage (real template), llcommon (gzip) and LLDir.
 *
 * Corpora (survey 2 section 8.2; survey 4 section 7 set 8), each a text
 * fixture of "== <case>" sections under tests/fixtures/inventory/:
 *   cache/c1_cache_files.txt   C1: cache files written by cache(); the
 *                              uncompressed bytes in cache/c1/<state>.inv.llsd;
 *                              decode (loadFromFile) and re-encode (saveToFile)
 *   cache/c1_realistic.txt     C1: the realistic 100k-item / 8k-folder set
 *                              (digests; bytes in cache/c1/realistic_100k.inv.llsd.gz)
 *   cache/c2_load.txt          C2: login load and discard cases; inputs in cache/c2/<case>/
 *   cache/c3_mutations.txt     C3: mutation sequences: every notification
 *                              batch, outbound message and outward call, and
 *                              the model after each step
 *   cache/c4_validation.txt    C4: validate() over constructed model states
 *   cache/c2_logs.txt          <Vulkanstorm> R2: the log lines of the login
 *                              load (llinventorymodel_parity_logs.h) </Vulkanstorm>
 *   cache/library_real.txt     the public Linden Library cache (read from
 *                              indra/llinventory/tests/fixtures/library/)
 *   msgs/set8_handlers.txt     set 8: the inventory UDP message handlers
 *
 * Environment:
 *   VKS_INVMODEL_DUMP=<dir>    write the fixtures under <dir> instead of
 *                              comparing (tools/inventory-fixtures/cache in
 *                              vkstorm-core-rust copies them)
 *   VKS_INVMODEL_COMMIT=<hash> recorded in the fixture headers when dumping
 *   VKS_INVMODEL_BENCH=<runs>  times the cache and validation paths
 *                              (100k realistic, 1M stress) and prints a table;
 *                              <Vulkanstorm> R2: also a 50k random-id world,
 *                              and loadFromFile/saveToFile against their C++
 *                              before the port (llinventorymodel_parity_frozen.cpp)
 *                              on the Library, realistic and random caches.
 *                              VKS_INVMODEL_BENCH_SKIP_REALISTIC=1 skips the
 *                              100k realistic world's model rows </Vulkanstorm>
 *   VKS_INVMODEL_USER_CACHE=<path to an .inv.llsd.gz>  local only: the
 *                              decode/re-encode identity check and timings of
 *                              a real cache; prints counts and timings only
 *   VKS_INVMODEL_TRACE=1       progress on stderr (to locate a crash or hang)
 *   VKS_INVMODEL_WORK_ROOT=<dir>  where the per-process work directory goes
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "../llinventorymodel.h"
#include "../llviewerinventory.h"
#include "../llinventoryobserver.h"
#include "../llinventorybridge.h"
#include "../llinventoryfunctions.h"

#include "llinventorymodel_parity_harness.h"
#include "llinventorymodel_parity_frozen.h" // <Vulkanstorm/> R2 bench baseline

#include "llassetstorage.h"
#include "lldir.h"
#include "lldispatcher.h"
#include "llfile.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llsys.h"
#include "lltimer.h"
#include "message.h"
#include "llwindow.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <sstream>
#if LL_WINDOWS
#include <process.h>
#define vks_getpid _getpid
#else
#include <unistd.h>
#define vks_getpid getpid
#endif

#ifndef VKS_INVMODEL_FIXTURE_DIR
#error VKS_INVMODEL_FIXTURE_DIR must name indra/newview/tests/fixtures/inventory
#endif

// The frozen copies of the GenericMessage handlers (llinventorymodel_parity_generic.cpp).
namespace vks_inv_frozen
{
    void process_generic_message(LLMessageSystem* msg, void**);
    void process_large_generic_message(LLMessageSystem* msg, void**);
}

namespace
{
    using namespace vks_inv;
    namespace fs = std::filesystem;

    // ---- fixed identities ------------------------------------------------------
    const LLUUID AGENT = pid(0xa9e00000, 1);
    const LLUUID SESSION = pid(0x5e550000, 1);
    const LLUUID LIB_OWNER = pid(0x11b00000, 1);
    const LLUUID GROUP = pid(0x96000000, 1);
    const LLUUID CREATOR = pid(0xc4000000, 1);
    const U32 TAG_FOLDER = 0xf0000000;
    const U32 TAG_ITEM = 0xa0000000;
    const U32 TAG_ASSET = 0xa5000000;
    const U32 TAG_LIBF = 0xf1000000;
    const U32 TAG_LIBI = 0xa1000000;
    const U32 TAG_THUMB = 0x7b000000;
    const U32 TAG_TXN = 0x7a000000;

    // ---- environment -------------------------------------------------------------
    struct Env
    {
        std::string fixtures;   // committed fixtures (replay)
        std::string out;        // dump directory ("" = replay)
        std::string work;       // scratch
        std::string library_dir;
        std::string commit;
        std::string platform;
        bool dump() const { return !out.empty(); }
        std::set<std::string> touched;  // fixture paths read or written (relative)
        std::set<int> ran;              // tests that ran
    };

    std::string getenv_str(const char* name)
    {
        const char* v = getenv(name);
        return v ? std::string(v) : std::string();
    }

    Env& env()
    {
        static Env* e = NULL;
        if (!e)
        {
            e = new Env;
            e->fixtures = VKS_INVMODEL_FIXTURE_DIR;
            e->out = getenv_str("VKS_INVMODEL_DUMP");
            // A work directory per process: concurrent runs never share files.
            // VKS_INVMODEL_WORK_ROOT moves it (for example into a folder that
            // scanners and indexers leave alone; see the README).
            std::string work_root = getenv_str("VKS_INVMODEL_WORK_ROOT");
            if (work_root.empty()) work_root = VKS_INVMODEL_WORK_DIR;
            e->work = work_root + "/" + std::to_string((long)vks_getpid());
            e->library_dir = VKS_INVMODEL_LIBRARY_DIR;
            e->commit = getenv_str("VKS_INVMODEL_COMMIT");
            if (e->commit.empty()) e->commit = "unknown";
#if LL_WINDOWS
            e->platform = "windows-msvc";
#elif defined(__clang__)
            e->platform = "linux-clang";
#else
            e->platform = "linux-gcc" + std::to_string(__GNUC__);
#endif
            fs::remove_all(e->work);
            fs::create_directories(e->work);
            init_environment(VKS_INVMODEL_TEMPLATE_FILE, e->work);
            LLInventoryModel::registerCallbacks(gMessageSystem);
            gMessageSystem->setHandlerFunc("GenericMessage", vks_inv_frozen::process_generic_message);
            gMessageSystem->setHandlerFunc("LargeGenericMessage", vks_inv_frozen::process_large_generic_message);
            // The stub translation table: a few dictionary entries localise to
            // a visibly different text so the localisation calls are recorded.
            // The dictionary is built once, at its first use, which is here.
            config().translations["New Folder"] = "Nouveau dossier";
            config().translations["New Script"] = "Nouveau script";
            config().translations["New Note"] = "Nouvelle note";
            config().translations["Contents"] = "Contenu";
            config().translations["New Shirt"] = "Nouvelle chemise";
            {
                LLPointer<LLViewerInventoryItem> warm = new LLViewerInventoryItem;
                warm->unpackMessage(LLSD::emptyMap());
            }
            reset_config();
            gAgentID = AGENT;
            gAgentSessionID = SESSION;
        }
        return *e;
    }

    std::string join(const std::string& a, const std::string& b) { return a + "/" + b; }

    // The C++ standard library this test runs on, as the fixture trees name it.
    const char* stdlib_name()
    {
#if LL_WINDOWS
        return "msvc";
#else
        return "libstdcxx";
#endif
    }
    const char* other_stdlib_name()
    {
#if LL_WINDOWS
        return "libstdcxx";
#else
        return "msvc";
#endif
    }

    // Where a fixture lives. Since upstream 28fee038e0 the model's maps are
    // std::unordered_map, so the order the model walks them in (the cache
    // file's records, the child vectors, the journals) is the standard
    // library's: those records are kept per library, in <area>/<library>/.
    // Shared by both: the C2 inputs (written in id order) and the QA-mode
    // records that already name their library.
    std::string platform_rel(const std::string& rel)
    {
        if (rel.rfind("cache/c2/", 0) == 0) return rel;
        if (rel.rfind("cache/c4_validation_qa.", 0) == 0) return rel;
        if (rel == "cache/c2_logs.txt") return rel; // <Vulkanstorm/> R2: no unordered walk
        size_t slash = rel.find('/');
        if (slash == std::string::npos) return rel;
        return rel.substr(0, slash) + "/" + stdlib_name() + rel.substr(slash);
    }

    // VKS_INVMODEL_TRACE=1: progress on stderr (for locating a crash).
    void trace(const std::string& what)
    {
        static bool on = getenv("VKS_INVMODEL_TRACE") != NULL;
        if (on) { fprintf(stderr, "[r2] %s\n", what.c_str()); fflush(stderr); }
    }

    std::string header(const std::string& title, const std::string& seed)
    {
        std::string h;
        h += "# vkstorm inventory model oracle (recorder R2): " + title + "\n";
        h += "# generator: INTEGRATION_TEST_llinventorymodel_parity with VKS_INVMODEL_DUMP (indra/newview/tests/llinventorymodel_parity_test.cpp)\n";
        h += "# viewer commit: " + env().commit + "\n";
        h += "# platform: " + env().platform + "\n";
        h += "# seed: " + seed + "\n";
        h += "# format: tests/fixtures/inventory/cache/README.md; lines starting with '#' are not compared\n";
        return h;
    }

    // Writes (dump) or compares (replay) a text corpus.
    void corpus(const std::string& logical_rel, const std::string& title, const std::string& seed, const Cases& actual)
    {
        const std::string rel = platform_rel(logical_rel);
        env().touched.insert(rel);
        if (env().dump())
        {
            std::string path = join(env().out, rel);
            fs::create_directories(fs::path(path).parent_path());
            tut::ensure("write " + path, write_file(path, format_cases(header(title, seed), actual)));
            return;
        }
        std::string path = join(env().fixtures, rel);
        tut::ensure("fixture missing: " + path, file_exists(path));
        Cases expected = parse_cases(read_file(path));
        std::string diff = compare_cases(expected, actual);
        tut::ensure(rel + ": differences from the recorded C++\n" + diff, diff.empty());
    }

    // Writes (dump) or compares (replay) a binary fixture.
    void binary(const std::string& logical_rel, const std::string& bytes)
    {
        const std::string rel = platform_rel(logical_rel);
        env().touched.insert(rel);
        if (env().dump())
        {
            std::string path = join(env().out, rel);
            fs::create_directories(fs::path(path).parent_path());
            tut::ensure("write " + path, write_file(path, bytes));
            return;
        }
        std::string path = join(env().fixtures, rel);
        tut::ensure("fixture missing: " + path, file_exists(path));
        std::string expected = read_file(path);
        if (expected != bytes)
        {
            size_t i = 0;
            while (i < expected.size() && i < bytes.size() && expected[i] == bytes[i]) ++i;
            tut::ensure(llformat("%s: bytes differ at offset %u (expected size %u, actual %u)",
                                 rel.c_str(), (U32)i, (U32)expected.size(), (U32)bytes.size()), false);
        }
    }

    // ---- building model states ----------------------------------------------------
    struct Folder
    {
        LLUUID id;
        LLUUID parent;
        std::string name;
        S32 type;
        S32 version;
    };

    LLSD skeleton(const std::vector<Folder>& folders)
    {
        // As the login response's inventory-skeleton (XML-RPC): ids as strings.
        LLSD arr = LLSD::emptyArray();
        for (const Folder& f : folders)
        {
            LLSD e;
            e["folder_id"] = f.id.asString();
            e["parent_id"] = f.parent.asString();
            e["name"] = f.name;
            e["type_default"] = f.type;
            e["version"] = f.version;
            arr.append(e);
        }
        return arr;
    }

    LLPermissions perms(const LLUUID& owner, PermissionMask base, PermissionMask next = PERM_ALL)
    {
        LLPermissions p;
        p.init(CREATOR, owner, owner, LLUUID::null);
        p.initMasks(base, base, PERM_NONE, PERM_NONE, next);
        return p;
    }

    LLPointer<LLViewerInventoryItem> item(const LLUUID& id, const LLUUID& parent, LLAssetType::EType type,
                                          LLInventoryType::EType inv, const std::string& name,
                                          const LLUUID& asset, const LLUUID& owner = AGENT,
                                          PermissionMask base = PERM_ALL, U32 flags = 0,
                                          S32 created = 1700000000, const std::string& desc = "")
    {
        return new LLViewerInventoryItem(id, parent, perms(owner, base), asset, type, inv, name, desc,
                                         LLSaleInfo::DEFAULT, flags, created);
    }

    LLPointer<LLViewerInventoryItem> link(const LLUUID& id, const LLUUID& parent, const LLUUID& target,
                                          const std::string& name, bool folder_link = false,
                                          LLInventoryType::EType inv = LLInventoryType::IT_OBJECT)
    {
        return item(id, parent, folder_link ? LLAssetType::AT_LINK_FOLDER : LLAssetType::AT_LINK,
                    folder_link ? LLInventoryType::IT_CATEGORY : inv, name, target);
    }

    // A world: the server's view of an agent inventory and a library.
    struct World
    {
        LLUUID agent_root;
        LLUUID lib_root;
        std::vector<Folder> agent;
        std::vector<Folder> library;
        std::vector<LLPointer<LLViewerInventoryItem> > items; // agent and library
        std::map<LLUUID, S32> count_delta;  // server count = loaded + delta (an "incomplete" folder)
        std::set<LLUUID> unknown_version;   // left at -1 after populating
        std::map<LLUUID, LLUUID> folder_thumbnails; // set after login, as AIS does
        std::set<LLUUID> folder_favorites;
    };

    LLUUID F(U64 n) { return pid(TAG_FOLDER, n); }
    LLUUID I(U64 n) { return pid(TAG_ITEM, n); }
    LLUUID A(U64 n) { return pid(TAG_ASSET, n); }
    LLUUID LF(U64 n) { return pid(TAG_LIBF, n); }
    LLUUID LI(U64 n) { return pid(TAG_LIBI, n); }

    // Standard system folders under the agent root (ids F(1)..), as SL sends.
    // F(1) root; F(2..) the system folders; F(40..) user folders.
    enum StdFolder
    {
        SF_ROOT = 1, SF_TEXTURES, SF_SOUNDS, SF_CALLING, SF_LANDMARKS, SF_CLOTHING, SF_OBJECTS,
        SF_NOTECARDS, SF_SCRIPTS, SF_BODYPARTS, SF_TRASH, SF_PHOTOS, SF_LOSTFOUND, SF_ANIMATIONS,
        SF_GESTURES, SF_FAVORITES, SF_COF, SF_MYOUTFITS, SF_INBOX, SF_SETTINGS, SF_MATERIALS,
        SF_MARKETPLACE
    };

    World standard_world()
    {
        World w;
        w.agent_root = F(SF_ROOT);
        w.lib_root = LF(1);
        auto add = [&](U64 n, U64 parent, const char* name, S32 type, S32 version)
        {
            w.agent.push_back({ F(n), parent ? F(parent) : LLUUID::null, name, type, version });
        };
        add(SF_ROOT, 0, "My Inventory", LLFolderType::FT_ROOT_INVENTORY, 40);
        add(SF_TEXTURES, SF_ROOT, "Textures", LLFolderType::FT_TEXTURE, 3);
        add(SF_SOUNDS, SF_ROOT, "Sounds", LLFolderType::FT_SOUND, 1);
        add(SF_CALLING, SF_ROOT, "Calling Cards", LLFolderType::FT_CALLINGCARD, 2);
        add(SF_LANDMARKS, SF_ROOT, "Landmarks", LLFolderType::FT_LANDMARK, 5);
        add(SF_CLOTHING, SF_ROOT, "Clothing", LLFolderType::FT_CLOTHING, 4);
        add(SF_OBJECTS, SF_ROOT, "Objects", LLFolderType::FT_OBJECT, 12);
        add(SF_NOTECARDS, SF_ROOT, "Notecards", LLFolderType::FT_NOTECARD, 2);
        add(SF_SCRIPTS, SF_ROOT, "Scripts", LLFolderType::FT_LSL_TEXT, 2);
        add(SF_BODYPARTS, SF_ROOT, "Body Parts", LLFolderType::FT_BODYPART, 6);
        add(SF_TRASH, SF_ROOT, "Trash", LLFolderType::FT_TRASH, 9);
        add(SF_PHOTOS, SF_ROOT, "Photo Album", LLFolderType::FT_SNAPSHOT_CATEGORY, 1);
        add(SF_LOSTFOUND, SF_ROOT, "Lost And Found", LLFolderType::FT_LOST_AND_FOUND, 1);
        add(SF_ANIMATIONS, SF_ROOT, "Animations", LLFolderType::FT_ANIMATION, 1);
        add(SF_GESTURES, SF_ROOT, "Gestures", LLFolderType::FT_GESTURE, 2);
        add(SF_FAVORITES, SF_ROOT, "Favorites", LLFolderType::FT_FAVORITE, 3);
        add(SF_COF, SF_ROOT, "Current Outfit", LLFolderType::FT_CURRENT_OUTFIT, 30);
        add(SF_MYOUTFITS, SF_ROOT, "My Outfits", LLFolderType::FT_MY_OUTFITS, 4);
        add(SF_INBOX, SF_ROOT, "Received Items", LLFolderType::FT_INBOX, 2);
        add(SF_SETTINGS, SF_ROOT, "Settings", LLFolderType::FT_SETTINGS, 1);
        add(SF_MATERIALS, SF_ROOT, "Materials", LLFolderType::FT_MATERIAL, 1);
        add(40, SF_MYOUTFITS, "Outfit A", LLFolderType::FT_OUTFIT, 3);
        add(41, SF_MYOUTFITS, "Outfit B", LLFolderType::FT_OUTFIT, 2);
        add(42, SF_OBJECTS, "Furniture", LLFolderType::FT_NONE, 7);
        add(43, 42, "Chairs", LLFolderType::FT_NONE, 2);
        add(44, SF_TRASH, "Old stuff", LLFolderType::FT_NONE, 3);
        add(45, 44, "Older stuff", LLFolderType::FT_NONE, 1);

        // Folder thumbnails and favourites (they reach the model through AIS,
        // are saved in the cache and copied back for folders whose version
        // matches).
        w.folder_thumbnails[F(42)] = pid(TAG_THUMB, 0x42);
        w.folder_thumbnails[F(SF_LANDMARKS)] = pid(TAG_THUMB, 0x05);
        w.folder_favorites.insert(F(42));
        w.folder_favorites.insert(F(40));
        w.folder_thumbnails[LF(3)] = pid(TAG_THUMB, 0x103);
        w.folder_favorites.insert(LF(4));
        w.library.push_back({ LF(1), LLUUID::null, "Library", LLFolderType::FT_ROOT_INVENTORY, 11 });
        w.library.push_back({ LF(2), LF(1), "Clothing", LLFolderType::FT_CLOTHING, 5 });
        w.library.push_back({ LF(3), LF(1), "Objects", LLFolderType::FT_OBJECT, 4 });
        w.library.push_back({ LF(4), LF(3), "Furniture", LLFolderType::FT_NONE, 2 });

        auto add_item = [&](U64 n, U64 parent, LLAssetType::EType t, LLInventoryType::EType inv, const char* name, U32 flags = 0)
        {
            w.items.push_back(item(I(n), F(parent), t, inv, name, A(n), AGENT, PERM_ALL, flags));
        };
        add_item(1, SF_TEXTURES, LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Wood");
        add_item(2, SF_TEXTURES, LLAssetType::AT_TEXTURE, LLInventoryType::IT_SNAPSHOT, "Snapshot 1");
        add_item(3, SF_LANDMARKS, LLAssetType::AT_LANDMARK, LLInventoryType::IT_LANDMARK, "Home");
        add_item(4, SF_CLOTHING, LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Shirt", 4);
        add_item(5, SF_CLOTHING, LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Pants", 5);
        add_item(6, SF_BODYPARTS, LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Shape", 0);
        add_item(7, SF_BODYPARTS, LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Skin", 1);
        add_item(8, SF_BODYPARTS, LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Hair", 2);
        add_item(9, SF_BODYPARTS, LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE, "Eyes", 3);
        add_item(10, SF_OBJECTS, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Box");
        add_item(11, 42, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Table");
        add_item(12, 43, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Chair");
        add_item(13, SF_NOTECARDS, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Notes");
        add_item(14, SF_SCRIPTS, LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL, "Script");
        add_item(15, SF_GESTURES, LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE, "Wave");
        add_item(16, SF_TRASH, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Deleted note");
        add_item(17, 44, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Old box");
        add_item(18, 45, LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Old texture");
        add_item(19, SF_CALLING, LLAssetType::AT_CALLINGCARD, LLInventoryType::IT_CALLINGCARD, "Friend");
        add_item(20, SF_SETTINGS, LLAssetType::AT_SETTINGS, LLInventoryType::IT_SETTINGS, "Sky", 1);
        // Outfit and COF links.
        w.items.push_back(link(I(30), F(40), I(4), "Shirt", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(31), F(40), I(6), "Shape", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(32), F(41), I(5), "Pants", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(33), F(SF_COF), I(4), "Shirt", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(34), F(SF_COF), I(6), "Shape", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(35), F(SF_COF), I(7), "Skin", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(36), F(SF_COF), I(8), "Hair", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(37), F(SF_COF), I(9), "Eyes", false, LLInventoryType::IT_WEARABLE));
        w.items.push_back(link(I(38), F(SF_COF), F(40), "Outfit A", true));
        w.items.push_back(link(I(39), F(SF_FAVORITES), I(3), "Home", false, LLInventoryType::IT_LANDMARK));
        // Library items (owned by the library owner, no-modify like the Library).
        w.items.push_back(item(LI(1), LF(2), LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Library shirt", pid(TAG_ASSET, 1001), LIB_OWNER, PERM_COPY | PERM_TRANSFER | PERM_MOVE, 4));
        w.items.push_back(item(LI(2), LF(3), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Library box", pid(TAG_ASSET, 1002), LIB_OWNER, PERM_COPY | PERM_TRANSFER | PERM_MOVE));
        w.items.push_back(item(LI(3), LF(4), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Library chair", pid(TAG_ASSET, 1003), LIB_OWNER, PERM_ALL));
        return w;
    }

    // A fresh model, configuration and recorder, as at process start.
    void fresh(const std::string& cache_dir)
    {
        env();
        trace("fresh " + cache_dir);
        model::reset();
        reset_config();
        fs::remove_all(cache_dir);
        fs::create_directories(cache_dir);
        set_cache_dir(cache_dir);
        install_recorder();
        set_busy_count(5);
        clear_journal();
        gAgentID = AGENT;
        gAgentSessionID = SESSION;
    }

    void take(std::vector<std::string>& out)
    {
        for (const std::string& l : take_journal()) out.push_back("  " + l);
    }

    void files(std::vector<std::string>& out, const std::string& dir, const std::string& label)
    {
        std::vector<std::string> names;
        if (fs::is_directory(dir))
        {
            for (const auto& e : fs::directory_iterator(dir))
            {
                if (e.is_directory()) continue;
                // A .gz's size depends on the zlib build: only plain sizes are recorded.
                std::string name = e.path().filename().string();
                bool gz = name.size() > 3 && name.compare(name.size() - 3, 3, ".gz") == 0;
                // getTempFilename() names are random: "<uuid>.tmp" stands for them.
                if (name.size() == 40 && name.compare(36, 4, ".tmp") == 0 && LLUUID::validate(name.substr(0, 36)))
                {
                    name = "<uuid>.tmp";
                }
                names.push_back(name + " " + (gz ? std::string("gz") : std::to_string((U64)e.file_size())));
            }
        }
        std::sort(names.begin(), names.end());
        std::string line = "files " + label + " [";
        for (size_t i = 0; i < names.size(); ++i) line += (i ? "; " : "") + names[i];
        out.push_back(line + "]");
    }

    // The login sequence of llstartup.cpp (process_login_success_response,
    // STATE_INVENTORY_SKEL, STATE_INVENTORY_SEND2, STATE_INVENTORY_CALLBACKS),
    // inventory calls only, recorded step by step.
    struct LoginInput
    {
        LLUUID agent_root;
        LLUUID lib_root;
        LLUUID lib_owner;
        LLSD agent_skel;
        LLSD lib_skel;      // undefined = no library skeleton in the response
    };

    // The model dump, with the keyed lines (objects "cat"/"item", child
    // vectors "children", broken-link lists "broken", "backlink" pairs)
    // reduced to those new or changed since 'seen' (which is updated); keys
    // that disappeared are listed as "gone <key>". The other lines (model
    // header, locks, rebuild list, pending batch) are always written.
    void dump_model_delta(std::vector<std::string>& out, std::map<std::string, std::string>& seen)
    {
        std::vector<std::string> lines;
        dump_model(lines, true);
        std::map<std::string, std::string> now;
        for (const std::string& l : lines)
        {
            std::string key;
            if (l.compare(0, 4, "cat ") == 0 || l.compare(0, 5, "item ") == 0
                || l.compare(0, 9, "children ") == 0 || l.compare(0, 7, "broken ") == 0)
            {
                size_t a = l.find(' ') + 1;
                key = l.substr(0, l.find(' ', a));
            }
            else if (l.compare(0, 9, "backlink ") == 0)
            {
                key = l;
            }
            if (key.empty())
            {
                out.push_back(l);
                continue;
            }
            now[key] = l;
            auto it = seen.find(key);
            if (it == seen.end() || it->second != l) out.push_back(l);
        }
        for (const auto& kv : seen)
        {
            if (!now.count(kv.first)) out.push_back("gone " + kv.first);
        }
        seen.swap(now);
    }

    void login(const LoginInput& in, std::vector<std::string>& out, bool record_models, const std::string& cache_dir)
    {
        std::map<std::string, std::string> seen;
        if (in.agent_root.notNull()) gInventory.setRootFolderID(in.agent_root);
        if (in.lib_root.notNull()) gInventory.setLibraryRootFolderID(in.lib_root);
        if (in.lib_owner.notNull()) gInventory.setLibraryOwnerID(in.lib_owner);
        take(out);
        if (in.lib_skel.isDefined() && gInventory.getLibraryOwnerID().notNull())
        {
            bool rv = gInventory.loadSkeleton(in.lib_skel, gInventory.getLibraryOwnerID());
            out.push_back(std::string("step load_library rv=") + (rv ? "1" : "0"));
            take(out);
            files(out, cache_dir, "after_library");
            if (record_models) dump_model_delta(out, seen);
        }
        trace("load agent skeleton");
        bool rv = gInventory.loadSkeleton(in.agent_skel, AGENT);
        out.push_back(std::string("step load_agent rv=") + (rv ? "1" : "0"));
        take(out);
        files(out, cache_dir, "after_agent");
        files(out, gDirUtilp->getTempDir(), "temp");
        if (record_models) dump_model_delta(out, seen);
        trace("buildParentChildMap");
        gInventory.buildParentChildMap();
        out.push_back("step build_parent_child_map");
        take(out);
        if (record_models) dump_model_delta(out, seen); else dump_model(out, false);
        dump_validation(out, *gInventory.mValidationInfo);
        out.push_back(std::string("usable=") + (gInventory.isInventoryUsable() ? "1" : "0")
                      + " root=" + gInventory.getRootFolderID().asString()
                      + " first_time_in_viewer2=" + (model::first_time_in_viewer2() ? "1" : "0"));
        gInventory.createCommonSystemCategories();
        out.push_back("step create_common_system_categories pending=" + std::to_string(model::pending_system_folders()));
        take(out);
        gInventory.addChangedMask(LLInventoryObserver::ALL, LLUUID::null);
        gInventory.notifyObservers();
        out.push_back("step startup_notify");
        take(out);
    }

    LoginInput login_input(const World& w, bool with_library = true)
    {
        LoginInput in;
        in.agent_root = w.agent_root;
        in.lib_root = with_library ? w.lib_root : LLUUID::null;
        in.lib_owner = with_library ? LIB_OWNER : LLUUID::null;
        in.agent_skel = skeleton(w.agent);
        if (with_library) in.lib_skel = skeleton(w.library);
        return in;
    }

    // Logs in without a cache, then makes the model hold the world: items
    // added through updateItem(), versions set as the server's and counts as
    // the loaded children (plus count_delta), as AIS and the background fetch
    // do. The journal is discarded.
    void populate(const World& w, const std::string& cache_dir)
    {
        std::vector<std::string> ignore;
        login(login_input(w, w.lib_root.notNull()), ignore, false, cache_dir);
        for (const auto& it : w.items)
        {
            gInventory.updateItem(it);
        }
        std::map<LLUUID, S32> versions;
        for (const Folder& f : w.agent) versions[f.id] = f.version;
        for (const Folder& f : w.library) versions[f.id] = f.version;
        for (const auto& kv : model::categories())
        {
            LLViewerInventoryCategory* cat = kv.second;
            auto v = versions.find(kv.first);
            if (v == versions.end() || w.unknown_version.count(kv.first)) continue;
            cat->setVersion(v->second);
            auto t = w.folder_thumbnails.find(kv.first);
            if (t != w.folder_thumbnails.end()) cat->setThumbnailUUID(t->second);
            if (w.folder_favorites.count(kv.first)) cat->setFavorite(true);
            auto d = w.count_delta.find(kv.first);
            cat->setDescendentCount(cat->getViewerDescendentCount() + (d == w.count_delta.end() ? 0 : d->second));
        }
        gInventory.notifyObservers();
        clear_journal();
    }

    std::string gunzip_bytes(const std::string& gz_path, const std::string& scratch)
    {
        std::string plain = scratch + ".plain";
        if (!gunzip_file(gz_path, plain)) return std::string();
        std::string bytes = read_file(plain);
        LLFile::remove(plain);
        return bytes;
    }

    // gzip as the viewer writes it (llcommon gzip_file, zlib-ng at the
    // default level); the header's OS byte is set to 3 (Unix) so the bytes
    // do not depend on the recording platform.
    std::string gzip_bytes(const std::string& plain, const std::string& scratch)
    {
        static U32 serial = 0;
        std::string src = scratch + std::to_string(++serial) + ".src";
        std::string dst = scratch + std::to_string(serial) + ".gz";
        write_file(src, plain);
        bool ok = gzip_file(src, dst);
        std::string gz = read_file(dst);
        LLFile::remove(src);
        LLFile::remove(dst);
        tut::ensure("gzip_file failed for " + dst, ok && gz.size() >= 18);
        if (gz.size() > 9) gz[9] = (char)3;
        return gz;
    }

    // The cache file layout: big-endian S32 version, binary LLSD, "\n".
    std::string cache_plain(S32 version, const LLSD& body, bool newline = true)
    {
        std::string out;
        U32 v = (U32)version;
        out.push_back((char)(v >> 24));
        out.push_back((char)(v >> 16));
        out.push_back((char)(v >> 8));
        out.push_back((char)v);
        std::ostringstream s;
        LLPointer<LLSDBinaryFormatter>(new LLSDBinaryFormatter())->format(body, s);
        out += s.str();
        if (newline) out.push_back('\n');
        return out;
    }

    LLSD parse_cache_body(const std::string& plain)
    {
        LLSD sd;
        if (plain.size() < 4) return sd;
        std::istringstream in(plain.substr(4));
        LLPointer<LLSDParser> parser = new LLSDBinaryParser();
        parser->parse(in, sd, LLSDSerialize::SIZE_UNLIMITED);
        return sd;
    }

    // A cache body's "categories" and "items" arrays in id order (stable).
    void sort_cache_body(LLSD& body)
    {
        auto sort_array = [&body](const char* array_key, const char* id_key)
        {
            if (!body.has(array_key) || !body[array_key].isArray()) return;
            std::vector<LLSD> v;
            for (LLSD::array_const_iterator it = body[array_key].beginArray(); it != body[array_key].endArray(); ++it) v.push_back(*it);
            std::stable_sort(v.begin(), v.end(), [id_key](const LLSD& a, const LLSD& b) { return a[id_key].asUUID() < b[id_key].asUUID(); });
            LLSD out = LLSD::emptyArray();
            for (const LLSD& e : v) out.append(e);
            body[array_key] = out;
        };
        sort_array("categories", "cat_id");
        sort_array("items", "item_id");
    }
}

namespace tut
{
    struct invmodel_data
    {
        invmodel_data() { env(); }
    };
    typedef test_group<invmodel_data> invmodel_group;
    typedef invmodel_group::object invmodel_object;
    tut::invmodel_group invmodel_grp("llinventorymodel_parity");
}

#include "llinventorymodel_parity_c1.h"
#include "llinventorymodel_parity_c2.h"
#include "llinventorymodel_parity_c3.h"
#include "llinventorymodel_parity_c4.h"
#include "llinventorymodel_parity_msgs.h"
#include "llinventorymodel_parity_real.h"
#include "llinventorymodel_parity_c5.h"
#include "llinventorymodel_parity_logs.h" // <Vulkanstorm/> R2 log parity
#include "llinventorymodel_parity_final.h"
