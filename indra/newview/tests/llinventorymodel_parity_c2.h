/**
 * @file llinventorymodel_parity_c2.h
 * @brief Corpus C2 of llinventorymodel_parity_test.cpp: load and discard at
 *        login (included once, after llinventorymodel_parity_corpora.h).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace vks_inv_frozen
{
    void save_inventory_at_logout(bool second_instance);
}

namespace
{
    // The pinned C2 case list (cache/c2/cases.txt): its count and the SHA-256
    // of its text ("<id>\n" per case, in order).
    const S32 C2_CASE_COUNT = 64;
    const char* const C2_CASE_LIST_SHA256 = "04f97e4778018fbb51f2ab14e33a1c769ae4cf87148a4560ee1219029777b7db";

    struct C2Case
    {
        std::string id;
        std::string note;
        bool second_instance = false;
        bool production_grid = true;
        std::string grid_id = "util.agni.lindenlab.com";
        bool logout = false;
        LLUUID agent_root;
        LLUUID lib_root;
        LLUUID lib_owner;
        LLSD agent_skel;
        LLSD lib_skel;
        std::map<std::string, std::string> files;  // cache directory contents before login
    };

    struct CacheBody
    {
        LLSD body;
        CacheBody() {}
        // LLSD copies share their maps and arrays: a variant must not change the base.
        CacheBody(const CacheBody& other) : body(llsd_clone(other.body)) {}
        CacheBody& operator=(const CacheBody& other) { body = llsd_clone(other.body); return *this; }
        LLSD& cats() { return body["categories"]; }
        LLSD& items() { return body["items"]; }
        LLSD* cat(const LLUUID& id)
        {
            for (LLSD::array_iterator it = cats().beginArray(); it != cats().endArray(); ++it)
                if ((*it)["cat_id"].asUUID() == id) return &(*it);
            tut::ensure("C2 generator: no cached folder " + id.asString(), false);
            return NULL;
        }
        LLSD* item(const LLUUID& id)
        {
            for (LLSD::array_iterator it = items().beginArray(); it != items().endArray(); ++it)
                if ((*it)["item_id"].asUUID() == id) return &(*it);
            tut::ensure("C2 generator: no cached item " + id.asString(), false);
            return NULL;
        }
        void remove_cat(const LLUUID& id) { remove(cats(), "cat_id", id); }
        void remove_item(const LLUUID& id) { remove(items(), "item_id", id); }
        static void remove(LLSD& arr, const char* key, const LLUUID& id)
        {
            LLSD out = LLSD::emptyArray();
            for (LLSD::array_const_iterator it = arr.beginArray(); it != arr.endArray(); ++it)
                if ((*it)[key].asUUID() != id) out.append(*it);
            arr = out;
        }
        std::string plain(S32 version = 5) const { return cache_plain(version, body); }
    };

    std::string gz_name(const LLUUID& owner, const std::string& grid = std::string())
    {
        return owner.asString() + (grid.empty() ? std::string() : "." + grid) + ".inv.llsd.gz";
    }
    std::string plain_name(const LLUUID& owner) { return owner.asString() + ".inv.llsd"; }

    // The base caches: a world cached at logout.
    struct Base
    {
        World world;
        CacheBody agent;
        CacheBody library;
    };

    Base make_base(const World& w)
    {
        Base b;
        b.world = w;
        std::string dir = join(env().work, "c2base");
        fresh(dir);
        populate(w, dir);
        gInventory.cache(w.agent_root, AGENT);
        gInventory.cache(w.lib_root, LIB_OWNER);
        clear_journal();
        std::string scratch = join(env().work, "c2base_scratch");
        b.agent.body = parse_cache_body(gunzip_bytes(join(dir, gz_name(AGENT)), scratch));
        b.library.body = parse_cache_body(gunzip_bytes(join(dir, gz_name(LIB_OWNER)), scratch));
        // The C2 inputs are shared by every platform: the records in id
        // order, not in the order cache() wrote them (the model's maps are
        // std::unordered_map since upstream 28fee038e0, so that order is the
        // platform's).
        sort_cache_body(b.agent.body);
        sort_cache_body(b.library.body);
        return b;
    }

    C2Case base_case(const Base& b, const std::string& id, const std::string& note)
    {
        C2Case c;
        c.id = id;
        c.note = note;
        c.agent_root = b.world.agent_root;
        c.lib_root = b.world.lib_root;
        c.lib_owner = LIB_OWNER;
        c.agent_skel = skeleton(b.world.agent);
        c.lib_skel = skeleton(b.world.library);
        std::string scratch = join(env().work, "c2gz");
        c.files[gz_name(AGENT)] = gzip_bytes(b.agent.plain(), scratch);
        c.files[gz_name(LIB_OWNER)] = gzip_bytes(b.library.plain(), scratch);
        return c;
    }

    void set_skel_version(LLSD& skel, const LLUUID& id, S32 version)
    {
        for (LLSD::array_iterator it = skel.beginArray(); it != skel.endArray(); ++it)
            if (LLUUID((*it)["folder_id"].asString()) == id) (*it)["version"] = version;
    }

    LLSD skel_entry(const LLUUID& id, const LLUUID& parent, const std::string& name, S32 type, S32 version)
    {
        return skeleton({ { id, parent, name, type, version } })[0];
    }

    std::vector<C2Case> make_c2_cases()
    {
        std::vector<C2Case> cases;
        std::string scratch = join(env().work, "c2gz");
        Base base = make_base(standard_world());
        auto gz = [&](const std::string& plain) { return gzip_bytes(plain, scratch); };
        auto with_agent = [&](const std::string& id, const std::string& note, const std::string& agent_gz)
        {
            trace("case " + id);
            C2Case c = base_case(base, id, note);
            c.files[gz_name(AGENT)] = agent_gz;
            return c;
        };

        cases.push_back(base_case(base, "valid", "both caches current: every folder kept"));
        {
            C2Case c = base_case(base, "valid_logout", "valid caches, then the logout save (disconnectViewer)");
            c.logout = true;
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "second_instance_valid", "second instance: the cache is read through a temp copy (a sentinel plain <agent>.inv.llsd is left alone); the library is not saved at logout");
            c.second_instance = true;
            c.files[plain_name(AGENT)] = "sentinel: a second instance must not unpack into this file\n";
            c.logout = true;
            cases.push_back(c);
        }
        {
            C2Case c = with_agent("second_instance_obsolete", "second instance with an obsolete cache: the .gz is not deleted", gz(base.agent.plain(4)));
            c.second_instance = true;
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "marker_clear_cache", "FS DELETE_INV_GZ marker of the agent: the library is loaded from its cache first, then both files are deleted; logout writes the library back (survey 2 Q9)");
            c.files[AGENT.asString() + "_DELETE_INV_GZ"] = "";
            c.logout = true;
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "marker_library_owner", "a DELETE_INV_GZ marker named after the library owner");
            c.files[LIB_OWNER.asString() + "_DELETE_INV_GZ"] = "";
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "no_cache", "no cache files: every folder unknown");
            c.files.clear();
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "plain_only", "a plain .inv.llsd and no .gz: loaded and left in place (Q7)");
            c.files.erase(gz_name(AGENT));
            c.files[plain_name(AGENT)] = base.agent.plain();
            cases.push_back(c);
        }
        {
            std::string g = gz(base.agent.plain());
            g[g.size() - 6] ^= 0x5a; // inside the CRC-32 of the trailer
            C2Case c = with_agent("plain_and_corrupt_gz", "a corrupt .gz and a stale plain file: gunzip fails, the plain file is used", g);
            CacheBody stale = base.agent;
            (*stale.cat(F(SF_OBJECTS)))["version"] = 11;
            c.files[plain_name(AGENT)] = stale.plain();
            cases.push_back(c);
        }
        {
            std::string g = gz(base.agent.plain());
            cases.push_back(with_agent("gz_truncated", "the .gz cut at half its length", g.substr(0, g.size() / 2)));
        }
        {
            std::string g = gz(base.agent.plain());
            g[g.size() - 6] ^= 0x5a;
            cases.push_back(with_agent("gz_corrupt_crc", "the gzip trailer's CRC-32 changed", g));
        }
        {
            std::string g = gz(base.agent.plain());
            g[g.size() / 2] ^= 0x5a;
            cases.push_back(with_agent("gz_corrupt_data", "a byte in the middle of the deflate stream changed", g));
        }
        {
            std::string p = base.agent.plain();
            size_t half = p.size() / 2;
            cases.push_back(with_agent("gz_concatenated", "two gzip members, each half of the file", gz(p.substr(0, half)) + gz(p.substr(half))));
        }
        cases.push_back(with_agent("gz_plain_bytes", "the plain cache bytes under the .gz name", base.agent.plain()));
        cases.push_back(with_agent("gz_trailing_garbage", "a valid member followed by 16 bytes of garbage", gz(base.agent.plain()) + std::string("garbage-garbage!")));
        cases.push_back(with_agent("gz_empty_file", "a 0-byte .gz", std::string()));
        cases.push_back(with_agent("short_0", "a 0-byte cache file (gzipped)", gz(std::string())));
        cases.push_back(with_agent("short_3", "a 3-byte cache file (gzipped): one byte short of the header", gz(std::string("\0\0\0", 3))));
        cases.push_back(with_agent("short_4_header_only", "a 4-byte cache file: the version header (5) and nothing else", gz(base.agent.plain().substr(0, 4))));
        cases.push_back(with_agent("short_5", "a 5-byte cache file: the header and one byte of LLSD", gz(base.agent.plain().substr(0, 5))));
        cases.push_back(with_agent("header_4", "cache version 4", gz(base.agent.plain(4))));
        cases.push_back(with_agent("header_6", "cache version 6", gz(base.agent.plain(6))));
        {
            std::string p = base.agent.plain();
            cases.push_back(with_agent("llsd_truncated_50", "valid header, the binary LLSD cut at 50 %", gz(p.substr(0, p.size() / 2))));
        }
        {
            std::string p = base.agent.plain();
            p.pop_back();
            cases.push_back(with_agent("no_trailing_newline", "the file without its final newline", gz(p)));
        }
        {
            CacheBody b = base.agent;
            b.body["categories"] = LLSD("not an array");
            cases.push_back(with_agent("categories_not_array", "categories is a string", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            b.body["items"] = LLSD::emptyMap();
            cases.push_back(with_agent("items_not_array", "items is a map", gz(b.plain())));
        }
        {
            CacheBody b;
            b.body = LLSD::emptyArray();
            b.body.append(base.agent.body["categories"]);
            cases.push_back(with_agent("body_is_array", "the LLSD root is an array", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(13)))["item_id"] = LLUUID::null;
            cases.push_back(with_agent("item_null_id", "an item with a null id is skipped", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(10)))["type"] = "nonsense";
            cases.push_back(with_agent("item_unknown_type", "an item whose type name is unknown: AT_UNKNOWN, its folder invalidated", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(10)))["type"] = "llassettype_bad_lookup";
            cases.push_back(with_agent("item_bad_lookup_type", "an item saved with the bad-lookup type name (Q10)", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(13)))["parent_id"] = pid(TAG_FOLDER, 77777);
            cases.push_back(with_agent("item_parent_missing", "an item whose parent is not in the model: dropped, never lost", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(13)))["sale_info"] = LLSD("garbage");
            cases.push_back(with_agent("item_bad_sale_info", "an item whose sale_info is not a map", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(14)))["type"] = "mesh";
            (*b.item(I(14)))["inv_type"] = "mesh";
            cases.push_back(with_agent("item_mesh_type", "a mesh item in the cache (loadSkeleton's addItem accepts what updateItem refuses)", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(14)))["type"] = 57;
            cases.push_back(with_agent("item_type_integer", "an item whose type is an integer, not a name", gz(b.plain())));
        }
        {
            C2Case c = base_case(base, "version_mismatch", "a folder whose skeleton version differs from the cache: -1, its items dropped");
            set_skel_version(c.agent_skel, F(SF_OBJECTS), 13);
            cases.push_back(c);
        }
        {
            CacheBody b = base.agent;
            b.remove_cat(F(42));
            cases.push_back(with_agent("skeleton_folder_not_in_cache", "a skeleton folder missing from the cache: -1, its items dropped", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            LLSD extra = *b.cat(F(42));
            extra["cat_id"] = pid(TAG_FOLDER, 88888);
            extra["name"] = "Removed on the server";
            b.cats().append(extra);
            LLSD it = *b.item(I(11));
            it["item_id"] = pid(TAG_ITEM, 88888);
            it["parent_id"] = pid(TAG_FOLDER, 88888);
            b.items().append(it);
            cases.push_back(with_agent("cache_folder_not_in_skeleton", "a cached folder the skeleton does not have: ignored, its items dropped", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            LLSD dup = *b.cat(F(SF_OBJECTS));
            dup["version"] = 99;
            b.cats().append(dup);
            cases.push_back(with_agent("duplicate_cache_category", "the same folder twice in the cache, the second with another version", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            LLSD dup = *b.cat(F(SF_OBJECTS));
            dup["version"] = 99;
            LLSD arr = LLSD::emptyArray();
            arr.append(dup);
            for (LLSD::array_const_iterator it = b.cats().beginArray(); it != b.cats().endArray(); ++it) arr.append(*it);
            b.cats() = arr;
            cases.push_back(with_agent("duplicate_cache_category_first", "the same folder twice, the first with another version", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            LLSD nocat = *b.cat(F(42));
            nocat.erase("cat_id");
            b.cats().append(nocat);
            cases.push_back(with_agent("cache_category_without_id", "a cached folder record without cat_id (importLLSDMap accepts it)", gz(b.plain())));
        }
        {
            C2Case c = base_case(base, "duplicate_skeleton_ids", "a folder twice in the skeleton: the first entry wins");
            LLSD dup = c.agent_skel[(LLSD::Integer)6];
            dup["name"] = "Second entry";
            dup["version"] = 77;
            c.agent_skel.append(dup);
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_malformed_entry", "a skeleton entry without version: the folder is missing, its child folder is lost (server move), its items dropped");
            for (LLSD::array_iterator it = c.agent_skel.beginArray(); it != c.agent_skel.endArray(); ++it)
                if (LLUUID((*it)["folder_id"].asString()) == F(42)) it->erase("version");
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_null_id", "a skeleton entry with a null folder id");
            LLSD e = c.agent_skel[(LLSD::Integer)3];
            e["folder_id"] = LLUUID::null.asString();
            c.agent_skel.append(e);
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_empty", "an empty agent skeleton");
            c.agent_skel = LLSD::emptyArray();
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_parent_absent", "folders whose parent is not in the skeleton: lost; FT_NONE to Lost And Found, typed to the root (MoveInventoryFolder each)");
            c.agent_skel.append(skel_entry(F(70), pid(TAG_FOLDER, 66666), "Orphan plain", LLFolderType::FT_NONE, 1));
            c.agent_skel.append(skel_entry(F(71), pid(TAG_FOLDER, 66666), "Orphan typed", LLFolderType::FT_TRASH, 1));
            c.agent_skel.append(skel_entry(F(72), F(70), "Child of orphan", LLFolderType::FT_NONE, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_null_parent", "non-root folders with a null parent: lost (the Second Life rule)");
            c.agent_skel.append(skel_entry(F(73), LLUUID::null, "Null parent plain", LLFolderType::FT_NONE, 1));
            c.agent_skel.append(skel_entry(F(74), LLUUID::null, "Null parent outfit", LLFolderType::FT_OUTFIT, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_mesh_folder", "an FT_MESH folder with a child folder: the mesh folder is never added, its child is lost");
            c.agent_skel.append(skel_entry(F(75), F(SF_ROOT), "Meshes", LLFolderType::FT_MESH, 1));
            c.agent_skel.append(skel_entry(F(76), F(75), "In meshes", LLFolderType::FT_NONE, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_second_root", "a second FT_ROOT_INVENTORY folder named MY INVENTORY: the root-fix hack replaces the root id");
            c.agent_skel.append(skel_entry(F(77), F(SF_ROOT), "MY INVENTORY", LLFolderType::FT_ROOT_INVENTORY, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_duplicate_system_folders", "a second Trash and a second Current Outfit under the root: fatal duplicates; lookups take the smallest UUID");
            c.agent_skel.append(skel_entry(pid(TAG_FOLDER, 0), F(SF_ROOT), "Trash 2", LLFolderType::FT_TRASH, 1));
            c.agent_skel.append(skel_entry(pid(TAG_FOLDER, 0x7fff), F(SF_ROOT), "COF 2", LLFolderType::FT_CURRENT_OUTFIT, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_no_common_system_folders", "none of the nine folders createCommonSystemCategories ensures: one AIS creation each, in its order");
            LLSD skel = LLSD::emptyArray();
            std::set<LLUUID> drop = { F(SF_TRASH), F(SF_FAVORITES), F(SF_CALLING), F(SF_MYOUTFITS), F(SF_COF), F(SF_LANDMARKS), F(SF_SETTINGS), F(SF_MATERIALS), F(SF_INBOX), F(40), F(41), F(44), F(45) };
            for (LLSD::array_const_iterator it = c.agent_skel.beginArray(); it != c.agent_skel.endArray(); ++it)
            {
                if (!drop.count(LLUUID((*it)["folder_id"].asString()))) skel.append(*it);
            }
            c.agent_skel = skel;
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_three_roots", "two further FT_ROOT_INVENTORY folders named My Inventory (any case): the root-fix hack keeps the last match in parent-UUID, then vector order");
            c.agent_skel.append(skel_entry(F(78), F(SF_OBJECTS), "My Inventory", LLFolderType::FT_ROOT_INVENTORY, 1));
            c.agent_skel.append(skel_entry(F(79), F(SF_ROOT), "MY INVENTORY", LLFolderType::FT_ROOT_INVENTORY, 1));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "skeleton_missing_system_folders", "no Current Outfit, Trash and Settings: missing system folders and creation requests");
            LLSD skel = LLSD::emptyArray();
            for (LLSD::array_const_iterator it = c.agent_skel.beginArray(); it != c.agent_skel.endArray(); ++it)
            {
                LLUUID id((*it)["folder_id"].asString());
                if (id == F(SF_COF) || id == F(SF_TRASH) || id == F(SF_SETTINGS) || id == F(44) || id == F(45)) continue;
                skel.append(*it);
            }
            c.agent_skel = skel;
            cases.push_back(c);
        }
        {
            CacheBody b = base.agent;
            LLSD arr = LLSD::emptyArray();
            LLSD rest = LLSD::emptyArray();
            for (LLSD::array_const_iterator it = b.items().beginArray(); it != b.items().endArray(); ++it)
            {
                if ((*it)["type"].asString() == "link" || (*it)["type"].asString() == "link_f") arr.append(*it); else rest.append(*it);
            }
            for (LLSD::array_const_iterator it = rest.beginArray(); it != rest.endArray(); ++it) arr.append(*it);
            b.items() = arr;
            cases.push_back(with_agent("link_target_later", "every link before its target in the file: deferred, then recovered", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            b.remove_item(I(9));
            cases.push_back(with_agent("link_target_absent", "a COF link whose target is not in the cache: the COF invalidated, its other links stay", gz(b.plain())));
        }
        {
            C2Case c = base_case(base, "link_into_stale_folder", "Body Parts changed on the server: its items are dropped, so the COF links into it are broken and the COF is invalidated (the COF cascade)");
            set_skel_version(c.agent_skel, F(SF_BODYPARTS), 7);
            cases.push_back(c);
        }
        {
            CacheBody b = base.agent;
            (*b.item(I(38)))["asset_id"] = pid(TAG_FOLDER, 55555);
            cases.push_back(with_agent("folder_link_target_absent", "a COF folder link whose folder does not exist", gz(b.plain())));
        }
        {
            CacheBody b = base.agent;
            LLSD l = *b.item(I(33));
            l["item_id"] = pid(TAG_ITEM, 4000);
            l["asset_id"] = LI(1);
            l["name"] = "Library shirt";
            b.items().append(l);
            cases.push_back(with_agent("library_link_from_agent", "a COF link to a Library item: resolved because the library loads first", gz(b.plain())));
        }
        {
            C2Case c = base_case(base, "library_cache_missing", "no library cache file");
            c.files.erase(gz_name(LIB_OWNER));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "no_library", "no library in the login response (no library root, owner or skeleton)");
            c.lib_root.setNull();
            c.lib_owner.setNull();
            c.lib_skel = LLSD();
            c.files.erase(gz_name(LIB_OWNER));
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "library_version_mismatch", "a Library folder changed on the server");
            set_skel_version(c.lib_skel, LF(3), 5);
            cases.push_back(c);
        }
        {
            C2Case c = base_case(base, "grid_beta_names", "not SL main: the file names carry the scrubbed, lower-cased grid id; the SL-named files are ignored");
            c.production_grid = false;
            c.grid_id = "Aditi:Beta?<Grid>";
            std::string agent_gz = c.files[gz_name(AGENT)];
            std::string lib_gz = c.files[gz_name(LIB_OWNER)];
            c.files[gz_name(AGENT, "aditi_beta__grid_")] = agent_gz;
            c.files[gz_name(LIB_OWNER, "aditi_beta__grid_")] = lib_gz;
            CacheBody stale = base.agent;
            (*stale.cat(F(SF_OBJECTS)))["version"] = 1;
            c.files[gz_name(AGENT)] = gz(stale.plain());
            c.logout = true;
            cases.push_back(c);
        }
        {
            World w = standard_world();
            w.count_delta[F(SF_ROOT)] = 2;
            Base inc = make_base(w);
            cases.push_back(base_case(inc, "incomplete_root_reload", "the cache of a session whose root was incomplete (count 2 above its loaded children): the root is saved without its items and reloads as complete (Q8)"));
        }
        {
            World w = standard_world();
            w.unknown_version.insert(F(42));
            Base inc = make_base(w);
            cases.push_back(base_case(inc, "incomplete_parent_complete_child", "a session where Furniture had an unknown version: Furniture not saved, Chairs (complete) saved"));
        }
        {
            Base tb = make_base(types_world());
            cases.push_back(base_case(tb, "types_world", "the every-type world of C1 reloaded (AT_UNKNOWN invalidates its folder, Q10)"));
        }
        return cases;
    }

    // ---- inputs on disk -------------------------------------------------------------
    void write_c2_inputs(const std::string& dir, const C2Case& c)
    {
        fs::create_directories(join(dir, "files"));
        std::ostringstream t;
        t << "note " << c.note << "\n"
          << "second_instance " << (c.second_instance ? 1 : 0) << "\n"
          << "production_grid " << (c.production_grid ? 1 : 0) << "\n"
          << "grid_id " << c.grid_id << "\n"
          << "logout " << (c.logout ? 1 : 0) << "\n"
          << "agent_root " << c.agent_root << "\n"
          << "lib_root " << c.lib_root << "\n"
          << "lib_owner " << c.lib_owner << "\n";
        write_file(join(dir, "case.txt"), t.str());
        std::ostringstream a;
        LLSDSerialize::toPrettyNotation(c.agent_skel, a);
        write_file(join(dir, "skel_agent.llsd"), a.str() + "\n");
        if (c.lib_skel.isDefined())
        {
            std::ostringstream l;
            LLSDSerialize::toPrettyNotation(c.lib_skel, l);
            write_file(join(dir, "skel_library.llsd"), l.str() + "\n");
        }
        for (const auto& kv : c.files) write_file(join(join(dir, "files"), kv.first), kv.second);
    }

    LLSD read_notation(const std::string& path)
    {
        std::string text = read_file(path);
        std::istringstream in(text);
        LLSD sd;
        LLSDSerialize::fromNotation(sd, in, text.size());
        return sd;
    }

    C2Case read_c2_inputs(const std::string& dir, const std::string& id)
    {
        C2Case c;
        c.id = id;
        std::istringstream t(read_file(join(dir, "case.txt")));
        std::string line;
        while (std::getline(t, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t sp = line.find(' ');
            std::string k = line.substr(0, sp);
            std::string v = sp == std::string::npos ? std::string() : line.substr(sp + 1);
            if (k == "note") c.note = v;
            else if (k == "second_instance") c.second_instance = v == "1";
            else if (k == "production_grid") c.production_grid = v == "1";
            else if (k == "grid_id") c.grid_id = v;
            else if (k == "logout") c.logout = v == "1";
            else if (k == "agent_root") c.agent_root = LLUUID(v);
            else if (k == "lib_root") c.lib_root = LLUUID(v);
            else if (k == "lib_owner") c.lib_owner = LLUUID(v);
        }
        c.agent_skel = read_notation(join(dir, "skel_agent.llsd"));
        if (file_exists(join(dir, "skel_library.llsd"))) c.lib_skel = read_notation(join(dir, "skel_library.llsd"));
        if (fs::is_directory(join(dir, "files")))
        {
            for (const auto& e : fs::directory_iterator(join(dir, "files")))
                c.files[e.path().filename().string()] = read_file(e.path().string());
        }
        return c;
    }

    Case run_c2(const C2Case& c)
    {
        Case out;
        out.id = c.id;
        trace("run " + c.id);
        std::string dir = join(env().work, "c2run");
        fresh(dir);
        config().second_instance = c.second_instance;
        config().production_grid = c.production_grid;
        config().grid_id = c.grid_id;
        set_cache_dir(dir);
        for (const auto& kv : c.files) write_file(join(dir, kv.first), kv.second);
        out.lines.push_back("note " + c.note);
        files(out.lines, dir, "before");
        LoginInput in;
        in.agent_root = c.agent_root;
        in.lib_root = c.lib_root;
        in.lib_owner = c.lib_owner;
        in.agent_skel = c.agent_skel;
        in.lib_skel = c.lib_skel;
        login(in, out.lines, true, dir);
        if (c.logout)
        {
            trace("logout save");
            vks_inv_frozen::save_inventory_at_logout(c.second_instance);
            out.lines.push_back("step logout_save");
            take(out.lines);
            files(out.lines, dir, "after_logout");
            files(out.lines, gDirUtilp->getTempDir(), "temp");
            std::string scratch = join(env().work, "c2scratch");
            for (const LLUUID& owner : { AGENT, LIB_OWNER })
            {
                std::string path = LLInventoryModel::getInvCacheAddres(owner) + ".gz";
                if (!file_exists(path)) continue;
                std::string plain = gunzip_bytes(path, scratch);
                out.lines.push_back("saved " + owner.asString() + " size=" + std::to_string(plain.size()) + " sha256=" + sha256_hex(plain));
            }
        }
        // The plain files left in the cache directory, by content.
        std::vector<std::string> plain_files;
        for (const auto& e : fs::directory_iterator(dir))
        {
            std::string name = e.path().filename().string();
            if (e.is_directory() || (name.size() > 3 && name.compare(name.size() - 3, 3, ".gz") == 0)) continue;
            plain_files.push_back("content " + name + " sha256=" + sha256_hex(read_file(e.path().string())));
        }
        std::sort(plain_files.begin(), plain_files.end());
        out.lines.insert(out.lines.end(), plain_files.begin(), plain_files.end());
        return out;
    }
}

namespace tut
{
    // C2: every load and discard case at login (survey 2 sections 2.4, 2.5, 8.2).
    template<> template<> void invmodel_object::test<2>()
    {
        env().ran.insert(2);
        set_test_name("C2 load and discard");
        std::string inputs = join(env().dump() ? env().out : env().fixtures, "cache/c2");
        std::vector<std::string> ids;
        if (env().dump())
        {
            std::string list;
            for (const C2Case& c : make_c2_cases())
            {
                write_c2_inputs(join(inputs, c.id), c);
                ids.push_back(c.id);
                list += c.id + "\n";
            }
            write_file(join(inputs, "cases.txt"), list);
        }
        else
        {
            std::istringstream in(read_file(join(inputs, "cases.txt")));
            std::string id;
            while (std::getline(in, id))
            {
                if (!id.empty() && id.back() == '\r') id.pop_back();
                if (!id.empty()) ids.push_back(id);
            }
            ensure("C2 inputs missing: " + inputs, !ids.empty());
        }
        // The case list is pinned here, so it cannot shrink or change silently.
        std::string list;
        for (const std::string& id : ids) list += id + "\n";
        trace("C2 cases " + std::to_string(ids.size()) + " sha " + sha256_hex(list));
        ensure_equals("C2 case count", (S32)ids.size(), C2_CASE_COUNT);
        ensure_equals("C2 case list digest (cases.txt)", sha256_hex(list), std::string(C2_CASE_LIST_SHA256));
        env().touched.insert("cache/c2/cases.txt");
        Cases cases;
        for (const std::string& id : ids)
        {
            std::string dir = join(inputs, id);
            for (const auto& e : fs::recursive_directory_iterator(dir))
            {
                if (e.is_regular_file())
                {
                    env().touched.insert("cache/c2/" + id + "/" + fs::relative(e.path(), dir).generic_string());
                }
            }
            cases.push_back(run_c2(read_c2_inputs(dir, id)));
        }
        corpus("cache/c2_load.txt", "C2 load and discard at login", "none", cases);
    }
}
