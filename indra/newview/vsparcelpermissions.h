// Parcel indicators use the reference viewer's region/parcel policy. LGPL-2.1.
#pragma once
#include "llparcel.h"
#include "llregionflags.h"

struct VSParcelPermissions
{
    bool known = false;
    bool voice = false, fly = false, push = false, build = false;
    bool scripts = false, damage = false, seeAvatars = true;

    static VSParcelPermissions from(U64 region, U32 flags, bool seeAvatars)
    {
        // Same policy as LLViewerParcelMgr::allowAgent* and both location bars.
        // These describe general parcel permissions, not owner/group exceptions.
        LLParcel parcel;
        parcel.setAllParcelFlags(flags);
        return {true,
            bool(region & REGION_FLAGS_ALLOW_VOICE) && parcel.getParcelFlagAllowVoice(),
            !(region & REGION_FLAGS_BLOCK_FLY) && parcel.getAllowFly(),
            !(region & REGION_FLAGS_RESTRICT_PUSHOBJECT) && !parcel.getRestrictPushObject(),
            parcel.getAllowModify(),
            !(region & (REGION_FLAGS_SKIP_SCRIPTS | REGION_FLAGS_ESTATE_SKIP_SCRIPTS)) && parcel.getAllowOtherScripts(),
            bool(region & REGION_FLAGS_ALLOW_DAMAGE) || parcel.getAllowDamage(), seeAvatars};
    }
};
