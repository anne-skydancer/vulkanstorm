void LLAgent::setAFK()
{
    if (gNonInteractive || !gAgent.getRegion())
    {
        // Don't set AFK if we're not talking to a region yet.
        return;
    }

    if (!(mControlFlags & AGENT_CONTROL_AWAY))
    {
        sendAnimationRequest(ANIM_AGENT_AWAY, ANIM_REQUEST_START);
        setControlFlags(AGENT_CONTROL_AWAY | AGENT_CONTROL_STOP);
        gAwayTimer.start();

        // <FS:AO> Gsit on away, antigrief protection
        if (gSavedSettings.getBOOL("AvatarSitOnAway"))
        {
            if (isAgentAvatarValid() && !gAgentAvatarp->isSitting() && !gRlvHandler.hasBehaviour(RLV_BHVR_SIT))
            {
                gAgent.sitDown();
                gAgent.setIsAfkSitting(true);
            }
        }
        // </FS:AO>
    }
}
void LLAgent::clearAFK()
{
    gAwayTriggerTimer.reset();

    // Gods can sometimes get into away state (via gestures)
    // without setting the appropriate control flag. JC
    if (mControlFlags & AGENT_CONTROL_AWAY
        || (isAgentAvatarValid()
            && (gAgentAvatarp->mSignaledAnimations.find(ANIM_AGENT_AWAY) != gAgentAvatarp->mSignaledAnimations.end())))
    {
        sendAnimationRequest(ANIM_AGENT_AWAY, ANIM_REQUEST_STOP);
        clearControlFlags(AGENT_CONTROL_AWAY);

        // <FS:AO> if we sat while away, stand back up on clear
        if (gSavedSettings.getBOOL("AvatarSitOnAway"))
        {
            if (gAgent.isAfkSitting() && !gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT))
            {
                gAgent.standUp();
            }
        }
        // </FS:AO>
    }

    // <FS:Ansariel> FIRE-1568: Fix sit on AFK issues (standing up when sitting before)
    gAgent.setIsAfkSitting(false);
}