bool Reference::canStand()
{
    // NOTE: return false only if we're @unsit=n restricted and the avie is currently sitting on something and true for everything else
    return (!gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || ((isAgentAvatarValid()) && (!gAgentAvatarp->isSitting()));
}
bool Reference::canStand(const LLUUID& idRlvObjExcept)
{
    // NOTE: must match generic function above
    return (!gRlvHandler.hasBehaviourExcept(RLV_BHVR_UNSIT, idRlvObjExcept)) || ((isAgentAvatarValid()) && (!gAgentAvatarp->isSitting()));
}