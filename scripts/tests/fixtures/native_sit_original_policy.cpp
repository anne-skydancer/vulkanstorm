bool RlvActions::canSit(const LLViewerObject* pObj, const LLVector3& posOffset /*=LLVector3::zero*/)
{
    // User can sit on the specified object if:
    //   - not prevented from sitting
    //   - not prevented from standing up or not currently sitting
    //   - not standtp restricted or not currently sitting (if the user is sitting and tried to sit elsewhere the tp would just kick in)
    //   - not a regular sit (i.e. due to @sit:<uuid>=force)
    //   - not @sittp=n or @fartouch=n restricted or if they clicked on a point within the allowed radius
    static RlvCachedBehaviourModifier<float> s_nFarTouchDist(RLV_MODIFIER_FARTOUCHDIST);
    static RlvCachedBehaviourModifier<float> s_nSitTpDist(RLV_MODIFIER_SITTPDIST);
    return
        ( (pObj) && (LL_PCODE_VOLUME == pObj->getPCode()) ) &&
        (!hasBehaviour(RLV_BHVR_SIT)) &&
        ( ((!hasBehaviour(RLV_BHVR_UNSIT)) && (!hasBehaviour(RLV_BHVR_STANDTP))) ||
          agentAvatarStanding() ) &&
        ( ( (NULL != gRlvHandler.getCurrentCommand()) && (RLV_BHVR_SIT == gRlvHandler.getCurrentCommand()->getBehaviourType()) ) ||
          ( ((!hasBehaviour(RLV_BHVR_SITTP)) || (dist_vec_squared(gAgent.getPositionGlobal(), pObj->getPositionGlobal() + LLVector3d(posOffset)) < s_nSitTpDist * s_nSitTpDist)) &&
            ((!hasBehaviour(RLV_BHVR_FARTOUCH)) || (dist_vec_squared(gAgent.getPositionGlobal(), pObj->getPositionGlobal() + LLVector3d(posOffset)) < s_nFarTouchDist * s_nFarTouchDist)) ) );
}