/**
 * @file llinventoryoffer_vks.cpp
 * @brief The viewer build of llinventoryoffer_vks.inl: the host and executor
 * of the Rust inventory offer and give decisions (rung seven, sub-rung R4b),
 * and the wrappers declared in llinventoryoffer_vks.h. The R4b oracle
 * compiles the same .inl against its fakes.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llinventoryoffer_vks.h"

#include "llagent.h"
#include "llagentwearables.h"
#include "llappviewer.h"
#include "llavatarname.h"
#include "llavatarnamecache.h"
#include "llcachename.h"
#include "llfirstuse.h"
#include "llfloaterreg.h"
#include "llfloatertools.h"
#include "llframetimer.h"
#include "llgesturemgr.h"
#include "llhudeffecttrail.h"
#include "llhudmanager.h"
#include "llimprocessing.h"
#include "llimview.h"
#include "llinstantmessage.h"
#include "llinventorybridge.h"
#include "llinventoryfilter.h"
#include "llinventorymodel.h"
#include "llinventoryobserver.h"
#include "llinventorypanel.h"
#include "llmutelist.h"
#include "llnotifications.h"
#include "llnotificationtemplate.h"
#include "llnotificationsutil.h"
#include "llpanelgroup.h"
#include "llrecentpeople.h"
#include "llsdserialize.h"
#include "llslurl.h"
#include "llstartup.h"
#include "lltrans.h"
#include "lluistring.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermessage.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llvoavatarself.h"
#include "llworld.h"
#include "message.h"
#include "fscommon.h"
#include "rlvactions.h"
#include "rlvcommon.h"
#include "rlvhandler.h"
#include "rlvinventory.h"
#include "rlvlocks.h"
#include "rlvui.h"

#include "vkstorm_ffi.h"

#include <cstring>
#include <iterator>
#include <sstream>

// The offer callbacks the executor binds (llviewermessage.cpp,
// llimprocessing.cpp, llmutelist.cpp).
void inventory_offer_mute_callback(const LLUUID& blocked_id, const std::string& full_name, bool is_group);
void inventory_offer_mute_avatar_callback(const LLUUID& blocked_id, const LLAvatarName& av_name);
void inventory_offer_name_callback(const LLAvatarName& av_name, const std::string& from_name, const std::string& message);
void notification_group_name_cb(const std::string& group_name, const std::string& sender, const std::string& subject,
                                const std::string& message, const LLSD& payload, U32 timestamp);
void notify_automute_callback(const LLUUID& agent_id, const LLAvatarName& av_name, LLMuteList::EAutoReason reason);
// The classes those files keep local: LLDiscardAgentOffer and
// LLOpenTaskGroupOffer (llviewermessage.cpp), LLPostponedOfferNotification
// (llimprocessing.cpp).
LLInventoryObserver* vks_r4b_new_discard_agent_offer(const LLUUID& folder, const LLUUID& object);
LLInventoryObserver* vks_r4b_new_open_task_group_offer();
void vks_r4b_post_offer_notification(const LLNotification::Params& p, const LLUUID& from, bool is_group);

// ------------------------------------------------------------- the hooks

static F64 vks_r4b_frame_time()
{
    return LLFrameTimer::getElapsedSeconds();
}

static F64 vks_r4b_now()
{
    return LLDate::now().secondsSinceEpoch();
}

static LLUUID vks_r4b_generate()
{
    LLUUID id;
    id.generate();
    return id;
}

static bool vks_r4b_notification_form(const std::string& name, LLSD& form)
{
    LLNotificationTemplatePtr templatep = LLNotifications::instance().getTemplate(name);
    if (!templatep || !templatep->mForm)
    {
        return false;
    }
    form = templatep->mForm->asLLSD();
    return true;
}

static void vks_r4b_children(const LLUUID& parent, std::vector<LLViewerInventoryCategory*>& cats,
                             std::vector<LLViewerInventoryItem*>& items)
{
    LLInventoryModel::cat_array_t* child_cats = nullptr;
    LLInventoryModel::item_array_t* child_items = nullptr;
    gInventory.getDirectDescendentsOf(parent, child_cats, child_items);
    if (child_cats)
    {
        for (const LLPointer<LLViewerInventoryCategory>& cat : *child_cats)
        {
            cats.push_back(cat.get());
        }
    }
    if (child_items)
    {
        for (const LLPointer<LLViewerInventoryItem>& item : *child_items)
        {
            items.push_back(item.get());
        }
    }
}

// gAgentAvatarp->getRoot() (an LLXform; the C++ compared it with the drop
// target's root object)
static LLViewerObject* vks_r4b_avatar_root()
{
    return gAgentAvatarp ? dynamic_cast<LLViewerObject*>(gAgentAvatarp->getRoot()) : nullptr;
}

// The offer throttle (gThrottleTimer and check_offer_throttle's statics
// before R4b): an expiry on the frame clock and the counters.
static VksInvR4bThrottle s_offer_throttle = { 0.0, 0, false };

static VksInvR4bThrottle* vks_r4b_throttle()
{
    return &s_offer_throttle;
}

static void vks_r4b_throttle_sync(bool)
{
}

#include "llinventoryoffer_vks.inl"
