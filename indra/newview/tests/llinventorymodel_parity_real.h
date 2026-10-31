/**
 * @file llinventorymodel_parity_real.h
 * @brief The realistic synthetic set (C1, 100k items / 8k folders), the 1M
 *        stress set, the public Linden Library cache, the local-only user
 *        cache check and the timings of llinventorymodel_parity_test.cpp
 *        (included once, after the set 8 corpus).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace
{
    const U64 REALISTIC_SEED = 0x5eed100000ULL;
    const U64 STRESS_SEED = 0x5eed1000000ULL;

    // A realistic inventory: the standard system folders, a user folder tree
    // with a depth distribution, outfit folders of links, a COF of links into
    // them, a flat Objects folder of flat_objects items, a large Trash, and
    // the remaining items spread over the user folders. Random ids (the
    // child vectors follow UUID order at login).
    // Ids: pid(tag, scrambled counter), the counter multiplied by an odd
    // constant modulo 2^32 (a bijection), so the UUID order is unrelated to
    // the creation order while only four bytes vary (the committed .gz stays
    // small; random 16-byte ids do not compress).
    struct IdMaker
    {
        U32 tag;
        U32 counter = 0;
        explicit IdMaker(U32 t) : tag(t) {}
        LLUUID next() { ++counter; return pid(tag, (U64)(U32)(counter * 0x9E3779B1u)); }
    };

    World realistic_world(U64 seed, U32 nfolders, U32 nitems, U32 flat_objects, U32 trash_items)
    {
        Rng rng(seed);
        IdMaker folder_ids(0xf2000000), item_ids(0xa2000000), asset_ids(0xa6000000), thumb_ids(0x7c000000);
        World w = standard_world();
        w.items.clear();
        struct Node { LLUUID id; U32 depth; };
        std::vector<Node> folders;
        folders.push_back({ F(SF_OBJECTS), 1 });
        folders.push_back({ F(SF_CLOTHING), 1 });
        folders.push_back({ F(SF_TEXTURES), 1 });
        folders.push_back({ F(SF_NOTECARDS), 1 });
        folders.push_back({ F(SF_LANDMARKS), 1 });
        folders.push_back({ F(SF_ROOT), 0 });
        std::vector<Node> trash_folders;
        trash_folders.push_back({ F(SF_TRASH), 1 });
        U32 trash_folder_count = nfolders / 20;
        U32 outfit_count = nfolders / 40;
        std::vector<LLUUID> outfits;
        for (U32 i = 0; i < nfolders; ++i)
        {
            LLUUID id = folder_ids.next();
            if (i < outfit_count)
            {
                w.agent.push_back({ id, F(SF_MYOUTFITS), "Outfit " + std::to_string(i), LLFolderType::FT_OUTFIT, (S32)(1 + rng.below(30)) });
                outfits.push_back(id);
                continue;
            }
            if (i < outfit_count + trash_folder_count)
            {
                const Node& p = trash_folders[rng.below((U32)trash_folders.size())];
                w.agent.push_back({ id, p.id, "Deleted folder " + std::to_string(i), LLFolderType::FT_NONE, (S32)(1 + rng.below(9)) });
                trash_folders.push_back({ id, p.depth + 1 });
                continue;
            }
            // Shallow trees are more likely: a parent of depth d is kept with
            // probability 1/(1+d/2).
            Node p;
            do { p = folders[rng.below((U32)folders.size())]; } while (p.depth > 1 && rng.below(2 + p.depth) > 2);
            w.agent.push_back({ id, p.id, "Folder " + std::to_string(i), LLFolderType::FT_NONE, (S32)(1 + rng.below(200)) });
            folders.push_back({ id, p.depth + 1 });
        }
        static const struct { LLAssetType::EType t; LLInventoryType::EType i; } kinds[] = {
            { LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT }, { LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT },
            { LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE }, { LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD },
            { LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE }, { LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE },
            { LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL }, { LLAssetType::AT_LANDMARK, LLInventoryType::IT_LANDMARK },
            { LLAssetType::AT_SOUND, LLInventoryType::IT_SOUND }, { LLAssetType::AT_ANIMATION, LLInventoryType::IT_ANIMATION },
            { LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE }, { LLAssetType::AT_SETTINGS, LLInventoryType::IT_SETTINGS },
            { LLAssetType::AT_MATERIAL, LLInventoryType::IT_MATERIAL } };
        U32 made = 0;
        std::vector<LLUUID> wearables;
        auto make = [&](const LLUUID& parent, const std::string& prefix)
        {
            const auto& kind = kinds[rng.below(sizeof(kinds) / sizeof(kinds[0]))];
            PermissionMask base = (rng.below(3) == 0) ? (PERM_COPY | PERM_MOVE | PERM_TRANSFER) : PERM_ALL;
            LLUUID id = item_ids.next();
            // One draw per statement (argument evaluation order is unspecified).
            U32 flags = (kind.i == LLInventoryType::IT_WEARABLE) ? rng.below(16) : 0;
            LLUUID asset = asset_ids.next();
            S32 created = (S32)(1500000000 + rng.below(250000000));
            std::string desc;
            if (rng.below(4) == 0) desc = "Description " + std::to_string(rng.below(1000));
            LLPointer<LLViewerInventoryItem> it = item(id, parent, kind.t, kind.i, prefix + " " + std::to_string(made), asset, AGENT, base,
                                                       flags, created, desc);
            if (rng.below(50) == 0) it->setThumbnailUUID(thumb_ids.next());
            if (kind.i == LLInventoryType::IT_WEARABLE) wearables.push_back(id);
            w.items.push_back(it);
            ++made;
        };
        for (U32 i = 0; i < flat_objects; ++i) make(F(SF_OBJECTS), "Object");
        for (U32 i = 0; i < trash_items; ++i) make(trash_folders[rng.below((U32)trash_folders.size())].id, "Deleted");
        U32 links_per_outfit = 10;
        U32 rest = nitems - flat_objects - trash_items - outfit_count * links_per_outfit - 40;
        for (U32 i = 0; i < rest; ++i) make(folders[rng.below((U32)folders.size())].id, "Item");
        std::vector<LLUUID> outfit_links;
        for (const LLUUID& o : outfits)
        {
            for (U32 k = 0; k < links_per_outfit; ++k)
            {
                LLUUID id = item_ids.next();
                w.items.push_back(link(id, o, wearables[rng.below((U32)wearables.size())], "Outfit link", false, LLInventoryType::IT_WEARABLE));
                outfit_links.push_back(id);
            }
        }
        for (U32 k = 0; k < 39; ++k)
        {
            w.items.push_back(link(item_ids.next(), F(SF_COF), wearables[rng.below((U32)wearables.size())], "COF link", false, LLInventoryType::IT_WEARABLE));
        }
        w.items.push_back(link(item_ids.next(), F(SF_COF), outfits.empty() ? F(40) : outfits[0], "Current outfit", true));
        return w;
    }

    double seconds_since(const std::chrono::steady_clock::time_point& t)
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
    }

    std::string digest_lines(const std::vector<std::string>& lines)
    {
        std::string all;
        for (const std::string& l : lines) all += l + "\n";
        return sha256_hex(all);
    }

    // Digests per chunk of 'chunk' lines, so a mismatch names a range.
    void chunked_digests(Case& c, const std::string& label, const std::vector<std::string>& lines, size_t chunk)
    {
        for (size_t first = 0; first < lines.size(); first += chunk)
        {
            size_t last = std::min(lines.size(), first + chunk);
            std::vector<std::string> part(lines.begin() + first, lines.begin() + last);
            c.lines.push_back(label + "[" + std::to_string(first) + ".." + std::to_string(last - 1) + "] sha256=" + digest_lines(part));
        }
    }

    struct Summary
    {
        size_t cats = 0, cats_known = 0, items = 0, links = 0;
    };

    Summary summarize()
    {
        Summary s;
        s.cats = model::categories().size();
        for (const auto& kv : model::categories()) if (kv.second->getVersion() != LLViewerInventoryCategory::VERSION_UNKNOWN) ++s.cats_known;
        s.items = model::items().size();
        for (const auto& kv : model::items()) if (kv.second->getIsLinkType()) ++s.links;
        return s;
    }

    std::string summary_line(const std::string& label, const Summary& s)
    {
        return label + " cats=" + std::to_string(s.cats) + " cats_known_version=" + std::to_string(s.cats_known)
               + " items=" + std::to_string(s.items) + " links=" + std::to_string(s.links);
    }

    // Logs in from a world's cache files (in dir) and records the outcome as
    // digests and counts.
    void reload_and_record(Case& c, const World& w, const std::string& dir)
    {
        std::vector<std::string> lines;
        // Keep the files, start a fresh model.
        model::reset();
        install_recorder();
        clear_journal();
        login(login_input(w), lines, false, dir);
        std::vector<std::string> model_lines;
        dump_model(model_lines, true);
        c.lines.push_back(summary_line("reload", summarize()));
        c.lines.push_back("reload_login_lines sha256=" + digest_lines(lines) + " count=" + std::to_string(lines.size()));
        c.lines.push_back("reload_model sha256=" + digest_lines(model_lines) + " lines=" + std::to_string(model_lines.size()));
        chunked_digests(c, "reload_model", model_lines, 10000);
        for (const std::string& l : lines)
        {
            if (l.compare(0, 10, "validation") == 0 || l.compare(0, 7, "usable=") == 0 || l.compare(0, 5, "step ") == 0) c.lines.push_back(l);
        }
    }
}

namespace tut
{
    // C1, realistic: 100k items and 8k folders.
    template<> template<> void invmodel_object::test<6>()
    {
        env().ran.insert(6);
        set_test_name("C1 realistic 100k");
        Cases cases;
        Case c;
        c.id = "realistic_100k";
        std::string dir = join(env().work, "realistic");
        fresh(dir);
        World w = realistic_world(REALISTIC_SEED, 8000, 100000, 20000, 10000);
        c.lines.push_back("world folders=" + std::to_string(w.agent.size()) + " items=" + std::to_string(w.items.size()));
        populate(w, dir);
        c.lines.push_back(summary_line("populated", summarize()));
        gInventory.cache(w.agent_root, AGENT);
        take(c.lines);
        std::string scratch = join(env().work, "realistic_scratch");
        std::string plain = gunzip_bytes(LLInventoryModel::getInvCacheAddres(AGENT) + ".gz", scratch);
        c.lines.push_back("file size=" + std::to_string(plain.size()) + " sha256=" + sha256_hex(plain));
        // The bytes, gzipped (zlib at the viewer's default level, OS byte 3),
        // are an input for the crate (decode and re-encode to the SHA-256
        // above). Only the decompressed content is compared: a different
        // zlib build may compress differently.
        {
            const std::string rel = platform_rel("cache/c1/realistic_100k.inv.llsd.gz");
            env().touched.insert(rel);
            if (env().dump())
            {
                std::string path = join(env().out, rel);
                fs::create_directories(fs::path(path).parent_path());
                ensure("write " + path, write_file(path, gzip_bytes(plain, scratch)));
            }
            else
            {
                std::string path = join(env().fixtures, rel);
                ensure("fixture missing: " + path, file_exists(path));
                ensure_equals(rel + ": decompressed SHA-256", sha256_hex(gunzip_bytes(path, scratch)), sha256_hex(plain));
            }
        }
        {
            std::string path = scratch + ".inv.llsd";
            write_file(path, plain);
            LLInventoryModel::cat_array_t cats;
            LLInventoryModel::item_array_t items;
            std::set<LLUUID> to_update;
            bool obsolete = false;
            bool rv = model::load_from_file(path, cats, items, to_update, obsolete);
            std::vector<std::string> decoded;
            for (const auto& cat : cats) decoded.push_back(dump_category(cat));
            for (const auto& it : items) decoded.push_back(dump_item(it));
            c.lines.push_back(std::string("decode rv=") + (rv ? "1" : "0") + " cats=" + std::to_string(cats.size()) + " items=" + std::to_string(items.size())
                              + " cats_to_update=" + ids(to_update) + " records_sha256=" + digest_lines(decoded));
            chunked_digests(c, "records", decoded, 10000);
            std::string again = scratch + ".again";
            model::save_to_file(again, cats, items);
            c.lines.push_back(std::string("reencode identical=") + (read_file(again) == plain ? "1" : "0"));
            LLFile::remove(path);
            LLFile::remove(again);
        }
        LLPointer<LLInventoryValidationInfo> info = gInventory.validate();
        dump_validation(c.lines, *info);
        reload_and_record(c, w, dir);
        cases.push_back(c);
        corpus("cache/c1_realistic.txt", "C1 realistic synthetic set (100k items, 8k folders)",
               llformat("0x%llx (xorshift64*, realistic_world(seed, 8000, 100000, 20000, 10000))", (unsigned long long)REALISTIC_SEED), cases);
    }

    // The public Linden Library cache (committed by R1 under
    // indra/llinventory/tests/fixtures/library/).
    template<> template<> void invmodel_object::test<7>()
    {
        env().ran.insert(7);
        set_test_name("Library cache (real data)");
        const LLUUID owner("ba2a564a-f0f1-4b82-9c61-b7520bfcd09f");
        std::string src = join(env().library_dir, owner.asString() + ".inv.llsd.gz");
        ensure("the Library cache fixture is missing: " + src
               + " (committed by recorder R1; for a local run copy it from %LOCALAPPDATA%\\Firestorm_x64)", file_exists(src));
        Cases cases;
        Case c;
        c.id = "library";
        std::string scratch = join(env().work, "library_scratch");
        std::string plain = gunzip_bytes(src, scratch);
        c.lines.push_back("source " + owner.asString() + ".inv.llsd.gz plain_size=" + std::to_string(plain.size()) + " plain_sha256=" + sha256_hex(plain));
        std::string path = scratch + ".inv.llsd";
        write_file(path, plain);
        LLInventoryModel::cat_array_t cats;
        LLInventoryModel::item_array_t items;
        std::set<LLUUID> to_update;
        bool obsolete = false;
        bool rv = model::load_from_file(path, cats, items, to_update, obsolete);
        std::vector<std::string> decoded;
        std::map<S32, U32> types;
        for (const auto& cat : cats) decoded.push_back(dump_category(cat));
        for (const auto& it : items) { decoded.push_back(dump_item(it)); ++types[(S32)it->LLInventoryItem::getType()]; }
        c.lines.push_back(std::string("decode rv=") + (rv ? "1" : "0") + " obsolete=" + (obsolete ? "1" : "0") + " cats=" + std::to_string(cats.size())
                          + " items=" + std::to_string(items.size()) + " cats_to_update=" + ids(to_update) + " records_sha256=" + digest_lines(decoded));
        chunked_digests(c, "records", decoded, 1000);
        std::string type_line = "item_types";
        for (const auto& kv : types) type_line += " " + std::to_string(kv.first) + ":" + std::to_string(kv.second);
        c.lines.push_back(type_line);
        std::string again = scratch + ".again";
        model::save_to_file(again, cats, items);
        std::string bytes = read_file(again);
        size_t i = 0;
        while (i < bytes.size() && i < plain.size() && bytes[i] == plain[i]) ++i;
        c.lines.push_back(std::string("reencode identical=") + (bytes == plain ? "1" : "0") + (bytes == plain ? std::string() : " first_difference=" + std::to_string(i))
                          + " size=" + std::to_string(bytes.size()));
        LLFile::remove(path);
        LLFile::remove(again);

        // Load as a library skeleton: the skeleton the login response would
        // carry for the same versions (one entry per cached folder), then the
        // standard agent inventory without a cache.
        LLUUID lib_root;
        std::vector<Folder> lib;
        for (const auto& cat : cats)
        {
            if (cat->getParentUUID().isNull()) lib_root = cat->getUUID();
            lib.push_back({ cat->getUUID(), cat->getParentUUID(), cat->getName(), (S32)cat->getPreferredType(), cat->getVersion() });
        }
        std::string dir = join(env().work, "library");
        fresh(dir);
        write_file(join(dir, owner.asString() + ".inv.llsd.gz"), read_file(src));
        World w = standard_world();
        LoginInput in;
        in.agent_root = w.agent_root;
        in.lib_root = lib_root;
        in.lib_owner = owner;
        in.agent_skel = skeleton(w.agent);
        in.lib_skel = skeleton(lib);
        std::vector<std::string> lines;
        login(in, lines, false, dir);
        std::vector<std::string> model_lines;
        dump_model(model_lines, true);
        c.lines.push_back("library_root " + lib_root.asString());
        c.lines.push_back(summary_line("loaded", summarize()));
        c.lines.push_back("login_lines sha256=" + digest_lines(lines) + " count=" + std::to_string(lines.size()));
        c.lines.push_back("model sha256=" + digest_lines(model_lines) + " lines=" + std::to_string(model_lines.size()));
        chunked_digests(c, "model", model_lines, 1000);
        for (const std::string& l : lines)
        {
            if (l.compare(0, 10, "validation") == 0 || l.compare(0, 7, "usable=") == 0 || l.compare(0, 6, "files ") == 0) c.lines.push_back(l);
        }
        cases.push_back(c);
        corpus("cache/library_real.txt", "the public Linden Library cache (indra/llinventory/tests/fixtures/library)", "none", cases);
    }

    // Timings (VKS_INVMODEL_BENCH=<runs>) and the local-only user cache check
    // (VKS_INVMODEL_USER_CACHE=<path>). Neither writes a fixture; both print
    // counts and timings only.
    template<> template<> void invmodel_object::test<8>()
    {
        env().ran.insert(8);
        set_test_name("bench and user cache");
        S32 runs = atoi(getenv_str("VKS_INVMODEL_BENCH").c_str());
        std::string user_cache = getenv_str("VKS_INVMODEL_USER_CACHE");
        if (runs <= 0 && user_cache.empty()) return;
        if (runs <= 0) runs = 3;

        struct Timing { std::string name; double best = 1e30; };
        // <Vulkanstorm> R2: the cache codec in one binary, the C++ before
        // the port (llinventorymodel_parity_frozen.cpp) against the live,
        // Rust-backed functions, over the uncompressed file `plain`: load
        // into fresh arrays, save the loaded records. Best of `runs`; the
        // side that goes first alternates from run to run, so neither gets
        // the warm caches and allocator every time.
        auto bench_codec = [&](const std::string& label, const std::string& plain)
        {
            std::string scratch = join(env().work, "codec_scratch");
            write_file(scratch + ".in", plain);
            double load_cpp = 1e30, load_rust = 1e30, save_cpp = 1e30, save_rust = 1e30;
            size_t ncats = 0, nitems = 0;
            bool same_cpp = false, same_rust = false;
            for (S32 r = 0; r < runs; ++r)
            {
                for (int turn = 0; turn < 2; ++turn)
                {
                    const int side = (turn + r) % 2; // 0: C++ (frozen), 1: Rust (live)
                    LLInventoryModel::cat_array_t cats;
                    LLInventoryModel::item_array_t items;
                    std::set<LLUUID> to_update;
                    bool obsolete = false;
                    auto t0 = std::chrono::steady_clock::now();
                    if (side == 0) vks_inv_frozen_cache::load_from_file(scratch + ".in", cats, items, to_update, obsolete);
                    else model::load_from_file(scratch + ".in", cats, items, to_update, obsolete);
                    double t = seconds_since(t0);
                    (side == 0 ? load_cpp : load_rust) = std::min(side == 0 ? load_cpp : load_rust, t);
                    ncats = cats.size();
                    nitems = items.size();
                    t0 = std::chrono::steady_clock::now();
                    if (side == 0) vks_inv_frozen_cache::save_to_file(scratch + ".out", cats, items);
                    else model::save_to_file(scratch + ".out", cats, items);
                    t = seconds_since(t0);
                    (side == 0 ? save_cpp : save_rust) = std::min(side == 0 ? save_cpp : save_rust, t);
                    (side == 0 ? same_cpp : same_rust) = read_file(scratch + ".out") == plain;
                    LLFile::remove(scratch + ".out");
                }
            }
            LLFile::remove(scratch + ".in");
            printf("\nbench codec %s (%s, minimum of %d runs): %u bytes, %u folders, %u items; re-encode identical C++=%d Rust=%d\n",
                   label.c_str(), env().platform.c_str(), runs, (U32)plain.size(), (U32)ncats, (U32)nitems, same_cpp ? 1 : 0, same_rust ? 1 : 0);
            printf("  %-48s %10.3f ms -> %10.3f ms\n", "loadFromFile C++ (frozen) -> Rust (live)", load_cpp * 1000.0, load_rust * 1000.0);
            printf("  %-48s %10.3f ms -> %10.3f ms\n", "saveToFile C++ (frozen) -> Rust (live)", save_cpp * 1000.0, save_rust * 1000.0);
            fflush(stdout);
        };
        // </Vulkanstorm>
        auto bench_world = [&](const std::string& label, const World& w, bool deep_validate)
        {
            std::vector<Timing> t = { { "populate (login without cache + updateItem)" }, { "cache() agent (collect, saveToFile, gzip)" },
                                      { "saveToFile only" }, { "loadSkeleton (library + agent, with cache)" },
                                      { "buildParentChildMap (incl. validate)" }, { "validate()" } };
            std::string dir = join(env().work, "bench");
            std::string scratch = join(env().work, "bench_scratch");
            Summary loaded;
            for (S32 r = 0; r < runs; ++r)
            {
                fresh(dir);
                auto t0 = std::chrono::steady_clock::now();
                populate(w, dir);
                t[0].best = std::min(t[0].best, seconds_since(t0));
                t0 = std::chrono::steady_clock::now();
                gInventory.cache(w.agent_root, AGENT);
                t[1].best = std::min(t[1].best, seconds_since(t0));
                {
                    LLInventoryModel::cat_array_t cats;
                    LLInventoryModel::item_array_t items;
                    std::set<LLUUID> to_update;
                    bool obsolete = false;
                    std::string plain = gunzip_bytes(LLInventoryModel::getInvCacheAddres(AGENT) + ".gz", scratch);
                    write_file(scratch + ".in", plain);
                    model::load_from_file(scratch + ".in", cats, items, to_update, obsolete);
                    t0 = std::chrono::steady_clock::now();
                    model::save_to_file(scratch + ".out", cats, items);
                    t[2].best = std::min(t[2].best, seconds_since(t0));
                    LLFile::remove(scratch + ".in");
                    LLFile::remove(scratch + ".out");
                }
                gInventory.cache(w.lib_root, LIB_OWNER);
                model::reset();
                install_recorder();
                LoginInput in = login_input(w);
                gInventory.setRootFolderID(in.agent_root);
                gInventory.setLibraryRootFolderID(in.lib_root);
                gInventory.setLibraryOwnerID(in.lib_owner);
                t0 = std::chrono::steady_clock::now();
                gInventory.loadSkeleton(in.lib_skel, LIB_OWNER);
                gInventory.loadSkeleton(in.agent_skel, AGENT);
                t[3].best = std::min(t[3].best, seconds_since(t0));
                t0 = std::chrono::steady_clock::now();
                gInventory.buildParentChildMap();
                t[4].best = std::min(t[4].best, seconds_since(t0));
                loaded = summarize();
                t0 = std::chrono::steady_clock::now();
                gInventory.validate();
                t[5].best = std::min(t[5].best, seconds_since(t0));
                clear_journal();
            }
            printf("\nbench %s (%s, RelWithDebInfo, minimum of %d runs): %s\n", label.c_str(), env().platform.c_str(), runs, summary_line("loaded", loaded).c_str());
            for (const Timing& x : t) printf("  %-48s %10.3f ms\n", x.name.c_str(), x.best * 1000.0);
            fflush(stdout);
            // <Vulkanstorm> R2: the codec over this world's agent cache.
            bench_codec(label, gunzip_bytes(LLInventoryModel::getInvCacheAddres(AGENT) + ".gz", join(env().work, "codec_gunzip")));
            // </Vulkanstorm>
        };

        if (atoi(getenv_str("VKS_INVMODEL_BENCH").c_str()) > 0)
        {
            // <Vulkanstorm> R2 codec rows: the Linden Library cache and the
            // realistic 100k-item cache (both committed fixtures).
            {
                const LLUUID owner("ba2a564a-f0f1-4b82-9c61-b7520bfcd09f");
                bench_codec("Linden Library", gunzip_bytes(join(env().library_dir, owner.asString() + ".inv.llsd.gz"),
                                                           join(env().work, "codec_gunzip")));
                bench_codec("realistic 100k items / 8k folders",
                            gunzip_bytes(join(env().fixtures, platform_rel("cache/c1/realistic_100k.inv.llsd.gz")), join(env().work, "codec_gunzip")));
            }
            // </Vulkanstorm>
            // <Vulkanstorm> Random ids, as real inventories have. The
            // realistic world's ids (pid()) differ only in bytes 10-15,
            // which std::hash<LLUUID> folds into the high bits of the hash
            // alone: the model's unordered_maps put them in one bucket, so
            // every model lookup over that world is a list walk.
            // VKS_INVMODEL_BENCH_SKIP_REALISTIC=1 (CI) times that world only
            // through the codec rows above.
            bench_world("random 50k items / 5k folders", random_world(0x5eed50000ULL, 5000, 50000), true);
            if (getenv_str("VKS_INVMODEL_BENCH_SKIP_REALISTIC").empty())
            {
                bench_world("realistic 100k items / 8k folders", realistic_world(REALISTIC_SEED, 8000, 100000, 20000, 10000), true);
            }
            // </Vulkanstorm>
            if (!getenv_str("VKS_INVMODEL_BENCH_STRESS").empty())
            {
                bench_world("stress 1M items / 80k folders", realistic_world(STRESS_SEED, 80000, 1000000, 200000, 100000), true);
            }
        }

        if (!user_cache.empty())
        {
            // Local only: counts, sizes and timings; no names, ids or contents.
            std::string scratch = join(env().work, "user_scratch");
            double gunzip_s = 1e30, load_s = 1e30, save_s = 1e30, skel_s = 1e30, build_s = 1e30, validate_s = 1e30;
            size_t ncats = 0, nitems = 0, plain_size = 0, kept_items = 0, known = 0;
            bool identical = false;
            S32 fatal = -1, warnings = -1;
            for (S32 r = 0; r < runs; ++r)
            {
                auto t0 = std::chrono::steady_clock::now();
                std::string plain = gunzip_bytes(user_cache, scratch);
                gunzip_s = std::min(gunzip_s, seconds_since(t0));
                plain_size = plain.size();
                write_file(scratch + ".in", plain);
                LLInventoryModel::cat_array_t cats;
                LLInventoryModel::item_array_t items;
                std::set<LLUUID> to_update;
                bool obsolete = false;
                t0 = std::chrono::steady_clock::now();
                model::load_from_file(scratch + ".in", cats, items, to_update, obsolete);
                load_s = std::min(load_s, seconds_since(t0));
                ncats = cats.size();
                nitems = items.size();
                t0 = std::chrono::steady_clock::now();
                model::save_to_file(scratch + ".out", cats, items);
                save_s = std::min(save_s, seconds_since(t0));
                identical = read_file(scratch + ".out") == plain;
                LLFile::remove(scratch + ".in");
                LLFile::remove(scratch + ".out");
                // Load as a skeleton of the same versions (the user's own
                // agent id is the cache's owner_id; the stub agent id is
                // replaced for this run only).
                LLUUID root, owner;
                std::vector<Folder> skel;
                for (const auto& cat : cats)
                {
                    if (cat->getParentUUID().isNull()) root = cat->getUUID();
                    owner = cat->getOwnerID();
                    skel.push_back({ cat->getUUID(), cat->getParentUUID(), cat->getName(), (S32)cat->getPreferredType(), cat->getVersion() });
                }
                std::string dir = join(env().work, "user");
                fresh(dir);
                gAgentID = owner;
                write_file(LLInventoryModel::getInvCacheAddres(owner) + ".gz", read_file(user_cache));
                gInventory.setRootFolderID(root);
                gInventory.setLibraryRootFolderID(pid(TAG_LIBF, 1));
                gInventory.setLibraryOwnerID(LIB_OWNER);
                t0 = std::chrono::steady_clock::now();
                gInventory.loadSkeleton(skeleton(skel), owner);
                skel_s = std::min(skel_s, seconds_since(t0));
                t0 = std::chrono::steady_clock::now();
                gInventory.buildParentChildMap();
                build_s = std::min(build_s, seconds_since(t0));
                Summary s = summarize();
                kept_items = s.items;
                known = s.cats_known;
                t0 = std::chrono::steady_clock::now();
                LLPointer<LLInventoryValidationInfo> v = gInventory.validate();
                validate_s = std::min(validate_s, seconds_since(t0));
                fatal = v->mFatalErrorCount;
                warnings = v->mWarningCount;
                clear_journal();
                model::reset();
                fs::remove_all(dir);
                gAgentID = AGENT;
            }
            printf("\nuser cache (local only, %s, minimum of %d runs): plain %u bytes, %u folders, %u items decoded; reencode identical=%d;"
                   " loaded as skeleton: %u folders kept, %u items; validate fatal=%d warnings=%d\n",
                   env().platform.c_str(), runs, (U32)plain_size, (U32)ncats, (U32)nitems, identical ? 1 : 0, (U32)known, (U32)kept_items, fatal, warnings);
            printf("  gunzip %.3f ms, loadFromFile %.3f ms, saveToFile %.3f ms, loadSkeleton %.3f ms, buildParentChildMap %.3f ms, validate %.3f ms\n",
                   gunzip_s * 1000, load_s * 1000, save_s * 1000, skel_s * 1000, build_s * 1000, validate_s * 1000);
            fflush(stdout);
        }
    }
}
