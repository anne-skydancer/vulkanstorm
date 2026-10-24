/**
 * @file llinventorymodel_parity_c4.h
 * @brief Corpus C4 of llinventorymodel_parity_test.cpp: validate() over
 *        constructed model states (included once, after the C3 corpus).
 *
 * Each case starts from the standard world populated as in C1, changes the
 * model the way the named defect would appear (in place, through the public
 * setters, or through the stubs' access to the containers), and records
 * validate()'s LLInventoryValidationInfo (asLLSD as binary-LLSD hex and
 * notation, every field, every warning key, mLog) and the model.
 *
 * The QAModeFakeSystemFolderIssues cases draw from a static
 * std::default_random_engine, which is a different engine in MSVC's and
 * libstdc++'s standard libraries: they are recorded per standard library in
 * cache/c4_validation_qa.<msvc|libstdcxx>.txt (a runtime edge).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace
{
    Case validation_case(const std::string& id, const std::function<void()>& change, const World& w = standard_world())
    {
        Case c;
        c.id = id;
        trace("c4 " + id);
        std::string dir = join(env().work, "c4");
        fresh(dir);
        populate(w, dir);
        change();
        clear_journal();
        LLPointer<LLInventoryValidationInfo> info = gInventory.validate();
        take(c.lines);
        dump_validation(c.lines, *info);
        std::vector<std::string> model_lines;
        dump_model(model_lines, false);
        for (const std::string& l : model_lines)
        {
            // Very long child vectors are recorded by count and digest.
            if (l.size() > 4000)
            {
                size_t sp = l.find(' ', 9);
                c.lines.push_back(l.substr(0, sp) + " long=" + std::to_string(l.size()) + " sha256=" + sha256_hex(l));
            }
            else
            {
                c.lines.push_back(l);
            }
        }
        return c;
    }

    void remove_from_vector(const LLUUID& parent, const LLUUID& child)
    {
        auto it = model::item_tree().find(parent);
        if (it != model::item_tree().end())
        {
            auto& v = *it->second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const LLPointer<LLViewerInventoryItem>& p) { return p->getUUID() == child; }), v.end());
        }
        auto ct = model::cat_tree().find(parent);
        if (ct != model::cat_tree().end())
        {
            auto& v = *ct->second;
            v.erase(std::remove_if(v.begin(), v.end(), [&](const LLPointer<LLViewerInventoryCategory>& p) { return p->getUUID() == child; }), v.end());
        }
    }

    World with_folder(const Folder& f)
    {
        World w = standard_world();
        w.agent.push_back(f);
        return w;
    }

    World with_folders(const std::vector<Folder>& fs)
    {
        World w = standard_world();
        for (const Folder& f : fs) w.agent.push_back(f);
        return w;
    }

    // A folder of 'count' items under the root (validate() scans every
    // entry of every item vector).
    World with_big_folder(U32 count)
    {
        World w = standard_world();
        w.agent.push_back({ F(850), F(SF_ROOT), "Big folder", LLFolderType::FT_NONE, 1 });
        for (U32 n = 0; n < count; ++n)
        {
            w.items.push_back(item(pid(0xa8000000, n), F(850), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Big " + std::to_string(n), A(9000 + n)));
        }
        return w;
    }

    World without_folder(U64 n)
    {
        World w = standard_world();
        std::vector<Folder> keep;
        for (const Folder& f : w.agent) if (f.id != F(n)) keep.push_back(f);
        w.agent = keep;
        std::vector<LLPointer<LLViewerInventoryItem> > items;
        for (const auto& i : w.items) if (i->getParentUUID() != F(n)) items.push_back(i);
        w.items = items;
        return w;
    }

    void c4_cases(Cases& cases)
    {
        cases.push_back(validation_case("clean", [] {}));
        cases.push_back(validation_case("no_root_id", [] { gInventory.setRootFolderID(LLUUID::null); }));
        cases.push_back(validation_case("no_library_root_id", [] { gInventory.setLibraryRootFolderID(LLUUID::null); }));
        cases.push_back(validation_case("loop_two_folders", [] { gInventory.getCategory(F(42))->setParent(F(43)); }));
        cases.push_back(validation_case("loop_self", [] { gInventory.getCategory(F(45))->setParent(F(45)); }));
        cases.push_back(validation_case("folder_parent_missing", [] { gInventory.getCategory(F(43))->setParent(pid(TAG_FOLDER, 9999)); }));
        cases.push_back(validation_case("folder_not_in_parent_vector", [] { remove_from_vector(F(42), F(43)); }));
        cases.push_back(validation_case("folder_null_parent_not_root", [] { gInventory.getCategory(F(43))->setParent(LLUUID::null); }));
        cases.push_back(validation_case("item_null_parent", [] { gInventory.getItem(I(13))->setParent(LLUUID::null); }));
        cases.push_back(validation_case("item_wrong_parent", [] { gInventory.getItem(I(13))->setParent(F(SF_OBJECTS)); }));
        cases.push_back(validation_case("item_not_in_parent_vector", [] { remove_from_vector(F(SF_NOTECARDS), I(13)); }));
        cases.push_back(validation_case("item_in_vector_not_in_map", [] { model::items().erase(I(13)); }));
        cases.push_back(validation_case("item_parent_missing", [] { gInventory.getItem(I(13))->setParent(pid(TAG_FOLDER, 9999)); }));
        cases.push_back(validation_case("item_under_unrecognised_top", []
        {
            gInventory.updateCategory(new LLViewerInventoryCategory(F(800), F(SF_ROOT), LLFolderType::FT_NONE, "Detached", AGENT));
            gInventory.updateItem(item(I(800), F(800), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Detached note", A(800)));
            gInventory.getCategory(F(800))->setParent(LLUUID::null);
        }));
        cases.push_back(validation_case("duplicate_trash_fatal", [] {}, with_folder({ pid(TAG_FOLDER, 0x900), F(SF_ROOT), "Trash 2", LLFolderType::FT_TRASH, 1 })));
        cases.push_back(validation_case("duplicate_cof_fatal", [] {}, with_folder({ pid(TAG_FOLDER, 0x901), F(SF_ROOT), "COF 2", LLFolderType::FT_CURRENT_OUTFIT, 1 })));
        cases.push_back(validation_case("duplicate_textures_automatic", [] {}, with_folder({ pid(TAG_FOLDER, 0x902), F(SF_ROOT), "Textures 2", LLFolderType::FT_TEXTURE, 1 })));
        cases.push_back(validation_case("duplicate_settings_warning", [] {}, with_folder({ pid(TAG_FOLDER, 0x903), F(SF_ROOT), "Settings 2", LLFolderType::FT_SETTINGS, 1 })));
        cases.push_back(validation_case("duplicate_material_warning", [] {}, with_folder({ pid(TAG_FOLDER, 0x904), F(SF_ROOT), "Materials 2", LLFolderType::FT_MATERIAL, 1 })));
        cases.push_back(validation_case("system_folder_elsewhere", [] {}, with_folder({ pid(TAG_FOLDER, 0x905), F(SF_OBJECTS), "Trash elsewhere", LLFolderType::FT_TRASH, 1 })));
        cases.push_back(validation_case("suitcase_ignored", [] {}, with_folder({ pid(TAG_FOLDER, 0x906), F(SF_ROOT), "My Suitcase", LLFolderType::FT_MY_SUITCASE, 1 })));
        cases.push_back(validation_case("missing_textures", [] {}, without_folder(SF_TEXTURES)));
        cases.push_back(validation_case("missing_trash", [] {}, without_folder(SF_TRASH)));
        cases.push_back(validation_case("missing_cof", [] {}, without_folder(SF_COF)));
        cases.push_back(validation_case("missing_inbox", [] {}, without_folder(SF_INBOX)));
        cases.push_back(validation_case("missing_settings", [] {}, without_folder(SF_SETTINGS)));
        cases.push_back(validation_case("agent_count_mismatch", [] { gInventory.getCategory(F(SF_OBJECTS))->setDescendentCount(99); }));
        cases.push_back(validation_case("library_count_mismatch_exempt", [] { gInventory.getCategory(LF(3))->setDescendentCount(99); }));
        cases.push_back(validation_case("unknown_counts_and_versions", [] { gInventory.getCategory(F(42))->setDescendentCount(-1); gInventory.getCategory(F(43))->setVersion(-1); }));
        cases.push_back(validation_case("locks", []
        {
            LLInventoryModel::cat_array_t* cats;
            LLInventoryModel::item_array_t* items;
            gInventory.lockDirectDescendentArrays(F(42), cats, items);
        }));
        cases.push_back(validation_case("category_tree_extra_entry", [] { model::cat_tree()[pid(TAG_FOLDER, 0x999)] = new LLInventoryModel::cat_array_t; }));
        cases.push_back(validation_case("category_missing_child_arrays", []
        {
            delete model::item_tree()[F(45)];
            model::item_tree().erase(F(45));
        }));
        cases.push_back(validation_case("category_key_mismatch", [] { model::categories()[pid(TAG_FOLDER, 0x998)] = gInventory.getCategory(F(45)); }));
        cases.push_back(validation_case("item_key_mismatch", [] { model::items()[pid(TAG_ITEM, 0x998)] = gInventory.getItem(I(18)); }));
        cases.push_back(validation_case("link_missing_backlink", [] { model::backlinks().clear(); }));
        cases.push_back(validation_case("links_broken_and_to_links", []
        {
            gInventory.updateItem(link(I(810), F(SF_COF), pid(TAG_ITEM, 0x9999), "broken", false, LLInventoryType::IT_WEARABLE));
            gInventory.updateItem(link(I(811), F(SF_COF), pid(TAG_FOLDER, 0x9999), "broken folder", true));
            gInventory.updateItem(link(I(812), F(SF_COF), I(33), "link to link", false, LLInventoryType::IT_WEARABLE));
            model::backlinks().insert(std::make_pair(I(33), I(10)));
            model::backlinks().insert(std::make_pair(I(30), I(812)));
        }));
        cases.push_back(validation_case("two_roots_named_my_inventory", [] {}, with_folder({ pid(TAG_FOLDER, 0x907), F(SF_ROOT), "my inventory", LLFolderType::FT_ROOT_INVENTORY, 1 })));
        // The root-fix hack walks the child vectors in parent-UUID order and,
        // within a vector, keeps the last match: two extra roots in the same
        // vector (the root's; UUID order there), and two under different
        // parents.
        cases.push_back(validation_case("three_roots_named_my_inventory", [] {}, with_folders({
            { pid(TAG_FOLDER, 0x908), F(SF_ROOT), "My Inventory", LLFolderType::FT_ROOT_INVENTORY, 1 },
            { pid(TAG_FOLDER, 0x909), F(SF_ROOT), "MY INVENTORY", LLFolderType::FT_ROOT_INVENTORY, 1 } })));
        cases.push_back(validation_case("three_roots_different_parents", [] {}, with_folders({
            { pid(TAG_FOLDER, 0x90a), F(SF_ROOT), "My Inventory", LLFolderType::FT_ROOT_INVENTORY, 1 },
            { pid(TAG_FOLDER, 0x90b), F(SF_OBJECTS), "MY INVENTORY", LLFolderType::FT_ROOT_INVENTORY, 1 } })));
        // Every entry of a 20,005-item vector is checked: the wrong parent is
        // on the last entry.
        cases.push_back(validation_case("item_vector_20005_wrong_parent_last", []
        {
            LLInventoryModel::cat_array_t* cats;
            LLInventoryModel::item_array_t* items;
            gInventory.getDirectDescendentsOf(F(850), cats, items);
            items->back()->setParent(F(SF_OBJECTS));
        }, with_big_folder(20005)));
    }

    void c4_qa_cases(Cases& cases)
    {
        gSavedSettings.setBOOL("QAModeFakeSystemFolderIssues", true);
        cases.push_back(validation_case("qa_fake_1", [] {}));
        cases.push_back(validation_case("qa_fake_2", [] {}));
        cases.push_back(validation_case("qa_fake_3", [] {}));
        gSavedSettings.setBOOL("QAModeFakeSystemFolderIssues", false);
        cases.push_back(validation_case("qa_off_again", [] {}));
    }
}

namespace tut
{
    template<> template<> void invmodel_object::test<4>()
    {
        env().ran.insert(4);
        set_test_name("C4 validation");
        Cases cases;
        c4_cases(cases);
        corpus("cache/c4_validation.txt", "C4 validation", "none", cases);
        Cases qa;
        c4_qa_cases(qa);
#if LL_WINDOWS
        const char* lib = "msvc";
#else
        const char* lib = "libstdcxx";
#endif
        corpus(std::string("cache/c4_validation_qa.") + lib + ".txt",
               "C4 validation, QAModeFakeSystemFolderIssues (std::default_random_engine of this standard library)", "default-constructed engine", qa);
    }
}
