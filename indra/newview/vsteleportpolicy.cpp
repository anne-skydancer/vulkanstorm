// Shared reference teleport admission; backend transports retain ordinary UI/RLVa policy. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsteleportpolicy.h"
#include "llagent.h"
#include "llstartup.h"
#include "llteleportflags.h"
#include "llviewercontrol.h"
#include "rlvactions.h"
#include "rlvhandler.h"

bool vs_teleport_location_allowed(const LLVector3d& pos_global)
{
// [RLVa:KB] - Checked: RLVa-2.0.0
    if ( (RlvActions::isRlvEnabled()) && (!RlvUtil::isForceTp()) )
    {
        if (LLStartUp::getStartupState() < STATE_STARTED)
        {
            return false;
        }

        if ( (RlvActions::isLocalTp(pos_global)) ? !RlvActions::canTeleportToLocal(pos_global) : !RlvActions::canTeleportToLocation() )
        {
            RlvUtil::notifyBlocked(RlvStringKeys::Blocked::Teleport);
            return false;
        }

        if ( (gRlvHandler.getCurrentCommand()) && (RLV_BHVR_TPTO == gRlvHandler.getCurrentCommand()->getBehaviourType()) )
        {
            gRlvHandler.setCanCancelTp(false);
        }
    }
// [/RLVa:KB]

    return true;
}

bool vs_teleport_landmark_allowed(const LLUUID& landmark_asset_id)
{
    // <FS:Ansariel> FIRE-21576: Prevent TPing home while still logging in if RLVa is enabled
    if (RlvActions::isRlvEnabled() && LLStartUp::getStartupState() < STATE_STARTED)
    {
        return false;
    }
    // </FS:Ansariel>

// [RLVa:KB] - Checked: 2010-08-22 (RLVa-1.2.1a) | Modified: RLVa-1.2.1a
    // NOTE: we'll allow teleporting home unless both @tplm=n *and* @tploc=n restricted
    if ( (rlv_handler_t::isEnabled()) &&
         ( ( (landmark_asset_id.notNull()) ? gRlvHandler.hasBehaviour(RLV_BHVR_TPLM)
                                           : gRlvHandler.hasBehaviour(RLV_BHVR_TPLM) && gRlvHandler.hasBehaviour(RLV_BHVR_TPLOC) ) ||
           ((gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) && gAgent.isSitting()) ))
    {
        RlvUtil::notifyBlocked(RlvStringKeys::Blocked::Teleport);
        return false;
    }
// [/RLVa:KB]

    return true;
}

bool vs_teleport_cancel_allowed(U32 flags)
{
    return !((flags & TELEPORT_FLAGS_DISABLE_CANCEL) && !gSavedSettings.getBOOL("FSAlwaysShowTPCancel")) &&
        gRlvHandler.getCanCancelTp();
}
