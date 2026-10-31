/**
 * @file llgiveinventory.cpp
 * @brief LLGiveInventory class implementation
 *
 * $LicenseInfo:firstyear=2010&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"
#include "llgiveinventory.h"

// library includes
#include "llnotificationsutil.h"
#include "lltrans.h"

// newview includes
#include "llagent.h"
#include "llagentdata.h"
#include "llagentui.h"
#include "llagentwearables.h"
#include "llavatarnamecache.h"
#include "llfloatertools.h" // for gFloaterTool
#include "llhudeffecttrail.h"
#include "llhudmanager.h"
#include "llimview.h"
#include "llinventory.h"
#include "llinventoryfunctions.h"
#include "llmutelist.h"
#include "llrecentpeople.h"
#include "llviewerobjectlist.h"
#include "llvoavatarself.h"
// [RLVa:KB] - Checked: RLVa-1.2.2
#include "llavatarnamecache.h"
#include "llslurl.h"
#include "rlvactions.h"
#include "rlvcommon.h"
#include "rlvui.h"
// [/RLVa:KB]
#include "llworld.h"    // <FS:CR> Aurora Sim
// <Vulkanstorm>
#include "llinventoryoffer_vks.h"
#include "vkstorm_ffi.h"
// </Vulkanstorm>

// <Vulkanstorm> Giving is decided in Rust since rung seven R4b
// (vkstorm-inventory exchange::give through llinventoryoffer_vks.h): the
// acceptance checks, the item and folder gives with their packets, the
// no-copy confirmations, the IM log lines, the RLVa @share checks and the
// folder limits (MAX_ITEMS, LLWorld's transfer limit). LLGiveable and
// LLUncopyableItems are the Rust functors.

// static
bool LLGiveInventory::isInventoryGiveAcceptable(const LLInventoryItem* item)
{
    return vks_r4b_item_predicate(0, item);
}

// static
bool LLGiveInventory::isInventoryGroupGiveAcceptable(const LLInventoryItem* item)
{
    return vks_r4b_item_predicate(1, item);
}

// static
bool LLGiveInventory::doGiveInventoryItem(const LLUUID& to_agent,
                                       const LLInventoryItem* item,
                                       const LLUUID& im_session_id/* = LLUUID::null*/)
{
    return vks_r4b_give_item(to_agent, item, im_session_id);
}

bool LLGiveInventory::doGiveInventoryCategory(const LLUUID& to_agent,
                                              const LLInventoryCategory* cat,
                                              const LLUUID& im_session_id,
                                              const std::string& notification_name)
{
    return vks_r4b_give_category(to_agent, cat, im_session_id, notification_name);
}

// static
bool LLGiveInventory::handleCopyProtectedItem(const LLSD& notification, const LLSD& response)
{
    return vks_r4b_handler_answer(VKS_INV_R4B_H_COPY_PROTECTED_ITEM, notification, response);
}
// </Vulkanstorm>

// EOF
