/**
 * @file llinventorymodel_parity_c3.h
 * @brief Corpus C3 of llinventorymodel_parity_test.cpp: mutation sequences
 *        through the public API (included once, after the C2 corpus).
 *
 * Every sequence starts from the standard world populated as in C1 (the
 * first step records the whole model), then runs steps. Each step records,
 * in order, every journal line (notification batches, messages, AIS calls,
 * fetch requests, callbacks and the other outward calls) and then the model
 * lines that changed (dump_model_delta).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "llavatarnamecache.h"
#include "llavatarname.h"

namespace
{
    struct Steps
    {
        Case c;
        std::map<std::string, std::string> seen;

        explicit Steps(const std::string& id, const World& w = standard_world())
        {
            c.id = id;
            std::string dir = join(env().work, "c3");
            fresh(dir);
            populate(w, dir);
            c.lines.push_back("step initial");
            dump_model_delta(c.lines, seen);
        }

        void step(const std::string& label, const std::function<void()>& fn)
        {
            trace("c3 " + c.id + " " + label);
            c.lines.push_back("step " + label);
            fn();
            take(c.lines);
            dump_model_delta(c.lines, seen);
        }

        void note(const std::string& line) { c.lines.push_back(line); }
    };

    LLPointer<LLViewerInventoryItem> copy_of(const LLUUID& id)
    {
        return new LLViewerInventoryItem(gInventory.getItem(id));
    }
    LLPointer<LLViewerInventoryCategory> copy_cat(const LLUUID& id)
    {
        return new LLViewerInventoryCategory(gInventory.getCategory(id));
    }

    std::string mask_str(U32 m) { return "returned mask=" + hexmask(m); }

    // An observer that changes the model while it is being notified.
    class ReentrantObserver : public LLInventoryObserver
    {
    public:
        void changed(U32 mask) override
        {
            vks_inv::note("reentrant observer called mask=" + hexmask(mask));
            if (mFired) return;
            mFired = true;
            gInventory.addChangedMask(LLInventoryObserver::LABEL, I(13));
            gInventory.notifyObservers();
            vks_inv::note("reentrant observer done");
        }
        bool mFired = false;
        static void* operator new(size_t size) { return observer_slot(1, size); }
        static void operator delete(void*) {}
    };

    // Items whose parent is missing when buildParentChildMap() runs go to
    // Lost And Found with batched MoveInventoryItem messages, a new packet
    // whenever LLMessageSystem::isSendFull() says the current one is full.
    // Such items can only come from outside the cache load (it drops them),
    // so they are put into the item map directly between the skeleton loads
    // and the build, as code running in that window would.
    Case lost_items_case(U32 count)
    {
        Case c;
        c.id = "lost_items_" + std::to_string(count);
        trace("c3 " + c.id);
        std::string dir = join(env().work, "c3lost");
        fresh(dir);
        World w = standard_world();
        LoginInput in = login_input(w);
        gInventory.setRootFolderID(in.agent_root);
        gInventory.setLibraryRootFolderID(in.lib_root);
        gInventory.setLibraryOwnerID(in.lib_owner);
        gInventory.loadSkeleton(in.lib_skel, LIB_OWNER);
        gInventory.loadSkeleton(in.agent_skel, AGENT);
        for (U32 n = 0; n < count; ++n)
        {
            LLPointer<LLViewerInventoryItem> i = item(pid(0xa7000000, n), pid(TAG_FOLDER, 0xdead00 + (n % 3)), LLAssetType::AT_NOTECARD,
                                                      LLInventoryType::IT_NOTECARD, "Lost " + std::to_string(n), A(5000 + n));
            model::items()[i->getUUID()] = i;
        }
        clear_journal();
        gInventory.buildParentChildMap();
        std::vector<std::string> journal = take_journal();
        S32 messages = 0;
        for (const std::string& l : journal)
        {
            c.lines.push_back("  " + l);
            if (l.compare(0, 22, "msg MoveInventoryItem ") == 0) ++messages;
        }
        c.lines.push_back("move_messages=" + std::to_string(messages));
        std::map<std::string, std::string> none;
        std::vector<std::string> model_lines;
        dump_model(model_lines, false);
        for (const std::string& l : model_lines)
        {
            if (l.compare(0, 9, "children ") == 0 && l.find(F(SF_LOSTFOUND).asString()) == 9) c.lines.push_back(l);
        }
        return c;
    }

    class AddedObserver : public LLInventoryAddedObserver
    {
    protected:
        void done() override { vks_inv::note("added_observer done added=" + ids(gInventory.getAddedIDs())); }
    public:
        static void* operator new(size_t size) { return observer_slot(2, size); }
        static void operator delete(void*) {}
    };

    class DescendentsObserver : public LLInventoryFetchDescendentsObserver
    {
    public:
        explicit DescendentsObserver(const uuid_vec_t& ids) : LLInventoryFetchDescendentsObserver(ids) {}
        void done() override
        {
            std::string complete, incomplete;
            for (const LLUUID& id : mComplete) complete += id.asString() + " ";
            for (const LLUUID& id : mIncomplete) incomplete += id.asString() + " ";
            vks_inv::note("descendents_observer done complete=[" + complete + "] incomplete=[" + incomplete + "]");
        }
        static void* operator new(size_t size) { return observer_slot(3, size); }
        static void operator delete(void*) {}
    };

    void c3_cases(Cases& cases)
    {
        {
            Steps s("update_item_new");
            s.step("normal", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(100), F(SF_OBJECTS), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "New box", A(100))))); });
            s.step("null_parent", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(101), LLUUID::null, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Null parent note", A(101))))); });
            s.step("categorize_lost_and_found", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(102), CATEGORIZE_LOST_AND_FOUND_ID, LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "To lost and found", A(102))))); });
            s.step("missing_parent", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(103), pid(TAG_FOLDER, 12345), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Lost item", A(103))))); });
            s.step("mesh_type_refused", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(104), F(SF_OBJECTS), LLAssetType::AT_MESH, LLInventoryType::IT_MESH, "Mesh", A(104))))); });
            s.step("gltf_type_refused", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(105), F(SF_OBJECTS), LLAssetType::AT_GLTF, LLInventoryType::IT_GLTF, "GLTF", A(105))))); });
            s.step("type_none", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(106), F(SF_OBJECTS), LLAssetType::AT_NONE, LLInventoryType::IT_OBJECT, "Type none", A(106))))); });
            s.step("type_unknown_200", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(107), F(SF_OBJECTS), (LLAssetType::EType)200, LLInventoryType::IT_OBJECT, "Type 200", A(107))))); });
            s.step("gesture", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(108), F(SF_GESTURES), LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE, "New gesture", A(108))))); });
            s.step("calling_card_uuid_desc_uncached", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(109), F(SF_CALLING), LLAssetType::AT_CALLINGCARD, LLInventoryType::IT_CALLINGCARD, "Card", A(109), AGENT, PERM_ALL, 0, 1700000000, pid(0xab000000, 1).asString())))); });
            s.step("calling_card_uuid_desc_cached", []
            {
                LLAvatarName name;
                LLSD sd;
                sd["username"] = "cached.resident";
                sd["display_name"] = "Cached";
                sd["legacy_first_name"] = "Cached";
                sd["legacy_last_name"] = "Resident";
                sd["id"] = pid(0xab000000, 2);
                sd["is_display_name_default"] = false;
                sd["display_name_next_update"] = LLDate();
                name.fromLLSD(sd);
                LLAvatarNameCache::getInstance()->insert(pid(0xab000000, 2), name);
                set_avatar_name_cache_running(true);
                vks_inv::note(mask_str(gInventory.updateItem(item(I(110), F(SF_CALLING), LLAssetType::AT_CALLINGCARD, LLInventoryType::IT_CALLINGCARD, "Card 2", A(110), AGENT, PERM_ALL, 0, 1700000000, pid(0xab000000, 2).asString()))));
            });
            s.step("calling_card_plain_desc_cache_stopped", [] { set_avatar_name_cache_running(false); });
            s.step("calling_card_plain_desc", [] { vks_inv::note(mask_str(gInventory.updateItem(item(I(111), F(SF_CALLING), LLAssetType::AT_CALLINGCARD, LLInventoryType::IT_CALLINGCARD, "Card 3", A(111), AGENT, PERM_ALL, 0, 1700000000, "not a uuid")))); });
            s.step("notify", [] { gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("update_item_existing");
            s.step("same_values", [] { vks_inv::note(mask_str(gInventory.updateItem(copy_of(I(10))))); });
            s.step("rename", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(10)); i->rename("Renamed box"); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("rename_link_target_propagates", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(4)); i->rename("Renamed shirt"); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("reparent", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(10)); i->setParent(F(42)); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("permissions", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(13)); i->setPermissions(perms(AGENT, PERM_COPY | PERM_TRANSFER)); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("null_parent_old_parent_known", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(13)); i->setParent(LLUUID::null); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("incomplete_copy", []
            {
                LLPointer<LLViewerInventoryItem> j = new LLViewerInventoryItem(I(14), F(SF_SCRIPTS), "Script", LLInventoryType::IT_LSL);
                vks_inv::note(mask_str(gInventory.updateItem(j)));
            });
            s.step("link_retarget_stale_backlink", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(30)); i->setAssetUUID(I(5)); vks_inv::note(mask_str(gInventory.updateItem(i))); });
            s.step("notify", [] { gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("update_category");
            s.step("new", [] { LLPointer<LLViewerInventoryCategory> c = new LLViewerInventoryCategory(F(200), F(SF_OBJECTS), LLFolderType::FT_NONE, "New folder", AGENT); gInventory.updateCategory(c); });
            s.step("new_localised_name", [] { LLPointer<LLViewerInventoryCategory> c = new LLViewerInventoryCategory(F(201), F(SF_OBJECTS), LLFolderType::FT_NONE, "New Folder", AGENT); gInventory.updateCategory(c); });
            s.step("new_parent_missing_orphan", [] { LLPointer<LLViewerInventoryCategory> c = new LLViewerInventoryCategory(F(202), F(203), LLFolderType::FT_NONE, "Child first", AGENT); gInventory.updateCategory(c); });
            s.step("then_parent", [] { LLPointer<LLViewerInventoryCategory> c = new LLViewerInventoryCategory(F(203), F(SF_OBJECTS), LLFolderType::FT_NONE, "Parent second", AGENT); gInventory.updateCategory(c); });
            s.step("no_change_mask_0", [] { gInventory.updateCategory(copy_cat(F(42))); });
            s.step("rename", [] { LLPointer<LLViewerInventoryCategory> c = copy_cat(F(42)); c->rename("Renamed furniture"); gInventory.updateCategory(c); });
            s.step("reparent", [] { LLPointer<LLViewerInventoryCategory> c = copy_cat(F(43)); c->setParent(F(SF_OBJECTS)); gInventory.updateCategory(c); });
            s.step("version_from_copy", [] { LLPointer<LLViewerInventoryCategory> c = copy_cat(F(42)); c->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN); c->setDescendentCount(LLViewerInventoryCategory::DESCENDENT_COUNT_UNKNOWN); gInventory.updateCategory(c); });
            s.step("marketplace_listings_label", []
            {
                LLPointer<LLViewerInventoryCategory> m = new LLViewerInventoryCategory(F(210), F(SF_ROOT), LLFolderType::FT_MARKETPLACE_LISTINGS, "Marketplace Listings", AGENT);
                gInventory.updateCategory(m);
                LLPointer<LLViewerInventoryCategory> l = new LLViewerInventoryCategory(F(211), F(210), LLFolderType::FT_NONE, "Listing", AGENT);
                gInventory.updateCategory(l);
                gInventory.notifyObservers();
                gInventory.updateCategory(copy_cat(F(211)));
            });
            s.step("notify", [] { gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("move_object");
            s.step("item", [] { gInventory.moveObject(I(10), F(42)); });
            s.step("category", [] { gInventory.moveObject(F(43), F(SF_OBJECTS)); });
            s.step("same_parent", [] { gInventory.moveObject(I(10), F(42)); });
            s.step("to_missing_folder", [] { gInventory.moveObject(I(11), pid(TAG_FOLDER, 4242)); });
            s.step("to_self", [] { gInventory.moveObject(F(42), F(42)); });
            s.step("first_item_swap_remove", [] { gInventory.moveObject(I(4), F(SF_OBJECTS)); });
            s.step("notify", [] { gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("change_parent");
            s.step("item_restamp", [] { gInventory.changeItemParent(gInventory.getItem(I(10)), F(42), true); });
            s.step("item_same_parent", [] { gInventory.changeItemParent(gInventory.getItem(I(10)), F(42), false); });
            s.step("item_into_incomplete_folder", [] { gInventory.getCategory(F(43))->setDescendentCount(5); gInventory.changeItemParent(gInventory.getItem(I(13)), F(43), false); });
            s.step("category", [] { gInventory.changeCategoryParent(gInventory.getCategory(F(43)), F(SF_NOTECARDS), false); });
            s.step("category_into_own_descendant", [] { gInventory.changeCategoryParent(gInventory.getCategory(F(44)), F(45), true); });
            s.step("category_null", [] { gInventory.changeCategoryParent(NULL, F(45), true); });
            s.step("locked_ao_folder", []
            {
                config().ao_folder = F(42);
                gInventory.changeItemParent(gInventory.getItem(I(11)), F(SF_OBJECTS), true);
                gInventory.changeCategoryParent(gInventory.getCategory(F(42)), F(SF_NOTECARDS), true);
            });
            s.step("locked_favorites_folder_setting_off", []
            {
                config().ao_folder.setNull();
                config().favorites_folder = F(SF_FAVORITES);
                gSavedPerAccountSettings.setBOOL("LockWearableFavoritesFolders", false);
                gInventory.changeItemParent(gInventory.getItem(I(39)), F(SF_LANDMARKS), true);
                gSavedPerAccountSettings.setBOOL("LockWearableFavoritesFolders", true);
            });
            cases.push_back(s.c);
        }
        {
            Steps s("delete_object");
            s.step("item_with_links_fix_links", [] { gInventory.deleteObject(I(4), true, true); });
            s.step("item_no_notify_q4", [] { gInventory.deleteObject(I(13), false, false); });
            s.step("pending_after_no_notify", [] { gInventory.notifyObservers(); });
            s.step("link_item", [] { gInventory.deleteObject(I(33)); });
            s.step("folder_with_children", [] { gInventory.deleteObject(F(42)); });
            s.step("orphaned_child_left", [] { vks_inv::note(std::string("chairs present=") + (gInventory.getCategory(F(43)) ? "1" : "0")); });
            s.step("nonexistent", [] { gInventory.deleteObject(pid(TAG_ITEM, 9999)); });
            cases.push_back(s.c);
        }
        {
            Steps s("deleted_from_server");
            s.step("item_parent_version", [] { gInventory.onObjectDeletedFromServer(I(10), true, true, true); });
            s.step("script_item_no_preview_hide", [] { gInventory.onObjectDeletedFromServer(I(14), false, false, false); });
            s.step("folder_purges_first", [] { gInventory.onObjectDeletedFromServer(F(44), true, true, true); });
            s.step("ais_style_flags", [] { gInventory.onObjectDeletedFromServer(I(5), false, false, false); });
            s.step("unknown", [] { gInventory.onObjectDeletedFromServer(pid(TAG_ITEM, 9999)); });
            cases.push_back(s.c);
        }
        {
            World w = standard_world();
            U64 prev = SF_OBJECTS;
            for (U64 d = 0; d < 30; ++d)
            {
                w.agent.push_back({ F(300 + d), F(prev), "Depth " + std::to_string(d), LLFolderType::FT_NONE, 1 });
                w.items.push_back(item(I(300 + d), F(300 + d), LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "Note at depth " + std::to_string(d), A(300 + d)));
                prev = 300 + d;
            }
            Steps s("purge_deep_chain", w);
            s.step("purge", [] { gInventory.onDescendentsPurgedFromServer(F(300), true); });
            s.step("notify", [] { gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("account_for_update");
            s.step("complete_plus", [] { gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(F(SF_OBJECTS), 1)); });
            s.step("now_incomplete_ignored", [] { gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(F(SF_OBJECTS), 1)); });
            s.step("no_version_change", [] { gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(F(SF_NOTECARDS), 1, false)); });
            s.step("unknown_version", [] { gInventory.getCategory(F(SF_SOUNDS))->setVersion(-1); gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(F(SF_SOUNDS), 1)); });
            s.step("missing_folder", [] { gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(pid(TAG_FOLDER, 9999), 1)); });
            s.step("list", []
            {
                LLInventoryModel::update_list_t l;
                l.push_back(LLInventoryModel::LLCategoryUpdate(F(SF_TEXTURES), -1));
                l.push_back(LLInventoryModel::LLCategoryUpdate(F(SF_TEXTURES), 0));
                gInventory.accountForUpdate(l);
            });
            s.step("map_always_bumps", []
            {
                LLInventoryModel::update_map_t m;
                m[F(SF_LANDMARKS)];
                --m[F(SF_GESTURES)];
                gInventory.accountForUpdate(m);
            });
            cases.push_back(s.c);
        }
        {
            Steps s("broken_links");
            s.step("link_while_fetch_active", [] { config().folder_fetch_active = true; gInventory.updateItem(link(I(400), F(SF_COF), I(401), "Shoes", false, LLInventoryType::IT_WEARABLE)); });
            s.step("second_broken_link_same_target", [] { gInventory.updateItem(link(I(402), F(40), I(401), "Shoes", false, LLInventoryType::IT_WEARABLE)); });
            s.step("broken_link_fetch_idle", [] { config().folder_fetch_active = false; gInventory.updateItem(link(I(403), F(41), I(404), "Gloves", false, LLInventoryType::IT_WEARABLE)); });
            s.step("target_arrives", [] { gInventory.updateItem(item(I(401), F(SF_CLOTHING), LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, "Shoes", A(401), AGENT, PERM_ALL, 6)); });
            s.step("idle_notify", [] { gInventory.idleNotifyObservers(); });
            s.step("idle_notify_nothing_pending", [] { gInventory.idleNotifyObservers(); });
            s.step("fetch_completion_rebuild", [] { config().folder_fetch_active = true; gInventory.updateItem(link(I(405), F(SF_COF), I(406), "Jacket", false, LLInventoryType::IT_WEARABLE)); gInventory.notifyObservers(); fire_fetch_completion(); });
            cases.push_back(s.c);
        }
        {
            Steps s("notify_reentry");
            s.step("install", [] { gInventory.addObserver(new ReentrantObserver); });
            s.step("change_and_notify", [] { gInventory.addChangedMask(LLInventoryObserver::INTERNAL, I(10)); gInventory.notifyObservers(); });
            s.step("backlog_delivered", [] { gInventory.notifyObservers(); });
            s.step("transaction_id", [] { gInventory.addChangedMask(LLInventoryObserver::ADD, I(11)); gInventory.notifyObservers(pid(TAG_TXN, 1)); });
            s.step("idle_with_nothing", [] { gInventory.idleNotifyObservers(); });
            s.step("null_referent", [] { gInventory.addChangedMask(LLInventoryObserver::ALL, LLUUID::null); gInventory.idleNotifyObservers(); });
            s.step("label_twice_then_add", [] { gInventory.addChangedMask(LLInventoryObserver::LABEL, I(12)); gInventory.addChangedMask(LLInventoryObserver::ADD, I(12)); gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        {
            Steps s("remove_to_trash");
            s.step("item", [] { gInventory.removeItem(I(13)); });
            s.step("category_with_active_gesture", []
            {
                gInventory.updateItem(item(I(500), F(42), LLAssetType::AT_GESTURE, LLInventoryType::IT_GESTURE, "Folder gesture", A(500)));
                gInventory.updateItem(link(I(501), F(42), I(15), "Wave", false, LLInventoryType::IT_GESTURE));
                config().active_gestures.insert(I(500));
                config().active_gestures.insert(I(15));
                gInventory.removeCategory(F(42));
            });
            s.step("category_not_removable", [] { config().category_removable = false; gInventory.removeCategory(F(41)); config().category_removable = true; });
            s.step("object_unknown_and_null", [] { gInventory.removeObject(pid(TAG_ITEM, 9999)); gInventory.removeObject(LLUUID::null); gInventory.removeObject(I(12)); });
            s.step("trash_overflow", [] { gSavedSettings.setU32("InventoryTrashMaxCapacity", 3); gInventory.removeObject(F(41)); gSavedSettings.setU32("InventoryTrashMaxCapacity", 5000); });
            cases.push_back(s.c);
        }
        {
            // checkTrashOverflow() notifies when the Trash holds at least
            // InventoryTrashMaxCapacity objects (folders and items): counts
            // one below, at and one above the capacity. The standard Trash
            // starts with 5 (2 folders, 3 items).
            Steps s("trash_capacity");
            s.step("count_7_capacity_8", [] { gSavedSettings.setU32("InventoryTrashMaxCapacity", 8); gInventory.removeCategory(F(41)); });
            s.step("count_10_capacity_10", [] { gSavedSettings.setU32("InventoryTrashMaxCapacity", 10); gInventory.removeCategory(F(40)); });
            s.step("count_12_capacity_11", [] { gSavedSettings.setU32("InventoryTrashMaxCapacity", 11); gInventory.removeCategory(F(43)); });
            s.step("preview_visible", [] { config().trash_preview_visible = true; gInventory.removeCategory(F(42)); });
            s.step("restore", [] { gSavedSettings.setU32("InventoryTrashMaxCapacity", 5000); });
            cases.push_back(s.c);
        }
        {
            Steps s("create_category");
            s.step("ais", []
            {
                gInventory.createNewCategory(F(SF_OBJECTS), LLFolderType::FT_NONE, "Created", [](const LLUUID& id) { vks_inv::note("create callback " + id.asString()); });
            });
            s.step("ais_completes", [] { ais_completions().back().second(F(600)); });
            s.step("ais_completes_existing", [] { ais_completions().back().second(F(600)); });
            s.step("ais_fails", [] { gInventory.createNewCategory(F(SF_OBJECTS), LLFolderType::FT_OUTFIT, "", [](const LLUUID& id) { vks_inv::note("create callback " + id.asString()); }); ais_completions().back().second(LLUUID::null); });
            s.step("bad_type", [] { gInventory.createNewCategory(F(SF_OBJECTS), (LLFolderType::EType)99, "Bad", [](const LLUUID& id) { vks_inv::note("create callback " + id.asString()); }); });
            s.step("no_ais_no_cap", [] { config().ais_available = false; gInventory.createNewCategory(F(SF_OBJECTS), LLFolderType::FT_NONE, "No AIS", [](const LLUUID& id) { vks_inv::note("create callback " + id.asString()); }); config().ais_available = true; });
            s.step("unusable", [] { model::agent_inv_usable() = false; gInventory.createNewCategory(F(SF_OBJECTS), LLFolderType::FT_NONE, "Unusable", [](const LLUUID& id) { vks_inv::note("create callback " + id.asString()); }); model::agent_inv_usable() = true; });
            cases.push_back(s.c);
        }
        {
            World w = standard_world();
            std::vector<Folder> keep;
            for (const Folder& f : w.agent) if (f.id != F(SF_SETTINGS) && f.id != F(SF_MATERIALS) && f.id != F(SF_INBOX)) keep.push_back(f);
            w.agent = keep;
            for (auto& i : w.items) if (i->getParentUUID() == F(SF_SETTINGS)) i->setParent(F(SF_OBJECTS));
            Steps s("system_categories", w);
            s.step("create_common", [] { gInventory.createCommonSystemCategories(); vks_inv::note("pending=" + std::to_string(model::pending_system_folders())); });
            s.step("complete_first", [] { ais_completions().front().second(F(700)); vks_inv::note("pending=" + std::to_string(model::pending_system_folders())); });
            s.step("ensure_existing", [] { gInventory.ensureCategoryForTypeExists(LLFolderType::FT_TRASH); vks_inv::note("pending=" + std::to_string(model::pending_system_folders())); });
            s.step("find_smallest_uuid", []
            {
                gInventory.updateCategory(new LLViewerInventoryCategory(pid(TAG_FOLDER, 0), F(SF_ROOT), LLFolderType::FT_TRASH, "Trash low id", AGENT));
                gInventory.updateCategory(new LLViewerInventoryCategory(pid(TAG_FOLDER, 0xffff), F(SF_ROOT), LLFolderType::FT_TRASH, "Trash high id", AGENT));
                vks_inv::note("trash=" + gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH).asString());
                vks_inv::note("library clothing=" + gInventory.findLibraryCategoryUUIDForType(LLFolderType::FT_CLOTHING).asString());
                vks_inv::note("by name Objects=" + gInventory.findCategoryByName("Objects").asString());
                vks_inv::note("user defined texture=" + gInventory.findUserDefinedCategoryUUIDForType(LLFolderType::FT_TEXTURE).asString());
            });
            cases.push_back(s.c);
        }
        {
            Steps s("on_updated");
            s.step("item_name_desc_parent_version", []
            {
                LLSD u;
                u["name"] = "Updated name";
                u["desc"] = "Updated desc";
                gInventory.onItemUpdated(I(10), u, true);
            });
            s.step("item_missing", [] { LLSD u; u["name"] = "x"; gInventory.onItemUpdated(pid(TAG_ITEM, 9999), u, true); });
            s.step("category_name", [] { LLSD u; u["name"] = "Updated folder"; gInventory.onCategoryUpdated(F(42), u); });
            cases.push_back(s.c);
        }
        {
            Steps s("server_helpers");
            s.step("update_inventory_item_ais", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(10)); i->rename("Via AIS"); update_inventory_item(i, new RecordingCallback("update")); });
            s.step("update_inventory_item_udp", [] { config().ais_available = false; LLPointer<LLViewerInventoryItem> i = copy_of(I(13)); i->rename("Via UDP"); update_inventory_item(i, new RecordingCallback("update")); config().ais_available = true; });
            s.step("update_server_item", [] { config().ais_available = false; gInventory.getItem(I(12))->updateServer(false); config().ais_available = true; gInventory.getItem(I(12))->updateServer(true); });
            s.step("update_parent_on_server", [] { gInventory.getItem(I(11))->updateParentOnServer(true); gInventory.getCategory(F(43))->updateParentOnServer(false); });
            s.step("remove_inventory_item_ais", [] { remove_inventory_item(I(16), new RecordingCallback("remove")); });
            s.step("remove_inventory_item_ais_immediate", [] { remove_inventory_item(I(17), new RecordingCallback("remove"), true); });
            s.step("remove_inventory_category_ais", [] { remove_inventory_category(F(45), new RecordingCallback("remove_cat")); });
            s.step("purge_descendents_of_ais", [] { purge_descendents_of(F(SF_TRASH), new RecordingCallback("purge")); });
            s.step("move_inventory_item", [] { move_inventory_item(AGENT, SESSION, I(12), F(SF_OBJECTS), "Moved chair", new RecordingCallback("move")); });
            s.step("category_fetch_unknown_version_ais", [] { LLViewerInventoryCategory* c = gInventory.getCategory(F(41)); c->setVersion(-1); vks_inv::note(std::string("fetch=") + (c->fetch() ? "1" : "0")); vks_inv::note(std::string("again=") + (c->fetch() ? "1" : "0")); });
            s.step("category_fetch_udp", [] { config().ais_available = false; LLViewerInventoryCategory* c = gInventory.getCategory(F(40)); c->setVersion(-1); vks_inv::note(std::string("fetch=") + (c->fetch() ? "1" : "0")); config().ais_available = true; });
            s.step("category_fetch_known_version", [] { vks_inv::note(std::string("fetch=") + (gInventory.getCategory(F(42))->fetch() ? "1" : "0")); });
            s.step("set_fetching", []
            {
                LLViewerInventoryCategory* c = gInventory.getCategory(F(42));
                c->setFetching(LLViewerInventoryCategory::FETCH_NORMAL);
                vks_inv::note("after normal " + model::fetch_timer_state(c) + " state=" + std::to_string(model::fetching_state(c)));
                c->setFetching(LLViewerInventoryCategory::FETCH_RECURSIVE);
                vks_inv::note("after recursive state=" + std::to_string(model::fetching_state(c)));
                c->setFetching(LLViewerInventoryCategory::FETCH_FAILED);
                vks_inv::note("after failed " + model::fetch_timer_state(c) + " state=" + std::to_string(model::fetching_state(c)));
                vks_inv::note("getFetching=" + std::to_string((S32)c->getFetching()));
                c->setFetching(LLViewerInventoryCategory::FETCH_NONE);
                vks_inv::note("after none " + model::fetch_timer_state(c) + " getFetching=" + std::to_string((S32)c->getFetching()));
            });
            s.step("change_type", [] { gInventory.getCategory(F(43))->changeType(LLFolderType::FT_OUTFIT); });
            cases.push_back(s.c);
        }
        {
            Steps s("observers");
            s.step("added_observer_install", [] { gInventory.addObserver(new AddedObserver); });
            s.step("add_only", [] { gInventory.updateItem(item(I(1100), F(SF_OBJECTS), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Plain add", A(1100))); gInventory.notifyObservers(); });
            s.step("create_and_update_create_without_add", [] { gInventory.updateItem(copy_of(I(10)), LLInventoryObserver::CREATE | LLInventoryObserver::UPDATE_CREATE); gInventory.notifyObservers(); });
            s.step("add_create_update_create", [] { gInventory.updateItem(item(I(1101), F(SF_OBJECTS), LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, "Created", A(1101)), LLInventoryObserver::CREATE | LLInventoryObserver::UPDATE_CREATE); gInventory.notifyObservers(); });
            s.step("descendents_observer", []
            {
                // Furniture complete; Chairs reports fewer children than it
                // holds (the observer writes the count); Outfit B unknown.
                gInventory.getCategory(F(43))->setDescendentCount(0);
                gInventory.getCategory(F(41))->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                uuid_vec_t folders = { F(42), F(43), F(41) };
                DescendentsObserver* o = new DescendentsObserver(folders);
                o->startFetch();
                gInventory.addObserver(o);
                gInventory.addChangedMask(LLInventoryObserver::INTERNAL, F(42));
                gInventory.notifyObservers();
            });
            s.step("descendents_observer_fetcher_idle", [] { config().everything_fetched = true; gInventory.addChangedMask(LLInventoryObserver::INTERNAL, F(42)); gInventory.notifyObservers(); });
            cases.push_back(s.c);
        }
        // Lost items: isSendFull() closes a MoveInventoryItem packet after
        // its 37th InventoryData block (1265 bytes); 36, 37 and 38 pin that
        // limit, 74 and 75 the second packet.
        for (U32 n : { 1u, 36u, 37u, 38u, 74u, 75u, 200u }) cases.push_back(lost_items_case(n));
        {
            Steps s("queries");
            s.step("queries", []
            {
                vks_inv::note(std::string("descendent_of=") + (gInventory.isObjectDescendentOf(I(12), F(SF_OBJECTS)) ? "1" : "0"));
                vks_inv::note(std::string("category_complete=") + (gInventory.isCategoryComplete(F(42)) ? "1" : "0"));
                vks_inv::note("has_children=" + std::to_string((S32)gInventory.categoryHasChildren(F(42))) + "," + std::to_string((S32)gInventory.categoryHasChildren(F(SF_SOUNDS))));
                LLUUID top;
                vks_inv::note("topmost=" + std::to_string((S32)gInventory.getObjectTopmostAncestor(I(12), top)) + " " + top.asString());
                vks_inv::note("names_hash=" + gInventory.hashDirectDescendentNames(F(SF_BODYPARTS)).asString());
                LLInventoryModel::cat_array_t cats;
                LLInventoryModel::item_array_t items;
                gInventory.collectDescendents(F(SF_ROOT), cats, items, LLInventoryModel::EXCLUDE_TRASH);
                std::string order;
                for (const auto& c : cats) order += c->getUUID().asString().substr(30) + " ";
                vks_inv::note("collect cats " + order);
                order.clear();
                for (const auto& i : items) order += i->getUUID().asString().substr(30) + " ";
                vks_inv::note("collect items " + order);
                LLInventoryModel::item_array_t links = gInventory.collectLinksTo(I(4));
                order.clear();
                for (const auto& i : links) order += i->getUUID().asString().substr(30) + " ";
                vks_inv::note("links to shirt " + order);
                vks_inv::note("full path " + q(gInventory.getFullPath(gInventory.getItem(I(12)))));
            });
            cases.push_back(s.c);
        }
    }
}

namespace tut
{
    template<> template<> void invmodel_object::test<3>()
    {
        env().ran.insert(3);
        set_test_name("C3 mutation sequences");
        Cases cases;
        c3_cases(cases);
        corpus("cache/c3_mutations.txt", "C3 mutation sequences", "none", cases);
    }
}

namespace
{
    // True when 'id' is 'ancestor' or below it (a walk with a visited set:
    // moveObject() does not refuse a cycle, and the model's own
    // isObjectDescendentOf() then never returns).
    bool under(const LLUUID& id, const LLUUID& ancestor)
    {
        std::set<LLUUID> seen;
        LLUUID cur = id;
        while (cur.notNull() && seen.insert(cur).second)
        {
            if (cur == ancestor) return true;
            LLViewerInventoryCategory* c = gInventory.getCategory(cur);
            if (!c) return false;
            cur = c->getParentUUID();
        }
        return false;
    }

    // A seeded random sequence of mutations over a small random world.
    // Folder moves that would make a cycle are skipped (see under()).
    Case random_sequence(U64 seed, U32 steps)
    {
        Rng rng(seed);
        World w = random_world(seed, 12, 60);
        Steps s(llformat("random_%llx", (unsigned long long)seed), w);
        std::vector<LLUUID> cats, items;
        auto refresh = [&]()
        {
            cats.clear();
            items.clear();
            for (const auto& kv : model::categories()) cats.push_back(kv.first);
            for (const auto& kv : model::items()) items.push_back(kv.first);
            // The draws index these lists: key order, not the hash table's.
            std::sort(cats.begin(), cats.end());
            std::sort(items.begin(), items.end());
        };
        U64 next_id = 0x10000;
        for (U32 n = 0; n < steps; ++n)
        {
            refresh();
            U32 op = rng.below(12);
            LLUUID c1 = cats[rng.below((U32)cats.size())];
            LLUUID c2 = cats[rng.below((U32)cats.size())];
            LLUUID i1 = items.empty() ? LLUUID::null : items[rng.below((U32)items.size())];
            std::string label = llformat("%u op%u", n, op);
            s.step(label, [&]()
            {
                switch (op)
                {
                case 0: gInventory.updateItem(item(I(next_id), c1, LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, "r", A(next_id))); ++next_id; break;
                case 1: if (i1.notNull()) { LLPointer<LLViewerInventoryItem> i = copy_of(i1); i->rename("renamed " + std::to_string(n)); gInventory.updateItem(i); } break;
                case 2: if (i1.notNull()) gInventory.moveObject(i1, c1); break;
                case 3: if (!under(c2, c1)) gInventory.moveObject(c1, c2); break;
                case 4: if (i1.notNull()) gInventory.changeItemParent(gInventory.getItem(i1), c1, rng.below(2) == 1); break;
                case 5: gInventory.changeCategoryParent(gInventory.getCategory(c1), c2, false); break;
                case 6: if (i1.notNull()) { bool fix = rng.below(2) == 1; bool notify = rng.below(2) == 1; gInventory.deleteObject(i1, fix, notify); } break;
                case 7: { S32 delta = (S32)rng.below(3) - 1; bool bump = rng.below(2) == 1; gInventory.accountForUpdate(LLInventoryModel::LLCategoryUpdate(c1, delta, bump)); } break;
                case 8: gInventory.updateCategory(new LLViewerInventoryCategory(F(next_id), c1, LLFolderType::FT_NONE, "rc", AGENT)); ++next_id; break;
                case 9: gInventory.notifyObservers(); break;
                case 10: if (i1.notNull()) gInventory.updateItem(link(I(next_id), c1, i1, "rl")); ++next_id; break;
                default: if (i1.notNull()) gInventory.onObjectDeletedFromServer(i1, true, true, rng.below(2) == 1); break;
                }
            });
        }
        s.step("final_validate", [] { LLPointer<LLInventoryValidationInfo> v = gInventory.validate(); std::vector<std::string> l; dump_validation(l, *v); for (auto& x : l) vks_inv::note(x); });
        return s.c;
    }
}

namespace tut
{
    // C3, seeded random sequences: two in full, the rest as digests of the
    // complete recorded stream (compared in process; the crate reads the
    // full ones).
    template<> template<> void invmodel_object::test<9>()
    {
        env().ran.insert(9);
        set_test_name("C3 random mutation sweep");
        Cases full, digests;
        Case d;
        d.id = "digests";
        for (U64 seed = 1; seed <= 40; ++seed)
        {
            Case c = random_sequence(0xc3000000ULL + seed, 40);
            std::string all;
            for (const std::string& l : c.lines) all += l + "\n";
            d.lines.push_back(c.id + " lines=" + std::to_string(c.lines.size()) + " sha256=" + sha256_hex(all));
            if (seed <= 2) full.push_back(c);
        }
        full.push_back(d);
        corpus("cache/c3_random.txt", "C3 seeded random mutation sequences", "0xc3000001..0xc3000028 (xorshift64*), 40 steps each", full);
    }
}
