/**
 * @file llinventorymodel_parity_harness.h
 * @brief Recording helpers over the real inventory model (tests only).
 *
 * Built on llinventorymodel_parity_stubs.h. Reusable by any test that drives
 * the real LLInventoryModel (the R2 model/cache oracle, the R3b fetch
 * scenarios):
 *
 *   - ids: patterned and seeded (xorshift64*) UUIDs;
 *   - Recorder: an LLInventoryObserver that writes every notification batch
 *     (union mask, transaction id, changed and added ids in UUID order) into
 *     the stubs' journal, and an LLInventoryCallback that writes every
 *     callback fired;
 *   - dump_model(): the canonical text snapshot of the whole model (every
 *     field of every folder and item, every child vector in order, backlinks,
 *     broken-link lists, locks, the pending change batch, root ids, the
 *     usable flag);
 *   - dump_validation(): LLInventoryValidationInfo::asLLSD as binary-LLSD hex
 *     plus a notation rendering, and mLog;
 *   - message builders for the inventory UDP messages, dispatched through
 *     the real template reader to the handlers LLInventoryModel registers;
 *   - Case/compare: text fixtures ("== <case>" sections) compared line by
 *     line, reporting case, line number, expected and actual.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLINVENTORYMODEL_PARITY_HARNESS_H
#define LL_LLINVENTORYMODEL_PARITY_HARNESS_H

#include "llinventorymodel_parity_stubs.h"

#include "../llinventorymodel.h"
#include "../llinventoryobserver.h"
#include "../llviewerinventory.h"

#include <map>
#include <string>
#include <vector>

namespace vks_inv
{
    // ---- ids and the seeded generator ---------------------------------------
    struct Rng
    {
        U64 s;
        explicit Rng(U64 seed) : s(seed ? seed : 0x9E3779B97F4A7C15ULL) {}
        U64 next()  // xorshift64*
        {
            s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
            return s * 0x2545F4914F6CDD1DULL;
        }
        U32 below(U32 n) { return n ? (U32)((next() >> 11) % n) : 0; }
        LLUUID uuid();
    };
    // "<tag8>-0000-4000-8000-<n as 12 hex digits>": tag in the first four
    // bytes, so ids sort by tag, then by n.
    LLUUID pid(U32 tag, U64 n);

    // ---- recording --------------------------------------------------------------
    std::string q(const std::string& s);        // quoted, escaped (\\, \", \xNN)
    std::string ids(const std::set<LLUUID>& s);
    std::string hexmask(U32 mask);

    // LLInventoryModel calls its observers in pointer order: observers made
    // by the tests live in fixed slots of one static block (slot 0 lowest)
    // so that order is the same in every run. At most one live object per
    // slot; the model deletes its observers, which frees nothing.
    void* observer_slot(int slot, size_t size);

    class Recorder : public LLInventoryObserver
    {
    public:
        void changed(U32 mask) override;
        S32 mBatches = 0;
        static void* operator new(size_t size) { return observer_slot(0, size); }
        static void operator delete(void*) {}
    };
    // Installs one Recorder into gInventory (after model::reset()).
    Recorder* install_recorder();

    class RecordingCallback : public LLInventoryCallback
    {
    public:
        explicit RecordingCallback(const std::string& tag) : mTag(tag) {}
        void fire(const LLUUID& inv_item) override;
        std::string mTag;
    };

    // ---- snapshots ----------------------------------------------------------------
    std::string dump_category(const LLViewerInventoryCategory* cat);
    std::string dump_item(const LLViewerInventoryItem* item);
    void dump_model(std::vector<std::string>& out, bool with_objects = true);
    void dump_validation(std::vector<std::string>& out, const LLInventoryValidationInfo& info);
    std::string llsd_hex(const LLSD& sd);         // binary LLSD (no header), hex
    std::string llsd_notation(const LLSD& sd);

    // ---- messages ----------------------------------------------------------------
    // Item fields as packed by LLViewerInventoryItem::packMessage().
    // with_callback false for the blocks without CallbackID
    // (InventoryDescendents, FetchInventoryReply).
    void pack_item(LLMessageSystem* msg, const LLViewerInventoryItem& item, U32 callback_id, bool with_callback = true);
    // Builds the message pending in gMessageSystem and dispatches it to the
    // registered handler through the template reader; journals
    // "in <name> <hex>" first.
    bool send_to_viewer();

    // ---- fixtures ----------------------------------------------------------------
    struct Case
    {
        std::string id;
        std::vector<std::string> lines;
    };
    typedef std::vector<Case> Cases;
    std::string read_file(const std::string& path);
    bool write_file(const std::string& path, const std::string& body);
    bool file_exists(const std::string& path);
    Cases parse_cases(const std::string& text);
    std::string format_cases(const std::string& header, const Cases& cases);
    // Compares; returns a readable report of at most max_reports differences
    // (empty when equal).
    std::string compare_cases(const Cases& expected, const Cases& actual, size_t max_reports = 20);
    std::string sha256_hex(const std::string& bytes);
}

#endif
