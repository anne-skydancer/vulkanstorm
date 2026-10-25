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
    // Expectations a step must meet (checked in code, in record and replay):
    // a non-empty notification batch (a non-zero mask or changed ids), or
    // none; the number of callbacks fired; journal lines that must or must
    // not occur.
    struct StepExpect
    {
        enum Batch { ANY, NONEMPTY, NONE } batch = ANY;
        S32 callbacks = -1;
        std::vector<std::string> present;
        std::vector<std::string> absent;
    };

    struct MsgSteps : public Steps
    {
        explicit MsgSteps(const std::string& id, const World& w = standard_world()) : Steps(id, w)
        {
            callbacks_reset();
            set_busy_count(5);
            gInventory.notifyObservers();
            clear_journal();
        }

        typedef StepExpect Expect;

        void msg(const std::string& label, const std::function<void()>& fn, const Expect& expect = Expect())
        {
            trace("msgs " + c.id + " " + label);
            c.lines.push_back("step " + label);
            fn();
            std::vector<std::string> journal = take_journal();
            bool nonempty = false;
            S32 fired = 0;
            for (const std::string& l : journal)
            {
                c.lines.push_back("  " + l);
                if (l.compare(0, 6, "batch ") == 0)
                {
                    if (l.compare(0, 14, "batch mask=0x0") != 0 || l.find("changed=[]") == std::string::npos) nonempty = true;
                }
                if (l.compare(0, 9, "callback ") == 0) ++fired;
            }
            std::string where = c.id + "/" + label + ": ";
            if (expect.batch == Expect::NONEMPTY) tut::ensure(where + "expected a non-empty notification batch", nonempty);
            if (expect.batch == Expect::NONE) tut::ensure(where + "expected no non-empty notification batch", !nonempty);
            if (expect.callbacks >= 0) tut::ensure_equals(where + "callbacks fired", fired, expect.callbacks);
            for (const std::string& want : expect.present)
            {
                bool found = false;
                for (const std::string& l : journal) if (l.find(want) != std::string::npos) found = true;
                tut::ensure(where + "expected a journal line with " + want, found);
            }
            for (const std::string& bad : expect.absent)
            {
                for (const std::string& l : journal) tut::ensure(where + "unexpected journal line " + l, l.find(bad) == std::string::npos);
            }
            std::string pending;
            for (U32 id : callbacks_pending()) pending += (pending.empty() ? "" : ",") + std::to_string(id);
            c.lines.push_back("callbacks_registered=[" + pending + "] busy=" + std::to_string(window()->getBusyCount()));
            dump_model_delta(c.lines, seen);
        }
    };

    MsgSteps::Expect expect_batch(S32 callbacks = -1, std::vector<std::string> present = {}, std::vector<std::string> absent = {})
    {
        MsgSteps::Expect e;
        e.batch = MsgSteps::Expect::NONEMPTY;
        e.callbacks = callbacks;
        e.present = present;
        e.absent = absent;
        return e;
    }

    MsgSteps::Expect expect_nothing()
    {
        MsgSteps::Expect e;
        e.batch = MsgSteps::Expect::NONE;
        e.callbacks = 0;
        return e;
    }

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

    // An item record as LLInventoryItem::fromLLSD reads it (asLLSD's keys),
    // plus agent_id and callback_id. integer_flags writes flags as an LLSD
    // integer (fromLLSD reads both); otherwise flags stay asLLSD's 4-byte
    // binary, which notation writes raw (b(4)"...", with NUL bytes) or, as
    // pretty binary, b16"...".
    LLSD item_llsd(const LLViewerInventoryItem& i, U32 callback_id, const LLUUID& agent = AGENT, bool integer_flags = true)
    {
        LLSD sd = i.asLLSD();
        sd["agent_id"] = agent;
        sd["callback_id"] = (LLSD::Integer)callback_id;
        if (integer_flags) sd["flags"] = (LLSD::Integer)i.LLInventoryItem::getFlags();
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

    // The encodings LLDispatchBulkUpdateInventory's LLSDSerialize::deserialize
    // accepts: notation (raw binary or b16 pretty binary) and XML.
    std::string notation_param(const LLSD& sd)
    {
        std::ostringstream s;
        LLSDSerialize::toNotation(sd, s);
        return s.str();
    }
    std::string notation_b16_param(const LLSD& sd)
    {
        std::ostringstream s;
        LLSDSerialize::toPrettyBinaryNotation(sd, s);
        return s.str();
    }
    std::string xml_param(const LLSD& sd)
    {
        std::ostringstream s;
        LLSDSerialize::toXML(sd, s);
        return s.str();
    }

    // LargeGenericMessage through the event queue: LLMessageSystem::dispatch
    // with the message as LLSD ({"body": {...}}), read by the LLSD message
    // reader (getString returns the whole LLSD string, NUL bytes included).
    void large_generic_via_event_queue(const std::string& method, const std::vector<std::string>& params)
    {
        LLSD body;
        body["AgentData"][0]["AgentID"] = AGENT;
        body["AgentData"][0]["SessionID"] = SESSION;
        body["AgentData"][0]["TransactionID"] = LLUUID::null;
        body["MethodData"][0]["Method"] = method;
        body["MethodData"][0]["Invoice"] = pid(TAG_TXN, 9);
        for (size_t k = 0; k < params.size(); ++k) body["ParamList"][(LLSD::Integer)k]["Parameter"] = params[k];
        LLSD message;
        message["body"] = body;
        message["sender"] = "127.0.0.1:13000";
        vks_inv::note("in.event_queue LargeGenericMessage " + llsd_hex(message));
        LLMessageSystem::dispatch("LargeGenericMessage", message);
    }

    // A folder-only notation parameter of exactly 'size' bytes (the name is
    // padded); 0 when the record alone is longer.
    std::string folder_param_of_size(size_t size, U64 n)
    {
        for (size_t pad = 0; pad < 1400; ++pad)
        {
            LLSD m;
            m["folder_data"].append(folder_llsd({ F(n), F(SF_OBJECTS), "P" + std::string(pad, 'x'), LLFolderType::FT_NONE, 0 }));
            std::string p = notation_param(m);
            if (p.size() == size) return p;
            if (p.size() > size) break;
        }
        return std::string();
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
            s.msg("item_into_unknown_version_fetches_parent", []
            {
                // Outfit B: version made unknown here, its fetch timer expired
                // since it was loaded, so the parent fetch() is observable.
                gInventory.getCategory(F(41))->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                bulk_update({}, { { new_item(916, F(41), "Into unknown"), cb("bu7") } }, pid(TAG_TXN, 9));
            }, expect_batch(1, { "fetch.start " + F(41).asString() }));
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
                config().marketplace_depth[F(923)] = 0;
                config().listing_ids[F(920)] = 4242;
                config().listing_ids[F(SF_OBJECTS)] = 4343;
                bulk_update({ { F(920), F(SF_OBJECTS), "Listing", LLFolderType::FT_NONE, 0 }, { F(921), F(SF_OBJECTS), "Version", LLFolderType::FT_NONE, 0 },
                              { F(922), F(SF_OBJECTS), "Stock", LLFolderType::FT_MARKETPLACE_STOCK, 0 },
                              { F(923), F(SF_OBJECTS), "Depth 0 (the listings root)", LLFolderType::FT_NONE, 0 } }, {}, pid(TAG_TXN, 11));
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
            // BulkUpdateInventory through gGenericDispatcher, which
            // LLDispatchBulkUpdateInventory reads with LLSDSerialize::deserialize
            // (notation, or XML/binary with their headers). Carriers:
            //  - GenericMessage (UDP): a Variable 1 parameter, at most 255
            //    bytes with its NUL (254 of text): a folder record fits, an
            //    item record never does;
            //  - LargeGenericMessage over UDP (UDPDeprecated): the template
            //    reader's getString() keeps at most 1200 bytes of text (a
            //    1201-byte parameter is cut) and stops at the first NUL: one item record
            //    with integer flags fits, two do not, raw binary is cut;
            //  - LargeGenericMessage through the event queue (the LLSD
            //    message reader): the whole string, NULs included.
            MsgSteps s("bulk_update_generic");
            s.msg("register_dispatcher", [] { model::register_generic_dispatch(); });
            s.msg("generic_folder_only", []
            {
                LLSD m;
                m["folder_data"].append(folder_llsd({ F(950), F(SF_OBJECTS), "Generic folder", LLFolderType::FT_NONE, 0 }));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(0));
            s.msg("generic_param_253_bytes", [] { generic_message(false, "BulkUpdateInventory", { folder_param_of_size(253, 951) }); }, expect_batch(0));
            s.msg("generic_param_254_bytes_limit", [] { generic_message(false, "BulkUpdateInventory", { folder_param_of_size(254, 952) }); }, expect_batch(0));
            s.msg("generic_param_255_bytes_cut", [] { generic_message(false, "BulkUpdateInventory", { folder_param_of_size(255, 953) }); }, expect_nothing());
            s.msg("generic_item_does_not_fit", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(954, F(SF_OBJECTS), "Too long"), cb("g0")));
                generic_message(false, "BulkUpdateInventory", { notation_param(m) });
            }, expect_nothing());
            s.msg("large_udp_param_1199_bytes", [] { generic_message(true, "BulkUpdateInventory", { folder_param_of_size(1199, 955) }); }, expect_batch(0));
            s.msg("large_udp_param_1200_bytes_limit", [] { generic_message(true, "BulkUpdateInventory", { folder_param_of_size(1200, 956) }); }, expect_batch(0));
            s.msg("large_udp_param_1201_bytes_cut", [] { generic_message(true, "BulkUpdateInventory", { folder_param_of_size(1201, 957) }); }, expect_nothing());
            s.msg("large_udp_one_item_integer_flags", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(958, F(SF_OBJECTS), "UDP item", LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, 0x123), cb("g1")));
                generic_message(true, "BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(1));
            s.msg("large_udp_two_items_cut", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(959, F(SF_OBJECTS), "UDP first"), cb("g2")));
                m["item_data"].append(item_llsd(*new_item(960, F(SF_OBJECTS), "UDP second"), cb("g3")));
                generic_message(true, "BulkUpdateInventory", { notation_param(m) });
            }, expect_nothing());
            s.msg("large_udp_raw_binary_cut_at_nul", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(961, F(SF_OBJECTS), "raw binary flags"), cb("g4"), AGENT, false));
                generic_message(true, "BulkUpdateInventory", { notation_param(m) });
            }, expect_nothing());
            s.msg("queue_items_and_folder", []
            {
                // Items before folders in the message; the first item's parent
                // is the folder of the same message (unpacked after the items,
                // applied before them); per-item callback ids.
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(962, F(963), "Queued item in new folder"), cb("g5")));
                m["item_data"].append(item_llsd(*new_item(964, F(SF_OBJECTS), "Queued item 2", LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, 0x123), cb("g6")));
                m["folder_data"].append(folder_llsd({ F(963), F(SF_OBJECTS), "Queued folder", LLFolderType::FT_NONE, 0 }));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(2, { "callback g5 " + I(962).asString(), "callback g6 " + I(964).asString() }));
            s.msg("queue_raw_binary_flags", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(965, F(SF_OBJECTS), "raw binary flags", LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, 0x80000001), cb("g7"), AGENT, false));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(1));
            s.msg("queue_b16_binary_flags", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(966, F(SF_OBJECTS), "b16 flags", LLAssetType::AT_OBJECT, LLInventoryType::IT_OBJECT, 0x80000002), cb("g8"), AGENT, false));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_b16_param(m) });
            }, expect_batch(1));
            s.msg("queue_xml", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(967, F(SF_OBJECTS), "XML item"), cb("g9"), AGENT, false));
                large_generic_via_event_queue("BulkUpdateInventory", { xml_param(m) });
            }, expect_batch(1));
            s.msg("queue_existing_folder_resets_version", []
            {
                LLSD m;
                m["folder_data"].append(folder_llsd({ F(42), F(SF_OBJECTS), "Furniture", LLFolderType::FT_NONE, 0 }));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(0));
            s.msg("queue_item_into_unknown_version_no_fetch", []
            {
                // Outfit B: unknown version, fetch timer expired: the UDP form
                // would fetch() it; this form does not.
                gInventory.getCategory(F(41))->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(968, F(41), "Into unknown, no fetch"), cb("g10")));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(1, {}, { "fetch." }));
            s.msg("queue_folder_with_folder_id_key", []
            {
                LLSD m;
                m["folder_data"].append(folder_llsd({ F(969), F(SF_OBJECTS), "Keyed folder_id", LLFolderType::FT_NONE, 0 }, true));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_nothing());
            s.msg("queue_foreign_agent_midway", []
            {
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(970, F(SF_OBJECTS), "Mine"), cb("g11")));
                m["item_data"].append(item_llsd(*new_item(971, F(SF_OBJECTS), "Not mine"), cb("g12"), pid(0xbad00000, 1)));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_nothing());
            s.msg("queue_wear_rule_without_transaction", []
            {
                LLInventoryState::sWearNewClothing = true;
                LLSD m;
                m["item_data"].append(item_llsd(*new_item(972, F(SF_CLOTHING), "Generic shirt", LLAssetType::AT_CLOTHING, LLInventoryType::IT_WEARABLE, 4), cb("g13")));
                m["item_data"].append(item_llsd(*new_item(973, F(SF_OBJECTS), "Generic box"), cb("g14")));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(2, { "appearance.wear " + I(972).asString() }));
            s.msg("queue_six_items", []
            {
                LLSD m;
                for (U64 n = 980; n < 986; ++n) m["item_data"].append(item_llsd(*new_item(n, F(SF_OBJECTS), "Queued " + std::to_string(n)), cb("q" + std::to_string(n))));
                large_generic_via_event_queue("BulkUpdateInventory", { notation_param(m) });
            }, expect_batch(6));
            s.msg("queue_unparsable_parameter", [] { large_generic_via_event_queue("BulkUpdateInventory", { "{not llsd" }); }, expect_nothing());
            s.msg("unknown_method", [] { generic_message(false, "NoSuchMethod", { "x" }); }, expect_nothing());
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
        {
            // Item creation and item copy with AIS available. On Second Life
            // both go over UDP whatever AIS offers: create_inventory_item's AIS
            // branch is compiled only with USE_AIS_FOR_NC ("not yet implemented
            // within AIS3"), and copy_inventory_item has none. The server's
            // reply carries the callback id the request registered, which
            // fires once when the item arrives.
            MsgSteps s("create_and_copy_with_ais");
            static U32 request_cb = 0;
            const auto newest_cb = [] { U32 id = 0; for (U32 c : callbacks_pending()) id = std::max(id, c); return id; };
            MsgSteps::Expect request;
            request.batch = MsgSteps::Expect::NONE;
            request.callbacks = 0;
            request.absent = { "ais " };
            request.present = { "msg CreateInventoryItem " };
            s.msg("create_inventory_item_sends_udp", [newest_cb]
            {
                config().ais_available = true;
                LLTransactionID tid;
                static_cast<LLUUID&>(tid) = pid(TAG_TXN, 20);
                // The harness agent is zeroed storage: its session id is null, as
                // copy_inventory_item writes it.
                create_inventory_item(AGENT, LLUUID::null, F(SF_OBJECTS), tid, "New Note", "A note",
                                      LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD, NO_INV_SUBTYPE, PERM_ALL,
                                      new RecordingCallback("cr1"));
                request_cb = newest_cb();
            }, request);
            s.msg("update_create_reply", []
            {
                update_create({ { new_item(980, F(SF_OBJECTS), "New Note", LLAssetType::AT_NOTECARD, LLInventoryType::IT_NOTECARD), request_cb } });
            }, expect_batch(1, { "callback cr1 " + I(980).asString() }));
            request.present = { "msg CopyInventoryItem " };
            s.msg("copy_inventory_item_sends_udp", [newest_cb]
            {
                config().ais_available = true;
                copy_inventory_item(AGENT, AGENT, I(10), F(42), "Copied chair", new RecordingCallback("cp1"));
                request_cb = newest_cb();
            }, request);
            s.msg("bulk_update_reply", []
            {
                bulk_update({}, { { new_item(981, F(42), "Copied chair"), request_cb } }, pid(TAG_TXN, 21));
            }, expect_batch(1, { "callback cp1 " + I(981).asString() }));
            cases.push_back(s.c);
        }
    }
}

namespace tut
{
    template<> template<> void invmodel_object::test<5>()
    {
        env().ran.insert(5);
        set_test_name("set 8 inventory message handlers");
        Cases cases;
        msgs_cases(cases);
        corpus("msgs/set8_handlers.txt", "survey 4 set 8: the inventory UDP message handlers", "none", cases);
    }
}
