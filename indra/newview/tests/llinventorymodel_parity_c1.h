/**
 * @file llinventorymodel_parity_c1.h
 * @brief Corpus C1 of llinventorymodel_parity_test.cpp: cache files (included once, at
 *        its end; it uses that file's helpers).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace
{
    // ======================================================================
    // C1: cache files
    // ======================================================================

    // Records one cache file: the uncompressed bytes as a binary fixture,
    // the notation rendering beside it, the decode by loadFromFile() and the
    // re-encode by saveToFile() of what it decoded.
    // full: the bytes, their notation and every decoded record are committed;
    // otherwise only digests (the bytes are regenerated in process and
    // compared through their SHA-256).
    void record_cache_file(Cases& cases, const std::string& name, const std::string& plain, const std::string& scratch, bool full = true)
    {
        Case c;
        c.id = name;
        std::string rel = "cache/c1/" + name + ".inv.llsd";
        if (full)
        {
            binary(rel, plain);
            LLSD body = parse_cache_body(plain);
            binary("cache/c1/" + name + ".notation.txt", llsd_notation(body) + "\n");
        }
        else
        {
            rel = "(not committed)";
        }
        c.lines.push_back("file " + rel + " size=" + std::to_string(plain.size()) + " sha256=" + sha256_hex(plain));
        U32 version = plain.size() >= 4 ? (((U32)(U8)plain[0] << 24) | ((U32)(U8)plain[1] << 16) | ((U32)(U8)plain[2] << 8) | (U32)(U8)plain[3]) : 0;
        c.lines.push_back("header=" + std::to_string(version) + " last_byte=" + std::to_string(plain.empty() ? -1 : (S32)(U8)plain.back()));

        std::string path = scratch + ".inv.llsd";
        write_file(path, plain);
        LLInventoryModel::cat_array_t cats;
        LLInventoryModel::item_array_t items;
        std::set<LLUUID> to_update;
        bool obsolete = false;
        bool rv = model::load_from_file(path, cats, items, to_update, obsolete);
        c.lines.push_back(std::string("decode rv=") + (rv ? "1" : "0") + " obsolete=" + (obsolete ? "1" : "0")
                          + " cats=" + std::to_string(cats.size()) + " items=" + std::to_string(items.size())
                          + " cats_to_update=" + ids(to_update));
        std::vector<std::string> decoded;
        for (const auto& cat : cats) decoded.push_back("  " + dump_category(cat));
        for (const auto& it : items) decoded.push_back("  " + dump_item(it));
        if (full)
        {
            c.lines.insert(c.lines.end(), decoded.begin(), decoded.end());
        }
        else
        {
            std::string all;
            for (const std::string& l : decoded) all += l + "\n";
            c.lines.push_back("decoded_records sha256=" + sha256_hex(all));
        }
        std::string again = scratch + ".again";
        bool saved = model::save_to_file(again, cats, items);
        std::string bytes = read_file(again);
        size_t i = 0;
        while (i < bytes.size() && i < plain.size() && bytes[i] == plain[i]) ++i;
        c.lines.push_back(std::string("reencode rv=") + (saved ? "1" : "0") + " identical=" + (bytes == plain ? "1" : "0")
                          + (bytes == plain ? std::string() : " first_difference=" + std::to_string(i))
                          + " size=" + std::to_string(bytes.size()));
        LLFile::remove(path);
        LLFile::remove(again);
        cases.push_back(c);
    }

    // Runs cache() for the agent and the library as LLAppViewer::disconnectViewer
    // does and records both files.
    void cache_and_record(Cases& cases, const std::string& state, const World& w, const std::string& dir,
                          bool full = true, bool library = false)
    {
        Case c;
        c.id = state + ".cache_call";
        gInventory.cache(w.agent_root, AGENT);
        take(c.lines);
        if (w.lib_root.notNull())
        {
            gInventory.cache(w.lib_root, LIB_OWNER);
            take(c.lines);
        }
        files(c.lines, dir, "cache_dir");
        files(c.lines, gDirUtilp->getTempDir(), "temp");
        cases.push_back(c);
        std::string scratch = join(env().work, "c1scratch");
        record_cache_file(cases, state + ".agent", gunzip_bytes(LLInventoryModel::getInvCacheAddres(AGENT) + ".gz", scratch), scratch, full);
        std::string lib = gunzip_bytes(LLInventoryModel::getInvCacheAddres(LIB_OWNER) + ".gz", scratch);
        if (library)
        {
            record_cache_file(cases, state + ".library", lib, scratch, true);
        }
        else
        {
            // The library is the same in every state: its digest only.
            cases.back().lines.push_back("library file size=" + std::to_string(lib.size()) + " sha256=" + sha256_hex(lib));
        }
    }

    // Every asset type, inventory type and folder type, and every record
    // feature the cache codec handles.
    World types_world()
    {
        World w = standard_world();
        w.agent.push_back({ F(60), F(SF_ROOT), "Every asset type", LLFolderType::FT_NONE, 8 });
        w.agent.push_back({ F(61), F(SF_ROOT), "Every folder type", LLFolderType::FT_NONE, 3 });
        w.agent.push_back({ F(62), F(SF_ROOT), "Record features", LLFolderType::FT_NONE, 5 });
        // One folder per folder type with a dictionary entry, plus types
        // without one, under F(61) (not under the root: no system duplicates).
        U64 n = 100;
        for (S32 ft = -1; ft <= LLFolderType::FT_MY_SUITCASE + 1; ++ft)
        {
            bool known = LLFolderType::lookup((LLFolderType::EType)ft) != LLFolderType::badLookup();
            if (!known && ft != 4 && ft != 61 && ft != 99 && ft != 101) continue;
            if (ft == LLFolderType::FT_ROOT_INVENTORY) continue;
            w.agent.push_back({ F(n++), F(61), "type " + std::to_string(ft), ft, 1 });
        }
        // One item per asset type value 0..AT_COUNT, AT_UNKNOWN and 200.
        U64 k = 200;
        std::vector<S32> types;
        for (S32 t = 0; t <= LLAssetType::AT_COUNT; ++t) types.push_back(t);
        types.push_back(LLAssetType::AT_UNKNOWN);
        types.push_back(200);
        for (S32 t : types)
        {
            if (t == LLAssetType::AT_LINK || t == LLAssetType::AT_LINK_FOLDER || t == LLAssetType::AT_CATEGORY) continue;
            LLInventoryType::EType inv = LLInventoryType::defaultForAssetType((LLAssetType::EType)t);
            w.items.push_back(item(I(k), F(60), (LLAssetType::EType)t, inv, "asset type " + std::to_string(t), A(k)));
            ++k;
        }
        // Every inventory type value on a texture.
        for (S32 it = -1; it <= LLInventoryType::IT_COUNT; ++it)
        {
            w.items.push_back(item(I(k), F(60), LLAssetType::AT_TEXTURE, (LLInventoryType::EType)it, "inv type " + std::to_string(it), A(k)));
            ++k;
        }
        // Record features in F(62).
        auto feat = [&](const std::string& name) -> LLPointer<LLViewerInventoryItem>
        {
            LLPointer<LLViewerInventoryItem> i = item(I(k), F(62), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, name, A(k));
            ++k;
            w.items.push_back(i);
            return i;
        };
        feat("no-modify (shadow_id)")->setPermissions(perms(AGENT, PERM_COPY | PERM_TRANSFER | PERM_MOVE));
        feat("no-copy (shadow_id)")->setPermissions(perms(AGENT, PERM_MODIFY | PERM_TRANSFER | PERM_MOVE));
        // The shadow_id rule is "base mask lacks one of modify, copy,
        // transfer": exactly those three (no move) keep asset_id; each one
        // missing switches to shadow_id.
        feat("unrestricted without move (asset_id)")->setPermissions(perms(AGENT, PERM_MODIFY | PERM_COPY | PERM_TRANSFER));
        feat("no-transfer (shadow_id)")->setPermissions(perms(AGENT, PERM_MODIFY | PERM_COPY | PERM_MOVE));
        feat("owner mask differs from base")->setPermissions([] { LLPermissions p = perms(AGENT, PERM_ALL); p.initMasks(PERM_ALL, PERM_COPY | PERM_MOVE, PERM_NONE, PERM_NONE, PERM_ALL); return p; }());
        {
            LLPointer<LLViewerInventoryItem> i = feat("no-modify, null asset");
            i->setPermissions(perms(AGENT, PERM_COPY | PERM_TRANSFER));
            i->setAssetUUID(LLUUID::null);
        }
        feat("thumbnail")->setThumbnailUUID(pid(TAG_THUMB, 1));
        feat("favourite")->setFavorite(true);
        feat("script runtime")->setRuntime("luau");
        feat("name | with | bars");
        feat(std::string("control\x01\x1f chars\tand\nLF"));
        feat("non-ascii \xc3\xa9\xe2\x82\xac bytes");
        feat("");
        feat("trailing spaces   ");
        feat(std::string(300, 'L'));
        {
            LLPointer<LLViewerInventoryItem> i = feat("description");
            i->setDescription("a | b \xc3\xa9 c\nd");
        }
        feat("flags high bit")->setFlags(0x80000000u | 0x123u);
        feat("negative date")->setCreationDate((time_t)-5);
        feat("zero date")->setCreationDate((time_t)0);
        feat("for sale")->setSaleInfo(LLSaleInfo(LLSaleInfo::FS_COPY, 250));
        feat("for sale original")->setSaleInfo(LLSaleInfo(LLSaleInfo::FS_ORIGINAL, 0));
        {
            LLPointer<LLViewerInventoryItem> i = feat("group owned");
            LLPermissions p = perms(AGENT, PERM_ALL);
            p.setOwnerAndGroup(AGENT, LLUUID::null, GROUP, true);
            i->setPermissions(p);
        }
        {
            LLPointer<LLViewerInventoryItem> i = feat("next owner restricted");
            i->setPermissions(perms(AGENT, PERM_ALL, PERM_MOVE | PERM_TRANSFER));
        }
        // Links: to an item, to a folder, broken item and folder links, a link to a link.
        w.items.push_back(link(I(k++), F(62), I(13), "link to Notes", false, LLInventoryType::IT_NOTECARD));
        w.items.push_back(link(I(k++), F(62), F(42), "link to Furniture", true));
        w.items.push_back(link(I(k++), F(62), pid(TAG_ITEM, 99999), "broken item link", false, LLInventoryType::IT_NOTECARD));
        w.items.push_back(link(I(k++), F(62), pid(TAG_FOLDER, 99999), "broken folder link", true));
        w.items.push_back(link(I(k), F(62), I(k - 4), "link to a link", false, LLInventoryType::IT_NOTECARD));
        ++k;
        return w;
    }

    // Seeded random worlds: depth distribution, links (some broken), all
    // common types, random ids (so the child vectors follow UUID order).
    World random_world(U64 seed, U32 nfolders, U32 nitems)
    {
        Rng rng(seed);
        World w = standard_world();
        std::vector<LLUUID> folders;
        for (const Folder& f : w.agent) if (f.id != F(SF_TRASH) || true) folders.push_back(f.id);
        for (U32 i = 0; i < nfolders; ++i)
        {
            LLUUID id = rng.uuid();
            LLUUID parent = folders[rng.below((U32)folders.size())];
            w.agent.push_back({ id, parent, "folder " + std::to_string(i), (rng.below(10) == 0) ? (S32)LLFolderType::FT_OUTFIT : (S32)LLFolderType::FT_NONE, (S32)(1 + rng.below(50)) });
            folders.push_back(id);
        }
        static const struct { LLAssetType::EType t; LLInventoryType::EType i; } kinds[] = {
            { LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE }, { LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT },
            { LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD }, { LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE },
            { LLAssetType::AT_BODYPART, LLInventoryType::IT_WEARABLE }, { LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL },
            { LLAssetType::AT_LANDMARK, LLInventoryType::IT_LANDMARK }, { LLAssetType::AT_SOUND, LLInventoryType::IT_SOUND },
            { LLAssetType::AT_ANIMATION, LLInventoryType::IT_ANIMATION }, { LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE },
            { LLAssetType::AT_SETTINGS, LLInventoryType::IT_SETTINGS }, { LLAssetType::AT_MATERIAL, LLInventoryType::IT_MATERIAL } };
        std::vector<LLUUID> item_ids;
        for (U32 i = 0; i < nitems; ++i)
        {
            LLUUID id = rng.uuid();
            LLUUID parent = folders[rng.below((U32)folders.size())];
            U32 r = rng.below(100);
            if (r < 8 && !item_ids.empty())
            {
                LLUUID target = (r == 0) ? rng.uuid() : item_ids[rng.below((U32)item_ids.size())];
                w.items.push_back(link(id, parent, target, "link " + std::to_string(i)));
            }
            else
            {
                const auto& kind = kinds[rng.below(sizeof(kinds) / sizeof(kinds[0]))];
                PermissionMask base = (rng.below(4) == 0) ? (PERM_COPY | PERM_MOVE | PERM_TRANSFER) : PERM_ALL;
                // One draw per statement (argument evaluation order is unspecified).
                LLUUID asset = rng.uuid();
                U32 flags = rng.below(16);
                S32 created = (S32)(1600000000 + rng.below(100000000));
                LLPointer<LLViewerInventoryItem> it = item(id, parent, kind.t, kind.i, "item " + std::to_string(i), asset, AGENT, base, flags, created);
                if (rng.below(20) == 0) it->setThumbnailUUID(rng.uuid());
                if (rng.below(25) == 0) it->setFavorite(true);
                w.items.push_back(it);
                item_ids.push_back(id);
            }
        }
        return w;
    }
}

namespace tut
{
    // C1: cache files of model states made through the C++ API.
    template<> template<> void invmodel_object::test<1>()
    {
        env().ran.insert(1);
        set_test_name("C1 cache files");
        Cases cases;
        std::string dir = join(env().work, "c1");

        {   // the standard inventory
            fresh(dir);
            World w = standard_world();
            populate(w, dir);
            cache_and_record(cases, "standard", w, dir, true, true);
        }
        {   // every type and record feature
            fresh(dir);
            World w = types_world();
            populate(w, dir);
            cache_and_record(cases, "types", w, dir);
        }
        {   // incomplete folders: an incomplete root (survey 2 Q8), an unknown
            // version under a complete parent, a complete child of an
            // incomplete parent
            fresh(dir);
            World w = standard_world();
            w.count_delta[F(SF_ROOT)] = 2;
            w.unknown_version.insert(F(41));
            w.count_delta[F(42)] = 1;
            populate(w, dir);
            cache_and_record(cases, "incomplete", w, dir);
        }
        {   // a mutation history: child vectors after push_back and swap-remove
            fresh(dir);
            World w = standard_world();
            populate(w, dir);
            gInventory.moveObject(I(10), F(42));
            gInventory.moveObject(I(1), F(SF_OBJECTS));
            gInventory.deleteObject(I(2));
            gInventory.updateItem(item(I(50), F(SF_TEXTURES), LLAssetType::AT_TEXTURE, LLInventoryType::IT_TEXTURE, "Added later", A(50)));
            gInventory.updateItem(item(I(51), F(SF_OBJECTS), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Added later too", A(51)));
            gInventory.moveObject(F(43), F(SF_OBJECTS));
            gInventory.deleteObject(I(3));
            gInventory.notifyObservers();
            for (auto& kv : model::categories())
            {
                if (kv.second->getVersion() != LLViewerInventoryCategory::VERSION_UNKNOWN)
                    kv.second->setDescendentCount(kv.second->getViewerDescendentCount());
            }
            clear_journal();
            cache_and_record(cases, "history", w, dir);
        }
        for (U64 seed : { 0x1001ULL, 0x2002ULL, 0x3003ULL })
        {
            fresh(dir);
            World w = random_world(seed, 40, 300);
            populate(w, dir);
            cache_and_record(cases, llformat("random_%llx", (unsigned long long)seed), w, dir, seed == 0x1001ULL);
        }
        corpus("cache/c1_cache_files.txt", "C1 cache files", "random worlds 0x1001 0x2002 0x3003 (xorshift64*)", cases);
    }
}
