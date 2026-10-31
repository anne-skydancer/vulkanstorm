/**
 * @file llinventory_offer_oracle_boundary.h
 * @brief Recorder R4b (inventory rung): the W6/W7 seam of the offer and give
 * oracle.
 *
 * Every corpus reaches the offer and give decision code through Decisions,
 * and only through it. Inputs come in as the arguments below and the state of
 * the fakes (llinventory_offer_oracle_fakes.h: model, mute list, settings,
 * RLVa verdicts, agent, avatar, objects, notifications); the output is the
 * action list, i.e. the ordered events the fakes log while the call runs
 * (r4b::ev(): messages sent with their bytes, notifications posted or
 * answered, observers, deletions, mutes, RLVa observers and notices, offer
 * state snapshots) plus the returned values. The fixtures record exactly that.
 *
 * Two implementations: FrozenDecisions (llinventory_offer_oracle_frozen.cpp),
 * the verbatim copies of the C++ decision code the fixtures were recorded
 * from, and RustDecisions (llinventory_offer_oracle_rust.cpp), the Rust
 * decisions (vkstorm-ffi vks_inv_r4b_*, W6 offers and W7 giving) with the
 * viewer's executor (llinventoryoffer_vks.inl) compiled against the same
 * fakes. The fixtures replay unchanged with either; VKS_R4B_IMPL=frozen|rust
 * selects one (default: rust for a replay, frozen for a dump).
 *
 * LLOfferInfo (r4b::LLOfferInfo, the frozen class) is the offer state that
 * crosses the seam: it is created by receive_im/offer_from_llsd and handed
 * back to respond/force_response/send_* as the viewer's notifications do.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORY_OFFER_ORACLE_BOUNDARY_H
#define LL_LLINVENTORY_OFFER_ORACLE_BOUNDARY_H

#include "llinventory_offer_oracle_copies.h"

#include <set>
#include <string>

namespace r4b_boundary
{
    class Decisions
    {
    public:
        virtual ~Decisions() {}
        virtual const char* name() const = 0;

        // ------------------------------------------------- offers (W6)
        // LLIMProcessing::processNewMessage for IM_INVENTORY_OFFERED,
        // IM_TASK_INVENTORY_OFFERED, IM_GROUP_NOTICE and
        // IM_GROUP_NOTICE_REQUESTED, after process_improved_im copied the
        // bucket into U8[MTUBYTES] (`bucket` points at that buffer;
        // `bucket_size` is the size the message declared).
        virtual void receive_im(const LLUUID& from_id, bool from_group, EInstantMessage dialog, const LLUUID& session_id,
                                U32 timestamp, const std::string& name, const std::string& message, U8* bucket,
                                S32 bucket_size, LLHost& sender, const LLUUID& aux_id) = 0;
        // The ReadOfflineMsgs capability's message loop over its validated
        // `messages` array (each dispatched to processNewMessage from the
        // main coroutine, i.e. from idle here).
        virtual void receive_offline(const LLSD& messages) = 0;
        // An offer notification answered by the user (LLOfferInfo::handleRespond
        // with the notification's LLSD and the response map).
        virtual void respond(r4b::LLOfferInfo* offer, const LLSD& notification, const LLSD& response) = 0;
        // LLOfferInfo::forceResponse(InventoryOfferResponse) (group notice
        // toasts, the group panel, and the arrival paths).
        virtual void force_response(r4b::LLOfferInfo* offer, S32 response) = 0;
        virtual void send_receive_response(r4b::LLOfferInfo* offer, bool accept, const LLUUID& folder) = 0;
        virtual void send_auto_receive_response(r4b::LLOfferInfo* offer) = 0;
        virtual void send_decline_response(r4b::LLOfferInfo* offer) = 0;
        virtual LLSD offer_as_llsd(r4b::LLOfferInfo* offer) = 0;
        virtual r4b::LLOfferInfo* offer_from_llsd(const LLSD& sd) = 0;           // new LLOfferInfo(sd)
        virtual void offer_from_llsd_assign(r4b::LLOfferInfo* offer, const LLSD& sd) = 0; // fromLLSD
        virtual bool check_offer_throttle(const std::string& from_name, bool check_only) = 0;

        // ------------------------------------------------- giving (W7)
        virtual bool give_item(const LLUUID& to, const LLInventoryItem* item, const LLUUID& im_session) = 0;
        virtual bool give_category(const LLUUID& to, const LLInventoryCategory* cat, const LLUUID& im_session,
                                   const std::string& success_notification) = 0;
        // The answer to a notification the give code posted with a response
        // handler (CannotCopyWarning, CannotCopyCountItems,
        // ShareItemsConfirmation): `handler` is what that code registered.
        virtual void answer(const r4b::ResponseFunctor& handler, const LLSD& notification, const LLSD& response) = 0;
        // The share dialog's callback (llavataractions.cpp) for `avatars`.
        virtual void share(const uuid_vec_t& avatars, const LLSD& notification, const LLSD& response,
                           const std::set<LLUUID>& selected) = 0;
        virtual bool is_give_acceptable(const LLInventoryItem* item) = 0;
        virtual bool is_group_give_acceptable(const LLInventoryItem* item) = 0;
        virtual bool transfer_commonly_allowed(const LLInventoryItem* item) = 0;
        // giveable, uncopyable, droppable_transfer, droppable_no_transfer,
        // drop_copyable: the functor's verdict and its countNoCopy (-1 if none)
        virtual bool collect_functor(const std::string& functor, LLInventoryCategory* cat, LLInventoryItem* item, S32* count) = 0;
        virtual r4b::EAcceptance will_object_accept(r4b::LLViewerObject* obj, LLInventoryItem* item, r4b::EDragAndDropType type) = 0;
        virtual bool world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop, r4b::LLInventoryFilter* filter) = 0;
        // LLToolDragAndDrop::handleGiveDragAndDrop (drops on IM sessions, profiles, the avatar picker)
        virtual bool give_drag_and_drop(const LLUUID& dest_agent, const LLUUID& session, bool drop, r4b::EDragAndDropType type,
                                        void* cargo, r4b::EAcceptance* accept, const LLSD& dest) = 0;
        // LLToolDragAndDrop::dad3dGiveInventory{Object,,Category} (kind 0, 1, 2): drops on an avatar in world
        virtual r4b::EAcceptance dad3d_give(int kind, r4b::LLToolDragAndDrop& tool, r4b::LLViewerObject* obj, bool drop) = 0;
    };

    // the implementation the corpora use (VKS_R4B_IMPL)
    Decisions& decisions();
    Decisions& frozen_decisions();
    Decisions& rust_decisions();
    // the bench (VKS_R4B_BENCH) switches the implementation; null restores it
    void bench_select(Decisions* d);
}

#endif
