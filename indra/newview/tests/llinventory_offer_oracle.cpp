/**
 * @file llinventory_offer_oracle.cpp
 * @brief Recorder R4b (inventory rung): the corpora of the offer and give
 * oracle (survey 4 section 7: the instant message part of set 4, sets 5, 6
 * and 7). See llinventory_offer_oracle.h and fixtures/inventory/offer/README.md.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_offer_oracle.h"

#include <chrono>
#include "llinventory_offer_oracle_copies.h"
#include "llinventory_offer_oracle_boundary.h"

#include "llapr.h"
#include "llsdserialize.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <memory>
#include <set>

namespace r4b_pack
{
    void pack_instant_message_block(r4b::LLMessageSystem* msg, const LLUUID& from_id, bool from_group,
                                    const LLUUID& session_id, const LLUUID& to_id, const std::string& name,
                                    const std::string& message, U8 offline, EInstantMessage dialog, const LLUUID& id,
                                    U32 parent_estate_id, const LLUUID& region_id, const LLVector3& position,
                                    U32 timestamp, const U8* binary_bucket, S32 binary_bucket_size);
}

namespace r4b_oracle
{
    namespace F = r4b;

    // every decision goes through the W6/W7 seam
    static r4b_boundary::Decisions& D() { return r4b_boundary::decisions(); }

    // ================================================================ basics
    bool is_note(const std::string& line)
    {
        size_t t = line.find('\t');
        return t != std::string::npos && line.compare(t + 1, 5, "note.") == 0;
    }

    static uint32_t fnv1a(const std::string& s)
    {
        uint32_t h = 2166136261u;
        for (unsigned char c : s) { h ^= c; h *= 16777619u; }
        return h;
    }

    bool in_sample(const std::string& case_id, uint32_t modulus)
    {
        return fnv1a(case_id) % modulus == 0;
    }

    // ------------------------------------------------------------- SHA-256
    static const uint32_t K256[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
        0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
        0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
        0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
        0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
        0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
        0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2 };

    static inline uint32_t rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

    Sha256::Sha256()
    {
        static const uint32_t init[8] = { 0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                          0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19 };
        memcpy(h, init, sizeof(h));
    }

    void Sha256::block(const uint8_t* p)
    {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i)
            w[i] = (uint32_t)p[i * 4] << 24 | (uint32_t)p[i * 4 + 1] << 16 | (uint32_t)p[i * 4 + 2] << 8 | p[i * 4 + 3];
        for (int i = 16; i < 64; ++i)
        {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i)
        {
            uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ (~e & g);
            uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
            uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t mj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t t2 = S0 + mj;
            hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
    }

    void Sha256::update(const void* data, size_t len)
    {
        const uint8_t* p = (const uint8_t*)data;
        total += len;
        while (len)
        {
            size_t n = std::min(len, (size_t)64 - buflen);
            memcpy(buf + buflen, p, n);
            buflen += n; p += n; len -= n;
            if (buflen == 64) { block(buf); buflen = 0; }
        }
    }

    std::string Sha256::hexdigest()
    {
        uint64_t bits = total * 8;
        uint8_t pad = 0x80;
        update(&pad, 1);
        uint8_t zero = 0;
        while (buflen != 56) update(&zero, 1);
        uint8_t len[8];
        for (int i = 0; i < 8; ++i) len[i] = (uint8_t)(bits >> (56 - 8 * i));
        update(len, 8);
        std::string out;
        char tmp[9];
        for (int i = 0; i < 8; ++i) { snprintf(tmp, sizeof(tmp), "%08x", h[i]); out += tmp; }
        return out;
    }

    // ------------------------------------------------------------- xorshift
    struct Rng
    {
        uint64_t s;
        explicit Rng(uint64_t seed) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
        uint64_t next() { s ^= s >> 12; s ^= s << 25; s ^= s >> 27; return s * 0x2545F4914F6CDD1Dull; }
        uint32_t below(uint32_t n) { return (uint32_t)(next() % n); }
        bool coin() { return next() & 1; }
    };

    // ------------------------------------------------------------- fixtures
    LLUUID U(U32 tag, U32 n)
    {
        LLUUID id;
        id.set(F::fmt("%08x-0000-4000-8000-%012x", tag, n));
        return id;
    }

    static const LLUUID AGENT = U(0xa1, 1);
    static const LLUUID SESSION = U(0xa1, 2);
    static const LLUUID OTHER = U(0xb2, 1);
    static const LLUUID OTHER2 = U(0xb2, 2);
    static const LLUUID CREATOR = U(0xb2, 9);
    static const LLUUID GROUP = U(0xc3, 1);
    static const LLUUID TXN = U(0xd4, 1);
    static const LLUUID OFFERED = U(0xe5, 1);
    static const LLUUID RLV_ROOT = U(0xf1, 1);
    static const char* OFFER_HOST = "10.0.0.9:13005";
    static const char* AGENT_HOST = "10.0.0.1:13000";

    static const char* const TEMPLATE_FORMS[] = { "UserGiveItem", "UserGiveItemLegacy", "ObjectGiveItem", "OwnObjectGiveItem" };

    void init_messaging(const std::string& template_file)
    {
        static bool done = false;
        if (done) return;
        done = true;
        ll_init_apr();
        // port 0: the system picks one; nothing is ever sent
        start_messaging_system(template_file, 0, 1, 0, 0, false, "notasharedsecret", NULL, false, 5.f, 100.f);
        llassert_always(::gMessageSystem);
    }

    static LLUUID folder_for(S32 ft) { return U(0xf0, (U32)(U16)ft); }

    // The common agent: fixed ids, position, name, region; every setting the
    // copies read pinned to its default (app_settings/settings*.xml).
    static void setup()
    {
        F::reset_fakes();
        F::gAgent.mID = AGENT;
        F::gAgent.mSessionID = SESSION;
        F::gAgent.mPosition = LLVector3(128.25f, 64.5f, 22.75f);
        F::gAgent.mRegionHost = LLHost(std::string(AGENT_HOST));
        F::gAgentID = AGENT;
        F::gAgentSessionID = SESSION;
        F::gAgentAvatarp->mValid = true;
        F::gAgentAvatarp->mFullname = "Oracle Tester";
        auto& b = F::gSavedSettings.b;
        b["AutoAcceptNewInventory"] = false;
        b["FSUseLegacyInventoryAcceptMessages"] = false;
        b["FSLogAutoAcceptInventoryToChat"] = true;
        b["FSShowAutoAcceptInventoryInNotifications"] = true;
        b["ShowNewInventory"] = true;
        b["ShowInInventory"] = false;
        b["FSDismissGroupNoticeAttachmentsToTrash"] = false;
        b["LogInventoryDecline"] = true;
        b["FSCreateGiveInventoryParticleEffect"] = true;
        b["UseLegacyIMLogNames"] = true;
        b["FSNotifyIncomingObjectSpamFrom"] = true;
        b["FSNotifyIncomingObjectSpam"] = true;
        F::gSavedSettings.u["FSOfferThrottleMaxCount"] = 5;
        F::gSavedPerAccountSettings.s["DoNotDisturbModeResponse"] =
            "This resident has turned on 'Do Not Disturb' and will see your message later.";
        // LLAvatarName's "trim Resident" switch (a real llmessage static) as the viewer defaults it
        ::LLAvatarName::setTrimResidentSurname(true);
        F::LLAvatarNameCache::instance().names[OTHER] = "other.sender";
        F::gCacheName->group_names[GROUP] = "Oracle Group";
    }

    static void run_idle()
    {
        auto& idle = F::LLAppViewer::instance()->mIdle;
        while (!idle.empty())
        {
            auto cb = idle.front();
            idle.erase(idle.begin());
            F::ev().add("idle.run");
            cb();
        }
    }

    // ------------------------------------------------------------ the sinks
    struct VecSink : Sink
    {
        std::vector<std::string> lines;
        void line(const std::string& l) override { lines.push_back(l); }
    };

    static void put(Sink& s, const std::string& c, const std::string& k, const std::string& v)
    {
        s.line(c + "\t" + k + "\t" + v);
    }

    // The event log of the case: events as eNN, a note after an event as
    // note.eNN (the same number).
    static void put_events(Sink& s, const std::string& c)
    {
        int n = -1;
        for (const std::string& e : F::ev().lines)
        {
            if (e.compare(0, 5, "note:") == 0)
            {
                put(s, c, F::fmt("note.e%02d", n < 0 ? 0 : n), e.substr(5));
            }
            else
            {
                ++n;
                put(s, c, F::fmt("e%02d", n), e);
            }
        }
        F::ev().lines.clear();
    }

    // ------------------------------------------------------------- offers
    static std::string offer_fields(const F::LLOfferInfo& o)
    {
        return F::fmt("im=%d from=%s from_group=%d from_object=%d txn=%s folder=%s object=%s type=%d from_name=\"%s\" desc=\"%s\" host=%s persist=%d",
                        (S32)o.mIM, o.mFromID.asString().c_str(), (S32)o.mFromGroup, (S32)o.mFromObject,
                        o.mTransactionID.asString().c_str(), o.mFolderID.asString().c_str(), o.mObjectID.asString().c_str(),
                        (S32)o.mType, F::esc(o.mFromName).c_str(), F::esc(o.mDesc).c_str(), o.mHost.getIPandPort().c_str(),
                        (S32)o.mPersist);
    }

    static void install_snapshot_hook()
    {
        F::offer_snapshot_hook() = [](const char* where)
        {
            std::vector<std::string> snaps;
            for (F::LLNotificationResponderInterface* r : F::live_responders())
            {
                if (F::LLOfferInfo* o = dynamic_cast<F::LLOfferInfo*>(r))
                {
                    snaps.push_back(offer_fields(*o));
                }
            }
            std::sort(snaps.begin(), snaps.end());
            for (const std::string& s : snaps)
            {
                F::ev().add(std::string("offer.") + where + " " + s);
            }
        };
    }

    // Answer the last notification posted with a functor (CannotCopyWarning,
    // CannotCopyCountItems) by its form option.
    static void respond_pending(S32 option)
    {
        if (F::world().pending.empty())
        {
            F::ev().add("respond: nothing pending");
            return;
        }
        F::World::Pending p = F::world().pending.back();
        F::world().pending.pop_back();
        LLSD notification;
        notification["name"] = p.name;
        notification["form"] = F::notification_form(p.name);
        notification["payload"] = p.payload;
        LLSD response = LLSD::emptyMap();
        LLSD form = notification["form"];
        for (S32 i = 0; i < form.size(); ++i)
        {
            if (form[i]["index"].asInteger() == option)
            {
                response[form[i]["name"].asString()] = true;
            }
        }
        F::ev().add(F::fmt("respond %s option=%d", p.name.c_str(), option));
        D().answer(p.functor, notification, response);
    }

    // ================================================================ items
    static U32 perm_mask(bool copy, bool mod, bool transfer)
    {
        return PERM_MOVE | (copy ? PERM_COPY : 0) | (mod ? PERM_MODIFY : 0) | (transfer ? PERM_TRANSFER : 0);
    }

    static LLPermissions make_perms(const LLUUID& owner, U32 owner_mask, U32 everyone = PERM_NONE, U32 base = PERM_ALL,
                                    const LLUUID& group = LLUUID::null, bool group_owned = false)
    {
        LLPermissions perm;
        perm.init(CREATOR, owner, owner, group);
        perm.initMasks(base, owner_mask, everyone, PERM_NONE, PERM_ALL);
        if (group_owned)
        {
            perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, group, true);
        }
        return perm;
    }

    static F::LLViewerInventoryItem* add_item(const LLUUID& id, const LLUUID& parent, S32 type, const LLPermissions& perm,
                                              const std::string& name, const LLUUID& asset = LLUUID::null)
    {
        F::LLViewerInventoryItem* item = new F::LLViewerInventoryItem(
            id, parent, perm, asset.notNull() ? asset : U(0xaa, id.mData[15] | (id.mData[14] << 8)), (LLAssetType::EType)type,
            LLInventoryType::defaultForAssetType((LLAssetType::EType)type), name);
        F::gInventory.addItem(item);
        return item;
    }

    static F::LLViewerInventoryCategory* add_cat(const LLUUID& id, const LLUUID& parent, const std::string& name,
                                                 LLFolderType::EType type = LLFolderType::FT_NONE)
    {
        F::LLViewerInventoryCategory* cat = new F::LLViewerInventoryCategory(id, parent, type, name);
        F::gInventory.addCategory(cat);
        return cat;
    }

    static const LLUUID ROOT = U(0x1f, 1);

    // ===================================================== inputs (F3)
    // The records a case runs on, as compared `input.NN` lines, so a Rust
    // test can rebuild the case from the fixture alone.
    static std::string perm_rec(const LLPermissions& p)
    {
        return F::fmt("creator=%s owner=%s last_owner=%s group=%s group_owned=%d base=%08x owner_mask=%08x group_mask=%08x everyone=%08x next=%08x",
                      p.getCreator().asString().c_str(), p.getOwner().asString().c_str(), p.getLastOwner().asString().c_str(),
                      p.getGroup().asString().c_str(), (S32)p.isGroupOwned(), p.getMaskBase(), p.getMaskOwner(), p.getMaskGroup(),
                      p.getMaskEveryone(), p.getMaskNextOwner());
    }

    static std::string ids_list(const std::set<LLUUID>& v)
    {
        std::string out = "[";
        for (const LLUUID& id : v) out += (out.size() > 1 ? "," : "") + id.asString();
        return out + "]";
    }

    // the stored record (raw name and type, the item's own permissions)
    static std::string item_rec(const LLInventoryItem* it)
    {
        const F::LLViewerInventoryItem* v = dynamic_cast<const F::LLViewerInventoryItem*>(it);
        return F::fmt("item %s parent=%s type=%d inv_type=%d asset=%s flags=%u finished=%d name=\"",
                      it->getUUID().asString().c_str(), it->getParentUUID().asString().c_str(), (S32)it->getActualType(),
                      (S32)it->LLInventoryItem::getInventoryType(), it->LLInventoryItem::getAssetUUID().asString().c_str(),
                      it->LLInventoryItem::getFlags(), v ? (S32)v->isFinished() : 1) +
               F::esc(it->LLInventoryObject::getName()) + "\" " + perm_rec(it->LLInventoryItem::getPermissions());
    }

    static std::string cat_rec(const LLInventoryCategory* c)
    {
        return F::fmt("cat %s parent=%s pref=%d complete=%d name=\"", c->getUUID().asString().c_str(),
                      c->getParentUUID().asString().c_str(), (S32)c->getPreferredType(),
                      (S32)F::gInventory.isCategoryComplete(c->getUUID())) + F::esc(c->LLInventoryObject::getName()) + "\"";
    }

    struct Inputs
    {
        Sink& s;
        std::string c;
        int n = 0;
        void line(const std::string& v) { put(s, c, F::fmt("input.%02d", n++), v); }
    };

    // the model: parents in id order, each parent's children in the order
    // the model holds them (categories, then items)
    static void put_model(Inputs& in)
    {
        std::set<LLUUID> parents;
        for (auto& p : F::gInventory.mParentChildCategoryTree) parents.insert(p.first);
        for (auto& p : F::gInventory.mParentChildItemTree) parents.insert(p.first);
        for (const LLUUID& parent : parents)
        {
            if (auto* cats = get_ptr_in_map(F::gInventory.mParentChildCategoryTree, parent))
                for (auto& cp : *cats) in.line(cat_rec(cp.get()));
            if (auto* items = get_ptr_in_map(F::gInventory.mParentChildItemTree, parent))
                for (auto& ip : *items) in.line(item_rec(ip.get()));
        }
    }

    static void put_world(Sink& s, const std::string& c)
    {
        Inputs in{ s, c };
        in.line(F::fmt("agent id=%s session=%s avatar_valid=%d fullname=\"%s\" sitting=%d dnd=%d groups=",
                       F::gAgent.mID.asString().c_str(), F::gAgent.mSessionID.asString().c_str(), (S32)F::gAgentAvatarp->mValid,
                       F::esc(F::gAgentAvatarp->mFullname).c_str(), (S32)F::gAgentAvatarp->mSitting, (S32)F::gAgent.mDoNotDisturb) +
                ids_list(F::gAgent.mGroups));
        in.line("worn attachments=" + ids_list(F::gAgentAvatarp->mAttachments) + " wearables=" + ids_list(F::gAgentWearables.worn) +
                " active_gestures=" + ids_list(F::LLGestureMgr::instance().active));
        std::string folders;
        for (auto& f : F::gInventory.mSystemFolders) folders += F::fmt(" %d=%s", f.first, f.second.asString().c_str());
        in.line(F::fmt("limits max_inventory_items_transfer=%d system_folder_overrides=[", F::LLWorld::getInstance()->mMaxInventoryItemsTransfer) +
                folders + " ] (other folder types: 000000f0-0000-4000-8000-<type as U16>)");
        std::string set;
        for (auto& b : F::gSavedSettings.b) set += " " + b.first + "=" + (b.second ? "1" : "0");
        for (auto& u : F::gSavedSettings.u) set += F::fmt(" %s=%u", u.first.c_str(), u.second);
        in.line("settings" + set);
        const F::World& w = F::world();
        in.line(F::fmt("rlv enabled=%d forbid_give_to_rlv=%d shared_root=%s can_show_names=%d locked_attachment=%d unsit=%d sittp=%d share_blocked=",
                       (S32)w.rlv_enabled, (S32)w.rlv_forbid_give_to_rlv, w.rlv_shared_root.asString().c_str(), (S32)w.rlv_can_show_names,
                       (S32)w.rlv_locked_attachment, (S32)w.rlv_unsit, (S32)w.rlv_sittp) +
                ids_list(w.rlv_share_blocked) + " nearby=" + ids_list(w.rlv_nearby));
        std::string mutes;
        for (const F::LLMute& m : F::LLMuteList::getInstance()->mMutes)
            mutes += F::fmt(" %s:%d:%u:\"", m.mID.asString().c_str(), (S32)m.mType, m.mFlags) + F::esc(m.mName) + "\"";
        std::string legacy;
        for (const std::string& n : F::LLMuteList::getInstance()->mLegacyMutes) legacy += " \"" + F::esc(n) + "\"";
        in.line("mutes [" + mutes + " ] legacy [" + legacy + " ]");
        std::string names;
        for (auto& n : F::LLAvatarNameCache::instance().names) names += " " + n.first.asString() + "=\"" + F::esc(n.second) + "\"";
        in.line("im_sessions=" + ids_list(F::LLIMModel::getInstance()->open_sessions) + " avatar_names [" + names + " ]");
        put_model(in);
    }

    // the item(s) a predicate case runs on, and what is worn
    static void put_items(Sink& s, const std::string& c, const std::vector<const LLInventoryItem*>& items)
    {
        Inputs in{ s, c };
        for (const LLInventoryItem* it : items) in.line(item_rec(it));
        in.line(F::fmt("avatar_valid=%d attachments=", (S32)F::gAgentAvatarp->mValid) + ids_list(F::gAgentAvatarp->mAttachments) +
                " wearables=" + ids_list(F::gAgentWearables.worn) + " active_gestures=" + ids_list(F::LLGestureMgr::instance().active) +
                " groups=" + ids_list(F::gAgent.mGroups));
    }


    // =========================================================== set 4: items
    struct GiveItemCase
    {
        std::string id;
        std::function<LLUUID()> build;   // builds the model, returns the item given
        LLUUID session;
        int respond = -1;                // answer CannotCopyWarning: 0 yes, 1 no
        std::function<void()> between;   // runs between the warning and the answer
    };

    static std::vector<S32> asset_types()
    {
        return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 17, 18, 19, 20, 21, 22, 24, 25, 26, 40, 45, 49, 56, 57, 58, 59 };
    }

    static void corpus_give_item(Sink& s)
    {
        std::vector<GiveItemCase> cases;
        const LLPermissions full = make_perms(AGENT, perm_mask(true, true, true));
        const LLPermissions nocopy = make_perms(AGENT, perm_mask(false, true, true));
        const LLPermissions notrans = make_perms(AGENT, perm_mask(true, true, false));
        for (S32 t : asset_types())
        {
            cases.push_back({ F::fmt("type_%02d", t), [t, full]() {
                return add_item(U(0x11, 1), ROOT, t, full, "Gift Item")->getUUID(); } });
        }
        for (S32 t : { 5, 6, 13, 21 })
        {
            cases.push_back({ F::fmt("worn_type_%02d", t), [t, full]() {
                LLUUID id = U(0x11, 1);
                add_item(id, ROOT, t, full, "Worn Item");
                if (t == 6) F::gAgentAvatarp->mAttachments.insert(id);
                else if (t == 21) F::LLGestureMgr::instance().active.insert(id);
                else F::gAgentWearables.worn.insert(id);
                return id; } });
        }
        cases.push_back({ "link_to_notecard", [full]() {
            add_item(U(0x12, 1), ROOT, 7, full, "Target Notecard");
            return add_item(U(0x11, 1), ROOT, 24, full, "Link", U(0x12, 1))->getUUID(); } });
        cases.push_back({ "link_to_worn_object", [full]() {
            add_item(U(0x12, 1), ROOT, 6, full, "Target Object");
            F::gAgentAvatarp->mAttachments.insert(U(0x12, 1));
            return add_item(U(0x11, 1), ROOT, 24, full, "Link", U(0x12, 1))->getUUID(); } });
        cases.push_back({ "link_to_link", [full]() {
            add_item(U(0x13, 1), ROOT, 7, full, "Deep Target");
            add_item(U(0x12, 1), ROOT, 24, full, "Inner Link", U(0x13, 1));
            return add_item(U(0x11, 1), ROOT, 24, full, "Outer Link", U(0x12, 1))->getUUID(); } });
        cases.push_back({ "link_broken", [full]() {
            return add_item(U(0x11, 1), ROOT, 24, full, "Broken Link", U(0x12, 9))->getUUID(); } });
        cases.push_back({ "folder_link", [full]() {
            add_cat(U(0x14, 1), ROOT, "Linked Folder");
            return add_item(U(0x11, 1), ROOT, 25, full, "Folder Link", U(0x14, 1))->getUUID(); } });
        cases.push_back({ "folder_link_broken", [full]() {
            return add_item(U(0x11, 1), ROOT, 25, full, "Folder Link", U(0x14, 9))->getUUID(); } });
        cases.push_back({ "link_nocopy_link_perms", [nocopy]() {
            add_item(U(0x12, 1), ROOT, 7, make_perms(AGENT, perm_mask(true, true, true)), "Target");
            return add_item(U(0x11, 1), ROOT, 24, nocopy, "Link", U(0x12, 1))->getUUID(); } });
        // names
        struct NameCase { const char* id; std::string name; bool raw; };
        std::vector<NameCase> names = {
            { "name_empty", "", false },
            { "name_corrected", "a|b\x01" "c\xc3\xa9  ", false },
            { "name_62", std::string(62, 'n'), false },
            { "name_63", std::string(63, 'n'), false },
            { "name_64_corrected", std::string(64, 'n'), false },
            { "name_raw_1182", std::string(1182, 'x'), true },
            { "name_raw_1183", std::string(1183, 'x'), true },
            { "name_raw_1184", std::string(1184, 'x'), true },
            { "name_raw_1198", std::string(1198, 'x'), true },
            { "name_raw_1199", std::string(1199, 'x'), true },
            { "name_raw_1200", std::string(1200, 'x'), true },
            { "name_raw_1300", std::string(1300, 'x'), true },
            { "name_raw_embedded_nul", std::string("abc\0def", 7), true },
            { "name_raw_leading_nul", std::string("\0abc", 4), true },
        };
        for (const NameCase& n : names)
        {
            std::string name = n.name;
            bool raw = n.raw;
            cases.push_back({ n.id, [name, raw, full]() {
                F::LLViewerInventoryItem* item = add_item(U(0x11, 1), ROOT, 7, full, raw ? "placeholder" : name);
                if (raw) item->setRawName(name);
                return item->getUUID(); } });
        }
        cases.push_back({ "not_transferable", [notrans]() { return add_item(U(0x11, 1), ROOT, 7, notrans, "Kept")->getUUID(); } });
        cases.push_back({ "not_owner_everyone_copy", []() {
            return add_item(U(0x11, 1), ROOT, 7, make_perms(OTHER2, PERM_ALL, PERM_COPY), "Not Mine")->getUUID(); } });
        cases.push_back({ "null_item", []() { return LLUUID::null; } });
        cases.push_back({ "avatar_invalid", [full]() {
            F::gAgentAvatarp->mValid = false;
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        GiveItemCase sess{ "im_session", [full]() { return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } };
        sess.session = U(0x5e, 1);
        cases.push_back(sess);
        cases.push_back({ "recipient_muted", [full]() {
            F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(OTHER, "other.sender", F::LLMute::AGENT));
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        cases.push_back({ "recipient_muted_by_name_only", [full]() {
            F::LLMuteList::getInstance()->mLegacyMutes.insert("other.sender");
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        cases.push_back({ "rlv_share_blocked", [full]() {
            F::world().rlv_enabled = true;
            F::world().rlv_share_blocked.insert(OTHER);
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        cases.push_back({ "rlv_names_hidden_nearby", [full]() {
            F::world().rlv_enabled = true;
            F::world().rlv_can_show_names = false;
            F::world().rlv_nearby.insert(OTHER);
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        cases.push_back({ "open_im_session_logs_there", [full]() {
            F::LLIMModel::getInstance()->open_sessions.insert(OTHER ^ AGENT);
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        cases.push_back({ "no_particle_effect", [full]() {
            F::gSavedSettings.b["FSCreateGiveInventoryParticleEffect"] = false;
            return add_item(U(0x11, 1), ROOT, 7, full, "Gift")->getUUID(); } });
        // no-copy: the warning and its answers
        GiveItemCase nc{ "nocopy_warning_only", [nocopy]() { return add_item(U(0x11, 1), ROOT, 7, nocopy, "No Copy")->getUUID(); } };
        cases.push_back(nc);
        nc.id = "nocopy_yes"; nc.respond = 0; cases.push_back(nc);
        nc.id = "nocopy_no"; nc.respond = 1; cases.push_back(nc);
        nc.id = "nocopy_yes_item_gone";
        nc.respond = 0;
        nc.between = []() { F::gInventory.deleteObject(U(0x11, 1)); };
        cases.push_back(nc);
        nc.id = "nocopy_yes_rlv_share_blocked";
        nc.between = []() { F::world().rlv_enabled = true; F::world().rlv_share_blocked.insert(OTHER); };
        cases.push_back(nc);
        nc.id = "nocopy_yes_avatar_invalid";
        nc.between = []() { F::gAgentAvatarp->mValid = false; };
        cases.push_back(nc);
        nc.id = "nocopy_yes_session_lost";
        nc.session = U(0x5e, 1);
        nc.between = nullptr;
        cases.push_back(nc);

        for (const GiveItemCase& c : cases)
        {
            setup();
            LLUUID id = c.build();
            F::ev().lines.clear(); // model setup is not part of the record
            put_world(s, c.id);
            F::ev().add("give item " + id.asString() + " to " + OTHER.asString() + " session " + c.session.asString());
            bool r = D().give_item(OTHER, F::gInventory.getItem(id), c.session);
            F::ev().add(F::fmt("result %d", (S32)r));
            if (c.respond >= 0)
            {
                if (c.between) c.between();
                respond_pending(c.respond);
            }
            run_idle();
            put_events(s, c.id);
        }
    }

    // ========================================================== set 4/7: folders
    struct Tree
    {
        // children in model order
        struct Node
        {
            bool is_cat;
            LLUUID id;
            std::string name;
            S32 type = 7;
            bool copy = true, transfer = true;
            S32 worn = 0;           // 1 attachment, 2 wearable
            bool incomplete = false;
            LLUUID link_target;
            std::vector<Node> children;
        };
    };
    typedef Tree::Node Node;

    static U32 s_node_counter = 0;
    static Node cat(const std::string& name, std::vector<Node> children = {}, bool incomplete = false)
    {
        Node n;
        n.is_cat = true;
        n.id = U(0x2c, ++s_node_counter);
        n.name = name;
        n.children = children;
        n.incomplete = incomplete;
        return n;
    }
    static Node item(const std::string& name, S32 type = 7, bool copy = true, bool transfer = true, S32 worn = 0)
    {
        Node n;
        n.is_cat = false;
        n.id = U(0x21, ++s_node_counter);
        n.name = name;
        n.type = type;
        n.copy = copy;
        n.transfer = transfer;
        n.worn = worn;
        return n;
    }

    static void build(const Node& n, const LLUUID& parent)
    {
        if (n.is_cat)
        {
            add_cat(n.id, parent, n.name);
            if (n.incomplete) F::gInventory.mIncomplete.insert(n.id);
            for (const Node& c : n.children) build(c, n.id);
        }
        else
        {
            F::LLViewerInventoryItem* it = add_item(n.id, parent, n.type, make_perms(AGENT, perm_mask(n.copy, true, n.transfer)), n.name,
                                                    n.link_target);
            if (n.worn == 1) F::gAgentAvatarp->mAttachments.insert(it->getUUID());
            if (n.worn == 2) F::gAgentWearables.worn.insert(it->getUUID());
        }
    }

    static std::vector<Node> many(int n, const char* prefix, bool copy = true)
    {
        std::vector<Node> v;
        for (int i = 0; i < n; ++i) v.push_back(item(F::fmt("%s %02d", prefix, i), 7 + (i % 3 == 1 ? -7 : 0), copy));
        return v;
    }

    struct FolderCase
    {
        std::string id;
        std::function<Node()> tree;
        std::string notification;        // success notification passed to doGiveInventoryCategory
        int respond = -1;
        std::function<void(const Node&)> between;
        std::function<void(const Node&)> before; // after the model is built
        bool raw_name = false;
        std::string name;
        S32 limit = 42;
        LLUUID give_id;                  // give this id instead of the root (null: root)
    };

    static void corpus_give_folder(Sink& s)
    {
        s_node_counter = 0;
        std::vector<FolderCase> cases;
        cases.push_back({ "empty", []() { return cat("Empty"); } });
        cases.push_back({ "one_item", []() { return cat("One", { item("Only") }); } });
        cases.push_back({ "flat_mixed_types", []() {
            return cat("Flat", { item("Note", 7), item("Tex", 0), item("Obj", 6), item("Script", 10), item("Anim", 20) }); } });
        cases.push_back({ "nested", []() {
            return cat("Root", { cat("A", { cat("A1", { item("i1") }), item("i2") }), cat("B"), item("i3"), item("i4") }); } });
        cases.push_back({ "deep_4", []() {
            return cat("D0", { cat("D1", { cat("D2", { cat("D3", { item("deep") }) }) }), item("top") }); } });
        cases.push_back({ "entries_41", []() {
            std::vector<Node> v = many(39, "item");
            v.insert(v.begin(), cat("S1", {}));
            v.insert(v.begin(), cat("S2", {}));
            return cat("Forty One", v); } });
        cases.push_back({ "entries_42", []() {
            std::vector<Node> v = many(40, "item");
            v.insert(v.begin(), cat("S1", {}));
            v.insert(v.begin(), cat("S2", {}));
            return cat("Forty Two", v); } });
        cases.push_back({ "entries_43", []() {
            std::vector<Node> v = many(41, "item");
            v.insert(v.begin(), cat("S1", {}));
            v.insert(v.begin(), cat("S2", {}));
            return cat("Forty Three", v); } });
        FolderCase big{ "opensim_limit_100_entries_70", []() { return cat("Big", many(70, "item")); } };
        big.limit = 100;
        cases.push_back(big);
        big.id = "opensim_limit_100_entries_99";
        big.tree = []() { return cat("Big99", many(99, "item")); };
        cases.push_back(big);
        big.id = "opensim_limit_100_entries_100";
        big.tree = []() { return cat("Bigger", many(100, "item")); };
        cases.push_back(big);
        big.id = "opensim_limit_100_entries_101";
        big.tree = []() { return cat("Too Big", many(101, "item")); };
        cases.push_back(big);
        FolderCase longname{ "name_raw_700_entries_30", []() { return cat("x", many(30, "item")); } };
        longname.raw_name = true;
        longname.name = std::string(700, 'L');
        cases.push_back(longname);
        longname.id = "name_raw_1198_entries_1";
        longname.tree = []() { return cat("x", many(1, "item")); };
        longname.name = std::string(1198, 'L');
        cases.push_back(longname);
        longname.id = "name_raw_1200_entries_1";
        longname.name = std::string(1200, 'L');
        cases.push_back(longname);
        longname.id = "name_raw_1199_entries_1";
        longname.tree = []() { return cat("x", many(1, "item")); };
        longname.name = std::string(1199, 'L');
        cases.push_back(longname);
        longname.id = "name_raw_1300_entries_1";
        longname.name = std::string(1300, 'L');
        cases.push_back(longname);
        longname.id = "name_raw_embedded_nul";
        longname.name = std::string("Fol\0der", 7);
        cases.push_back(longname);
        longname.id = "name_raw_empty";
        longname.name = "";
        cases.push_back(longname);
        cases.push_back({ "name_63_corrected_max", []() { return cat(std::string(70, 'N'), many(42, "item")); } });
        cases.push_back({ "excluded_worn_and_no_transfer", []() {
            return cat("Mixed", { item("worn obj", 6, true, true, 1), item("worn shirt", 5, true, true, 2), item("no transfer", 7, true, false),
                                  item("plain") }); } });
        cases.push_back({ "only_excluded", []() {
            return cat("Nothing", { item("worn obj", 6, true, true, 1), item("no transfer", 7, true, false) }); } });
        cases.push_back({ "links_inside", []() {
            Node root = cat("Links", { item("target", 6) });
            Node l = item("link", 24);
            l.link_target = root.children[0].id;
            Node fl = item("folder link", 25);
            fl.link_target = U(0x2c, 999);
            root.children.push_back(l);
            root.children.push_back(fl);
            return root; } });
        cases.push_back({ "incomplete_subfolder", []() { return cat("Inc", { cat("Pending", {}, true), item("i") }); } });
        cases.push_back({ "incomplete_and_too_many", []() {
            std::vector<Node> v = many(43, "item");
            v.insert(v.begin(), cat("Pending", {}, true));
            return cat("Inc", v); } });
        cases.push_back({ "incomplete_and_empty", []() { return cat("Inc", { cat("Pending", {}, true) }); } });
        FolderCase trash{ "give_trash_itself", []() { return cat("Trash", { item("junk") }); } };
        trash.before = [](const Node& root) {
            // make the root the system trash
            F::gInventory.mSystemFolders[LLFolderType::FT_TRASH] = root.id; };
        cases.push_back(trash);
        // no-copy
        FolderCase nc{ "nocopy_warning_only", []() {
            return cat("NC", { item("copy a"), item("nocopy b", 7, false), cat("Sub", { item("nocopy c", 7, false), item("copy d") }) }); } };
        cases.push_back(nc);
        nc.id = "nocopy_yes"; nc.respond = 0; cases.push_back(nc);
        nc.id = "nocopy_no"; nc.respond = 1; cases.push_back(nc);
        nc.id = "nocopy_yes_success_notification"; nc.respond = 0; nc.notification = "ItemsShared"; cases.push_back(nc);
        nc.id = "nocopy_yes_count_changed_too_many";
        nc.notification = "";
        nc.between = [](const Node& root) {
            for (int i = 0; i < 40; ++i)
                add_item(U(0x29, i + 1), root.id, 7, make_perms(AGENT, perm_mask(true, true, true)), F::fmt("late %02d", i));
        };
        cases.push_back(nc);
        nc.id = "nocopy_yes_folder_gone";
        nc.between = [](const Node& root) { F::gInventory.mCategoryMap.erase(root.id); };
        cases.push_back(nc);
        nc.id = "nocopy_yes_rlv_share_blocked";
        nc.between = [](const Node&) { F::world().rlv_enabled = true; F::world().rlv_share_blocked.insert(OTHER); };
        cases.push_back(nc);
        nc.id = "nocopy_worn_nocopy_not_deleted";
        nc.between = nullptr;
        nc.tree = []() { return cat("NCW", { item("worn nocopy", 6, false, true, 1), item("nocopy", 7, false), item("copy") }); };
        cases.push_back(nc);
        FolderCase rlv{ "rlv_share_blocked_copyable", []() { return cat("R", { item("a") }); } };
        rlv.before = [](const Node&) { F::world().rlv_enabled = true; F::world().rlv_share_blocked.insert(OTHER); };
        cases.push_back(rlv);
        FolderCase muted{ "recipient_muted", []() { return cat("M", { item("a") }); } };
        muted.before = [](const Node&) {
            F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(OTHER, "other.sender", F::LLMute::AGENT)); };
        cases.push_back(muted);
        FolderCase inv{ "avatar_invalid", []() { return cat("A", { item("a") }); } };
        inv.before = [](const Node&) { F::gAgentAvatarp->mValid = false; };
        cases.push_back(inv);

        for (const FolderCase& c : cases)
        {
            setup();
            s_node_counter = 0;
            Node root = c.tree();
            build(root, ROOT);
            F::LLViewerInventoryCategory* rc = F::gInventory.getCategory(root.id);
            if (c.raw_name) rc->setRawName(c.name);
            F::LLWorld::getInstance()->mMaxInventoryItemsTransfer = c.limit;
            if (c.before) c.before(root);
            F::ev().lines.clear();
            put_world(s, c.id);
            F::ev().add(F::fmt("give folder %s to %s limit %d notification \"%s\"", root.id.asString().c_str(),
                                 OTHER.asString().c_str(), c.limit, c.notification.c_str()));
            bool r = D().give_category(OTHER, rc, LLUUID::null, c.notification);
            F::ev().add(F::fmt("result %d", (S32)r));
            if (c.respond >= 0)
            {
                if (c.between) c.between(root);
                respond_pending(c.respond);
            }
            run_idle();
            put_events(s, c.id);
        }
    }

    // ================================================= set 4: accept / decline
    static F::LLOfferInfo* make_offer(EInstantMessage im, const LLUUID& txn)
    {
        F::LLOfferInfo* o = new F::LLOfferInfo;
        o->mIM = im;
        o->mFromID = OTHER;
        o->mFromGroup = false;
        o->mFromObject = (im == IM_TASK_INVENTORY_OFFERED);
        o->mTransactionID = txn;
        o->mFolderID = folder_for(LLFolderType::FT_NOTECARD);
        o->mObjectID = (im == IM_INVENTORY_OFFERED) ? OFFERED : LLUUID::null;
        o->mType = LLAssetType::AT_NOTECARD;
        o->mFromName = "other.sender";
        o->mDesc = "Gift";
        o->mHost = LLHost(std::string(OFFER_HOST));
        return o;
    }

    static void corpus_responses(Sink& s)
    {
        const EInstantMessage dialogs[] = { IM_INVENTORY_OFFERED, IM_TASK_INVENTORY_OFFERED, IM_GROUP_NOTICE, IM_GROUP_NOTICE_REQUESTED };
        const LLUUID dest = U(0xde, 1);
        for (EInstantMessage d : dialogs)
        {
            for (int txn_null = 0; txn_null < 2; ++txn_null)
            {
                const LLUUID txn = txn_null ? LLUUID::null : TXN;
                const std::string base = F::fmt("d%d.%s", (S32)d, txn_null ? "txn_null" : "txn");
                struct Op { const char* id; std::function<void(F::LLOfferInfo*)> run; };
                std::vector<Op> ops = {
                    { "accept_dest", [dest](F::LLOfferInfo* o) { D().send_receive_response(o, true, dest); } },
                    { "accept_null_dest", [](F::LLOfferInfo* o) { D().send_receive_response(o, true, LLUUID::null); } },
                    { "decline", [dest](F::LLOfferInfo* o) { D().send_receive_response(o, false, dest); } },
                    { "accept_then_decline", [dest](F::LLOfferInfo* o) {
                        D().send_receive_response(o, true, dest); D().send_receive_response(o, false, dest); } },
                    { "auto_receive", [](F::LLOfferInfo* o) { D().send_auto_receive_response(o); } },
                    { "legacy_decline", [](F::LLOfferInfo* o) { D().send_decline_response(o); } },
                    { "legacy_decline_twice", [](F::LLOfferInfo* o) { D().send_decline_response(o); D().send_decline_response(o); } },
                    { "accept_avatar_invalid", [dest](F::LLOfferInfo* o) {
                        F::gAgentAvatarp->mValid = false; D().send_receive_response(o, true, dest); } },
                    { "accept_rlv_names_hidden_nearby", [dest](F::LLOfferInfo* o) {
                        F::world().rlv_enabled = true; F::world().rlv_can_show_names = false; F::world().rlv_nearby.insert(OTHER);
                        D().send_receive_response(o, true, dest); } },
                };
                for (const Op& op : ops)
                {
                    setup();
                    install_snapshot_hook();
                    F::LLOfferInfo* o = make_offer(d, txn);
                    F::ev().lines.clear();
                    F::ev().add("offer " + offer_fields(*o));
                    op.run(o);
                    F::ev().add("after " + offer_fields(*o));
                    F::ev().on = false;
                    delete o;
                    F::ev().on = true;
                    put_events(s, base + "." + op.id);
                }
            }
        }
    }

    // ====================================================== set 5: incoming
    struct Incoming
    {
        std::string id;
        EInstantMessage dialog;
        std::vector<U8> bucket;
        S32 declared_size = -1;     // binary_bucket_size if not the vector's
        LLUUID session = TXN;
        LLUUID aux;
        std::string name = "Sender Resident";
        std::string message = "Gift";
        bool from_group = false;
        LLUUID from = OTHER;
        int fill = 0;               // bytes after the bucket: 0 zeros, 1 'A' (0x41)
        bool trim = true;           // LLAvatarName::trimResidentSurname()
    };

    static std::vector<U8> agent_bucket(S8 type, const LLUUID& id)
    {
        std::vector<U8> b(17);
        b[0] = (U8)type;
        memcpy(&b[1], id.mData, 16);
        return b;
    }
    static std::vector<U8> str_bucket(const std::string& s) { return std::vector<U8>(s.begin(), s.end()); }

    static std::vector<U8> notice_bucket(U8 has_inv, U8 type, const LLUUID& group, const std::string& name, bool terminate = true)
    {
        std::vector<U8> b;
        b.push_back(has_inv);
        b.push_back(type);
        b.insert(b.end(), group.mData, group.mData + 16);
        b.insert(b.end(), name.begin(), name.end());
        if (terminate) b.push_back(0);
        return b;
    }

    // The legacy task bucket is read as a C string: when it holds no NUL the
    // read runs on past the bucket. This states what the stream conversion
    // would store with a sentinel, so a case whose `type` stays uninitialised
    // is reported rather than recorded with whatever the stack held.
    static std::string legacy_type_probe(const U8* buffer)
    {
        std::string str_bucket(reinterpret_cast<const char*>(buffer));
        std::string str_type(str_bucket.substr(0, str_bucket.find('|')));
        S32 a = 0x5a5a5a5a, b = -0x5a5a5a5b;
        { std::stringstream c(str_type); c >> a; }
        { std::stringstream c(str_type); c >> b; }
        bool uninit = (a == 0x5a5a5a5a && b == -0x5a5a5a5b);
        return F::fmt("legacy_read len=%d type_text=\"%s\" %s", (S32)str_bucket.size(), F::esc(str_type).c_str(),
                        uninit ? "type=UNINITIALISED" : F::fmt("type=%d", a).c_str());
    }

    static void run_incoming(const Incoming& c, bool record_probe)
    {
        // process_improved_im copies the bucket into U8[MTUBYTES]; the fill
        // stands for what the stack held beyond it (with a terminator past the
        // array so the over-read ends inside the test's buffer).
        std::vector<U8> buffer(MTUBYTES + 64, c.fill ? 0x41 : 0);
        buffer[MTUBYTES + 63] = 0;
        if (c.fill) buffer[MTUBYTES] = 0;
        size_t n = std::min(c.bucket.size(), (size_t)MTUBYTES);
        if (n) memcpy(buffer.data(), c.bucket.data(), n);
        S32 size = c.declared_size >= 0 ? c.declared_size : (S32)c.bucket.size();
        if (record_probe && c.dialog == IM_TASK_INVENTORY_OFFERED && size != 1)
        {
            F::ev().add(legacy_type_probe(buffer.data()));
        }
        LLHost sender{ std::string(OFFER_HOST) };
        F::ev().add(F::fmt("im dialog=%d from=%s group=%d session=%s aux=%s size=%d bucket=%s name=\"%s\" message=\"%s\" fill=%d",
                             (S32)c.dialog, c.from.asString().c_str(), (S32)c.from_group, c.session.asString().c_str(),
                             c.aux.asString().c_str(), size, F::hex(c.bucket.data(), c.bucket.size()).c_str(),
                             F::esc(c.name).c_str(), F::esc(c.message).c_str(), c.fill));
        D().receive_im(c.from, c.from_group, c.dialog, c.session, 0, c.name, c.message, buffer.data(), size, sender, c.aux);
        run_idle();
    }

    static std::vector<Incoming> incoming_cases()
    {
        std::vector<Incoming> v;
        auto add = [&v](Incoming c) { v.push_back(c); };
        // agent offers: the 17-byte struct only
        for (S32 sz : { 0, 1, 16, 18, 34 })
        {
            Incoming c{ F::fmt("agent_size_%d", sz), IM_INVENTORY_OFFERED, std::vector<U8>(sz, 0x07) };
            add(c);
        }
        for (S32 t : { 0, 6, 7, 8, 13, 24, 25, 56, 59, 60, 127, -1, -2, -128 })
        {
            Incoming c{ F::fmt("agent_type_%d", t), IM_INVENTORY_OFFERED, agent_bucket((S8)t, OFFERED) };
            add(c);
        }
        {
            Incoming c{ "agent_null_object", IM_INVENTORY_OFFERED, agent_bucket(7, LLUUID::null) };
            add(c);
            c.id = "agent_session_null_aux"; c.bucket = agent_bucket(7, OFFERED); c.session = LLUUID::null; c.aux = U(0xd4, 2);
            add(c);
            c.id = "agent_session_and_aux_null"; c.aux = LLUUID::null;
            add(c);
            c.id = "agent_session_and_aux"; c.session = TXN; c.aux = U(0xd4, 2);
            add(c);
            c.id = "agent_name_resident"; c.aux = LLUUID::null; c.name = "Sender Resident";
            add(c);
            c.id = "agent_name_blank"; c.name = "   ";
            add(c);
            c.id = "agent_name_legacy"; c.name = "Sender Lastname";
            add(c);
            c.id = "agent_name_resident_untrimmed"; c.name = "Sender Resident"; c.trim = false;
            add(c);
            c.trim = true;
            c.id = "agent_from_group"; c.name = "Sender Resident"; c.from_group = true;
            add(c);
            c.id = "agent_from_self"; c.from_group = false; c.from = AGENT;
            add(c);
        }
        // task offers: one byte, or the legacy "<type>|..." string
        for (S32 t : { 0, 6, 7, 8, 24, 59, 60, 127, 128, 254, 255 })
        {
            Incoming c{ F::fmt("task_type_%d", t), IM_TASK_INVENTORY_OFFERED, std::vector<U8>(1, (U8)t) };
            c.message = "'Gadget'  ( http://slurl.com/secondlife/Region/1/2/3 )";
            c.name = "Gadget";
            add(c);
        }
        struct Legacy { const char* id; std::string text; bool nul; };
        std::vector<Legacy> legacy = {
            { "7_pipe_nul", "7|abc", true },
            { "7_pipe_no_nul", "7|abc", false },
            { "8_pipe_nul", "8|folder|x", true },
            { "7_no_pipe_nul", "7", true },
            { "77_no_pipe_no_nul", "77", false },
            { "pipe_only", "|x", true },
            { "empty_type_no_nul", "|", false },
            { "space_7", " 7|x", true },
            { "neg_2", "-2|x", true },
            { "overflow", "99999999999|x", true },
            { "alpha", "x|y", true },
            { "7x", "7x|y", true },
            { "plus_8", "+8|y", true },
            { "leading_nul", std::string("\0" "7|x", 4), false },
        };
        for (const Legacy& l : legacy)
        {
            for (int fill = 0; fill < 2; ++fill)
            {
                std::vector<U8> b = str_bucket(l.text);
                if (l.nul) b.push_back(0);
                Incoming c{ F::fmt("task_legacy_%s_fill%d", l.id, fill), IM_TASK_INVENTORY_OFFERED, b };
                c.fill = fill;
                c.message = "'Gadget'  ( http://slurl.com/secondlife/Region/1/2/3 )";
                c.name = "Gadget";
                add(c);
            }
        }
        for (S32 sz : { 0, 16, 17, 18, 34 })
        {
            for (int fill = 0; fill < 2; ++fill)
            {
                std::vector<U8> b = sz == 17 ? agent_bucket(7, OFFERED) : std::vector<U8>(sz, (U8)'7');
                Incoming c{ F::fmt("task_size_%d_fill%d", sz, fill), IM_TASK_INVENTORY_OFFERED, b };
                c.fill = fill;
                c.name = "Gadget";
                c.message = "'Gadget'  ( http://slurl.com/secondlife/Region/1/2/3 )";
                add(c);
            }
        }
        {
            Incoming c{ "task_declared_1300", IM_TASK_INVENTORY_OFFERED, std::vector<U8>(1300, (U8)'7') };
            c.name = "Gadget";
            add(c);
            c.id = "task_session_null_aux"; c.bucket = std::vector<U8>(1, 7); c.session = LLUUID::null; c.aux = U(0xd4, 2);
            add(c);
            c.id = "task_from_group"; c.session = TXN; c.aux = LLUUID::null; c.from_group = true; c.from = GROUP;
            add(c);
            c.id = "task_from_self"; c.from_group = false; c.from = AGENT;
            add(c);
            c.id = "task_name_resident_kept"; c.from = OTHER; c.name = "Gadget Resident";
            add(c);
        }
        // group notices: the binary bucket (aux null) and the "<type>|<type>|<name>" string (aux = group)
        for (EInstantMessage d : { IM_GROUP_NOTICE, IM_GROUP_NOTICE_REQUESTED })
        {
            std::string p = F::fmt("d%d_", (S32)d);
            Incoming c{ p + "bin_full", d, notice_bucket(1, 7, GROUP, "Notice Item") };
            c.message = "Subject|Body text";
            c.name = "Officer Resident";
            add(c);
            c.id = p + "bin_no_inventory"; c.bucket = notice_bucket(0, 7, GROUP, "Notice Item"); add(c);
            c.id = p + "bin_19_empty_name"; c.bucket = notice_bucket(1, 7, GROUP, ""); add(c);
            c.id = p + "bin_18_short"; c.bucket = notice_bucket(1, 7, GROUP, "", false); add(c);
            c.id = p + "bin_not_terminated"; c.bucket = notice_bucket(1, 7, GROUP, "Item", false); add(c);
            c.id = p + "bin_type_category"; c.bucket = notice_bucket(1, 8, GROUP, "Kit"); add(c);
            c.id = p + "bin_type_255"; c.bucket = notice_bucket(1, 255, GROUP, "Odd"); add(c);
            c.id = p + "bin_embedded_nul"; c.bucket = notice_bucket(1, 7, GROUP, std::string("Ab\0cd", 5)); add(c);
            c.id = p + "bin_group_is_sender"; c.bucket = notice_bucket(1, 7, OTHER, "Item"); add(c);
            c.id = p + "bin_group_is_sender_known_name"; c.name = "Known Resident"; add(c);
            c.id = p + "bin_uuid_name"; c.bucket = notice_bucket(1, 7, GROUP, "Item"); c.name = OTHER2.asString(); add(c);
            c.id = p + "bin_sender_muted"; c.name = "Officer Resident"; add(c);
            c.id = p + "bin_session_null"; c.session = LLUUID::null; add(c);
            c.id = p + "message_no_pipe"; c.session = TXN; c.message = "Subject only"; add(c);
            c.message = "Subject|Body text";
            c.aux = GROUP;
            c.id = p + "str_full"; c.bucket = str_bucket("7|7|Notice Item"); add(c);
            c.id = p + "str_wearable"; c.bucket = str_bucket("5|4|Shirt"); add(c);
            c.id = p + "str_nul_terminated"; c.bucket = str_bucket(std::string("7|7|Item\0", 9)); add(c);
            c.id = p + "str_empty_fields"; c.bucket = str_bucket("||"); add(c);
            c.id = p + "str_extra_fields"; c.bucket = str_bucket("7|7|Item|more"); add(c);
            c.id = p + "str_alpha_type"; c.bucket = str_bucket("x|7|Item"); add(c);
            c.id = p + "str_size_1"; c.bucket = std::vector<U8>(1, 0); add(c);
            c.id = p + "str_size_0"; c.bucket = std::vector<U8>(); add(c);
            c.id = p + "str_from_group_false"; c.bucket = str_bucket("7|7|Item"); c.from_group = false; add(c);
        }
        return v;
    }

    static void corpus_incoming(Sink& s)
    {
        for (const Incoming& c : incoming_cases())
        {
            setup();
            install_snapshot_hook();
            ::LLAvatarName::setTrimResidentSurname(c.trim);
            F::LLAvatarNameCache::instance().ids_by_legacy_name["Known Resident"] = OTHER2;
            if (c.id.find("sender_muted") != std::string::npos)
            {
                F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(OTHER, "", F::LLMute::AGENT));
            }
            F::ev().lines.clear();
            run_incoming(c, true);
            // Runtime edge: when the legacy task string holds no digits before
            // '|' or its end, `type` stays uninitialised (the stack's value).
            // The probe line states it; what follows depends on that value and
            // is kept for review only.
            bool uninit = false;
            for (const std::string& e : F::ev().lines)
            {
                if (e.compare(0, 12, "legacy_read ") == 0 && e.find("type=UNINITIALISED") != std::string::npos) uninit = true;
            }
            if (uninit)
            {
                std::vector<std::string> kept;
                for (size_t i = 0; i < F::ev().lines.size(); ++i)
                {
                    const std::string& e = F::ev().lines[i];
                    if (i < 2 || e.compare(0, 5, "note:") == 0) kept.push_back(e);
                    else kept.push_back("note:uninitialised-type outcome: " + e);
                }
                kept.insert(kept.begin() + 2, "runtime_edge type uninitialised: the outcome depends on the stack and is not compared");
                F::ev().lines = kept;
            }
            put_events(s, c.id);
        }
    }

    // ------------------------------------------------ LLOfferInfo LLSD form
    static void put_llsd(Sink& s, const std::string& c, const std::string& key, const LLSD& sd)
    {
        put(s, c, key + ".bin", F::sd_bin_hex(sd));
        put(s, c, "note." + key, F::sd_notation(sd));
    }

    static void corpus_offerinfo_llsd(Sink& s)
    {
        setup();
        F::ev().on = false;
        struct Fields
        {
            const char* id;
            S32 im; LLUUID from; bool group; bool object; LLUUID txn; LLUUID folder; LLUUID obj; S32 type;
            std::string from_name; std::string desc; std::string host; bool persist;
        };
        std::vector<Fields> fields = {
            { "agent", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 7, "other.sender", "Gift", OFFER_HOST, true },
            { "task", 9, OTHER, false, true, TXN, U(0xf0, 7), LLUUID::null, 7, "Gadget", "'Gadget'  ( http://slurl.com/secondlife/R/1/2/3 )", OFFER_HOST, true },
            { "group_notice", 32, OTHER, true, false, TXN, U(0xf0, 8), LLUUID::null, 8, "{InvOfferGroupNoticeName|NAME=Officer}", "Kit", OFFER_HOST, false },
            { "group_requested", 37, GROUP, true, false, TXN, U(0xf0, 7), LLUUID::null, 7, "x", "y", OFFER_HOST, false },
            { "null_everything", 0, LLUUID::null, false, false, LLUUID::null, LLUUID::null, LLUUID::null, -1, "", "", "", false },
            { "type_link", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 24, "a", "b", OFFER_HOST, false },
            { "type_127", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 127, "a", "b", OFFER_HOST, false },
            { "type_unknown_255", 9, OTHER, false, true, TXN, U(0xf0, 7), LLUUID::null, 255, "a", "b", OFFER_HOST, false },
            { "type_minus_2", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, -2, "a", "b", OFFER_HOST, false },
            { "utf8_names", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 0, "J\xc3\xa9r\xc3\xb4me", "\xe2\x9c\x93 tick", OFFER_HOST, false },
            { "uuid_pipe_name", 32, OTHER, true, false, TXN, U(0xf0, 7), LLUUID::null, 7, OTHER2.asString() + "|{InvOfferGroupNoticeName|NAME=%s}", "Kit", OFFER_HOST, false },
            { "host_zero", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 7, "a", "b", "0.0.0.0:0", false },
            { "host_max_port", 4, OTHER, false, false, TXN, U(0xf0, 7), OFFERED, 7, "a", "b", "255.255.255.255:65535", false },
        };
        for (const Fields& f : fields)
        {
            F::LLOfferInfo o;
            o.mIM = (EInstantMessage)f.im; o.mFromID = f.from; o.mFromGroup = f.group; o.mFromObject = f.object;
            o.mTransactionID = f.txn; o.mFolderID = f.folder; o.mObjectID = f.obj; o.mType = (LLAssetType::EType)f.type;
            o.mFromName = f.from_name; o.mDesc = f.desc; o.mHost = f.host.empty() ? LLHost() : LLHost(f.host); o.mPersist = f.persist;
            std::string c = std::string("fields_") + f.id;
            put(s, c, "in", offer_fields(o));
            LLSD sd = D().offer_as_llsd(&o);
            put_llsd(s, c, "asLLSD", sd);
            std::unique_ptr<F::LLOfferInfo> back(D().offer_from_llsd(sd));
            put(s, c, "fromLLSD", offer_fields(*back));
            F::LLOfferInfo viaFrom;
            D().offer_from_llsd_assign(&viaFrom, sd);
            put(s, c, "fromLLSD_assign", offer_fields(viaFrom));
            LLSD again = D().offer_as_llsd(back.get());
            put(s, c, "round_trip_equal", F::sd_bin_hex(again) == F::sd_bin_hex(sd) ? "1" : "0");
        }
        // crafted documents
        struct Doc { const char* id; std::string notation; };
        std::vector<Doc> docs = {
            { "empty_map", "{}" },
            { "undefined", "!" },
            { "im_as_string", "{'im_type':'4','type':'notecard','sender':'10.0.0.9:13005','persist':1}" },
            { "im_as_real", "{'im_type':r9.7,'type':'object'}" },
            { "type_unknown_name", "{'im_type':i4,'type':'bogus'}" },
            { "type_empty", "{'im_type':i4,'type':''}" },
            { "type_upper", "{'im_type':i4,'type':'NOTECARD'}" },
            { "type_human_readable", "{'im_type':i4,'type':'notecard'}" },
            { "ids_as_strings", "{'from_id':'b2000000-0000-4000-8000-000000000001','transaction_id':'not a uuid','folder_id':''}" },
            { "sender_no_port", "{'sender':'10.0.0.9'}" },
            { "sender_garbage", "{'sender':'garbage:xx'}" },
            { "sender_name", "{'sender':'localhost:13000'}" },
            { "bools_as_strings", "{'from_group':'true','from_object':'false','persist':'1'}" },
            { "bools_as_ints", "{'from_group':i2,'from_object':i0,'persist':i-1}" },
            { "extra_keys", "{'responder_type':'offer_info','im_type':i9,'unknown':i1}" },
            { "binary_desc", "{'description':b(3)\"a\0b\",'from_name':i42}" },
        };
        for (const Doc& d : docs)
        {
            LLSD sd;
            std::istringstream in(d.notation);
            LLSDSerialize::fromNotation(sd, in, d.notation.size());
            std::string c = std::string("doc_") + d.id;
            put_llsd(s, c, "doc", sd);
            std::unique_ptr<F::LLOfferInfo> o(D().offer_from_llsd(sd));
            put(s, c, "fromLLSD", offer_fields(*o));
            put_llsd(s, c, "asLLSD", D().offer_as_llsd(o.get()));
        }
        F::ev().on = true;
        F::ev().lines.clear();
    }

    // ===================================== set 6: forced response indexing
    static void corpus_forced_index(Sink& s)
    {
        setup();
        // the form element order of every offer template
        for (const char* name : TEMPLATE_FORMS)
        {
            LLSD form = F::notification_form(name);
            for (S32 i = 0; i < form.size(); ++i)
            {
                put(s, std::string("form_") + name, F::fmt("element%d", i),
                    F::fmt("name=%s button_index=%d", form[i]["name"].asString().c_str(), form[i]["index"].asInteger()));
            }
        }
        // what forceResponse passes and what the callback receives
        for (S32 r = 0; r <= 6; ++r)
        {
            setup();
            install_snapshot_hook();
            F::LLOfferInfo* o = make_offer(IM_INVENTORY_OFFERED, TXN);
            o->mPersist = true;
            F::ev().lines.clear();
            F::ev().add(F::fmt("forceResponse(%d)", r));
            D().force_response(o, r);
            run_idle();
            put_events(s, F::fmt("force_%d", r));
            // the element index forceResponse used, and the button that
            // element is, against what the button index would have selected
            LLSD form = F::notification_form("UserGiveItem");
            S32 element = r == F::IOR_ACCEPT ? 1 : r == F::IOR_DECLINE ? 2 : r == F::IOR_MUTE ? 3 : -1;
            LLSD by_element = form.get(element);
            LLSD by_button = form.get(r);
            put(s, F::fmt("index_%d", r), "element_used",
                F::fmt("%d -> %s (button %d)", element, by_element.isDefined() ? by_element["name"].asString().c_str() : "-",
                         by_element.isDefined() ? by_element["index"].asInteger() : -1));
            put(s, F::fmt("index_%d", r), "if_button_index_were_used",
                F::fmt("%d -> %s (button %d)", r, by_button.isDefined() ? by_button["name"].asString().c_str() : "-",
                         by_button.isDefined() ? by_button["index"].asInteger() : -1));
            F::ev().on = false;
            delete o;
            F::ev().on = true;
            F::ev().lines.clear();
        }
    }

    // ============================================== set 6: offer throttle
    static void corpus_throttle(Sink& s)
    {
        struct Step { F64 t; bool check_only; const char* from; };
        struct Trace { const char* id; U32 max; bool started; bool spam_from; bool spam; std::vector<Step> steps; };
        std::vector<Step> burst;
        burst.push_back({ 0.0, false, "giver" });
        for (int i = 1; i <= 7; ++i) burst.push_back({ 0.1 * i, false, "giver" });
        burst.push_back({ 0.9, true, "giver" });
        burst.push_back({ 10.5, true, "giver" });
        burst.push_back({ 10.5, false, "giver" });
        burst.push_back({ 10.6, false, "" });
        std::vector<Step> anon = burst;
        for (Step& st : anon) st.from = "";
        std::vector<Trace> traces = {
            { "default_burst", 5, true, true, true, burst },
            { "anonymous_burst", 5, true, true, true, anon },
            { "max_2", 2, true, true, true, burst },
            { "max_0", 0, true, true, true, burst },
            { "before_started", 5, false, true, true, burst },
            { "notices_off", 5, true, false, false, burst },
            { "check_only_first", 5, true, true, true, { { 0.0, true, "x" }, { 0.0, false, "x" }, { 0.0, true, "x" }, { 9.99, true, "x" }, { 10.0, true, "x" } } },
        };
        for (const Trace& tr : traces)
        {
            setup();
            F::gSavedSettings.u["FSOfferThrottleMaxCount"] = tr.max;
            F::gSavedSettings.b["FSNotifyIncomingObjectSpamFrom"] = tr.spam_from;
            F::gSavedSettings.b["FSNotifyIncomingObjectSpam"] = tr.spam;
            F::world().startup_state = tr.started ? F::STATE_STARTED : F::STATE_FIRST;
            // the static counters of check_offer_throttle carry across traces:
            // every trace starts with an expired timer, which resets them
            F::gThrottleTimer.mExpiry = 0.0;
            F::ev().lines.clear();
            const F64 t0 = 5000.0;
            for (const Step& st : tr.steps)
            {
                F::fake_clock() = t0 + st.t;
                bool r = D().check_offer_throttle(st.from, st.check_only);
                F::ev().add(F::fmt("t=%.2f check_only=%d from=\"%s\" -> %d", st.t, (S32)st.check_only, st.from, (S32)r));
            }
            put_events(s, tr.id);
        }
    }

    // ================================================== set 6: policy table
    struct PolicyCase
    {
        S32 dialog;     // 4, 9, 32, 37
        bool self;
        S32 mute;       // 0 none, 1 by id, 2 by name, 3 by id with the text chat exception
        bool dnd;
        bool auto_accept;
        bool legacy;
        bool rlv;
        S32 give_rlv;   // 0 no prefix, 1 prefix with a shared root, 2 prefix without one
        bool forbid;
        S32 type;       // 0 category, 1 notecard, 2 invalid (127)
        S32 button;     // 0..6 the IOR values, 7 closed
        bool forced;
        bool dismiss;
        bool log_decline;
        bool present = false; // the offered object is already in the model and finished
        // the response table (policy_response) only
        bool response_table = false;
        bool from_group = false; // a group-owned object (task) / the group flag (notices)
        S32 late_mute = 0;       // mute added between arrival and response: 0 none, 1 by id, 2 by name
        bool linden = false;     // the sender is named "Grid Linden"
        bool defer = false;      // name lookups answer from idle
        std::string id() const
        {
            static const char* mutes[] = { "none", "id", "name", "id_textchat_exempt" };
            static const char* grlv[] = { "no", "root", "noroot" };
            static const char* types[] = { "cat", "note", "bad" };
            static const char* buttons[] = { "accept", "decline", "mute", "show", "accept_silent", "decline_silent", "show_silent", "closed" };
            return F::fmt("d%d.self%d.mute_%s.dnd%d.aa%d.legacy%d.rlv%d.grlv_%s.forbid%d.type_%s.%s.%s.dismiss%d.logdecl%d%s",
                            dialog, (S32)self, mutes[mute], (S32)dnd, (S32)auto_accept, (S32)legacy, (S32)rlv, grlv[give_rlv],
                            (S32)forbid, types[type], buttons[button], forced ? "forced" : "interactive", (S32)dismiss,
                            (S32)log_decline, present ? ".present" : "") +
                   (response_table ? F::fmt(".fg%d.late_%s.linden%d.defer%d", (S32)from_group, late_mute == 0 ? "none" : late_mute == 1 ? "id" : "name",
                                            (S32)linden, (S32)defer) : std::string());
        }
    };

    // The policy projection of the event log: the events the offer policy
    // decides (see the README): messages, the notification or forced
    // response, local actions.
    static bool policy_event(const std::string& e)
    {
        static const char* keep[] = {
            "send ", "notify.postponed ", "notify ", "notification.respond ", "model.removeObject ", "model.deleteObject ",
            "model.addObserver ", "fetch.start ", "fetch.done ", "open_inventory_offer ", "new LLOpenTaskGroupOffer",
            "new RlvGiveToRLV", "RlvGiveToRLVAgentOffer.done", "rlv.sendNotification ", "mute.add ", "mute.remove ",
            "recent_people.add ", "inventory_panel.select ", "group_panel.showNotice ", "stage ", "nearby_chat ",
            "toasts.kill ", "blocked_list.show ", "offer.", "responder.deleted", "name.deferred " };
        for (const char* k : keep)
        {
            if (e.compare(0, strlen(k), k) == 0) return true;
        }
        return false;
    }

    // notify lines lose their args (UI text): name and payload stay; offer
    // snapshots keep the offer's state (dialog, transaction, folder, object,
    // type, group flag, host, persist) and lose the sender's id, name and the
    // description, which the case id fixes
    static std::string policy_render(const std::string& e)
    {
        if (e.compare(0, 6, "offer.") == 0)
        {
            // drop the space-separated fields from=, from_object=, and the
            // quoted from_name="..." and desc="..." (with \-escapes)
            std::string out;
            size_t k = 0;
            while (k < e.size())
            {
                size_t sp = e.find(' ', k);
                if (sp == std::string::npos) { out += e.substr(k); break; }
                out += e.substr(k, sp - k);
                size_t f = sp + 1;
                auto starts = [&](const char* t) { return e.compare(f, strlen(t), t) == 0; };
                if (starts("from=") || starts("from_object="))
                {
                    size_t nx = e.find(' ', f);
                    k = nx == std::string::npos ? e.size() : nx;
                }
                else if (starts("from_name=\"") || starts("desc=\""))
                {
                    size_t q = e.find('"', f) + 1;
                    while (q < e.size() && e[q] != '"') q += e[q] == '\\' ? 2 : 1;
                    k = q + 1;
                }
                else
                {
                    out += ' ';
                    k = f;
                }
            }
            return out;
        }
        if (e.compare(0, 5, "send ") == 0)
        {
            size_t z = e.find(" zc=");
            if (z != std::string::npos) return e.substr(0, z);
        }
        if (e.compare(0, 7, "notify ") == 0 || e.compare(0, 16, "notify.postponed") == 0)
        {
            size_t a = e.find(" args=");
            size_t p = e.find(" payload=");
            if (a != std::string::npos && p != std::string::npos && p > a)
            {
                return e.substr(0, a) + e.substr(p);
            }
        }
        return e;
    }

    static void run_policy_case(const PolicyCase& pc, Sink& s, bool full_events, const std::string& named = std::string())
    {
        setup();
        install_snapshot_hook();
        F::world().defer_name_lookups = pc.defer;
        auto& b = F::gSavedSettings.b;
        b["AutoAcceptNewInventory"] = pc.auto_accept;
        b["FSUseLegacyInventoryAcceptMessages"] = pc.legacy;
        b["FSDismissGroupNoticeAttachmentsToTrash"] = pc.dismiss;
        b["LogInventoryDecline"] = pc.log_decline;
        F::gAgent.mDoNotDisturb = pc.dnd;
        F::world().rlv_enabled = pc.rlv;
        F::world().rlv_forbid_give_to_rlv = pc.forbid;
        F::world().rlv_shared_root = pc.give_rlv == 1 ? RLV_ROOT : LLUUID::null;
        if (pc.give_rlv == 2) F::world().rlv_shared_root.setNull();
        const bool agent = pc.dialog == IM_INVENTORY_OFFERED;
        const bool task = pc.dialog == IM_TASK_INVENTORY_OFFERED;
        const LLUUID from = (task && pc.from_group) ? GROUP : pc.self ? AGENT : OTHER;
        const std::string name = pc.linden ? "Grid Linden" : task ? "Gadget" : "Sender";
        if (pc.mute == 1) F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(from, name, F::LLMute::AGENT));
        if (pc.mute == 2) F::LLMuteList::getInstance()->mLegacyMutes.insert(name);
        if (pc.mute == 3) F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(from, name, F::LLMute::AGENT, F::LLMute::flagTextChat));
        const S32 type = pc.type == 0 ? LLAssetType::AT_CATEGORY : pc.type == 1 ? LLAssetType::AT_NOTECARD : 127;
        if (pc.present)
        {
            if (type == LLAssetType::AT_CATEGORY) add_cat(OFFERED, ROOT, "Offered");
            else add_item(OFFERED, ROOT, type, make_perms(AGENT, PERM_ALL), "Offered");
        }
        Incoming c;
        c.dialog = (EInstantMessage)pc.dialog;
        c.from = from;
        c.from_group = pc.from_group;
        c.name = name;
        c.session = TXN;
        if (agent)
        {
            c.bucket = agent_bucket((S8)type, OFFERED);
            c.message = pc.give_rlv ? "#RLV/~kit" : "Gift";
        }
        else if (task)
        {
            c.bucket = std::vector<U8>(1, (U8)type);
            c.message = pc.give_rlv ? "'#RLV/~kit'  ( http://slurl.com/secondlife/Region/1/2/3 )"
                                    : "'Gadget Item'  ( http://slurl.com/secondlife/Region/1/2/3 )";
        }
        else
        {
            c.bucket = notice_bucket(1, (U8)type, GROUP, pc.give_rlv ? "#RLV/~kit" : "Notice Item");
            c.message = "Subject|Body";
        }
        F::ev().lines.clear();
        F::ev().add("stage arrival");
        run_incoming(c, false);
        if (pc.late_mute)
        {
            F::ev().add(F::fmt("stage late_mute %s", pc.late_mute == 1 ? "by_id" : "by_name"));
            if (pc.late_mute == 1) F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(from, name, F::LLMute::AGENT));
            else F::LLMuteList::getInstance()->mLegacyMutes.insert(name);
        }
        // the response
        F::ev().add(F::fmt("stage response %s button=%d", pc.forced ? "forced" : "interactive", pc.button));
        if (pc.button == 7 && pc.forced)
        {
            F::ev().add("stage unreachable (no forced close)");
        }
        else if (agent || task)
        {
            if (F::world().posted.empty())
            {
                F::ev().add("stage none (no notification posted)");
            }
            else
            {
                const F::World::Posted& posted = F::world().posted.back();
                F::LLNotificationPtr n = F::LLNotifications::instance().find(posted.id);
                F::LLOfferInfo* info = n ? dynamic_cast<F::LLOfferInfo*>(n->mResponder.get()) : nullptr;
                if (!n || !info)
                {
                    F::ev().add("stage none (no responder)");
                }
                else if (pc.forced)
                {
                    D().force_response(info, pc.button);
                }
                else
                {
                    LLSD response;
                    bool found = pc.button == 7;
                    if (pc.button == 7)
                    {
                        response = n->getResponseTemplate();
                    }
                    else
                    {
                        LLSD form = n->getForm() ? n->getForm()->asLLSD() : LLSD::emptyArray();
                        for (S32 i = 0; i < form.size(); ++i)
                        {
                            if (form[i]["index"].asInteger() == pc.button)
                            {
                                response[form[i]["name"].asString()] = true;
                                found = true;
                            }
                        }
                    }
                    if (found) n->respond(response);
                    else F::ev().add("stage unreachable (" + n->getName() + " has no such button)");
                }
            }
        }
        else
        {
            // group notices are answered by forced responses only (toast,
            // notification well, group panel)
            F::LLOfferInfo* info = nullptr;
            if (pc.dialog == IM_GROUP_NOTICE && F::world().group_notice_payload.has("inventory_offer"))
            {
                info = new F::LLOfferInfo(F::world().group_notice_payload["inventory_offer"]);
            }
            else if (pc.dialog == IM_GROUP_NOTICE_REQUESTED)
            {
                auto it = F::world().group_panel_offers.find(GROUP);
                if (it != F::world().group_panel_offers.end()) info = it->second;
                F::world().group_panel_offers.clear();
            }
            if (!pc.forced)
            {
                F::ev().add("stage unreachable (group notice offers are answered by forced responses)");
                F::ev().on = false;
                delete info;
                F::ev().on = true;
            }
            else if (!info)
            {
                F::ev().add("stage none (no offer)");
            }
            else
            {
                D().force_response(info, pc.button);
                // forceResponse with an option outside the form calls nothing
                if (F::live_responders().count(info))
                {
                    F::ev().on = false;
                    delete info;
                    F::ev().on = true;
                }
            }
        }
        run_idle();
        const std::string cid = named.empty() ? pc.id() : named;
        int n = 0;
        for (const std::string& e : F::ev().lines)
        {
            if (e.compare(0, 5, "note:") == 0) continue;
            if (policy_event(e)) put(s, cid, F::fmt("p%02d", n++), policy_render(e));
        }
        if (full_events)
        {
            int k = -1;
            for (const std::string& e : F::ev().lines)
            {
                if (e.compare(0, 5, "note:") == 0) put(s, cid, F::fmt("note.full.e%02d.note", k), e.substr(5));
                else put(s, cid, F::fmt("note.full.e%02d", ++k), e);
            }
        }
        F::ev().lines.clear();
    }

    template<class Fn>
    static void for_each_policy_case(Fn fn)
    {
        PolicyCase pc;
        for (S32 d : { 4, 9, 32, 37 })
        for (int self = 0; self < 2; ++self)
        for (S32 mute = 0; mute < 4; ++mute)
        for (int dnd = 0; dnd < 2; ++dnd)
        for (int aa = 0; aa < 2; ++aa)
        for (int legacy = 0; legacy < 2; ++legacy)
        for (int rlv = 0; rlv < 2; ++rlv)
        for (S32 grlv = 0; grlv < 3; ++grlv)
        for (int forbid = 0; forbid < 2; ++forbid)
        for (S32 type = 0; type < 3; ++type)
        for (S32 button = 0; button < 8; ++button)
        for (int forced = 0; forced < 2; ++forced)
        for (int dismiss = 0; dismiss < 2; ++dismiss)
        for (int logdecl = 0; logdecl < 2; ++logdecl)
        {
            pc.dialog = d; pc.self = self; pc.mute = mute; pc.dnd = dnd; pc.auto_accept = aa; pc.legacy = legacy;
            pc.rlv = rlv; pc.give_rlv = grlv; pc.forbid = forbid; pc.type = type; pc.button = button; pc.forced = forced;
            pc.dismiss = dismiss; pc.log_decline = logdecl;
            fn(pc);
        }
    }

    static const uint32_t POLICY_SAMPLE_MODULUS = 8009;

    static void corpus_policy(Sink& s)
    {
        for_each_policy_case([&s](const PolicyCase& pc) {
            run_policy_case(pc, s, in_sample(pc.id(), POLICY_SAMPLE_MODULUS));
        });
    }

    // The supplementary table: the offered object already in the model and
    // finished (openers and discards complete at once), default settings.
    static void corpus_policy_present(Sink& s)
    {
        PolicyCase pc{};
        pc.present = true;
        pc.log_decline = true;
        for (S32 d : { 4, 9, 32, 37 })
        for (S32 type = 0; type < 2; ++type)
        for (int aa = 0; aa < 2; ++aa)
        for (int legacy = 0; legacy < 2; ++legacy)
        for (S32 button = 0; button < 8; ++button)
        for (int forced = 0; forced < 2; ++forced)
        {
            pc.dialog = d; pc.type = type; pc.auto_accept = aa; pc.legacy = legacy; pc.button = button; pc.forced = forced;
            run_policy_case(pc, s, false);
        }
    }

    // The response table (F1): what changes between arrival and response.
    // 43,008 cases: dialog with its group flag (4; 9 and 32 and 37 with and
    // without) x from self x sender named normally or "Grid Linden" x mute
    // added between arrival and response (none, by id, by name) x name
    // lookups answering at once or from idle x legacy mode x category or
    // notecard x 8 buttons x forced/interactive x dismiss-to-Trash x
    // LogInventoryDecline.
    template<class Fn>
    static void for_each_response_case(Fn fn)
    {
        PolicyCase pc{};
        pc.response_table = true;
        const S32 dialogs[][2] = { { 4, 0 }, { 9, 0 }, { 9, 1 }, { 32, 0 }, { 32, 1 }, { 37, 0 }, { 37, 1 } };
        for (const auto& dg : dialogs)
        for (int self = 0; self < 2; ++self)
        for (int linden = 0; linden < 2; ++linden)
        for (S32 late = 0; late < 3; ++late)
        for (int defer = 0; defer < 2; ++defer)
        for (int legacy = 0; legacy < 2; ++legacy)
        for (S32 type = 0; type < 2; ++type)
        for (S32 button = 0; button < 8; ++button)
        for (int forced = 0; forced < 2; ++forced)
        for (int dismiss = 0; dismiss < 2; ++dismiss)
        for (int logdecl = 0; logdecl < 2; ++logdecl)
        {
            pc.dialog = dg[0]; pc.from_group = dg[1]; pc.self = self; pc.linden = linden; pc.late_mute = late; pc.defer = defer;
            pc.legacy = legacy; pc.type = type; pc.button = button; pc.forced = forced; pc.dismiss = dismiss; pc.log_decline = logdecl;
            fn(pc);
        }
    }

    static const uint32_t RESPONSE_SAMPLE_MODULUS = 1201;

    static void corpus_policy_response(Sink& s)
    {
        for_each_response_case([&s](const PolicyCase& pc) {
            run_policy_case(pc, s, in_sample(pc.id(), RESPONSE_SAMPLE_MODULUS));
        });
    }

    // Readable named cases for every survey 4 section 9 risk and every
    // behaviour the audit's mutations target (committed in full, full logs).
    static void corpus_policy_named(Sink& s)
    {
        struct Named { const char* id; std::function<void(PolicyCase&)> set; };
        auto base = [](S32 d, S32 button, bool forced) { PolicyCase pc{}; pc.dialog = d; pc.button = button; pc.forced = forced; pc.type = 1; pc.log_decline = true; return pc; };
        std::vector<Named> named = {
            { "agent_offer_accepted_on_arrival_closed", [&](PolicyCase& p) { p = base(4, 7, false); } },
            { "agent_offer_keep_sends_nothing_more", [&](PolicyCase& p) { p = base(4, 0, false); } },
            { "agent_offer_discard_moves_to_trash_locally", [&](PolicyCase& p) { p = base(4, 1, false); } },
            { "agent_offer_mute_button", [&](PolicyCase& p) { p = base(4, 2, false); } },
            { "agent_offer_mute_button_deferred_names", [&](PolicyCase& p) { p = base(4, 2, false); p.response_table = true; p.defer = true; } },
            { "agent_offer_legacy_keep_sends_accept", [&](PolicyCase& p) { p = base(4, 0, false); p.legacy = true; } },
            { "agent_offer_legacy_discard_sends_decline_to_trash", [&](PolicyCase& p) { p = base(4, 1, false); p.legacy = true; } },
            { "agent_offer_muted_by_id_forced_decline", [&](PolicyCase& p) { p = base(4, 0, false); p.mute = 1; } },
            { "agent_offer_muted_by_name_forced_mute", [&](PolicyCase& p) { p = base(4, 0, false); p.mute = 2; } },
            { "agent_offer_textchat_exempt_mute_forced_mute", [&](PolicyCase& p) { p = base(4, 0, false); p.mute = 3; } },
            { "agent_offer_dnd_accepts_and_replies", [&](PolicyCase& p) { p = base(4, 7, false); p.dnd = true; } },
            { "agent_offer_auto_accept", [&](PolicyCase& p) { p = base(4, 0, false); p.auto_accept = true; } },
            { "agent_offer_invalid_type_forced_decline", [&](PolicyCase& p) { p = base(4, 0, false); p.type = 2; } },
            { "agent_offer_forced_show_does_nothing", [&](PolicyCase& p) { p = base(4, 3, true); } },
            { "agent_give_to_rlv_keep", [&](PolicyCase& p) { p = base(4, 0, false); p.rlv = true; p.give_rlv = 1; p.type = 0; } },
            { "agent_give_to_rlv_not_auto_accepted", [&](PolicyCase& p) { p = base(4, 0, false); p.rlv = true; p.give_rlv = 1; p.type = 0; p.auto_accept = true; } },
            { "agent_give_to_rlv_forbidden_auto_accepted", [&](PolicyCase& p) { p = base(4, 0, false); p.rlv = true; p.give_rlv = 1; p.type = 0; p.auto_accept = true; p.forbid = true; } },
            { "agent_give_to_rlv_no_shared_root_auto_accepted", [&](PolicyCase& p) { p = base(4, 0, false); p.rlv = true; p.give_rlv = 2; p.type = 0; p.auto_accept = true; } },
            { "object_offer_keep", [&](PolicyCase& p) { p = base(9, 0, false); } },
            { "object_offer_discard_accepts_into_trash", [&](PolicyCase& p) { p = base(9, 1, false); } },
            { "object_offer_closed_accepts_into_trash", [&](PolicyCase& p) { p = base(9, 7, false); } },
            { "object_offer_mute_button_declines", [&](PolicyCase& p) { p = base(9, 2, false); } },
            { "object_offer_mute_button_deferred_names_declines", [&](PolicyCase& p) { p = base(9, 2, false); p.response_table = true; p.defer = true; } },
            { "object_offer_discard_after_late_mute_by_id_declines", [&](PolicyCase& p) { p = base(9, 1, false); p.response_table = true; p.late_mute = 1; } },
            { "object_offer_discard_after_late_mute_by_name_declines", [&](PolicyCase& p) { p = base(9, 1, false); p.response_table = true; p.late_mute = 2; } },
            { "object_offer_keep_after_late_mute_still_accepts", [&](PolicyCase& p) { p = base(9, 0, false); p.response_table = true; p.late_mute = 1; } },
            { "object_offer_group_owned_mute_button_mutes_group", [&](PolicyCase& p) { p = base(9, 2, false); p.response_table = true; p.from_group = true; } },
            { "object_offer_group_owned_mute_button_deferred", [&](PolicyCase& p) { p = base(9, 2, false); p.response_table = true; p.from_group = true; p.defer = true; } },
            { "object_offer_own_object_keep", [&](PolicyCase& p) { p = base(9, 0, false); p.self = true; } },
            { "object_offer_own_object_muted_by_name", [&](PolicyCase& p) { p = base(9, 0, false); p.self = true; p.mute = 2; } },
            { "object_offer_dnd_no_reply", [&](PolicyCase& p) { p = base(9, 0, false); p.dnd = true; } },
            { "object_offer_muted_no_reply", [&](PolicyCase& p) { p = base(9, 0, false); p.mute = 1; } },
            { "object_offer_auto_accept", [&](PolicyCase& p) { p = base(9, 0, false); p.auto_accept = true; } },
            { "object_offer_invalid_type_forced_decline", [&](PolicyCase& p) { p = base(9, 0, false); p.type = 2; } },
            { "object_give_to_rlv_keep_redirects_to_root", [&](PolicyCase& p) { p = base(9, 0, false); p.rlv = true; p.give_rlv = 1; p.type = 0; } },
            { "object_give_to_rlv_forbidden_keep", [&](PolicyCase& p) { p = base(9, 0, false); p.rlv = true; p.give_rlv = 1; p.type = 0; p.forbid = true; } },
            { "object_give_to_rlv_discard", [&](PolicyCase& p) { p = base(9, 1, false); p.rlv = true; p.give_rlv = 1; p.type = 0; } },
            { "object_offer_linden_discard_after_late_mute", [&](PolicyCase& p) { p = base(9, 1, false); p.response_table = true; p.late_mute = 1; p.linden = true; } },
            { "group_notice_accept", [&](PolicyCase& p) { p = base(32, 0, true); } },
            { "group_notice_discard_no_dismiss", [&](PolicyCase& p) { p = base(32, 1, true); } },
            { "group_notice_discard_dismiss_accepts_into_trash", [&](PolicyCase& p) { p = base(32, 1, true); p.dismiss = true; } },
            { "group_notice_discard_dismiss_after_late_mute_declines", [&](PolicyCase& p) { p = base(32, 1, true); p.dismiss = true; p.response_table = true; p.late_mute = 1; } },
            { "group_notice_mute_dismiss_declines", [&](PolicyCase& p) { p = base(32, 2, true); p.dismiss = true; } },
            { "group_notice_requested_accept_sends_33", [&](PolicyCase& p) { p = base(37, 0, true); } },
            { "group_notice_requested_discard_dismiss", [&](PolicyCase& p) { p = base(37, 1, true); p.dismiss = true; } },
            { "group_notice_from_group_mute", [&](PolicyCase& p) { p = base(32, 2, true); p.response_table = true; p.from_group = true; p.dismiss = true; } },
        };
        for (Named& n : named)
        {
            PolicyCase pc{};
            n.set(pc);
            run_policy_case(pc, s, true, n.id);
        }

        // Offers in states the IM paths do not produce (a persisted
        // notification revived from LLSD): the Linden exemption of the
        // decline path only shows for a group notice whose from_name is a
        // Linden name.
        struct Direct { const char* id; const char* from_name; bool muted; bool dismiss; S32 response; };
        const Direct direct[] = {
            { "direct_group_notice_discard_dismiss_muted_resident", "Grid Resident", true, true, 1 },
            { "direct_group_notice_discard_dismiss_muted_linden", "Grid Linden", true, true, 1 },
            { "direct_group_notice_discard_dismiss_not_muted_linden", "Grid Linden", false, true, 1 },
            { "direct_group_notice_mute_dismiss_linden", "Grid Linden", true, true, 2 },
            { "direct_group_notice_closed_dismiss_muted", "Grid Resident", true, true, 7 },
        };
        for (const Direct& d : direct)
        {
            setup();
            install_snapshot_hook();
            F::gSavedSettings.b["FSDismissGroupNoticeAttachmentsToTrash"] = d.dismiss;
            if (d.muted) F::LLMuteList::getInstance()->mMutes.insert(F::LLMute(OTHER, d.from_name, F::LLMute::AGENT));
            LLSD sd;
            sd["im_type"] = (S32)IM_GROUP_NOTICE;
            sd["from_id"] = OTHER;
            sd["from_group"] = true;
            sd["transaction_id"] = TXN;
            sd["folder_id"] = U(0xf0, 7);
            sd["type"] = "notecard";
            sd["from_name"] = d.from_name;
            sd["description"] = "Notice Item";
            sd["sender"] = OFFER_HOST;
            F::ev().lines.clear();
            put_world(s, d.id);
            F::ev().add("offer from LLSD " + F::sd_notation(sd));
            F::LLOfferInfo* o = D().offer_from_llsd(sd);
            if (d.response == 7)
            {
                // a closed notification: the response template
                LLSD notification;
                notification["name"] = "UserGiveItem";
                notification["form"] = F::notification_form("UserGiveItem");
                notification["id"] = U(0x6e, 999);
                D().respond(o, notification, LLSD::emptyMap());
            }
            else
            {
                D().force_response(o, d.response);
            }
            run_idle();
            if (F::live_responders().count(o)) { F::ev().on = false; delete o; F::ev().on = true; }
            put_events(s, d.id);
        }
    }

    // ===================================================== set 7: predicates
    static void corpus_predicates(Sink& s)
    {
        struct TypeSpec { const char* id; S32 type; bool link; };
        const TypeSpec types[] = { { "object", 6, false }, { "bodypart", 13, false }, { "clothing", 5, false }, { "gesture", 21, false },
                                   { "notecard", 7, false }, { "texture", 0, false }, { "callingcard", 2, false },
                                   { "link_object", 6, true }, { "link_notecard", 7, true } };
        const char* worns[] = { "none", "attached", "wearable", "gesture" };
        const char* owners[] = { "agent", "other", "group" };
        {
            // the records every matrix case shares; each case's `input` line
            // gives the rest (masks resolved, not tokens)
            setup();
            F::gAgent.mGroups.insert(GROUP);
            add_item(U(0x32, 1), ROOT, 6, make_perms(AGENT, PERM_ALL), "target");
            Inputs in{ s, "legend" };
            in.line("item " + U(0x31, 1).asString() + F::fmt(" parent=%s name=\"item\" (\"link\" when link=1, then type=24 and asset=%s, the target) inv_type=default for the type flags=0 finished=1",
                                                           ROOT.asString().c_str(), U(0x32, 1).asString().c_str()));
            in.line("perm creator=" + CREATOR.asString() + " last_owner=owner group=" + GROUP.asString() + " (the agent is in the group)");
            in.line("owner kinds: agent=" + AGENT.asString() + " other=" + OTHER2.asString() + " group=group-owned (owner null, group_owned=1)");
            in.line("link target: " + item_rec(F::gInventory.getItem(U(0x32, 1))) + " (its type is the link's type token)");
            in.line("worn: attached = the worn id is attached; wearable = worn by gAgentWearables; gesture = an active gesture; the worn id is the target for links");
            in.line("case id: <kind>.<worn>.<owner>.<owner mask: C/c copy, M/m modify, T/t transfer>.e<everyone copy>.b<f: base full, nt: base without transfer>.a<avatar valid>");
            in.line("in: t=asset type l=link o=owner kind base/own/grp/evr/nxt = the masks after LLPermissions::initMasks");
            in.line("out: g=isInventoryGiveAcceptable gg=isInventoryGroupGiveAcceptable tc=itemTransferCommonlyAllowed gv=LLGiveable/countNoCopy uc=LLUncopyableItems dt=LLDroppableItem(true)/countNoCopy dn=LLDroppableItem(false)/countNoCopy dc=LLDropCopyableItems");
            F::ev().lines.clear();
        }
        for (const TypeSpec& t : types)
        for (S32 worn = 0; worn < 4; ++worn)
        for (S32 owner = 0; owner < 3; ++owner)
        for (U32 mask = 0; mask < 8; ++mask)
        for (int everyone_copy = 0; everyone_copy < 2; ++everyone_copy)
        for (int base_full = 0; base_full < 2; ++base_full)
        for (int avatar = 0; avatar < 2; ++avatar)
        {
            setup();
            F::gAgent.mGroups.insert(GROUP);
            F::gAgentAvatarp->mValid = avatar;
            const bool copy = mask & 1, mod = mask & 2, transfer = mask & 4;
            const LLUUID owner_id = owner == 0 ? AGENT : owner == 1 ? OTHER2 : LLUUID::null;
            LLPermissions perm = make_perms(owner_id, perm_mask(copy, mod, transfer), everyone_copy ? PERM_COPY : PERM_NONE,
                                            base_full ? PERM_ALL : (PERM_ALL & ~PERM_TRANSFER), GROUP, owner == 2);
            LLUUID item_id = U(0x31, 1);
            LLUUID target = U(0x32, 1);
            if (t.link)
            {
                add_item(target, ROOT, t.type, make_perms(AGENT, PERM_ALL), "target");
                add_item(item_id, ROOT, 24, perm, "link", target);
            }
            else
            {
                add_item(item_id, ROOT, t.type, perm, "item");
            }
            const LLUUID worn_id = t.link ? target : item_id;
            if (worn == 1) F::gAgentAvatarp->mAttachments.insert(worn_id);
            if (worn == 2) F::gAgentWearables.worn.insert(worn_id);
            if (worn == 3) F::LLGestureMgr::instance().active.insert(worn_id);
            F::LLViewerInventoryItem* it = F::gInventory.getItem(item_id);
            std::string c = F::fmt("%s.%s.%s.%s%s%s.e%d.b%s.a%d", t.id, worns[worn], owners[owner],
                                     copy ? "C" : "c", mod ? "M" : "m", transfer ? "T" : "t", everyone_copy, base_full ? "f" : "nt",
                                     avatar);
            {
                // the item's record against the legend (case "legend")
                const LLPermissions& pm = it->LLInventoryItem::getPermissions();
                put(s, c, "in", F::fmt("t=%d l=%d o=%s base=%08x own=%08x grp=%08x evr=%08x nxt=%08x",
                                       (S32)it->getActualType(), (S32)t.link, owners[owner],
                                       pm.getMaskBase(), pm.getMaskOwner(), pm.getMaskGroup(), pm.getMaskEveryone(), pm.getMaskNextOwner()));
            }
            S32 count = -1;
            std::string out;
            out += F::fmt("g=%d", (S32)D().is_give_acceptable(it));
            out += F::fmt(" gg=%d", (S32)D().is_group_give_acceptable(it));
            out += F::fmt(" tc=%d", (S32)D().transfer_commonly_allowed(it));
            bool g = D().collect_functor("giveable", nullptr, it, &count);
            out += F::fmt(" gv=%d/%d", (S32)g, count);
            out += F::fmt(" uc=%d", (S32)D().collect_functor("uncopyable", nullptr, it, nullptr));
            g = D().collect_functor("droppable_transfer", nullptr, it, &count);
            out += F::fmt(" dt=%d/%d", (S32)g, count);
            g = D().collect_functor("droppable_no_transfer", nullptr, it, &count);
            out += F::fmt(" dn=%d/%d", (S32)g, count);
            out += F::fmt(" dc=%d", (S32)D().collect_functor("drop_copyable", nullptr, it, nullptr));
            put(s, c, "out", out);
            F::ev().lines.clear();
        }
        // named permission rules: each changes the outcome against its neighbour
        struct Rule { const char* id; std::function<LLPermissions()> perm; };
        std::vector<Rule> rules = {
            { "rule_owner_full_base_full", []() { return make_perms(AGENT, PERM_ALL, PERM_NONE, PERM_ALL); } },
            { "rule_owner_copy_base_nocopy", []() { return make_perms(AGENT, PERM_ALL, PERM_NONE, PERM_ALL & ~PERM_COPY); } },
            { "rule_owner_transfer_base_notransfer", []() { return make_perms(AGENT, PERM_ALL, PERM_NONE, PERM_ALL & ~PERM_TRANSFER); } },
            { "rule_next_owner_copy", []() { LLPermissions p = make_perms(AGENT, PERM_ALL); p.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL); return p; } },
            { "rule_next_owner_nocopy", []() { LLPermissions p = make_perms(AGENT, PERM_ALL); p.initMasks(PERM_ALL, PERM_ALL, PERM_NONE, PERM_NONE, PERM_ALL & ~PERM_COPY); return p; } },
            { "rule_group_owned_atomic", []() { LLPermissions p = make_perms(AGENT, PERM_ALL, PERM_NONE, PERM_ALL, GROUP); p.setOwnerAndGroup(AGENT, LLUUID::null, GROUP, true); return p; } },
            { "rule_group_owned_not_atomic", []() { LLPermissions p = make_perms(AGENT, PERM_ALL, PERM_NONE, PERM_ALL, GROUP); p.setOwnerAndGroup(AGENT, LLUUID::null, GROUP, false); return p; } },
            { "rule_other_owner_everyone_copy", []() { return make_perms(OTHER2, PERM_ALL, PERM_COPY); } },
            { "rule_other_owner_everyone_none", []() { return make_perms(OTHER2, PERM_ALL, PERM_NONE); } },
        };
        for (const Rule& r : rules)
        {
            setup();
            F::gAgent.mGroups.insert(GROUP);
            F::LLViewerInventoryItem* it = add_item(U(0x31, 1), ROOT, 7, r.perm(), "item");
            put_items(s, r.id, { it });
            S32 count = -1;
            std::string out;
            out += F::fmt("give=%d", (S32)D().is_give_acceptable(it));
            out += F::fmt(" group_give=%d", (S32)D().is_group_give_acceptable(it));
            bool g = D().collect_functor("giveable", nullptr, it, &count);
            out += F::fmt(" giveable=%d/%d", (S32)g, count);
            out += F::fmt(" uncopyable=%d", (S32)D().collect_functor("uncopyable", nullptr, it, nullptr));
            g = D().collect_functor("droppable_transfer", nullptr, it, &count);
            out += F::fmt(" droppable_transfer=%d/%d", (S32)g, count);
            out += F::fmt(" drop_copyable=%d", (S32)D().collect_functor("drop_copyable", nullptr, it, nullptr));
            put(s, r.id, "result", out);
            F::ev().lines.clear();
        }
        // categories: every functor on a category alone
        setup();
        F::LLViewerInventoryCategory* cc = add_cat(U(0x33, 1), ROOT, "cat");
        std::string out;
        for (const char* f : { "giveable", "uncopyable", "droppable_transfer", "droppable_no_transfer", "drop_copyable" })
        {
            S32 count = -1;
            bool r = D().collect_functor(f, cc, nullptr, &count);
            out += F::fmt("%s%s=%d/%d", out.empty() ? "" : " ", f, (S32)r, count);
        }
        put(s, "category_only", "result", out);
        out.clear();
        for (const char* f : { "giveable", "uncopyable", "droppable_transfer", "droppable_no_transfer", "drop_copyable" })
        {
            S32 count = -1;
            bool r = D().collect_functor(f, nullptr, nullptr, &count);
            out += F::fmt("%s%s=%d/%d", out.empty() ? "" : " ", f, (S32)r, count);
        }
        put(s, "null_both", "result", out);
        put(s, "null_item", "result", F::fmt("give=%d group_give=%d transfer_common=%d",
                                               (S32)D().is_give_acceptable(nullptr),
                                               (S32)D().is_group_give_acceptable(nullptr),
                                               (S32)D().transfer_commonly_allowed(nullptr)));
        F::ev().lines.clear();
    }

    // ============================================ set 7: willObjectAcceptInventory
    struct AcceptCase
    {
        bool modify, add, you_owner, volume, attachment;
        bool item_owner_agent, item_transfer, unrestricted;
        S32 type;       // index in kTypes
        bool worn, finished, parent_is_object;
        bool dad_category;
        S32 rlv;        // 0 off, 1 on, 2 on + locked attachment, 3 on + unsit + sitting on it, 4 on + sittp + sitting elsewhere
        bool null_obj = false, null_item = false;
    };
    static const S32 kAcceptTypes[] = { 6, 5, 2, 7, 24 };
    static const char* kAcceptTypeNames[] = { "object", "clothing", "callingcard", "notecard", "link" };
    static const char* kAcceptNames[] = { "ACCEPT_POSTPONED", "ACCEPT_NO", "ACCEPT_NO_CUSTOM", "ACCEPT_NO_LOCKED",
                                          "ACCEPT_YES_COPY_SINGLE", "ACCEPT_YES_SINGLE", "ACCEPT_YES_COPY_MULTI", "ACCEPT_YES_MULTI" };

    static std::string accept_id(const AcceptCase& a)
    {
        return F::fmt("obj_m%d_a%d_o%d_v%d_att%d.item_own%d_t%d_u%d_%s_w%d_f%d_p%d.dad_%s.rlv%d", (S32)a.modify, (S32)a.add,
                        (S32)a.you_owner, (S32)a.volume, (S32)a.attachment, (S32)a.item_owner_agent, (S32)a.item_transfer,
                        (S32)a.unrestricted, kAcceptTypeNames[a.type], (S32)a.worn, (S32)a.finished, (S32)a.parent_is_object,
                        a.dad_category ? "category" : "item", a.rlv);
    }

    static void run_accept(Sink& s, const AcceptCase& a, const std::string& cid)
    {
        setup();
        F::LLViewerObject obj;
        obj.mID = U(0x41, 1);
        obj.mPermModify = a.modify;
        obj.mFlagAllowInventoryAdd = a.add;
        obj.mPermYouOwner = a.you_owner;
        obj.mPCode = a.volume ? F::LL_PCODE_VOLUME : 47; // 47: any pcode other than LL_PCODE_VOLUME
        obj.mIsAttachment = a.attachment;
        F::LLViewerObject root;
        root.mID = U(0x41, 2);
        obj.mRoot = &root;
        F::world().rlv_enabled = a.rlv != 0;
        F::world().rlv_locked_attachment = a.rlv == 2;
        F::world().rlv_unsit = a.rlv == 3;
        F::world().rlv_sittp = a.rlv == 4;
        F::gAgentAvatarp->mSitting = a.rlv >= 3;
        F::gAgentAvatarp->mRoot = a.rlv == 3 ? &root : &obj;
        const S32 type = kAcceptTypes[a.type];
        LLPermissions perm = make_perms(a.item_owner_agent ? AGENT : OTHER2,
                                        perm_mask(true, true, a.item_transfer), PERM_NONE,
                                        a.unrestricted ? PERM_ALL : (PERM_ALL & ~PERM_TRANSFER));
        if (type == 24) add_item(U(0x42, 2), ROOT, 7, make_perms(AGENT, PERM_ALL), "target");
        F::LLViewerInventoryItem* it = add_item(U(0x42, 1), a.parent_is_object ? obj.mID : ROOT, type, perm, "drop", type == 24 ? U(0x42, 2) : LLUUID::null);
        it->mFinished = a.finished;
        if (a.worn)
        {
            if (type == 6) F::gAgentAvatarp->mAttachments.insert(it->getUUID());
            else F::gAgentWearables.worn.insert(it->getUUID());
        }
        {
            const LLPermissions& pm = it->LLInventoryItem::getPermissions();
            put(s, cid, "in", F::fmt("pcode=%d t=%d o=%s base=%08x own=%08x evr=%08x dad=%d",
                                     (S32)obj.mPCode, (S32)it->getActualType(), a.item_owner_agent ? "agent" : "other",
                                     pm.getMaskBase(), pm.getMaskOwner(), pm.getMaskEveryone(),
                                     a.dad_category ? (S32)F::DAD_CATEGORY : (S32)F::DAD_NOTECARD));
        }
        F::EAcceptance r = D().will_object_accept(a.null_obj ? nullptr : &obj, a.null_item ? nullptr : it,
                                                                            a.dad_category ? F::DAD_CATEGORY : F::DAD_NOTECARD);
        put(s, cid, "result", F::fmt("%d %s", (S32)r, kAcceptNames[(S32)r]));
        F::ev().lines.clear();
    }

    template<class Fn>
    static void for_each_accept_case(Fn fn)
    {
        AcceptCase a{};
        for (int m = 0; m < 2; ++m) for (int ad = 0; ad < 2; ++ad) for (int o = 0; o < 2; ++o) for (int v = 0; v < 2; ++v)
        for (int at = 0; at < 2; ++at) for (int io = 0; io < 2; ++io) for (int itr = 0; itr < 2; ++itr) for (int u = 0; u < 2; ++u)
        for (S32 t = 0; t < 5; ++t) for (int w = 0; w < 2; ++w) for (int f = 0; f < 2; ++f) for (int p = 0; p < 2; ++p)
        for (int dc = 0; dc < 2; ++dc) for (S32 rlv = 0; rlv < 5; ++rlv)
        {
            a.modify = m; a.add = ad; a.you_owner = o; a.volume = v; a.attachment = at; a.item_owner_agent = io;
            a.item_transfer = itr; a.unrestricted = u; a.type = t; a.worn = w; a.finished = f; a.parent_is_object = p;
            a.dad_category = dc; a.rlv = rlv;
            fn(a);
        }
    }

    static void will_accept_legend(Sink& s)
    {
        Inputs in{ s, "legend" };
        in.line("object " + U(0x41, 1).asString() + " root " + U(0x41, 2).asString() + " (sitting_on=root: the avatar sits on that root; object: on the object)");
        in.line("item " + U(0x42, 1).asString() + " name=\"drop\" creator=" + CREATOR.asString() + " group=null flags=0; parent " + ROOT.asString() +
                " or the object; link (type 24) to " + U(0x42, 2).asString() + " (a notecard owned by the agent, PERM_ALL)");
        in.line("perm: next=7fffffff group=0; worn=1: attached for objects, worn by gAgentWearables otherwise");
        in.line("owners: agent=" + AGENT.asString() + " other=" + OTHER2.asString() + "; pcode 9 = LL_PCODE_VOLUME, 47 = not a volume");
        in.line("case id: obj_m<perm modify>_a<allow inventory drop>_o<you own it>_v<volume>_att<attachment>.item_own<owned by agent>_t<transfer in owner mask>_u<base unrestricted>_<kind>_w<worn>_f<finished>_p<parent is the object>.dad_<item|category>.rlv<0 off, 1 on, 2 locked attachment, 3 @unsit sitting on the root, 4 @sittp sitting elsewhere>");
        in.line("in: pcode, t=asset type, o=item owner, base/own/evr = the item's masks after initMasks, dad=the EDragAndDropType passed");
    }

    // committed in full: RLV off, an item drop, finished, not from the object itself
    static void corpus_will_accept(Sink& s)
    {
        will_accept_legend(s);
        for_each_accept_case([&s](const AcceptCase& a) {
            if (a.rlv == 0 && !a.dad_category && a.finished && !a.parent_is_object) run_accept(s, a, accept_id(a));
        });
        AcceptCase a{};
        a.modify = a.add = a.you_owner = a.volume = true;
        a.item_owner_agent = a.item_transfer = a.unrestricted = a.finished = true;
        a.type = 3;
        AcceptCase n = a; n.null_obj = true; run_accept(s, n, "null_object");
        n = a; n.null_item = true; run_accept(s, n, "null_item");
    }

    // every combination, digest + sample
    static void corpus_will_accept_sweep(Sink& s)
    {
        will_accept_legend(s);
        for_each_accept_case([&s](const AcceptCase& a) { run_accept(s, a, accept_id(a)); });
    }

    // ============================== set 7: move_inv_category_world_to_agent
    static void corpus_world_to_agent(Sink& s)
    {
        {
            Inputs in{ s, "legend" };
            in.line("object " + U(0x51, 1).asString() + " category " + U(0x54, 1).asString() + "; the task inventory root (" + U(0x51, 1).asString() +
                    ", AT_CATEGORY \"Contents\") comes first and is filtered out");
            in.line("contents position i (1-based): id 00000052-0000-4000-8000-<i as 12 hex digits>, a notecard named \"item <i-1>\" with asset 00000053-...-<i>, creator " +
                    CREATOR.asString() + ", owner (and last owner) the agent if you_owner=1 else " + OTHER2.asString() + ", group null");
            in.line(F::fmt("c: base=%08x owner_mask=%08x everyone=%08x; n: base=%08x owner_mask=%08x everyone=%08x; t: base=%08x owner_mask=%08x everyone=%08x; x: an LLInventoryObject of type AT_OBJECT named \"inv_object\" (not an item)",
                           PERM_ALL, PERM_ALL, PERM_COPY, PERM_ALL, perm_mask(false, true, true), PERM_NONE, PERM_ALL, perm_mask(true, true, false), PERM_COPY));
            in.line("passes: the last 12 hex digits of the ids the filter's check() accepts; filter_types 1 = FILTERTYPE_OBJECT, 8 = FILTERTYPE_DATE");
        }
        // contents kinds: c = copy+transfer (acceptable), n = no copy, t = no transfer to agent, x = a non-item object
        const char* kinds = "cntx";
        std::vector<std::string> seqs = { "" };
        for (int len = 1; len <= 3; ++len)
        {
            std::vector<std::string> next;
            for (const std::string& p : seqs) if ((int)p.size() == len - 1) for (int k = 0; k < 4; ++k) next.push_back(p + kinds[k]);
            seqs.insert(seqs.end(), next.begin(), next.end());
        }
        for (const std::string& seq : seqs)
        for (int owned = 0; owned < 2; ++owned)
        for (S32 filter = 0; filter < 4; ++filter) // 0 none, 1 object-type filter, 2 date filter passing all, 3 date filter passing only 'c'
        for (int drop = 0; drop < 2; ++drop)
        for (int exists = 0; exists < 2; ++exists)
        {
            if (!exists && (filter || drop || owned || !seq.empty())) continue;
            setup();
            F::LLViewerObject* obj = new F::LLViewerObject;
            obj->mID = U(0x51, 1);
            obj->mPermYouOwner = owned;
            obj->mInventory = new LLInventoryObject::object_list_t;
            // the task inventory root, filtered out by getInventoryContents
            obj->mInventory->push_back(new LLInventoryObject(obj->mID, LLUUID::null, LLAssetType::AT_CATEGORY, "Contents"));
            F::LLInventoryFilter flt;
            flt.mTypes = filter == 1 ? F::LLInventoryFilter::FILTERTYPE_OBJECT : filter >= 2 ? F::LLInventoryFilter::FILTERTYPE_DATE : 0;
            const LLUUID task_owner = owned ? AGENT : OTHER2;
            for (size_t i = 0; i < seq.size(); ++i)
            {
                LLUUID id = U(0x52, (U32)i + 1);
                if (seq[i] == 'x')
                {
                    obj->mInventory->push_back(new LLInventoryObject(id, obj->mID, LLAssetType::AT_OBJECT, "inv_object"));
                    continue;
                }
                // c: copy and transfer (everyone may copy, the owner may transfer);
                // n: no copy for anyone; t: copyable but not transferable
                U32 mask = seq[i] == 'n' ? perm_mask(false, true, true) : seq[i] == 't' ? perm_mask(true, true, false) : PERM_ALL;
                U32 everyone = seq[i] == 'n' ? PERM_NONE : PERM_COPY;
                LLPermissions perm = make_perms(task_owner, mask, everyone);
                obj->mInventory->push_back(new LLInventoryItem(id, obj->mID, perm, U(0x53, (U32)i + 1), LLAssetType::AT_NOTECARD,
                                                               LLInventoryType::IT_NOTECARD, F::fmt("item %d", (S32)i), "",
                                                               LLSaleInfo::DEFAULT, 0, 0));
                if (filter == 2 || (filter == 3 && seq[i] == 'c')) flt.mPasses.insert(id);
            }
            if (exists) F::gObjectList.objects[obj->mID] = obj;
            F::ev().lines.clear();
            {
                const std::string cid = F::fmt("seq_%s.owned%d.filter%d.drop%d.exists%d", seq.empty() ? "empty" : seq.c_str(), owned, filter, drop, exists);
                std::string passes;
                for (const LLUUID& id : flt.mPasses) passes += (passes.empty() ? "" : ",") + id.asString().substr(24);
                put(s, cid, "input", F::fmt("object found=%d you_owner=%d drop=%d filter_types=%llu passes=[%s] contents=%s",
                                            exists, owned, drop, (unsigned long long)flt.mTypes, passes.c_str(),
                                            seq.empty() ? "-" : seq.c_str()));
            }
            bool r = D().world_to_agent(obj->mID, U(0x54, 1), drop, filter ? &flt : NULL);
            F::ev().add(F::fmt("result %d", (S32)r));
            put_events(s, F::fmt("seq_%s.owned%d.filter%d.drop%d.exists%d", seq.empty() ? "empty" : seq.c_str(), owned, filter, drop, exists));
            F::gObjectList.objects.clear();
            delete obj;
        }
    }

    // ===================================================== set 7: share dialog
    static void corpus_share(Sink& s)
    {
        struct ShareCase { const char* id; std::vector<std::pair<LLUUID, char>> sel; int avatars; S32 option; };
        // ids chosen so the std::set order is the order listed; kinds: c copyable item, n no-copy item, f folder
        const LLUUID a = U(0x61, 1), b = U(0x61, 2), c = U(0x61, 3), d = U(0x61, 4);
        std::vector<ShareCase> cases = {
            { "one_item", { { a, 'c' } }, 1, 0 },
            { "cancel", { { a, 'c' } }, 1, 1 },
            { "two_items", { { a, 'c' }, { b, 'c' } }, 1, 0 },
            { "folder_first_then_item", { { a, 'f' }, { b, 'c' } }, 1, 0 },
            { "item_then_folder_then_item", { { a, 'c' }, { b, 'f' }, { c, 'c' } }, 1, 0 },
            { "two_folders", { { a, 'f' }, { b, 'f' } }, 1, 0 },
            { "nocopy_items", { { a, 'n' }, { b, 'c' }, { c, 'n' } }, 1, 0 },
            { "nocopy_two_avatars", { { a, 'n' }, { b, 'c' } }, 2, 0 },
            { "copy_two_avatars", { { a, 'c' }, { b, 'c' } }, 2, 0 },
            { "folder_two_avatars", { { a, 'f' }, { d, 'c' } }, 2, 0 },
            { "nocopy_folder", { { a, 'g' } }, 1, 0 },
            { "no_avatars", { { a, 'c' } }, 0, 0 },
            { "empty_selection", {}, 1, 0 },
        };
        for (const ShareCase& sc : cases)
        {
            setup();
            std::set<LLUUID> selected;
            for (const auto& p : sc.sel)
            {
                selected.insert(p.first);
                if (p.second == 'f' || p.second == 'g')
                {
                    add_cat(p.first, ROOT, "Shared Folder");
                    add_item(U(0x62, p.first.mData[15]), p.first, 7, make_perms(AGENT, perm_mask(p.second == 'f', true, true)), "inside");
                }
                else
                {
                    add_item(p.first, ROOT, 7, make_perms(AGENT, perm_mask(p.second == 'c', true, true)), "Shared Item");
                }
            }
            uuid_vec_t avatars;
            for (int i = 0; i < sc.avatars; ++i) avatars.push_back(i == 0 ? OTHER : OTHER2);
            F::ev().lines.clear();
            LLSD notification;
            notification["name"] = "ShareItemsConfirmation";
            LLSD btn_ok; btn_ok["type"] = "button"; btn_ok["name"] = "OK"; btn_ok["index"] = 0;
            LLSD btn_cancel; btn_cancel["type"] = "button"; btn_cancel["name"] = "Cancel"; btn_cancel["index"] = 1;
            notification["form"].append(btn_ok);
            notification["form"].append(btn_cancel);
            LLSD response;
            response[sc.option == 0 ? "OK" : "Cancel"] = true;
            put_world(s, sc.id);
            {
                Inputs in{ s, sc.id };
                in.n = 90;
                std::string av;
                for (const LLUUID& a : avatars) av += " " + a.asString();
                in.line(F::fmt("share option=%d avatars=[", sc.option) + av + " ] selected=" + ids_list(selected));
            }
            F::ev().add(F::fmt("share option=%d selected=%d avatars=%d", sc.option, (S32)selected.size(), sc.avatars));
            D().share(avatars, notification, response, selected);
            run_idle();
            put_events(s, sc.id);
        }
    }

    // ======================================================= random sweeps
    static void corpus_pack_sweep(Sink& s, uint64_t seed)
    {
        Rng rng(seed);
        for (int i = 0; i < 4000; ++i)
        {
            setup();
            S32 len = rng.below(4) == 0 ? (S32)rng.below(1400) : (S32)rng.below(80);
            std::string name;
            for (S32 k = 0; k < len; ++k) name += (char)(rng.below(5) == 0 ? rng.below(256) : 'a' + rng.below(26));
            S32 bsize = rng.below(3) == 0 ? (S32)rng.below(1500) : 17 * (1 + (S32)rng.below(50));
            std::vector<U8> bucket(bsize);
            for (U8& x : bucket) x = (U8)rng.below(256);
            bool null_bucket = rng.below(10) == 0;
            EInstantMessage dialog = (EInstantMessage)rng.below(43);
            bool from_group = rng.coin();
            F::ev().lines.clear();
            F::pack_instant_message(F::gMessageSystem, AGENT, from_group, SESSION, OTHER, "Oracle Tester", name, IM_ONLINE, dialog, TXN, 0,
                                    LLUUID::null, LLVector3(1.f, 2.f, 3.f), NO_TIMESTAMP, null_bucket ? nullptr : bucket.data(), bsize);
            F::gAgent.sendReliableMessage();
            put_events(s, F::fmt("pack_%04d", i));
        }
    }

    static std::string first_send()
    {
        for (const std::string& e : F::ev().lines)
        {
            if (e.compare(0, 5, "send ") == 0) return e;
        }
        return "<no packet>";
    }

    int pack_live_vs_frozen(uint64_t seed, int count, std::string& first)
    {
        Rng rng(seed);
        int diffs = 0;
        for (int i = 0; i < count; ++i)
        {
            setup();
            S32 len = (S32)rng.below(1400);
            std::string name;
            for (S32 k = 0; k < len; ++k) name += (char)rng.below(256);
            S32 bsize = (S32)rng.below(1500);
            std::vector<U8> bucket(bsize);
            for (U8& x : bucket) x = (U8)rng.below(256);
            bool null_bucket = rng.below(8) == 0;
            EInstantMessage dialog = (EInstantMessage)rng.below(43);
            bool from_group = rng.coin();
            U32 pos_z = rng.below(4096);
            U32 pos_y = rng.below(256);
            U32 pos_x = rng.below(256);
            LLVector3 pos((F32)pos_x, (F32)pos_y, (F32)pos_z / 7.f);
            F::ev().lines.clear();
            // live: the real pack_instant_message
            F::pack_instant_message(F::gMessageSystem, AGENT, from_group, SESSION, OTHER, "Oracle Tester", name, IM_ONLINE, dialog, TXN,
                                    0, LLUUID::null, pos, NO_TIMESTAMP, null_bucket ? nullptr : bucket.data(), bsize);
            F::gAgent.sendReliableMessage();
            std::string live = first_send();
            F::ev().lines.clear();
            // frozen: the copy of pack_instant_message_block, through the fake
            F::gMessageSystem->newMessageFast(_PREHASH_ImprovedInstantMessage);
            r4b_pack::pack_instant_message_block(F::gMessageSystem, AGENT, from_group, SESSION, OTHER, "Oracle Tester", name, IM_ONLINE,
                                                 dialog, TXN, 0, LLUUID::null, pos, NO_TIMESTAMP,
                                                 null_bucket ? nullptr : bucket.data(), bsize);
            F::gAgent.sendReliableMessage();
            std::string frozen = first_send();
            F::ev().lines.clear();
            if (live != frozen)
            {
                if (!diffs) first = F::fmt("case %d: live %s frozen %s", i, live.c_str(), frozen.c_str());
                ++diffs;
            }
        }
        return diffs;
    }

    static void corpus_incoming_sweep(Sink& s, uint64_t seed)
    {
        Rng rng(seed);
        const EInstantMessage dialogs[] = { IM_INVENTORY_OFFERED, IM_TASK_INVENTORY_OFFERED, IM_GROUP_NOTICE, IM_GROUP_NOTICE_REQUESTED };
        for (int i = 0; i < 6000; ++i)
        {
            setup();
            install_snapshot_hook();
            Incoming c;
            c.dialog = dialogs[rng.below(4)];
            S32 size = rng.below(4) == 0 ? (S32)rng.below(200) : c.dialog == IM_INVENTORY_OFFERED ? 17 : c.dialog == IM_TASK_INVENTORY_OFFERED ? 1 : 19 + (S32)rng.below(30);
            c.bucket.resize(size);
            for (U8& x : c.bucket) x = (U8)(rng.below(3) == 0 ? rng.below(256) : rng.below(3) == 0 ? '|' : '0' + rng.below(10));
            if (!c.bucket.empty() && rng.below(2) == 0) c.bucket.back() = 0;
            c.session = rng.below(4) == 0 ? LLUUID::null : TXN;
            c.aux = rng.below(3) == 0 ? GROUP : LLUUID::null;
            c.from_group = rng.coin();
            c.from = rng.below(5) == 0 ? AGENT : OTHER;
            c.message = rng.coin() ? "Subject|Body" : "#RLV/~x";
            c.name = rng.coin() ? "Sender Resident" : "";
            c.fill = 0;
            // the legacy task string runs past a bucket without a NUL: skip
            // the cases whose type would stay uninitialised (no digits before
            // the end or a '|'), which the C++ leaves undefined
            if (c.dialog == IM_TASK_INVENTORY_OFFERED && size != 1)
            {
                std::vector<U8> buf(c.bucket);
                buf.push_back(0);
                if (legacy_type_probe(buf.data()).find("UNINITIALISED") != std::string::npos) continue;
            }
            F::ev().lines.clear();
            run_incoming(c, false);
            put_events(s, F::fmt("incoming_%04d", i));
        }
    }

    static void corpus_folder_sweep(Sink& s, uint64_t seed)
    {
        Rng rng(seed);
        for (int i = 0; i < 1500; ++i)
        {
            setup();
            s_node_counter = 0;
            std::function<Node(int)> gen = [&](int depth) {
                U32 cat_name = rng.below(100);
                Node n = cat(F::fmt("f%d", (S32)cat_name));
                n.incomplete = rng.below(25) == 0;
                int kids = (int)rng.below(depth >= 3 ? 4 : 9);
                for (int k = 0; k < kids; ++k)
                {
                    bool sub = depth < 3 && rng.below(4) == 0; // no draw at depth 3
                    if (sub) n.children.push_back(gen(depth + 1));
                    else
                    {
                        static const S32 types[] = { 0, 5, 6, 7, 10, 13, 20, 21, 24 };
                        S32 worn = rng.below(8) == 0 ? 1 + (S32)rng.below(2) : 0;
                        bool transfer = rng.below(6) != 0;
                        bool copy = rng.below(4) != 0;
                        S32 type = types[rng.below(9)];
                        U32 item_name = rng.below(1000);
                        Node it = item(F::fmt("i%d", (S32)item_name), type, copy, transfer, worn);
                        n.children.push_back(it);
                    }
                }
                return n;
            };
            Node root = gen(0);
            build(root, ROOT);
            S32 limit = rng.below(5) == 0 ? 100 : 42;
            F::LLWorld::getInstance()->mMaxInventoryItemsTransfer = limit;
            F::ev().lines.clear();
            put_world(s, F::fmt("folder_%04d", i));
            bool r = D().give_category(OTHER, F::gInventory.getCategory(root.id), LLUUID::null, std::string());
            F::ev().add(F::fmt("result %d", (S32)r));
            if (!F::world().pending.empty())
            {
                S32 option = rng.below(4) == 0 ? 1 : 0; // drawn only when a question is pending
                respond_pending(option);
            }
            run_idle();
            put_events(s, F::fmt("folder_%04d", i));
        }
    }

    static void corpus_predicate_sweep(Sink& s, uint64_t seed)
    {
        Rng rng(seed);
        static const S32 types[] = { 0, 2, 5, 6, 7, 13, 20, 21, 24, 25, 56 };
        for (int i = 0; i < 20000; ++i)
        {
            setup();
            F::gAgent.mGroups.insert(GROUP);
            F::gAgentAvatarp->mValid = rng.below(10) != 0;
            LLPermissions perm;
            const LLUUID owners[] = { AGENT, OTHER2, LLUUID::null };
            S32 o = rng.below(3);
            bool with_group = rng.coin();
            perm.init(CREATOR, owners[o], owners[o], with_group ? GROUP : LLUUID::null);
            U32 next_mask = (U32)rng.next();
            U32 group_mask = (U32)rng.next();
            U32 everyone_mask = (U32)rng.next();
            U32 owner_mask = (U32)rng.next();
            U32 base_mask = (U32)rng.next();
            perm.initMasks(base_mask, owner_mask, everyone_mask, group_mask, next_mask);
            if (o == 2) perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, GROUP, true);
            S32 type = types[rng.below(11)];
            add_item(U(0x71, 2), ROOT, 7, make_perms(AGENT, PERM_ALL), "target");
            F::LLViewerInventoryItem* it = add_item(U(0x71, 1), ROOT, type, perm, "item", type == 24 ? U(0x71, 2) : LLUUID::null);
            S32 worn = rng.below(4);
            const LLUUID wid = type == 24 ? U(0x71, 2) : U(0x71, 1);
            if (worn == 1) F::gAgentAvatarp->mAttachments.insert(wid);
            if (worn == 2) F::gAgentWearables.worn.insert(wid);
            if (worn == 3) F::LLGestureMgr::instance().active.insert(wid);
            S32 count = -1;
            std::string out = F::fmt("perm base=%08x owner=%08x group=%08x everyone=%08x next=%08x owner_kind=%d type=%d worn=%d avatar=%d ->",
                                       perm.getMaskBase(), perm.getMaskOwner(), perm.getMaskGroup(), perm.getMaskEveryone(),
                                       perm.getMaskNextOwner(), o, type, worn, (S32)F::gAgentAvatarp->mValid);
            out += F::fmt(" give=%d", (S32)D().is_give_acceptable(it));
            out += F::fmt(" group_give=%d", (S32)D().is_group_give_acceptable(it));
            bool g = D().collect_functor("giveable", nullptr, it, &count);
            out += F::fmt(" giveable=%d/%d", (S32)g, count);
            out += F::fmt(" uncopyable=%d", (S32)D().collect_functor("uncopyable", nullptr, it, nullptr));
            g = D().collect_functor("droppable_transfer", nullptr, it, &count);
            out += F::fmt(" droppable_transfer=%d/%d", (S32)g, count);
            out += F::fmt(" drop_copyable=%d", (S32)D().collect_functor("drop_copyable", nullptr, it, nullptr));
            put(s, F::fmt("pred_%05d", i), "result", out);
            F::ev().lines.clear();
        }
    }

    // ======================================== F8: offline offers (ReadOfflineMsgs)
    static void corpus_offline(Sink& s)
    {
        struct Off { const char* id; std::function<LLSD()> msg; S32 respond_button; bool forced; };
        auto msg = [](S32 dialog, const std::vector<U8>& bucket, bool has_bucket) {
            LLSD m;
            m["from_agent_id"] = OTHER;
            m["from_group"] = 0;
            m["to_agent_id"] = AGENT;
            m["dialog"] = dialog;
            m["transaction-id"] = TXN;
            m["timestamp"] = 1700000000;
            m["from_agent_name"] = dialog == IM_TASK_INVENTORY_OFFERED ? "Gadget" : "Sender Resident";
            m["message"] = dialog == IM_TASK_INVENTORY_OFFERED ? "'Gadget'  ( http://slurl.com/secondlife/Region/1/2/3 )" : "Gift";
            m["region_id"] = U(0x9e, 1);
            m["position"] = LLVector3(1.f, 2.f, 3.f).getValue();
            if (has_bucket) m["binary_bucket"] = bucket;
            return m;
        };
        std::vector<Off> cases = {
            { "agent_offer_keep", [&]() { return msg(IM_INVENTORY_OFFERED, agent_bucket(7, OFFERED), true); }, 0, false },
            { "agent_offer_discard", [&]() { return msg(IM_INVENTORY_OFFERED, agent_bucket(7, OFFERED), true); }, 1, false },
            { "agent_offer_no_bucket_key", [&]() { return msg(IM_INVENTORY_OFFERED, {}, false); }, 0, false },
            { "agent_offer_transaction_from_asset_id", [&]() { LLSD m = msg(IM_INVENTORY_OFFERED, agent_bucket(7, OFFERED), true);
                m["transaction-id"] = LLUUID::null; m["asset_id"] = U(0xd4, 2); return m; }, 0, false },
            { "task_offer_keep_replies_to_region_host", [&]() { return msg(IM_TASK_INVENTORY_OFFERED, { 7 }, true); }, 0, false },
            { "task_offer_discard_replies_to_region_host", [&]() { return msg(IM_TASK_INVENTORY_OFFERED, { 7 }, true); }, 1, false },
            { "task_offer_no_bucket_key", [&]() { return msg(IM_TASK_INVENTORY_OFFERED, {}, false); }, 0, false },
            { "task_offer_from_group_string_Y", [&]() { LLSD m = msg(IM_TASK_INVENTORY_OFFERED, { 7 }, true); m["from_group"] = "Y"; return m; }, 0, false },
            { "task_offer_local_position", [&]() { LLSD m = msg(IM_TASK_INVENTORY_OFFERED, { 7 }, true); m.erase("position");
                m["local_x"] = 4.5; m["local_y"] = 5.5; m["local_z"] = 6.5; return m; }, 0, false },
            { "group_notice_asset_id_is_group", [&]() { LLSD m = msg(IM_GROUP_NOTICE, str_bucket("7|7|Notice Item"), true);
                m["asset_id"] = GROUP; m["message"] = "Subject|Body"; return m; }, 0, true },
            { "group_notice_binary_bucket", [&]() { LLSD m = msg(IM_GROUP_NOTICE, notice_bucket(1, 7, GROUP, "Notice Item"), true);
                m["message"] = "Subject|Body"; return m; }, 0, true },
        };
        for (const Off& c : cases)
        {
            setup();
            install_snapshot_hook();
            LLSD messages = LLSD::emptyArray();
            messages.append(c.msg());
            F::ev().lines.clear();
            {
                Inputs in{ s, c.id };
                in.line("messages " + F::sd_notation(messages));
                in.line("agent_region_host " + F::gAgent.mRegionHost.getIPandPort());
            }
            D().receive_offline(messages);
            F::ev().add("stage main_coro");
            run_idle();
            F::ev().add(F::fmt("stage response %s button=%d", c.forced ? "forced" : "interactive", c.respond_button));
            if (!F::world().posted.empty())
            {
                F::LLNotificationPtr n = F::LLNotifications::instance().find(F::world().posted.back().id);
                F::LLOfferInfo* info = n ? dynamic_cast<F::LLOfferInfo*>(n->mResponder.get()) : nullptr;
                if (info && c.forced) D().force_response(info, c.respond_button);
                else if (n)
                {
                    LLSD response;
                    LLSD form = n->getForm() ? n->getForm()->asLLSD() : LLSD::emptyArray();
                    for (S32 i = 0; i < form.size(); ++i)
                        if (form[i]["index"].asInteger() == c.respond_button) response[form[i]["name"].asString()] = true;
                    n->respond(response);
                }
            }
            else if (F::world().group_notice_payload.has("inventory_offer"))
            {
                F::LLOfferInfo* o = D().offer_from_llsd(F::world().group_notice_payload["inventory_offer"]);
                D().force_response(o, c.respond_button);
                if (F::live_responders().count(o)) { F::ev().on = false; delete o; F::ev().on = true; }
            }
            run_idle();
            put_events(s, c.id);
        }
    }

    // ========================================= F8: drag-and-drop give acceptance
    static void corpus_drag_give(Sink& s)
    {
        const char* kinds[] = { "dad3dGiveInventoryObject", "dad3dGiveInventory", "dad3dGiveInventoryCategory" };
        const char* accept_names[] = { "ACCEPT_POSTPONED", "ACCEPT_NO", "ACCEPT_NO_CUSTOM", "ACCEPT_NO_LOCKED",
                                       "ACCEPT_YES_COPY_SINGLE", "ACCEPT_YES_SINGLE", "ACCEPT_YES_COPY_MULTI", "ACCEPT_YES_MULTI" };
        // handleGiveDragAndDrop: IM sessions, profiles, the avatar picker
        struct H { const char* id; F::EDragAndDropType type; S32 state; bool share_blocked; bool drop; bool session; bool name_cached; };
        // state: 0 copyable item in inventory, 1 worn object, 2 not in inventory, 3 category, 4 no-transfer item
        std::vector<H> hs;
        const std::pair<const char*, F::EDragAndDropType> types[] = {
            { "texture", F::DAD_TEXTURE }, { "object", F::DAD_OBJECT }, { "notecard", F::DAD_NOTECARD }, { "category", F::DAD_CATEGORY },
            { "callingcard", F::DAD_CALLINGCARD }, { "settings", F::DAD_SETTINGS }, { "material", F::DAD_MATERIAL },
            { "link", F::DAD_LINK }, { "widget", F::DAD_WIDGET }, { "person", F::DAD_PERSON } };
        for (auto& t : types)
            for (S32 state = 0; state < 5; ++state)
                for (int blocked = 0; blocked < 2; ++blocked)
                    hs.push_back({ nullptr, t.second, state, (bool)blocked, false, false, false });
        int k = 0;
        for (H& h : hs)
        {
            setup();
            const LLUUID id = U(0x81, 1);
            LLInventoryObject* cargo = nullptr;
            if (h.state == 3) cargo = add_cat(id, ROOT, "Dragged Folder");
            else
            {
                F::LLViewerInventoryItem* it = new F::LLViewerInventoryItem(id, ROOT, make_perms(AGENT, perm_mask(true, true, h.state != 4)),
                                                                            U(0x82, 1), h.state == 1 ? LLAssetType::AT_OBJECT : LLAssetType::AT_NOTECARD,
                                                                            LLInventoryType::IT_NOTECARD, "Dragged");
                if (h.state != 2) F::gInventory.addItem(it);
                cargo = it;
                if (h.state == 1) F::gAgentAvatarp->mAttachments.insert(id);
            }
            LLPointer<LLInventoryObject> keep = cargo;
            F::world().rlv_enabled = h.share_blocked;
            if (h.share_blocked) F::world().rlv_share_blocked.insert(OTHER);
            std::string cid = F::fmt("handle_%02d_type%d_state%d_blocked%d", k++, (S32)h.type, h.state, (S32)h.share_blocked);
            F::ev().lines.clear();
            put(s, cid, "input", (h.state == 3 ? cat_rec(F::gInventory.getCategory(id)) : item_rec(dynamic_cast<LLInventoryItem*>(cargo))) +
                                 F::fmt(" | in_model=%d attached=%d rlv=%d share_blocked_for=%s dest=%s", (S32)(h.state != 2),
                                        (S32)(h.state == 1), (S32)h.share_blocked, h.share_blocked ? OTHER.asString().c_str() : "-",
                                        OTHER.asString().c_str()));
            F::EAcceptance accept = F::ACCEPT_POSTPONED;
            bool handled = D().give_drag_and_drop(OTHER, LLUUID::null, false, h.type, cargo, &accept, LLSD());
            F::ev().add(F::fmt("result handled=%d accept=%d %s", (S32)handled, (S32)accept, accept_names[accept]));
            put_events(s, cid);
        }
        // drops: the confirmation and its answer
        struct Drop { const char* id; bool session; bool name_cached; S32 answer; bool folder; };
        const Drop drops[] = {
            { "drop_no_session_name_cached_ok", false, true, 0, false },
            { "drop_no_session_name_cached_cancel", false, true, 1, false },
            { "drop_no_session_name_lookup_ok", false, false, 0, false },
            { "drop_in_session_ok", true, true, 0, false },
            { "drop_folder_ok", false, true, 0, true },
        };
        for (const Drop& d : drops)
        {
            setup();
            const LLUUID id = U(0x81, 1);
            LLInventoryObject* cargo = nullptr;
            if (d.folder)
            {
                cargo = add_cat(id, ROOT, "Dragged Folder");
                add_item(U(0x83, 1), id, 7, make_perms(AGENT, PERM_ALL), "inside");
            }
            else cargo = add_item(id, ROOT, 7, make_perms(AGENT, PERM_ALL), "Dragged");
            if (!d.name_cached) F::LLAvatarNameCache::instance().names.erase(OTHER);
            const LLUUID session = OTHER ^ AGENT;
            if (d.session) F::LLIMModel::getInstance()->open_sessions.insert(session);
            F::ev().lines.clear();
            put_world(s, d.id);
            F::EAcceptance accept = F::ACCEPT_POSTPONED;
            bool handled = D().give_drag_and_drop(OTHER, d.session ? session : LLUUID::null, true, d.folder ? F::DAD_CATEGORY : F::DAD_NOTECARD,
                                                  cargo, &accept, LLSD("avatarpicker"));
            F::ev().add(F::fmt("result handled=%d accept=%d %s", (S32)handled, (S32)accept, accept_names[accept]));
            run_idle();
            if (!F::world().pending.empty()) respond_pending(d.answer);
            run_idle();
            put_events(s, d.id);
        }
        // drops on an avatar in world
        for (int kind = 0; kind < 3; ++kind)
        for (S32 source = 0; source < 2; ++source)       // agent, world
        for (S32 state = 0; state < 4; ++state)          // 0 ok, 1 not finished, 2 no transfer, 3 worn
        for (int blocked = 0; blocked < 2; ++blocked)
        for (int has_obj = 0; has_obj < 2; ++has_obj)
        for (int drop = 0; drop < 2; ++drop)
        {
            setup();
            F::LLToolDragAndDrop tool;
            tool.mSource = source ? F::LLToolDragAndDrop::SOURCE_WORLD : F::LLToolDragAndDrop::SOURCE_AGENT;
            F::LLViewerObject avatar_obj;
            avatar_obj.mID = OTHER;
            F::LLViewerInventoryItem* it = add_item(U(0x84, 1), ROOT, kind == 0 ? 6 : 7, make_perms(AGENT, perm_mask(true, true, state != 2)), "Dropped");
            it->mFinished = state != 1;
            if (state == 3) F::gAgentAvatarp->mAttachments.insert(it->getUUID());
            F::LLViewerInventoryCategory* cat = add_cat(U(0x85, 1), ROOT, "Dropped Folder");
            add_item(U(0x86, 1), cat->getUUID(), 7, make_perms(AGENT, PERM_ALL), "inside");
            tool.mItem = kind == 2 ? nullptr : it;
            tool.mCat = kind == 2 ? cat : nullptr;
            F::world().rlv_enabled = blocked;
            if (blocked) F::world().rlv_share_blocked.insert(OTHER);
            std::string cid = F::fmt("%s.source_%s.state%d.blocked%d.obj%d.drop%d", kinds[kind], source ? "world" : "agent", state, blocked, has_obj, drop);
            F::ev().lines.clear();
            put(s, cid, "input", (kind == 2 ? cat_rec(cat) : item_rec(it)) +
                                 F::fmt(" | source=%d attached=%d rlv=%d share_blocked_for=%s avatar_object=%s", (S32)tool.mSource,
                                        (S32)(state == 3), (S32)blocked, blocked ? OTHER.asString().c_str() : "-",
                                        has_obj ? OTHER.asString().c_str() : "null"));
            F::EAcceptance r = D().dad3d_give(kind, tool, has_obj ? &avatar_obj : nullptr, drop);
            F::ev().add(F::fmt("result accept=%d %s", (S32)r, accept_names[r]));
            run_idle();
            if (!F::world().pending.empty()) respond_pending(0);
            put_events(s, cid);
        }
    }

    // ============================================================ registry
    // ================================================================ bench
    // Per-operation cost of the frozen C++ and the Rust decisions, in the
    // same process over the same fakes, the event log off (the fakes still
    // format what they would log, in both columns). Each row: best of three
    // runs of `reps` operations, ns per operation.
    struct BenchOp
    {
        const char* name;
        std::function<void()> prepare;          // once per run, untimed
        std::function<void(int)> op;            // the timed operation
    };

    static std::vector<BenchOp> bench_ops()
    {
        static const LLUUID FOLDER = U(0x2f, 1);
        auto world = []()
        {
            setup();
            F::ev().on = false;
            add_item(OFFERED, ROOT, LLAssetType::AT_NOTECARD, make_perms(AGENT, PERM_ALL), "Offered");
            add_item(U(0x11, 1), ROOT, LLAssetType::AT_NOTECARD, make_perms(AGENT, PERM_ALL), "Gift");
            add_item(U(0x11, 2), ROOT, LLAssetType::AT_OBJECT, make_perms(AGENT, perm_mask(false, true, true)), "No Copy");
            add_cat(FOLDER, ROOT, "Kit");
            for (int i = 0; i < 20; ++i)
            {
                add_item(U(0x12, i), FOLDER, i % 2 ? LLAssetType::AT_NOTECARD : LLAssetType::AT_TEXTURE, make_perms(AGENT, PERM_ALL),
                         F::fmt("Part %d", i));
            }
            F::LLViewerObject* obj = new F::LLViewerObject;
            obj->mID = U(0x0b, 1);
            obj->mPermModify = true;
            obj->mFlagAllowInventoryAdd = true;
            F::gObjectList.objects[obj->mID] = obj;
        };
        auto clear_notifications = []()
        {
            F::LLNotifications::instance().mLive.clear();
            F::world().posted.clear();
            F::world().pending.clear();
            F::LLAppViewer::instance()->mIdle.clear();
            F::gInventory.mObservers.clear();
        };
        std::vector<U8> agent_bucket(17);
        agent_bucket[0] = (U8)LLAssetType::AT_NOTECARD;
        memcpy(&agent_bucket[1], OFFERED.mData, 16);
        static std::vector<U8> buffer;
        buffer.assign(MTUBYTES + 64, 0);
        memcpy(buffer.data(), agent_bucket.data(), agent_bucket.size());
        static LLHost sender;
        sender = LLHost(std::string(OFFER_HOST));

        std::vector<BenchOp> ops;
        ops.push_back({ "offer arrival, agent (UserGiveItem posted)", world, [=](int i)
            {
                D().receive_im(OTHER, false, IM_INVENTORY_OFFERED, TXN, 0, "Sender Resident", "Gift", buffer.data(), 17, sender,
                               LLUUID::null);
                if ((i & 63) == 63) clear_notifications();
            } });
        ops.push_back({ "offer arrival, object (ObjectGiveItem posted)", world, [=](int i)
            {
                static U8 task_bucket[MTUBYTES] = { (U8)LLAssetType::AT_NOTECARD };
                D().receive_im(OTHER, false, IM_TASK_INVENTORY_OFFERED, TXN, 0, "Gadget", "'Gadget'  ( http://slurl.com/secondlife/R/1/2/3 )",
                               task_bucket, 1, sender, LLUUID::null);
                if ((i & 63) == 63) clear_notifications();
            } });
        ops.push_back({ "forceResponse(IOR_ACCEPT), agent offer", world, [=](int)
            {
                F::LLOfferInfo* o = make_offer(IM_INVENTORY_OFFERED, TXN);
                o->mPersist = true;
                D().force_response(o, 0);
                delete o;
            } });
        ops.push_back({ "forceResponse(IOR_DECLINE), object offer", world, [=](int)
            {
                F::LLOfferInfo* o = make_offer(IM_TASK_INVENTORY_OFFERED, TXN);
                o->mPersist = true;
                D().force_response(o, 1);
                delete o;
            } });
        ops.push_back({ "sendReceiveResponse (accept packet)", world, [=](int)
            {
                F::LLOfferInfo* o = make_offer(IM_INVENTORY_OFFERED, TXN);
                D().send_receive_response(o, true, U(0xde, 1));
                delete o;
            } });
        ops.push_back({ "[C++] LLOfferInfo asLLSD + LLOfferInfo(LLSD)", world, [=](int)
            {
                F::LLOfferInfo* o = make_offer(IM_INVENTORY_OFFERED, TXN);
                LLSD sd = D().offer_as_llsd(o);
                F::LLOfferInfo* back = D().offer_from_llsd(sd);
                delete back;
                delete o;
            } });
        ops.push_back({ "check_offer_throttle(check_only)", world, [=](int)
            {
                D().check_offer_throttle("other.sender", true);
            } });
        ops.push_back({ "doGiveInventoryItem (copyable)", world, [=](int)
            {
                D().give_item(OTHER, F::gInventory.getItem(U(0x11, 1)), LLUUID::null);
            } });
        ops.push_back({ "doGiveInventoryCategory (20 items)", world, [=](int)
            {
                D().give_category(OTHER, F::gInventory.getCategory(FOLDER), LLUUID::null, "");
            } });
        ops.push_back({ "isInventoryGiveAcceptable", world, [=](int)
            {
                D().is_give_acceptable(F::gInventory.getItem(U(0x11, 1)));
            } });
        ops.push_back({ "itemTransferCommonlyAllowed (object)", world, [=](int)
            {
                D().transfer_commonly_allowed(F::gInventory.getItem(U(0x11, 2)));
            } });
        ops.push_back({ "LLDroppableItem(true) on one item", world, [=](int)
            {
                S32 count = -1;
                D().collect_functor("droppable_transfer", nullptr, F::gInventory.getItem(U(0x11, 1)), &count);
            } });
        ops.push_back({ "willObjectAcceptInventory", world, [=](int)
            {
                D().will_object_accept(F::gObjectList.findObject(U(0x0b, 1)), F::gInventory.getItem(U(0x11, 1)), F::DAD_NOTECARD);
            } });
        return ops;
    }

    std::string run_bench(int reps)
    {
        std::string out = F::fmt("R4b bench: ns per operation, best of 3 runs of %d (fakes, event log off)\n", reps);
        out += F::fmt("%-48s %12s %12s %8s\n", "operation", "C++ (frozen)", "Rust", "ratio");
        r4b_boundary::Decisions* impls[2] = { &r4b_boundary::frozen_decisions(), &r4b_boundary::rust_decisions() };
        for (const BenchOp& b : bench_ops())
        {
            double best[2] = { 1e300, 1e300 };
            for (int run = 0; run < 3; ++run)
            {
                for (int k = 0; k < 2; ++k)
                {
                    r4b_boundary::bench_select(impls[k]);
                    b.prepare();
                    auto t0 = std::chrono::steady_clock::now();
                    for (int i = 0; i < reps; ++i)
                    {
                        b.op(i);
                    }
                    double ns = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - t0).count() / reps;
                    best[k] = std::min(best[k], ns);
                }
            }
            out += F::fmt("%-48s %12.0f %12.0f %8.2f\n", b.name, best[0], best[1], best[1] / best[0]);
        }
        r4b_boundary::bench_select(nullptr);
        setup();
        return out;
    }

    const std::vector<Corpus>& corpora()
    {
        static std::vector<Corpus> list = {
            { "give", "give_item", "set 4: ImprovedInstantMessage of item gives, every asset type, links, names, no-copy answers", false, 0, corpus_give_item },
            { "give", "give_folder", "set 4 and 7: folder gives: buckets, counts, notifications, deletions on Yes", false, 0, corpus_give_folder },
            { "offer", "responses", "set 4: accept and decline packets (sendReceiveResponse, send_decline_response) for dialogs 4, 9, 32, 37", false, 0, corpus_responses },
            { "offer", "incoming", "set 5: received offers (process_offer_im): parsed LLOfferInfo, folder lookup, arrival actions", false, 0, corpus_incoming },
            { "offer", "offerinfo_llsd", "set 5: LLOfferInfo asLLSD / LLOfferInfo(LLSD) round trips and crafted documents", false, 0, corpus_offerinfo_llsd },
            { "offer", "forced_response_index", "set 6: forceResponse element indexing (Show=0 Keep=1 Discard=2 Mute=3)", false, 0, corpus_forced_index },
            { "offer", "throttle", "set 6: check_offer_throttle traces", false, 0, corpus_throttle },
            { "offer", "policy_present", "set 6 supplement: offered object already in the model, default settings", true, 0, corpus_policy_present, 8 },
            { "offer", "policy", "set 6: the offer policy cross product (projection; see README)", true, 0, corpus_policy, POLICY_SAMPLE_MODULUS },
            { "offer", "policy_response", "set 6 (F1): what changes between arrival and response (projection; see README)", true, 0, corpus_policy_response, RESPONSE_SAMPLE_MODULUS },
            { "offer", "policy_named", "set 6: a readable named case for every section 9 risk and audited behaviour", false, 0, corpus_policy_named },
            { "offer", "offline", "set 5/6 (F8): offers through the ReadOfflineMsgs capability; replies go to the agent's region host", false, 0, corpus_offline },
            { "give", "drag_give", "set 7 (F8): handleGiveDragAndDrop and dad3dGiveInventory* acceptance (RLVa @share -> ACCEPT_NO_LOCKED)", false, 0, corpus_drag_give },
            { "give", "predicates", "set 7: give and drop predicate matrix", false, 0, corpus_predicates },
            { "give", "will_accept", "set 7: willObjectAcceptInventory (RLV off, item drop, finished, foreign parent)", false, 0, corpus_will_accept },
            { "give", "will_accept_sweep", "set 7: willObjectAcceptInventory, every combination", true, 0, corpus_will_accept_sweep, 401 },
            { "give", "world_to_agent", "set 7: move_inv_category_world_to_agent accept test and move list", false, 0, corpus_world_to_agent },
            { "give", "share", "set 7: the share dialog callback (give_inventory_cb)", false, 0, corpus_share },
            { "give", "pack_sweep", "set 4: pack_instant_message over random names and buckets", true, 0x52344250414b0001ull,
              [](Sink& s) { corpus_pack_sweep(s, 0x52344250414b0001ull); }, 100 },
            { "offer", "incoming_sweep", "set 5: random received offers", true, 0x5234424f46460002ull,
              [](Sink& s) { corpus_incoming_sweep(s, 0x5234424f46460002ull); }, 60 },
            { "give", "folder_sweep", "set 7: random folder trees given and answered", true, 0x523442464f4c0003ull,
              [](Sink& s) { corpus_folder_sweep(s, 0x523442464f4c0003ull); }, 60 },
            { "give", "predicate_sweep", "set 7: random permission masks through the predicates", true, 0x5234425052450004ull,
              [](Sink& s) { corpus_predicate_sweep(s, 0x5234425052450004ull); }, 100 },
        };
        return list;
    }
}
