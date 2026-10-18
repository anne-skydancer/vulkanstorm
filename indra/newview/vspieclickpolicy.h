// Original Pie click-action and two-pick priority, shared by real scene peers.
// LGPL-2.1, like the viewer.
#pragma once
#include "indra_constants.h"
#include "llprimitive.h"
template<class Peer,class Object>bool vs_pie_use_click_action(const Peer& peer,MASK mask,const Object& object,const Object& parent)
{
    return mask==MASK_NONE && bool(object) && !peer.attachment(object) && LLPrimitive::isPrimitive(peer.pcode(object)) &&
        ((peer.action(object) && peer.action(object)!=CLICK_ACTION_DISABLED) ||
         (parent && peer.action(parent) && peer.action(parent)!=CLICK_ACTION_DISABLED));
}
// True selects visible_pick; false selects transparent_pick.
template<class Peer,class Object>bool vs_pie_prefer_visible(const Peer& peer,MASK mask,const Object& transparent,const Object& visible)
{
    if(peer.same(transparent,visible) || !visible || !transparent)return false;
    else if(!transparent)return true; // Original redundant null branch retained.
    else
    {
        const auto transparent_parent=peer.rootEdit(transparent),visible_parent=peer.rootEdit(visible);
        if(peer.attachment(transparent))return false;
        else if(peer.action(transparent)!=CLICK_ACTION_DISABLED &&
                (vs_pie_use_click_action(peer,mask,transparent,transparent_parent) || peer.touch(transparent) ||
                 (transparent_parent && peer.touch(transparent_parent))))return false;
        else if(peer.attachment(visible))return true;
        else if(peer.action(visible)!=CLICK_ACTION_DISABLED &&
                (vs_pie_use_click_action(peer,mask,visible,visible_parent) || peer.touch(visible) ||
                 (visible_parent && peer.touch(visible_parent))))return true;
        else return false;
    }
}
