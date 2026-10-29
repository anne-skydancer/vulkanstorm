/**
 * @file llinventorymodel_parity_harness.cpp
 * @brief Recording helpers over the real inventory model (tests only).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "llinventorymodel_parity_harness.h"

#include "llsdserialize.h"
#include "message.h"
#include "llfile.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

namespace vks_inv
{
    LLUUID Rng::uuid()
    {
        LLUUID id;
        U64 a = next();
        U64 b = next();
        memcpy(id.mData, &a, 8);
        memcpy(id.mData + 8, &b, 8);
        id.mData[6] = (U8)((id.mData[6] & 0x0F) | 0x40);
        id.mData[8] = (U8)((id.mData[8] & 0x3F) | 0x80);
        return id;
    }

    LLUUID pid(U32 tag, U64 n)
    {
        LLUUID id;
        id.mData[0] = (U8)(tag >> 24);
        id.mData[1] = (U8)(tag >> 16);
        id.mData[2] = (U8)(tag >> 8);
        id.mData[3] = (U8)tag;
        id.mData[6] = 0x40;
        id.mData[8] = 0x80;
        for (S32 i = 0; i < 6; ++i)
        {
            id.mData[15 - i] = (U8)(n >> (8 * i));
        }
        return id;
    }

    std::string q(const std::string& s)
    {
        std::string out = "\"";
        for (unsigned char c : s)
        {
            if (c == '\\' || c == '"')
            {
                out.push_back('\\');
                out.push_back((char)c);
            }
            else if (c < 0x20 || c >= 0x7f)
            {
                char buf[8];
                snprintf(buf, sizeof(buf), "\\x%02x", c);
                out += buf;
            }
            else
            {
                out.push_back((char)c);
            }
        }
        out.push_back('"');
        return out;
    }

    std::string ids(const std::set<LLUUID>& s)
    {
        std::string out = "[";
        bool first = true;
        for (const LLUUID& id : s)
        {
            if (!first) out.push_back(',');
            out += id.asString();
            first = false;
        }
        out.push_back(']');
        return out;
    }

    std::string hexmask(U32 mask)
    {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%x", mask);
        return buf;
    }

    void* observer_slot(int slot, size_t size)
    {
        const size_t SLOT = 512;
        alignas(64) static unsigned char block[4 * SLOT];
        llassert_always(slot >= 0 && slot < 4 && size <= SLOT);
        return block + slot * SLOT;
    }

    void Recorder::changed(U32 mask)
    {
        ++mBatches;
        const LLUUID& tid = gInventory.getTransactionId();
        note("batch mask=" + hexmask(mask)
             + " tid=" + (tid.isNull() ? std::string("-") : tid.asString())
             + " changed=" + ids(gInventory.getChangedIDs())
             + " added=" + ids(gInventory.getAddedIDs()));
    }

    Recorder* install_recorder()
    {
        Recorder* r = new Recorder;
        gInventory.addObserver(r);
        return r;
    }

    void RecordingCallback::fire(const LLUUID& inv_item)
    {
        note("callback " + mTag + " " + inv_item.asString());
    }

    // ---- snapshots ------------------------------------------------------------

    std::string dump_category(const LLViewerInventoryCategory* cat)
    {
        const LLInventoryCategory* base = cat;
        std::ostringstream s;
        s << "cat " << cat->getUUID()
          << " parent=" << cat->getParentUUID()
          << " pref=" << (S32)cat->getPreferredType()
          << " atype=" << (S32)base->getType()
          << " name=" << q(base->getName())
          << " owner=" << cat->getOwnerID()
          << " version=" << cat->getVersion()
          << " count=" << cat->getDescendentCount()
          << " thumb=" << cat->getThumbnailUUID()
          << " fav=" << (cat->getIsFavorite() ? 1 : 0)
          << " fetching=" << model::fetching_state(cat)
          << " timer{" << model::fetch_timer_state(cat) << "}";
        return s.str();
    }

    std::string dump_item(const LLViewerInventoryItem* item)
    {
        const LLInventoryItem* base = item;
        const LLPermissions& p = base->LLInventoryItem::getPermissions();
        const LLSaleInfo& sale = base->LLInventoryItem::getSaleInfo();
        std::ostringstream s;
        char flags[16];
        snprintf(flags, sizeof(flags), "0x%08x", base->LLInventoryItem::getFlags());
        s << "item " << item->getUUID()
          << " parent=" << item->getParentUUID()
          << " type=" << (S32)base->LLInventoryItem::getType()
          << " inv=" << (S32)base->LLInventoryItem::getInventoryType()
          << " flags=" << flags
          << " asset=" << base->LLInventoryItem::getAssetUUID()
          << " name=" << q(base->LLInventoryItem::getName())
          << " desc=" << q(base->LLInventoryItem::getDescription())
          << " created=" << (S64)base->LLInventoryItem::getCreationDate()
          << " thumb=" << base->LLInventoryItem::getThumbnailUUID()
          << " fav=" << (base->getIsFavorite() ? 1 : 0)
          << " runtime=" << q(base->getRuntime())
          << " perms=" << p.getCreator() << "," << p.getOwner() << "," << p.getLastOwner() << "," << p.getGroup()
          << "," << hexmask(p.getMaskBase()) << "," << hexmask(p.getMaskOwner()) << "," << hexmask(p.getMaskGroup())
          << "," << hexmask(p.getMaskEveryone()) << "," << hexmask(p.getMaskNextOwner())
          << "," << (p.isGroupOwned() ? 1 : 0)
          << " sale=" << (S32)sale.getSaleType() << "," << sale.getSalePrice()
          << " complete=" << (item->isFinished() ? 1 : 0)
          << " txn=" << item->getTransactionID()
          << " running=" << (item->getIsRunning() ? 1 : 0)
          << " faulted=" << (item->getIsFaulted() ? 1 : 0);
        return s.str();
    }

    void dump_model(std::vector<std::string>& out, bool with_objects)
    {
        std::ostringstream head;
        head << "model root=" << gInventory.getRootFolderID()
             << " library_root=" << gInventory.getLibraryRootFolderID()
             << " library_owner=" << gInventory.getLibraryOwnerID()
             << " usable=" << (model::agent_inv_usable() ? 1 : 0)
             << " cats=" << model::categories().size()
             << " items=" << model::items().size()
             << " cat_trees=" << model::cat_tree().size()
             << " item_trees=" << model::item_tree().size();
        out.push_back(head.str());
        if (with_objects)
        {
            // In key order: the model's maps are std::unordered_map since
            // upstream 28fee038e0, and the dump must not depend on the
            // platform's hash table layout.
            std::map<LLUUID, LLViewerInventoryCategory*> cats;
            for (const auto& kv : model::categories()) cats[kv.first] = kv.second;
            std::map<LLUUID, LLViewerInventoryItem*> items;
            for (const auto& kv : model::items()) items[kv.first] = kv.second;
            for (const auto& kv : cats)
            {
                std::string line = dump_category(kv.second);
                if (kv.first != kv.second->getUUID())
                {
                    line += " key=" + kv.first.asString();
                }
                out.push_back(line);
            }
            for (const auto& kv : items)
            {
                std::string line = dump_item(kv.second);
                if (kv.first != kv.second->getUUID())
                {
                    line += " key=" + kv.first.asString();
                }
                out.push_back(line);
            }
        }
        // Child vectors, in order, for every parent key of either tree.
        std::set<LLUUID> parents;
        for (const auto& kv : model::cat_tree()) parents.insert(kv.first);
        for (const auto& kv : model::item_tree()) parents.insert(kv.first);
        for (const LLUUID& parent : parents)
        {
            std::string line = "children " + parent.asString() + " cats=";
            auto cit = model::cat_tree().find(parent);
            if (cit == model::cat_tree().end()) line += "none";
            else
            {
                line += "[";
                bool first = true;
                for (const auto& c : *cit->second) { if (!first) line += ","; line += c ? c->getUUID().asString() : std::string("null"); first = false; }
                line += "]";
            }
            line += " items=";
            auto iit = model::item_tree().find(parent);
            if (iit == model::item_tree().end()) line += "none";
            else
            {
                line += "[";
                bool first = true;
                for (const auto& i : *iit->second) { if (!first) line += ","; line += i ? i->getUUID().asString() : std::string("null"); first = false; }
                line += "]";
            }
            if ((cit == model::cat_tree().end() || cit->second->empty())
                && (iit == model::item_tree().end() || iit->second->empty())
                && cit != model::cat_tree().end() && iit != model::item_tree().end())
            {
                continue; // empty pairs are implied by the folder list
            }
            out.push_back(line);
        }
        for (const auto& kv : model::backlinks())
        {
            out.push_back("backlink " + kv.first.asString() + " " + kv.second.asString());
        }
        for (const auto& kv : model::possibly_broken_links())
        {
            out.push_back("broken " + kv.first.asString() + " " + ids(kv.second));
        }
        if (!model::links_rebuild_list().empty())
        {
            out.push_back("rebuild " + ids(model::links_rebuild_list()));
        }
        // The lock maps gain a false entry on every unlocked-array lookup:
        // their sizes are recorded, and the entries that are set.
        out.push_back("locks cat_entries=" + std::to_string(model::category_locks().size())
                      + " item_entries=" + std::to_string(model::item_locks().size()));
        for (const auto& kv : model::category_locks())
        {
            if (kv.second) out.push_back("lock cat " + kv.first.asString());
        }
        for (const auto& kv : model::item_locks())
        {
            if (kv.second) out.push_back("lock item " + kv.first.asString());
        }
        out.push_back("pending mask=" + hexmask(model::modify_mask())
                      + " changed=" + ids(gInventory.getChangedIDs())
                      + " added=" + ids(gInventory.getAddedIDs())
                      + " backlog_mask=" + hexmask(model::modify_mask_backlog())
                      + " backlog_changed=" + ids(model::changed_backlog())
                      + " backlog_added=" + ids(model::added_backlog()));
    }

    std::string llsd_hex(const LLSD& sd)
    {
        std::ostringstream s;
        LLPointer<LLSDBinaryFormatter>(new LLSDBinaryFormatter())->format(sd, s);
        std::string b = s.str();
        return hex(std::vector<U8>(b.begin(), b.end()));
    }

    std::string llsd_notation(const LLSD& sd)
    {
        std::ostringstream s;
        // One line; binary as b16"..." (no raw bytes in text fixtures).
        LLPointer<LLSDNotationFormatter> f = new LLSDNotationFormatter(false, "", LLSDFormatter::OPTIONS_PRETTY_BINARY);
        f->format(sd, s);
        return s.str();
    }

    void dump_validation(std::vector<std::string>& out, const LLInventoryValidationInfo& info)
    {
        LLSD sd;
        info.asLLSD(sd);
        out.push_back("validation llsd=" + llsd_hex(sd));
        out.push_back("validation notation=" + llsd_notation(sd));
        std::ostringstream s;
        s << "validation fatal=" << info.mFatalErrorCount
          << " warnings=" << info.mWarningCount
          << " loops=" << info.mLoopCount
          << " orphans=" << info.mOrphanedCount
          << " fatal_system_duplicate=" << info.mFatalSystemDuplicate
          << " no_root=" << (info.mFatalNoRootFolder ? 1 : 0)
          << " no_library_root=" << (info.mFatalNoLibraryRootFolder ? 1 : 0)
          << " qa=" << (info.mFatalQADebugMode ? 1 : 0)
          << " initialized=" << (info.mInitialized ? 1 : 0);
        out.push_back(s.str());
        for (const auto& kv : info.mWarnings)
        {
            out.push_back("validation warning " + kv.first + " " + std::to_string(kv.second));
        }
        out.push_back("validation log=" + q(info.mLog.str()));
    }

    // ---- messages ------------------------------------------------------------------

    void pack_item(LLMessageSystem* msg, const LLViewerInventoryItem& item, U32 callback_id, bool with_callback)
    {
        msg->addUUIDFast(_PREHASH_ItemID, item.getUUID());
        msg->addUUIDFast(_PREHASH_FolderID, item.getParentUUID());
        if (with_callback) msg->addU32Fast(_PREHASH_CallbackID, callback_id);
        const LLPermissions& p = item.LLInventoryItem::getPermissions();
        p.packMessage(msg);
        msg->addUUIDFast(_PREHASH_AssetID, item.LLInventoryItem::getAssetUUID());
        msg->addS8Fast(_PREHASH_Type, (S8)item.LLInventoryItem::getType());
        msg->addS8Fast(_PREHASH_InvType, (S8)item.LLInventoryItem::getInventoryType());
        msg->addU32Fast(_PREHASH_Flags, item.LLInventoryItem::getFlags());
        item.LLInventoryItem::getSaleInfo().packMessage(msg);
        msg->addStringFast(_PREHASH_Name, item.LLInventoryItem::getName());
        msg->addStringFast(_PREHASH_Description, item.LLInventoryItem::getDescription());
        msg->addS32Fast(_PREHASH_CreationDate, (S32)item.LLInventoryItem::getCreationDate());
        msg->addU32Fast(_PREHASH_CRC, item.LLInventoryItem::getCRC32());
    }

    bool send_to_viewer()
    {
        std::string name;
        std::vector<U8> bytes = build_pending_message(name);
        note("in " + name + " " + hex(bytes));
        bool ok = dispatch_packet(bytes, true);
        if (!ok)
        {
            note("in.rejected " + name);
        }
        return ok;
    }

    // ---- fixtures --------------------------------------------------------------------

    std::string read_file(const std::string& path)
    {
        std::ifstream in(path.c_str(), std::ios::binary);
        if (!in) return std::string();
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    bool write_file(const std::string& path, const std::string& body)
    {
        std::ofstream out(path.c_str(), std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(body.data(), body.size());
        return out.good();
    }

    bool file_exists(const std::string& path)
    {
        return LLFile::isfile(path);
    }

    Cases parse_cases(const std::string& text)
    {
        Cases cases;
        std::istringstream in(text);
        std::string line;
        while (std::getline(in, line))
        {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.compare(0, 3, "== ") == 0)
            {
                cases.push_back(Case());
                cases.back().id = line.substr(3);
            }
            else if (!line.empty() && line[0] == '#')
            {
                continue;
            }
            else if (!cases.empty())
            {
                cases.back().lines.push_back(line);
            }
        }
        return cases;
    }

    std::string format_cases(const std::string& header, const Cases& cases)
    {
        std::string out = header;
        for (const Case& c : cases)
        {
            out += "== " + c.id + "\n";
            for (const std::string& l : c.lines)
            {
                out += l;
                out.push_back('\n');
            }
        }
        return out;
    }

    namespace
    {
        std::string clip(const std::string& s)
        {
            return s.size() > 400 ? s.substr(0, 400) + "...(" + std::to_string(s.size()) + " chars)" : s;
        }
        // The first differing "key=value" field of two lines, for the report.
        std::string first_field(const std::string& a, const std::string& b)
        {
            size_t i = 0;
            while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
            size_t start = a.rfind(' ', i);
            start = (start == std::string::npos) ? 0 : start + 1;
            size_t eq = a.find('=', start);
            if (eq != std::string::npos && eq < a.find(' ', start))
            {
                return a.substr(start, eq - start);
            }
            return "column " + std::to_string(i);
        }
    }

    std::string compare_cases(const Cases& expected, const Cases& actual, size_t max_reports)
    {
        std::ostringstream report;
        size_t reports = 0;
        std::map<std::string, const Case*> by_id;
        for (const Case& c : actual) by_id[c.id] = &c;
        for (const Case& e : expected)
        {
            if (reports >= max_reports) break;
            auto it = by_id.find(e.id);
            if (it == by_id.end())
            {
                report << "case " << e.id << ": not produced\n";
                ++reports;
                continue;
            }
            const Case& a = *it->second;
            size_t n = std::max(e.lines.size(), a.lines.size());
            for (size_t i = 0; i < n; ++i)
            {
                const std::string ev = i < e.lines.size() ? e.lines[i] : std::string("<end of case>");
                const std::string av = i < a.lines.size() ? a.lines[i] : std::string("<end of case>");
                if (ev != av)
                {
                    report << "case " << e.id << " line " << (i + 1) << " field " << first_field(ev, av) << "\n"
                           << "  expected: " << clip(ev) << "\n"
                           << "  actual:   " << clip(av) << "\n";
                    ++reports;
                    break;
                }
            }
        }
        std::set<std::string> expected_ids;
        for (const Case& e : expected) expected_ids.insert(e.id);
        for (const Case& a : actual)
        {
            if (reports >= max_reports) break;
            if (!expected_ids.count(a.id))
            {
                report << "case " << a.id << ": produced but not in the fixture\n";
                ++reports;
            }
        }
        return report.str();
    }

    // ---- SHA-256 (FIPS 180-4) ------------------------------------------------------------
    namespace
    {
        const U32 K256[64] = {
            0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
            0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
            0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
            0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
            0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
            0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
            0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
            0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2 };
        inline U32 rotr(U32 x, U32 n) { return (x >> n) | (x << (32 - n)); }
    }

    std::string sha256_hex(const std::string& bytes)
    {
        U32 h[8] = { 0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19 };
        std::string m = bytes;
        U64 bitlen = (U64)bytes.size() * 8;
        m.push_back((char)0x80);
        while (m.size() % 64 != 56) m.push_back((char)0);
        for (S32 i = 7; i >= 0; --i) m.push_back((char)(bitlen >> (8 * i)));
        for (size_t off = 0; off < m.size(); off += 64)
        {
            U32 w[64];
            for (S32 i = 0; i < 16; ++i)
            {
                const U8* p = (const U8*)m.data() + off + 4 * i;
                w[i] = ((U32)p[0] << 24) | ((U32)p[1] << 16) | ((U32)p[2] << 8) | (U32)p[3];
            }
            for (S32 i = 16; i < 64; ++i)
            {
                U32 s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
                U32 s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
                w[i] = w[i - 16] + s0 + w[i - 7] + s1;
            }
            U32 a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
            for (S32 i = 0; i < 64; ++i)
            {
                U32 S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
                U32 ch = (e & f) ^ (~e & g);
                U32 t1 = hh + S1 + ch + K256[i] + w[i];
                U32 S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
                U32 maj = (a & b) ^ (a & c) ^ (b & c);
                U32 t2 = S0 + maj;
                hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
            }
            h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
        }
        char out[65];
        for (S32 i = 0; i < 8; ++i) snprintf(out + 8 * i, 9, "%08x", h[i]);
        return std::string(out, 64);
    }
}
