// Shared reference click-action inheritance for both renderer peers. LGPL-2.1.
#pragma once
#include <string>
#include "indra_constants.h"

inline U8 vs_object_click_action(U8 object_action,U8 parent_action,bool attachment)
{
    if (attachment) return CLICK_ACTION_NONE;
    if (parent_action==CLICK_ACTION_DISABLED || object_action) return object_action;
    return parent_action ? parent_action : CLICK_ACTION_TOUCH;
}

// Retain the reference compile-time owner override without introducing a call
// or a grid lookup in the ordinary per-object permission hot path.
template<class Override>
inline bool vs_object_you_owner(bool simulatorOwner,Override overrideAllowed)
{
    (void)overrideAllowed;
#ifdef HACKED_GODLIKE_VIEWER
    (void)simulatorOwner;
    return true;
#else
#ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (overrideAllowed()) return true;
#endif
    return simulatorOwner;
#endif
}
