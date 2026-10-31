/**
 * @file llinventory_offer_oracle_rust.cpp
 * @brief Recorder R4b (inventory rung): RustDecisions, the implementation of
 * the W6/W7 seam (llinventory_offer_oracle_boundary.h) by the Rust offer and
 * give decisions (vkstorm-ffi vks_inv_r4b_*) and the viewer's executor,
 * llinventoryoffer_vks.inl, compiled here inside namespace r4b against the
 * same fakes the frozen copies use. The fixtures replay unchanged with either
 * implementation; VKS_R4B_IMPL=frozen selects the frozen copies (the default
 * for a dump), VKS_R4B_IMPL=rust the Rust decisions (the default for a
 * replay).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_offer_oracle_boundary.h"
#include "llsdserialize.h"
#include "llmath.h"
#include "vkstorm_ffi.h"

#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
#include <sstream>
#include <utility>
#include <vector>

namespace r4b
{
    // The offer callbacks the executor binds (frozen copies and fakes).
    void inventory_offer_mute_callback(const LLUUID& blocked_id, const std::string& full_name, bool is_group);
    void inventory_offer_mute_avatar_callback(const LLUUID& blocked_id, const LLAvatarName& av_name);
    void inventory_offer_name_callback(const LLAvatarName& av_name, const std::string& from_name, const std::string& message);
    // Harness factories for the classes the frozen copies define locally
    // (llinventory_offer_oracle_copies.cpp).
    LLInventoryObserver* harness_new_discard_agent_offer(const LLUUID& folder_id, const LLUUID& object_id);
    void harness_post_offer_notification(const LLNotification::Params& p, const LLUUID& from_id, bool is_group);

    // ------------------------------------------------- the executor's hooks
    static F64 vks_r4b_frame_time() { return fake_clock(); }
    static F64 vks_r4b_now() { return r4b_now().secondsSinceEpoch(); }
    static LLUUID vks_r4b_generate()
    {
        LLUUID id;
        r4b_generate(id);
        return id;
    }
    static bool vks_r4b_notification_form(const std::string& name, LLSD& form)
    {
        form = notification_form(name);
        return form.isArray();
    }
    static void vks_r4b_children(const LLUUID& parent, std::vector<LLViewerInventoryCategory*>& cats,
                                 std::vector<LLViewerInventoryItem*>& items)
    {
        auto c = gInventory.mParentChildCategoryTree.find(parent);
        if (c != gInventory.mParentChildCategoryTree.end() && c->second)
        {
            for (const LLPointer<LLViewerInventoryCategory>& cat : *c->second)
            {
                cats.push_back(cat.get());
            }
        }
        auto i = gInventory.mParentChildItemTree.find(parent);
        if (i != gInventory.mParentChildItemTree.end() && i->second)
        {
            for (const LLPointer<LLViewerInventoryItem>& item : *i->second)
            {
                items.push_back(item.get());
            }
        }
    }
    static LLViewerObject* vks_r4b_avatar_root() { return gAgentAvatarp ? gAgentAvatarp->getRoot() : nullptr; }
    // The throttle: gThrottleTimer's expiry (which the harness resets) and
    // the counters, which carry across cases as the frozen statics do.
    static VksInvR4bThrottle s_throttle = { 0.0, 0, false };
    static VksInvR4bThrottle* vks_r4b_throttle() { return &s_throttle; }
    static void vks_r4b_throttle_sync(bool before)
    {
        if (before)
        {
            s_throttle.expiry = gThrottleTimer.mExpiry;
        }
        else
        {
            gThrottleTimer.mExpiry = s_throttle.expiry;
        }
    }
    static LLInventoryObserver* vks_r4b_new_discard_agent_offer(const LLUUID& folder, const LLUUID& object)
    {
        return harness_new_discard_agent_offer(folder, object);
    }
    static LLInventoryObserver* vks_r4b_new_open_task_group_offer() { return new LLOpenTaskGroupOffer; }
    static void vks_r4b_post_offer_notification(const LLNotification::Params& p, const LLUUID& from, bool is_group)
    {
        harness_post_offer_notification(p, from, is_group);
    }

#include "../llinventoryoffer_vks.inl"
}

namespace r4b_boundary
{
    namespace F = r4b;

    class RustDecisions : public Decisions
    {
    public:
        const char* name() const override { return "rust"; }

        void receive_im(const LLUUID& from_id, bool from_group, EInstantMessage dialog, const LLUUID& session_id, U32 timestamp,
                        const std::string& name, const std::string& message, U8* bucket, S32 bucket_size, LLHost& sender,
                        const LLUUID& aux_id) override
        {
            F::vks_r4b_receive_im(from_id, from_group, dialog, session_id, timestamp, name, message, bucket, bucket_size, sender,
                                  aux_id);
        }
        void receive_offline(const LLSD& messages) override { F::vks_r4b_receive_offline(messages); }
        void respond(F::LLOfferInfo* offer, const LLSD& notification, const LLSD& response) override
        {
            F::vks_r4b_respond(offer, notification, response);
        }
        void force_response(F::LLOfferInfo* offer, S32 response) override { F::vks_r4b_force_response(offer, response); }
        void send_receive_response(F::LLOfferInfo* offer, bool accept, const LLUUID& folder) override
        {
            F::vks_r4b_send_receive_response(offer, accept, folder);
        }
        void send_auto_receive_response(F::LLOfferInfo* offer) override { F::vks_r4b_send_auto_receive_response(offer); }
        void send_decline_response(F::LLOfferInfo* offer) override { F::vks_r4b_send_decline_response(offer); }
        LLSD offer_as_llsd(F::LLOfferInfo* offer) override { return F::vks_r4b_offer_as_llsd(*offer); }
        F::LLOfferInfo* offer_from_llsd(const LLSD& sd) override
        {
            F::LLOfferInfo* offer = new F::LLOfferInfo();
            F::vks_r4b_offer_from_llsd(*offer, sd);
            return offer;
        }
        void offer_from_llsd_assign(F::LLOfferInfo* offer, const LLSD& sd) override { F::vks_r4b_offer_from_llsd(*offer, sd); }
        bool check_offer_throttle(const std::string& from_name, bool check_only) override
        {
            return F::vks_r4b_check_offer_throttle(from_name, check_only);
        }

        bool give_item(const LLUUID& to, const LLInventoryItem* item, const LLUUID& im_session) override
        {
            return F::vks_r4b_give_item(to, item, im_session);
        }
        bool give_category(const LLUUID& to, const LLInventoryCategory* cat, const LLUUID& im_session,
                           const std::string& success_notification) override
        {
            return F::vks_r4b_give_category(to, cat, im_session, success_notification);
        }
        void answer(const F::ResponseFunctor& handler, const LLSD& notification, const LLSD& response) override
        {
            handler(notification, response);
        }
        void share(const uuid_vec_t& avatars, const LLSD& notification, const LLSD& response, const std::set<LLUUID>& selected) override
        {
            F::vks_r4b_share(avatars, notification, response, selected);
        }
        bool is_give_acceptable(const LLInventoryItem* item) override { return F::vks_r4b_item_predicate(0, item); }
        bool is_group_give_acceptable(const LLInventoryItem* item) override { return F::vks_r4b_item_predicate(1, item); }
        bool transfer_commonly_allowed(const LLInventoryItem* item) override { return F::vks_r4b_item_predicate(2, item); }
        bool collect_functor(const std::string& functor, LLInventoryCategory* cat, LLInventoryItem* item, S32* count) override
        {
            S32 f;
            if (functor == "giveable") f = VKS_INV_R4B_F_GIVEABLE;
            else if (functor == "uncopyable") f = VKS_INV_R4B_F_UNCOPYABLE;
            else if (functor == "droppable_transfer") f = VKS_INV_R4B_F_DROPPABLE_TRANSFER;
            else if (functor == "droppable_no_transfer") f = VKS_INV_R4B_F_DROPPABLE_NO_TRANSFER;
            else if (functor == "drop_copyable") f = VKS_INV_R4B_F_DROP_COPYABLE;
            else
            {
                llassert_always(false);
                return false;
            }
            S32 n = -1;
            bool r = F::vks_r4b_collect_functor(f, cat, item, &n);
            // the frozen harness writes the count of the counting functors only
            if (count && n >= 0)
            {
                *count = n;
            }
            return r;
        }
        F::EAcceptance will_object_accept(F::LLViewerObject* obj, LLInventoryItem* item, F::EDragAndDropType type) override
        {
            return F::vks_r4b_will_object_accept(obj, item, type);
        }
        bool world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop, F::LLInventoryFilter* filter) override
        {
            return F::vks_r4b_world_to_agent(object_id, category_id, drop, NULL, NULL, filter);
        }
        bool give_drag_and_drop(const LLUUID& dest_agent, const LLUUID& session, bool drop, F::EDragAndDropType type, void* cargo,
                                F::EAcceptance* accept, const LLSD& dest) override
        {
            return F::vks_r4b_give_drag_and_drop(dest_agent, session, drop, type, cargo, accept, dest);
        }
        F::EAcceptance dad3d_give(int kind, F::LLToolDragAndDrop& tool, F::LLViewerObject* obj, bool drop) override
        {
            F::LLViewerInventoryItem* item = nullptr;
            F::LLViewerInventoryCategory* cat = nullptr;
            tool.locateInventory(item, cat);
            return F::vks_r4b_dad3d_give(kind, (S32)tool.mSource, item, cat, obj, drop);
        }
    };

    Decisions& rust_decisions()
    {
        static RustDecisions rust;
        return rust;
    }

    static Decisions* s_bench = nullptr;

    void bench_select(Decisions* d)
    {
        s_bench = d;
    }

    // VKS_R4B_IMPL=frozen|rust; without it a dump records from the frozen
    // copies and a replay runs the Rust decisions.
    Decisions& decisions()
    {
        if (s_bench)
        {
            return *s_bench;
        }
        static Decisions* chosen = nullptr;
        if (!chosen)
        {
            const char* impl = getenv("VKS_R4B_IMPL");
            if (impl && *impl)
            {
                if (!strcmp(impl, "frozen"))
                {
                    chosen = &frozen_decisions();
                }
                else if (!strcmp(impl, "rust"))
                {
                    chosen = &rust_decisions();
                }
                else
                {
                    fprintf(stderr, "VKS_R4B_IMPL=%s: expected frozen or rust\n", impl);
                    abort();
                }
            }
            else
            {
                const char* dump = getenv("VKS_R4B_DUMP");
                chosen = (dump && *dump) ? &frozen_decisions() : &rust_decisions();
            }
        }
        return *chosen;
    }
}
