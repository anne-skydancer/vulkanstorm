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
