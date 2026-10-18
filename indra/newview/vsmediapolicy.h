// Original LLVOVolume media permission policy for renderer-peer owners.
// LGPL-2.1, like the viewer. Preserve branch order and lazy permission lookups.
#pragma once
#include "llmediaentry.h"

template<class GroupAllowed,class OwnerAllowed>
bool vs_has_media_permission(const LLMediaEntry* entry,bool interact,
                            GroupAllowed groupAllowed,OwnerAllowed ownerAllowed)
{
    if(!entry)return false;
    const U8 permissions=interact?entry->getPermsInteract():entry->getPermsControl();
    if(permissions & LLMediaEntry::PERM_ANYONE)
        return true;
    else if(permissions & LLMediaEntry::PERM_GROUP)
    {
        if(groupAllowed())return true;
    }
    else if((permissions & LLMediaEntry::PERM_OWNER) && ownerAllowed())
        return true;
    return false;
}
