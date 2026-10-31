/**
 * @file llinventory_offer_oracle_frozen.cpp
 * @brief Recorder R4b (inventory rung): FrozenDecisions, the implementation of
 * the W6/W7 seam (llinventory_offer_oracle_boundary.h) by the verbatim copies
 * of the viewer's offer and give decision code.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_offer_oracle_boundary.h"

namespace r4b_boundary
{
    namespace F = r4b;

    class FrozenDecisions : public Decisions
    {
    public:
        const char* name() const override { return "frozen"; }

        void receive_im(const LLUUID& from_id, bool from_group, EInstantMessage dialog, const LLUUID& session_id, U32 timestamp,
                        const std::string& name, const std::string& message, U8* bucket, S32 bucket_size, LLHost& sender,
                        const LLUUID& aux_id) override
        {
            F::process_offer_im(from_id, from_group, dialog, session_id, timestamp, name, message, bucket, bucket_size, sender, aux_id);
        }
        void receive_offline(const LLSD& messages) override { F::process_offline_messages(messages); }
        void respond(F::LLOfferInfo* offer, const LLSD& notification, const LLSD& response) override
        {
            offer->handleRespond(notification, response);
        }
        void force_response(F::LLOfferInfo* offer, S32 response) override
        {
            offer->forceResponse((F::InventoryOfferResponse)response);
        }
        void send_receive_response(F::LLOfferInfo* offer, bool accept, const LLUUID& folder) override
        {
            offer->sendReceiveResponse(accept, folder);
        }
        void send_auto_receive_response(F::LLOfferInfo* offer) override { offer->send_auto_receive_response(); }
        void send_decline_response(F::LLOfferInfo* offer) override { offer->send_decline_response(); }
        LLSD offer_as_llsd(F::LLOfferInfo* offer) override { return offer->asLLSD(); }
        F::LLOfferInfo* offer_from_llsd(const LLSD& sd) override { return new F::LLOfferInfo(sd); }
        void offer_from_llsd_assign(F::LLOfferInfo* offer, const LLSD& sd) override { offer->fromLLSD(sd); }
        bool check_offer_throttle(const std::string& from_name, bool check_only) override
        {
            return F::check_offer_throttle(from_name, check_only);
        }

        bool give_item(const LLUUID& to, const LLInventoryItem* item, const LLUUID& im_session) override
        {
            return F::LLGiveInventory::doGiveInventoryItem(to, item, im_session);
        }
        bool give_category(const LLUUID& to, const LLInventoryCategory* cat, const LLUUID& im_session,
                           const std::string& success_notification) override
        {
            return F::LLGiveInventory::doGiveInventoryCategory(to, cat, im_session, success_notification);
        }
        void answer(const F::ResponseFunctor& handler, const LLSD& notification, const LLSD& response) override
        {
            handler(notification, response);
        }
        void share(const uuid_vec_t& avatars, const LLSD& notification, const LLSD& response, const std::set<LLUUID>& selected) override
        {
            F::share_set_avatars(avatars);
            F::share_give_inventory_cb(notification, response, selected);
        }
        bool is_give_acceptable(const LLInventoryItem* item) override { return F::LLGiveInventory::isInventoryGiveAcceptable(item); }
        bool is_group_give_acceptable(const LLInventoryItem* item) override
        {
            return F::LLGiveInventory::isInventoryGroupGiveAcceptable(item);
        }
        bool transfer_commonly_allowed(const LLInventoryItem* item) override
        {
            return F::LLInventoryCollectFunctor::itemTransferCommonlyAllowed(item);
        }
        bool collect_functor(const std::string& functor, LLInventoryCategory* cat, LLInventoryItem* item, S32* count) override
        {
            return F::eval_functor(functor, cat, item, count);
        }
        F::EAcceptance will_object_accept(F::LLViewerObject* obj, LLInventoryItem* item, F::EDragAndDropType type) override
        {
            return F::LLToolDragAndDrop::willObjectAcceptInventory(obj, item, type);
        }
        bool world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop, F::LLInventoryFilter* filter) override
        {
            return F::move_inv_category_world_to_agent(object_id, category_id, drop, NULL, NULL, filter);
        }
        bool give_drag_and_drop(const LLUUID& dest_agent, const LLUUID& session, bool drop, F::EDragAndDropType type, void* cargo,
                                F::EAcceptance* accept, const LLSD& dest) override
        {
            return F::LLToolDragAndDrop::handleGiveDragAndDrop(dest_agent, session, drop, type, cargo, accept, dest);
        }
        F::EAcceptance dad3d_give(int kind, F::LLToolDragAndDrop& tool, F::LLViewerObject* obj, bool drop) override
        {
            if (kind == 0) return tool.dad3dGiveInventoryObject(obj, 0, 0, drop);
            if (kind == 1) return tool.dad3dGiveInventory(obj, 0, 0, drop);
            return tool.dad3dGiveInventoryCategory(obj, 0, 0, drop);
        }
    };

    // decisions() (llinventory_offer_oracle_rust.cpp) picks this one or
    // RustDecisions.
    Decisions& frozen_decisions()
    {
        static FrozenDecisions frozen;
        return frozen;
    }
}
