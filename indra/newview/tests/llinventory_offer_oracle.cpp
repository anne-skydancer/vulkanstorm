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
#include "llinventory_offer_oracle_copies.h"

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
        p.functor(notification, response);
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
            { "name_63", std::string(63, 'n'), false },
            { "name_64_corrected", std::string(64, 'n'), false },
            { "name_raw_1182", std::string(1182, 'x'), true },
            { "name_raw_1183", std::string(1183, 'x'), true },
            { "name_raw_1184", std::string(1184, 'x'), true },
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
            F::ev().add("give item " + id.asString() + " to " + OTHER.asString() + " session " + c.session.asString());
            bool r = F::LLGiveInventory::doGiveInventoryItem(OTHER, F::gInventory.getItem(id), c.session);
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
            F::ev().add(F::fmt("give folder %s to %s limit %d notification \"%s\"", root.id.asString().c_str(),
                                 OTHER.asString().c_str(), c.limit, c.notification.c_str()));
            bool r = F::LLGiveInventory::doGiveInventoryCategory(OTHER, rc, LLUUID::null, c.notification);
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
                    { "accept_dest", [dest](F::LLOfferInfo* o) { o->sendReceiveResponse(true, dest); } },
                    { "accept_null_dest", [](F::LLOfferInfo* o) { o->sendReceiveResponse(true, LLUUID::null); } },
                    { "decline", [dest](F::LLOfferInfo* o) { o->sendReceiveResponse(false, dest); } },
                    { "accept_then_decline", [dest](F::LLOfferInfo* o) {
                        o->sendReceiveResponse(true, dest); o->sendReceiveResponse(false, dest); } },
                    { "auto_receive", [](F::LLOfferInfo* o) { o->send_auto_receive_response(); } },
                    { "legacy_decline", [](F::LLOfferInfo* o) { o->send_decline_response(); } },
                    { "legacy_decline_twice", [](F::LLOfferInfo* o) { o->send_decline_response(); o->send_decline_response(); } },
                    { "accept_avatar_invalid", [dest](F::LLOfferInfo* o) {
                        F::gAgentAvatarp->mValid = false; o->sendReceiveResponse(true, dest); } },
                    { "accept_rlv_names_hidden_nearby", [dest](F::LLOfferInfo* o) {
                        F::world().rlv_enabled = true; F::world().rlv_can_show_names = false; F::world().rlv_nearby.insert(OTHER);
                        o->sendReceiveResponse(true, dest); } },
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
        F::process_offer_im(c.from, c.from_group, c.dialog, c.session, 0, c.name, c.message, buffer.data(), size, sender, c.aux);
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
            LLSD sd = o.asLLSD();
            put_llsd(s, c, "asLLSD", sd);
            F::LLOfferInfo back(sd);
            put(s, c, "fromLLSD", offer_fields(back));
            F::LLOfferInfo viaFrom;
            viaFrom.fromLLSD(sd);
            put(s, c, "fromLLSD_assign", offer_fields(viaFrom));
            LLSD again = back.asLLSD();
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
            F::LLOfferInfo o(sd);
            put(s, c, "fromLLSD", offer_fields(o));
            put_llsd(s, c, "asLLSD", o.asLLSD());
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
            o->forceResponse((F::InventoryOfferResponse)r);
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
                bool r = F::check_offer_throttle(st.from, st.check_only);
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
        std::string id() const
        {
            static const char* mutes[] = { "none", "id", "name", "id_textchat_exempt" };
            static const char* grlv[] = { "no", "root", "noroot" };
            static const char* types[] = { "cat", "note", "bad" };
            static const char* buttons[] = { "accept", "decline", "mute", "show", "accept_silent", "decline_silent", "show_silent", "closed" };
            return F::fmt("d%d.self%d.mute_%s.dnd%d.aa%d.legacy%d.rlv%d.grlv_%s.forbid%d.type_%s.%s.%s.dismiss%d.logdecl%d%s",
                            dialog, (S32)self, mutes[mute], (S32)dnd, (S32)auto_accept, (S32)legacy, (S32)rlv, grlv[give_rlv],
                            (S32)forbid, types[type], buttons[button], forced ? "forced" : "interactive", (S32)dismiss,
                            (S32)log_decline, present ? ".present" : "");
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
            "toasts.kill ", "blocked_list.show " };
        for (const char* k : keep)
        {
            if (e.compare(0, strlen(k), k) == 0) return true;
        }
        return false;
    }

    // notify lines lose their args (UI text): name and payload stay
    static std::string policy_render(const std::string& e)
    {
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

    static void run_policy_case(const PolicyCase& pc, Sink& s, bool full_events)
    {
        setup();
        install_snapshot_hook();
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
        const LLUUID from = pc.self ? AGENT : OTHER;
        const std::string name = task ? "Gadget" : "Sender";
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
                    info->forceResponse((F::InventoryOfferResponse)pc.button);
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
                info->forceResponse((F::InventoryOfferResponse)pc.button);
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
        const std::string cid = pc.id();
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

    static const uint32_t POLICY_SAMPLE_MODULUS = 4001;

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
            run_policy_case(pc, s, in_sample(pc.id(), 8));
        }
    }

    // ===================================================== set 7: predicates
    static void corpus_predicates(Sink& s)
    {
        struct TypeSpec { const char* id; S32 type; bool link; };
        const TypeSpec types[] = { { "object", 6, false }, { "bodypart", 13, false }, { "clothing", 5, false }, { "gesture", 21, false },
                                   { "notecard", 7, false }, { "texture", 0, false }, { "callingcard", 2, false },
                                   { "link_object", 6, true }, { "link_notecard", 7, true } };
        const char* worns[] = { "none", "attached", "wearable", "gesture_active" };
        const char* owners[] = { "agent", "other", "group" };
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
            std::string c = F::fmt("%s.worn_%s.owner_%s.mask_%s%s%s.everyone_copy%d.base_%s.avatar%d", t.id, worns[worn], owners[owner],
                                     copy ? "C" : "c", mod ? "M" : "m", transfer ? "T" : "t", everyone_copy, base_full ? "full" : "notransfer",
                                     avatar);
            S32 count = -1;
            std::string out;
            out += F::fmt("give=%d", (S32)F::LLGiveInventory::isInventoryGiveAcceptable(it));
            out += F::fmt(" group_give=%d", (S32)F::LLGiveInventory::isInventoryGroupGiveAcceptable(it));
            out += F::fmt(" transfer_common=%d", (S32)F::LLInventoryCollectFunctor::itemTransferCommonlyAllowed(it));
            bool g = F::eval_functor("giveable", nullptr, it, &count);
            out += F::fmt(" giveable=%d/%d", (S32)g, count);
            out += F::fmt(" uncopyable=%d", (S32)F::eval_functor("uncopyable", nullptr, it, nullptr));
            g = F::eval_functor("droppable_transfer", nullptr, it, &count);
            out += F::fmt(" droppable_transfer=%d/%d", (S32)g, count);
            g = F::eval_functor("droppable_no_transfer", nullptr, it, &count);
            out += F::fmt(" droppable_no_transfer=%d/%d", (S32)g, count);
            out += F::fmt(" drop_copyable=%d", (S32)F::eval_functor("drop_copyable", nullptr, it, nullptr));
            put(s, c, "result", out);
            F::ev().lines.clear();
        }
        // categories: every functor on a category alone
        setup();
        F::LLViewerInventoryCategory* cc = add_cat(U(0x33, 1), ROOT, "cat");
        std::string out;
        for (const char* f : { "giveable", "uncopyable", "droppable_transfer", "droppable_no_transfer", "drop_copyable" })
        {
            S32 count = -1;
            bool r = F::eval_functor(f, cc, nullptr, &count);
            out += F::fmt("%s%s=%d/%d", out.empty() ? "" : " ", f, (S32)r, count);
        }
        put(s, "category_only", "result", out);
        out.clear();
        for (const char* f : { "giveable", "uncopyable", "droppable_transfer", "droppable_no_transfer", "drop_copyable" })
        {
            S32 count = -1;
            bool r = F::eval_functor(f, nullptr, nullptr, &count);
            out += F::fmt("%s%s=%d/%d", out.empty() ? "" : " ", f, (S32)r, count);
        }
        put(s, "null_both", "result", out);
        put(s, "null_item", "result", F::fmt("give=%d group_give=%d transfer_common=%d",
                                               (S32)F::LLGiveInventory::isInventoryGiveAcceptable(nullptr),
                                               (S32)F::LLGiveInventory::isInventoryGroupGiveAcceptable(nullptr),
                                               (S32)F::LLInventoryCollectFunctor::itemTransferCommonlyAllowed(nullptr)));
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
        F::EAcceptance r = F::LLToolDragAndDrop::willObjectAcceptInventory(a.null_obj ? nullptr : &obj, a.null_item ? nullptr : it,
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

    // committed in full: RLV off, an item drop, finished, not from the object itself
    static void corpus_will_accept(Sink& s)
    {
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
        for_each_accept_case([&s](const AcceptCase& a) { run_accept(s, a, accept_id(a)); });
    }

    // ============================== set 7: move_inv_category_world_to_agent
    static void corpus_world_to_agent(Sink& s)
    {
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
            bool r = F::move_inv_category_world_to_agent(obj->mID, U(0x54, 1), drop, NULL, NULL, filter ? &flt : NULL);
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
            F::share_set_avatars(avatars);
            F::ev().lines.clear();
            LLSD notification;
            notification["name"] = "ShareItemsConfirmation";
            LLSD btn_ok; btn_ok["type"] = "button"; btn_ok["name"] = "OK"; btn_ok["index"] = 0;
            LLSD btn_cancel; btn_cancel["type"] = "button"; btn_cancel["name"] = "Cancel"; btn_cancel["index"] = 1;
            notification["form"].append(btn_ok);
            notification["form"].append(btn_cancel);
            LLSD response;
            response[sc.option == 0 ? "OK" : "Cancel"] = true;
            F::ev().add(F::fmt("share option=%d selected=%d avatars=%d", sc.option, (S32)selected.size(), sc.avatars));
            F::share_give_inventory_cb(notification, response, selected);
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
            F::ev().lines.clear();
            F::pack_instant_message(F::gMessageSystem, AGENT, rng.coin(), SESSION, OTHER, "Oracle Tester", name, IM_ONLINE, dialog, TXN, 0,
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
            LLVector3 pos((F32)rng.below(256), (F32)rng.below(256), (F32)rng.below(4096) / 7.f);
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
                Node n = cat(F::fmt("f%d", (S32)rng.below(100)));
                n.incomplete = rng.below(25) == 0;
                int kids = (int)rng.below(depth >= 3 ? 4 : 9);
                for (int k = 0; k < kids; ++k)
                {
                    if (depth < 3 && rng.below(4) == 0) n.children.push_back(gen(depth + 1));
                    else
                    {
                        static const S32 types[] = { 0, 5, 6, 7, 10, 13, 20, 21, 24 };
                        Node it = item(F::fmt("i%d", (S32)rng.below(1000)), types[rng.below(9)], rng.below(4) != 0, rng.below(6) != 0,
                                       rng.below(8) == 0 ? 1 + (S32)rng.below(2) : 0);
                        n.children.push_back(it);
                    }
                }
                return n;
            };
            Node root = gen(0);
            build(root, ROOT);
            F::LLWorld::getInstance()->mMaxInventoryItemsTransfer = rng.below(5) == 0 ? 100 : 42;
            F::ev().lines.clear();
            bool r = F::LLGiveInventory::doGiveInventoryCategory(OTHER, F::gInventory.getCategory(root.id), LLUUID::null, std::string());
            F::ev().add(F::fmt("result %d", (S32)r));
            if (!F::world().pending.empty()) respond_pending(rng.below(4) == 0 ? 1 : 0);
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
            perm.init(CREATOR, owners[o], owners[o], rng.coin() ? GROUP : LLUUID::null);
            perm.initMasks((U32)rng.next(), (U32)rng.next(), (U32)rng.next(), (U32)rng.next(), (U32)rng.next());
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
            out += F::fmt(" give=%d", (S32)F::LLGiveInventory::isInventoryGiveAcceptable(it));
            out += F::fmt(" group_give=%d", (S32)F::LLGiveInventory::isInventoryGroupGiveAcceptable(it));
            bool g = F::eval_functor("giveable", nullptr, it, &count);
            out += F::fmt(" giveable=%d/%d", (S32)g, count);
            out += F::fmt(" uncopyable=%d", (S32)F::eval_functor("uncopyable", nullptr, it, nullptr));
            g = F::eval_functor("droppable_transfer", nullptr, it, &count);
            out += F::fmt(" droppable_transfer=%d/%d", (S32)g, count);
            out += F::fmt(" drop_copyable=%d", (S32)F::eval_functor("drop_copyable", nullptr, it, nullptr));
            put(s, F::fmt("pred_%05d", i), "result", out);
            F::ev().lines.clear();
        }
    }

    // ============================================================ registry
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
            { "offer", "policy_present", "set 6 supplement: offered object already in the model, default settings", false, 0, corpus_policy_present },
            { "offer", "policy", "set 6: the offer policy cross product (projection; see README)", true, 0, corpus_policy, POLICY_SAMPLE_MODULUS },
            { "give", "predicates", "set 7: give and drop predicate matrix", false, 0, corpus_predicates },
            { "give", "will_accept", "set 7: willObjectAcceptInventory (RLV off, item drop, finished, foreign parent)", false, 0, corpus_will_accept },
            { "give", "will_accept_sweep", "set 7: willObjectAcceptInventory, every combination", true, 0, corpus_will_accept_sweep, 101 },
            { "give", "world_to_agent", "set 7: move_inv_category_world_to_agent accept test and move list", false, 0, corpus_world_to_agent },
            { "give", "share", "set 7: the share dialog callback (give_inventory_cb)", false, 0, corpus_share },
            { "give", "pack_sweep", "set 4: pack_instant_message over random names and buckets", true, 0x52344250414b0001ull,
              [](Sink& s) { corpus_pack_sweep(s, 0x52344250414b0001ull); }, 50 },
            { "offer", "incoming_sweep", "set 5: random received offers", true, 0x5234424f46460002ull,
              [](Sink& s) { corpus_incoming_sweep(s, 0x5234424f46460002ull); }, 30 },
            { "give", "folder_sweep", "set 7: random folder trees given and answered", true, 0x523442464f4c0003ull,
              [](Sink& s) { corpus_folder_sweep(s, 0x523442464f4c0003ull); }, 15 },
            { "give", "predicate_sweep", "set 7: random permission masks through the predicates", true, 0x5234425052450004ull,
              [](Sink& s) { corpus_predicate_sweep(s, 0x5234425052450004ull); }, 100 },
        };
        return list;
    }
}
