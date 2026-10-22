/**
 * @file llinventorymodel_parity_msgs.h
 * @brief Survey 4 set 8 of llinventorymodel_parity_test.cpp: the inventory
 *        UDP message handlers (included once, after the C4 corpus).
 *
 * Each message is built with the real template builder through
 * gMessageSystem, journaled as "in <name> <hex>", read by the real template
 * reader, which calls the handler LLInventoryModel::registerCallbacks()
 * registered (GenericMessage and LargeGenericMessage go to the frozen copies
 * of llviewergenericmessage.cpp's handlers and on to the dispatcher
 * LLInventoryModel registers). Recorded per step: every journal line
 * (notification batches with their transaction id, callbacks fired, fetch
 * requests, marketplace and appearance calls, outbound messages), the
 * callback ids left registered (never fired), the window's busy count, and
 * the model lines that changed.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

namespace
{
    struct MsgSteps : public Steps
    {
        explicit MsgSteps(const std::string& id, const World& w = standard_world()) : Steps(id, w)
        {
            callbacks_reset();
            set_busy_count(5);
            gInventory.notifyObservers();
            clear_journal();
        }

        void msg(const std::string& label, const std::function<void()>& fn)
        {
            trace("msgs " + c.id + " " + label);
            c.lines.push_back("step " + label);
            fn();
            take(c.lines);
            std::string pending;
            for (U32 id : callbacks_pending()) pending += (pending.empty() ? "" : ",") + std::to_string(id);
            c.lines.push_back("callbacks_registered=[" + pending + "] busy=" + std::to_string(window()->getBusyCount()));
            dump_model_delta(c.lines, seen);
        }
    };

    U32 cb(const std::string& tag)
    {
        return gInventoryCallbacks.registerCB(new RecordingCallback(tag));
    }

    void agent_data(LLMessageSystem* msg, const LLUUID& agent = AGENT)
    {
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, agent);
    }

    void update_create(const std::vector<std::pair<LLPointer<LLViewerInventoryItem>, U32> >& items, const LLUUID& agent = AGENT)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_UpdateCreateInventoryItem);
        agent_data(msg, agent);
        msg->addBOOLFast(_PREHASH_SimApproved, true);
        msg->addUUIDFast(_PREHASH_TransactionID, pid(TAG_TXN, 7));
        for (const auto& p : items)
        {
            msg->nextBlockFast(_PREHASH_InventoryData);
            pack_item(msg, *p.first, p.second);
        }
        send_to_viewer();
    }

    void bulk_update(const std::vector<Folder>& folders, const std::vector<std::pair<LLPointer<LLViewerInventoryItem>, U32> >& items,
                     const LLUUID& tid, const LLUUID& agent = AGENT)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_BulkUpdateInventory);
        agent_data(msg, agent);
        msg->addUUIDFast(_PREHASH_TransactionID, tid);
        for (const Folder& f : folders)
        {
            msg->nextBlockFast(_PREHASH_FolderData);
            msg->addUUIDFast(_PREHASH_FolderID, f.id);
            msg->addUUIDFast(_PREHASH_ParentID, f.parent);
            msg->addS8Fast(_PREHASH_Type, (S8)f.type);
            msg->addStringFast(_PREHASH_Name, f.name);
        }
        for (const auto& p : items)
        {
            msg->nextBlockFast(_PREHASH_ItemData);
            pack_item(msg, *p.first, p.second);
        }
        send_to_viewer();
    }

    void ids_message(const char* name, const char* block, const char* var, const std::vector<LLUUID>& ids_list,
                     const LLUUID& agent = AGENT, const char* block2 = NULL, const std::vector<LLUUID>* ids2 = NULL)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessage(name);
        msg->nextBlock("AgentData");
        msg->addUUID("AgentID", agent);
        msg->addUUID("SessionID", SESSION);
        for (const LLUUID& id : ids_list)
        {
            msg->nextBlock(block);
            msg->addUUID(var, id);
        }
        if (block2 && ids2)
        {
            for (const LLUUID& id : *ids2)
            {
                msg->nextBlock(block2);
                msg->addUUID("ItemID", id);
            }
        }
        send_to_viewer();
    }

    void move_item_message(const std::vector<std::tuple<LLUUID, LLUUID, std::string> >& moves, const LLUUID& agent = AGENT)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_MoveInventoryItem);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, agent);
        msg->addUUIDFast(_PREHASH_SessionID, SESSION);
        msg->addBOOLFast(_PREHASH_Stamp, true);
        for (const auto& m : moves)
        {
            msg->nextBlockFast(_PREHASH_InventoryData);
            msg->addUUIDFast(_PREHASH_ItemID, std::get<0>(m));
            msg->addUUIDFast(_PREHASH_FolderID, std::get<1>(m));
            msg->addString("NewName", std::get<2>(m));
        }
        send_to_viewer();
    }

    void save_asset_message(const LLUUID& item_id, const LLUUID& agent = AGENT)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_SaveAssetIntoInventory);
        agent_data(msg, agent);
        msg->nextBlockFast(_PREHASH_InventoryData);
        msg->addUUIDFast(_PREHASH_ItemID, item_id);
        msg->addUUIDFast(_PREHASH_NewAssetID, pid(TAG_ASSET, 0xfff));
        send_to_viewer();
    }

    void generic_message(bool large, const std::string& method, const std::vector<std::string>& params, const LLUUID& agent = AGENT)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessage(large ? "LargeGenericMessage" : "GenericMessage");
        msg->nextBlock("AgentData");
        msg->addUUID("AgentID", agent);
        msg->addUUID("SessionID", SESSION);
        msg->addUUID("TransactionID", LLUUID::null);
        msg->nextBlock("MethodData");
        msg->addString("Method", method);
        msg->addUUID("Invoice", pid(TAG_TXN, 9));
        for (const std::string& p : params)
        {
            msg->nextBlock("ParamList");
            msg->addString("Parameter", p);
        }
        send_to_viewer();
    }

    LLSD item_llsd(const LLViewerInventoryItem& i, U32 callback_id, const LLUUID& agent = AGENT)
    {
        LLSD sd = i.asLLSD();
        sd["agent_id"] = agent;
        sd["callback_id"] = (LLSD::Integer)callback_id;
        return sd;
    }

    // The keys LLInventoryCategory::fromLLSD reads (category_id, parent_id,
    // type_default, name); folder_id_key true writes folder_id instead,
    // which the parser does not read.
    LLSD folder_llsd(const Folder& f, bool folder_id_key = false)
    {
        LLSD sd;
        sd[folder_id_key ? "folder_id" : "category_id"] = f.id;
        sd["parent_id"] = f.parent;
        sd["type_default"] = f.type;
        sd["name"] = f.name;
        return sd;
    }

    std::string notation_param(const LLSD& sd)
    {
        std::ostringstream s;
        LLSDSerialize::toNotation(sd, s);
        return s.str();
    }

    void inventory_descendents(const LLUUID& folder, S32 version, S32 descendents, const std::vector<Folder>& folders,
                               const std::vector<LLPointer<LLViewerInventoryItem> >& items)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_InventoryDescendents);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, AGENT);
        msg->addUUIDFast(_PREHASH_FolderID, folder);
        msg->addUUIDFast(_PREHASH_OwnerID, AGENT);
        msg->addS32Fast(_PREHASH_Version, version);
        msg->addS32Fast(_PREHASH_Descendents, descendents);
        for (const Folder& f : folders)
        {
            msg->nextBlockFast(_PREHASH_FolderData);
            msg->addUUIDFast(_PREHASH_FolderID, f.id);
            msg->addUUIDFast(_PREHASH_ParentID, f.parent);
            msg->addS8Fast(_PREHASH_Type, (S8)f.type);
            msg->addStringFast(_PREHASH_Name, f.name);
        }
        for (const auto& i : items)
        {
            msg->nextBlockFast(_PREHASH_ItemData);
            pack_item(msg, *i, 0, false);
        }
        send_to_viewer();
    }

    void fetch_inventory_reply(const std::vector<LLPointer<LLViewerInventoryItem> >& items)
    {
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessageFast(_PREHASH_FetchInventoryReply);
        agent_data(msg);
        for (const auto& i : items)
        {
            msg->nextBlockFast(_PREHASH_InventoryData);
            pack_item(msg, *i, 0, false);
        }
        send_to_viewer();
    }

    LLPointer<LLViewerInventoryItem> new_item(U64 n, const LLUUID& parent, const std::string& name,
                                              LLAssetType::EType t = LLAssetType::AT_OBJECT,
                                              LLInventoryType::EType inv = LLInventoryType::IT_OBJECT, U32 flags = 0)
    {
        return item(I(n), parent, t, inv, name, A(n), AGENT, PERM_ALL, flags);
    }

    void msgs_cases(Cases& cases)
    {
        {
            MsgSteps s("update_create_inventory_item");
            s.msg("new_item_callback", [] { update_create({ { new_item(900, F(SF_OBJECTS), "Rezzed back"), cb("uc1") } }); });
            s.msg("existing_item_moved", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(13)); i->setParent(F(SF_OBJECTS)); update_create({ { i, cb("uc2") } }); });
            s.msg("existing_item_same_parent", [] { update_create({ { copy_of(I(12)), 0 } }); });
            s.msg("two_blocks_block0_fires", [] { update_create({ { new_item(901, F(SF_OBJECTS), "First"), cb("uc3") }, { new_item(902, F(42), "Second"), cb("uc4") } }); });
            s.msg("parent_unknown_version", [] { gInventory.getCategory(F(43))->setVersion(-1); update_create({ { new_item(903, F(43), "Into unknown"), cb("uc5") } }); });
            s.msg("localised_name", [] { update_create({ { new_item(904, F(SF_SCRIPTS), "New Script", LLAssetType::AT_LSL_TEXT, LLInventoryType::IT_LSL), 0 } }); });
            s.msg("parent_missing", [] { update_create({ { new_item(905, pid(TAG_FOLDER, 0x4444), "No parent"), cb("uc6") } }); });
            s.msg("callback_id_0", [] { update_create({ { new_item(906, F(SF_OBJECTS), "No callback"), 0 } }); });
            s.msg("wrong_agent", [] { update_create({ { new_item(907, F(SF_OBJECTS), "Not mine"), cb("uc7") } }, pid(0xbad00000, 1)); });
            cases.push_back(s.c);
        }
        {
            MsgSteps s("bulk_update_udp");
            s.msg("new_folder_and_item", [] { bulk_update({ { F(910), F(SF_OBJECTS), "Bought folder", LLFolderType::FT_NONE, 0 } }, { { new_item(910, F(910), "Bought item"), cb("bu1") } }, pid(TAG_TXN, 1)); });
            s.msg("existing_folder_same_parent_resets_version_q3", [] { bulk_update({ { F(42), F(SF_OBJECTS), "Furniture", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 2)); });
            s.msg("existing_folder_unknown_version_fetches", [] { bulk_update({ { F(42), F(SF_OBJECTS), "Furniture", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 3)); });
            s.msg("existing_folder_moved", [] { bulk_update({ { F(45), F(SF_OBJECTS), "Older stuff", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 4)); });
            s.msg("new_folder_parent_unknown_version", [] { bulk_update({ { F(911), F(42), "Under unknown", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 5)); });
            s.msg("new_folder_parent_missing", [] { bulk_update({ { F(912), pid(TAG_FOLDER, 0x5555), "Parent missing", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 6)); });
            s.msg("items_multi_block_callback_block0", []
            {
                bulk_update({}, { { new_item(913, F(SF_OBJECTS), "Multi 1"), cb("bu2") }, { new_item(914, F(SF_OBJECTS), "Multi 2"), cb("bu3") },
                                  { new_item(915, F(SF_OBJECTS), "Multi 3"), cb("bu4") } }, pid(TAG_TXN, 7));
            });
            s.msg("item_existing_same_and_moved", [] { LLPointer<LLViewerInventoryItem> m = copy_of(I(11)); m->setParent(F(SF_OBJECTS)); bulk_update({}, { { copy_of(I(10)), cb("bu5") }, { m, cb("bu6") } }, pid(TAG_TXN, 8)); });
            s.msg("item_into_unknown_version_fetches_parent", [] { bulk_update({}, { { new_item(916, F(42), "Into unknown"), cb("bu7") } }, pid(TAG_TXN, 9)); });
            s.msg("null_item_block", []
            {
                LLPointer<LLViewerInventoryItem> n = new_item(917, F(SF_OBJECTS), "Null id");
                n->setUUID(LLUUID::null);
                bulk_update({}, { { n, cb("bu8") }, { new_item(918, F(SF_OBJECTS), "After null"), cb("bu9") } }, pid(TAG_TXN, 10));
            });
            s.msg("marketplace_depths", []
            {
                config().marketplace_depth[F(920)] = 1;
                config().marketplace_depth[F(921)] = 2;
                config().marketplace_depth[F(922)] = 3;
                config().listing_ids[F(920)] = 4242;
                config().listing_ids[F(SF_OBJECTS)] = 4343;
                bulk_update({ { F(920), F(SF_OBJECTS), "Listing", LLFolderType::FT_NONE, 0 }, { F(921), F(SF_OBJECTS), "Version", LLFolderType::FT_NONE, 0 },
                              { F(922), F(SF_OBJECTS), "Stock", LLFolderType::FT_MARKETPLACE_STOCK, 0 } }, {}, pid(TAG_TXN, 11));
            });
            s.msg("rlv_rename_kept", []
            {
                gInventory.getCategory(F(44))->setVersion(3);
                set_rlv_enabled(true);
                bulk_update({ { F(44), F(SF_TRASH), "#RLV/~new name", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 12));
                set_rlv_enabled(false);
            });
            s.msg("rlv_off_rename_applied", [] { gInventory.getCategory(F(43))->setVersion(2); bulk_update({ { F(43), F(42), "#RLV/~chairs", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 13)); });
            s.msg("wear_new_clothing", []
            {
                LLInventoryState::sWearNewClothing = true;
                bulk_update({}, { { new_item(930, F(SF_CLOTHING), "Bought shirt", LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, 4), cb("bu10") },
                                  { new_item(931, F(SF_OBJECTS), "Bought box"), cb("bu11") } }, pid(TAG_TXN, 14));
            });
            s.msg("wear_same_transaction_next_packet", [] { bulk_update({}, { { new_item(932, F(SF_CLOTHING), "Bought pants", LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, 5), cb("bu12") } }, pid(TAG_TXN, 14)); });
            s.msg("wear_other_transaction", [] { bulk_update({}, { { new_item(933, F(SF_CLOTHING), "Other", LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, 5), 0 } }, pid(TAG_TXN, 15)); });
            s.msg("wrong_agent", [] { bulk_update({ { F(940), F(SF_OBJECTS), "Not mine", LLFolderType::FT_NONE, 0 } }, { { new_item(940, F(SF_OBJECTS), "Not mine"), cb("bu13") } }, pid(TAG_TXN, 16), pid(0xbad00000, 1)); });
            s.msg("empty_packet", [] { bulk_update({}, {}, LLUUID::null); });
            cases.push_back(s.c);
        }
        {
            MsgSteps s("bulk_update_generic");
            s.msg("register_dispatcher", [] { model::register_generic_dispatch(); });
            s.msg("items_and_folder", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(950, F(951), "Generic item"), cb("g1")));
                m["item_data"].append(item_llsd(*new_item(952, F(SF_OBJECTS), "Generic item 2"), cb("g2")));
                m["folder_data"].append(folder_llsd({ F(951), F(SF_OBJECTS), "Generic folder", LLFolderType::FT_NONE, 0 }));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("existing_folder_resets_version", []
            {
                LLSD m;
                m["folder_data"].append(folder_llsd({ F(42), F(SF_OBJECTS), "Furniture", LLFolderType::FT_NONE, 0 }));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("folder_with_folder_id_key", []
            {
                LLSD m;
                m["folder_data"].append(folder_llsd({ F(956), F(SF_OBJECTS), "Keyed folder_id", LLFolderType::FT_NONE, 0 }, true));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("foreign_agent_midway", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(953, F(SF_OBJECTS), "Mine"), cb("g3")));
                m["item_data"].append(item_llsd(*new_item(954, F(SF_OBJECTS), "Not mine"), cb("g4"), pid(0xbad00000, 1)));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("wear_rule_without_transaction", []
            {
                LLInventoryState::sWearNewClothing = true;
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(955, F(SF_CLOTHING), "Generic shirt", LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, 4), cb("g5")));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("unparsable_parameter", [] { generic_message(false, "BulkUpdateInventory", { "{not llsd" }); });
            s.msg("large_generic", []
            {
                LLSD m;
                for (U64 n = 960; n < 966; ++n) m["item_data"].append(item_llsd(*new_item(n, F(SF_OBJECTS), "Large generic " + std::to_string(n)), cb("lg" + std::to_string(n))));
                generic_message(true, "BulkUpdateInventory", { notation_param(m) });
            });
            s.msg("unknown_method", [] { generic_message(false, "NoSuchMethod", { "x" }); });
            cases.push_back(s.c);
        }
        {
            MsgSteps s("remove_messages");
            s.msg("remove_items", [] { ids_message("RemoveInventoryItem", "InventoryData", "ItemID", { I(10), I(13), pid(TAG_ITEM, 0x7777) }); });
            s.msg("remove_items_wrong_agent", [] { ids_message("RemoveInventoryItem", "InventoryData", "ItemID", { I(11) }, pid(0xbad00000, 1)); });
            s.msg("remove_folder_with_children", [] { ids_message("RemoveInventoryFolder", "FolderData", "FolderID", { F(42), pid(TAG_FOLDER, 0x7777) }); });
            std::vector<LLUUID> objs = { I(14), I(4) };
            s.msg("remove_objects_folders_then_items", [objs] { ids_message("RemoveInventoryObjects", "FolderData", "FolderID", { F(44) }, AGENT, "ItemData", &objs); });
            cases.push_back(s.c);
        }
        {
            MsgSteps s("save_asset_and_move");
            s.msg("save_asset_known", [] { save_asset_message(I(10)); });
            s.msg("save_asset_unknown", [] { save_asset_message(pid(TAG_ITEM, 0x7777)); });
            s.msg("save_asset_rlv", [] { set_rlv_enabled(true); save_asset_message(I(12)); set_rlv_enabled(false); });
            s.msg("save_asset_wrong_agent", [] { save_asset_message(I(10), pid(0xbad00000, 1)); });
            s.msg("move_with_rename", [] { move_item_message({ std::make_tuple(I(10), F(42), std::string("Renamed by move")) }); });
            s.msg("move_two_and_unknown", [] { move_item_message({ std::make_tuple(I(11), F(SF_OBJECTS), std::string()), std::make_tuple(pid(TAG_ITEM, 0x7777), F(SF_OBJECTS), std::string("x")), std::make_tuple(I(12), F(42), std::string()) }); });
            s.msg("move_unknown_only", [] { move_item_message({ std::make_tuple(pid(TAG_ITEM, 0x7777), F(SF_OBJECTS), std::string()) }); });
            s.msg("move_wrong_agent", [] { move_item_message({ std::make_tuple(I(13), F(SF_OBJECTS), std::string()) }, pid(0xbad00000, 1)); });
            cases.push_back(s.c);
        }
        {
            MsgSteps s("udp_fetch_replies");
            s.msg("inventory_descendents", []
            {
                inventory_descendents(F(SF_OBJECTS), 20, 4, { { F(42), F(SF_OBJECTS), "Furniture", LLFolderType::FT_NONE, 0 }, { F(970), F(SF_OBJECTS), "New sub", LLFolderType::FT_NONE, 0 } },
                                      { copy_of(I(10)), new_item(970, F(SF_OBJECTS), "Fetched") });
            });
            s.msg("inventory_descendents_unknown_parent", [] { inventory_descendents(pid(TAG_FOLDER, 0x7777), 1, 0, {}, {}); });
            s.msg("fetch_inventory_reply_no_accounting", [] { LLPointer<LLViewerInventoryItem> i = copy_of(I(13)); i->setParent(F(SF_OBJECTS)); fetch_inventory_reply({ i, new_item(971, F(SF_OBJECTS), "Fetched item") }); });
            cases.push_back(s.c);
        }
    }
}

namespace tut
{
    template<> template<> void invmodel_object::test<5>()
    {
        set_test_name("set 8 inventory message handlers");
        Cases cases;
        msgs_cases(cases);
        corpus("msgs/set8_handlers.txt", "survey 4 set 8: the inventory UDP message handlers", "none", cases);
    }
}
