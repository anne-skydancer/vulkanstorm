/**
 * @file llinventoryoffer_vks.h
 * @brief The viewer's calls into the Rust inventory offer and give decisions
 * (vkstorm-ffi vks_inv_r4b_*, rung seven sub-rung R4b). Defined by
 * llinventoryoffer_vks.inl, compiled in llinventoryoffer_vks.cpp.
 *
 * Received offers (processNewMessage's offer and group notice cases, the
 * ReadOfflineMsgs loop), LLOfferInfo's responses, callbacks and LLSD form,
 * the offer throttle, LLGiveInventory, the give and drop functors and
 * predicates, willObjectAcceptInventory, move_inv_category_world_to_agent,
 * handleGiveDragAndDrop, the dad3dGiveInventory* handlers and the share
 * dialog's callback are decided in Rust; each wrapper below stands for the
 * C++ it replaced, and its effects are the same, in the same order.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#ifndef LL_LLINVENTORYOFFER_VKS_H
#define LL_LLINVENTORYOFFER_VKS_H

#include "llinstantmessage.h"
#include "llui.h"
#include "lluuid.h"

#include <functional>
#include <set>
#include <string>

class LLHost;
class LLInventoryCategory;
class LLInventoryFilter;
class LLInventoryItem;
class LLOfferInfo;
class LLSD;
class LLViewerInventoryCategory;
class LLViewerInventoryItem;
class LLViewerObject;
struct LLMoveInv;

// Offers. vks_r4b_receive_im: processNewMessage's case for
// IM_INVENTORY_OFFERED, IM_TASK_INVENTORY_OFFERED, IM_GROUP_NOTICE and
// IM_GROUP_NOTICE_REQUESTED, from processNewMessage's raw arguments; `bucket`
// is process_improved_im's U8[MTUBYTES] or the offline loop's padded vector.
// Returns false where the C++ case returned from processNewMessage instead
// of breaking (no window flash).
bool vks_r4b_receive_im(const LLUUID& from_id, bool from_group, EInstantMessage dialog, const LLUUID& session_id,
                        U32 timestamp, const std::string& name, const std::string& message, const U8* bucket,
                        S32 bucket_size, const LLHost& sender, const LLUUID& aux_id);
void vks_r4b_receive_offline(const LLSD& messages);
void vks_r4b_respond(LLOfferInfo* offer, const LLSD& notification, const LLSD& response);
void vks_r4b_force_response(LLOfferInfo* offer, S32 response);
void vks_r4b_send_receive_response(LLOfferInfo* offer, bool accept, const LLUUID& folder);
void vks_r4b_send_auto_receive_response(LLOfferInfo* offer);
void vks_r4b_send_decline_response(LLOfferInfo* offer);
LLSD vks_r4b_offer_as_llsd(const LLOfferInfo& offer);
void vks_r4b_offer_from_llsd(LLOfferInfo& offer, const LLSD& params);
bool vks_r4b_check_offer_throttle(const std::string& from_name, bool check_only);

// Giving.
bool vks_r4b_give_item(const LLUUID& to, const LLInventoryItem* item, const LLUUID& im_session);
bool vks_r4b_give_category(const LLUUID& to, const LLInventoryCategory* cat, const LLUUID& im_session,
                           const std::string& success_notification);
// The answer to a notification the give code posted (VKS_INV_R4B_H_*):
// the handler's result.
bool vks_r4b_handler_answer(S32 handler, const LLSD& notification, const LLSD& response);
void vks_r4b_share(const uuid_vec_t& avatars, const LLSD& notification, const LLSD& response,
                   const std::set<LLUUID>& selected);
// isInventoryGiveAcceptable (0), isInventoryGroupGiveAcceptable (1),
// itemTransferCommonlyAllowed (2).
bool vks_r4b_item_predicate(int which, const LLInventoryItem* item);
// A give or drop collect functor (VKS_INV_R4B_F_*) on one category or item;
// *count: countNoCopy() of the counting functors, -1 for the others.
bool vks_r4b_collect_functor(S32 functor, const LLInventoryCategory* cat, const LLInventoryItem* item, S32* count);
EAcceptance vks_r4b_will_object_accept(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type);
bool vks_r4b_world_to_agent(const LLUUID& object_id, const LLUUID& category_id, bool drop,
                            std::function<void(S32, void*, const LLMoveInv*)> callback, void* user_data,
                            LLInventoryFilter* filter);
bool vks_r4b_give_drag_and_drop(const LLUUID& dest_agent, const LLUUID& session_id, bool drop,
                                EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept, const LLSD& dest);
// dad3dGiveInventoryObject (0), dad3dGiveInventory (1),
// dad3dGiveInventoryCategory (2); source: LLToolDragAndDrop::ESource.
EAcceptance vks_r4b_dad3d_give(int kind, S32 source, LLViewerInventoryItem* item, LLViewerInventoryCategory* cat,
                               LLViewerObject* obj, bool drop);

#endif
