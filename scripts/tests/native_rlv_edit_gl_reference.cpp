// Frozen OpenGL policies from f2ddbb2aa7. LGPL-2.1.
bool RlvActions::canEdit(const LLViewerObject* pObj)
{
    // User can edit the specified object if:
    //   - not generally restricted from editing (or the object's root is an exception)
    //   - not specifically restricted from editing this object's root
    //   - it's an attachment   and not restricted from editing attachments
    //     it's a rezzed object and not restricted from editing any world objects

    // NOTE-RLVa: edit checks should *never* be subject to @fartouch distance checks since we don't have the pick offset so
    //            instead just implicitly rely on the presence of a (transient) selection
    return
        (pObj) &&
        ( (!RlvHandler::instance().hasBehaviour(RLV_BHVR_EDIT)) || (RlvHandler::instance().isException(RLV_BHVR_EDIT, pObj->getRootEdit()->getID())) ) &&
        ( (!RlvHandler::instance().hasBehaviour(RLV_BHVR_EDITOBJ)) || (!RlvHandler::instance().isException(RLV_BHVR_EDITOBJ, pObj->getRootEdit()->getID())) ) &&
        ( (pObj->isAttachment()) ? !RlvHandler::instance().hasBehaviour(RLV_BHVR_EDITATTACH)
                                 : !RlvHandler::instance().hasBehaviour(RLV_BHVR_EDITWORLD) );
}
bool RlvActions::canInteract(const LLViewerObject* pObj, const LLVector3& posOffset /*=LLVector3::zero*/)
{
    static RlvCachedBehaviourModifier<float> s_nFartouchDist(RLV_MODIFIER_FARTOUCHDIST);

    // User can interact with the specified object if:
    //   - not interaction restricted (or the specified object is a HUD attachment)
    //   - not prevented from touching faraway objects (or the object's center + pick offset is within range)
    RlvHandler& rlvHandler = RlvHandler::instance();
    return
        (!pObj) ||
        ( ( (!rlvHandler.hasBehaviour(RLV_BHVR_INTERACT)) || (pObj->isHUDAttachment())) &&
          ( (!rlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH)) || (pObj->isHUDAttachment()) || (dist_vec_squared(gAgent.getPositionGlobal(), pObj->getPositionGlobal() + LLVector3d(posOffset)) <= s_nFartouchDist * s_nFartouchDist)) );
}