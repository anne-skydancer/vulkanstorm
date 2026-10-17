bool RlvActions::referenceTouch(const LLViewerObject* pObj, const LLVector3& posOffset /*=LLVector3::zero*/)
{
    static RlvCachedBehaviourModifier<float> s_nFartouchDist(RLV_MODIFIER_FARTOUCHDIST);

    // User can touch a
    //  (1) World object if
    //        - a) not prevented from touching any object
    //        - b) not specifically prevented from touching that object
    //        - c) not prevented from touching world objects (or the object is an exception)
    //        - h) not prevented from touching faraway objects (or the object's center + pick offset is within range)
    //        - i) specifically allowed to touch that object (overrides all restrictions)
    //  (2) Attachment (on another avatar)
    //        - a) not prevented from touching any object
    //        - b) not specifically prevented from touching that object
    //        - d) not prevented from touching attachments (or the attachment and/or its wearer is/are an exception)
    //        - e) not prevented from touching other avatar's attachments (or the attachment is worn by a specific avatar on the exception list)
    //        - h) not prevented from touching faraway objects (or the attachment's center + pick offset is within range)
    //        - i) specifically allowed to touch that object (overrides all restrictions)
    //  (3) Attachment (on own avatar)
    //        - a) not prevented from touching any object
    //        - b) not specifically prevented from touching that object
    //        - d) not prevented from touching attachments (or the attachment is an exception)
    //        - f) not prevented from touching their own avatar's attachments (or the attachment is an exception)
    //        - h) not prevented from touching faraway objects (or the attachment's center + pick offset is within range)
    //        - i) specifically allowed to touch that object (overrides all restrictions)
    //  (4) Attachment (on HUD)
    //        - b) not specifically prevented from touching that object
    //        - g) not prevented from touching their own HUD attachments (or the attachment is an exception)
    //        - i) specifically allowed to touch that object (overrides all restrictions)

    // NOTE-RLVa: * touch restrictions apply linkset-wide (as opposed to, for instance, hover text which is object-specific) but only the root object's restrictions are tested
    //            * @touchall affects world objects and world attachments (self and others') but >not< HUD attachments
    //            * @fartouch distance matches against the specified object + pick offset (so >not< the linkset root)
    //            * @touchattachother exceptions change when they specify an avatar id (=block all) or an object id (=allow indiviual - see general @touchattach exceptions)
    //            * @touchattachself exceptions are only checked under the general @touchattach exceptions
    //            * @touchme in any object of a linkset affects that entire linkset (= if you can specifically touch one prim in a linkset you can touch that entire linkset)
    const LLUUID& idRoot = (pObj) ? pObj->getRootEdit()->getID() : LLUUID::null;

    RlvHandler& rlvHandler = RlvHandler::instance();
    // Short circuit test for (1/2/3/4.a) and (1/2/3/4.b)
    bool fCanTouch =
        (idRoot.notNull()) &&
        ( (pObj->isHUDAttachment()) || (!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHALL)) ) &&
        ( (!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHTHIS)) || (!rlvHandler.isException(RLV_BHVR_TOUCHTHIS, idRoot, ERlvExceptionCheck::Permissive)) );
    if (fCanTouch)
    {
        if (!pObj->isAttachment())
        {
            // Rezzed object - test for (1.c) and (1.h)
            fCanTouch =
                ( (!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHWORLD)) || (rlvHandler.isException(RLV_BHVR_TOUCHWORLD, idRoot, ERlvExceptionCheck::Permissive)) ) &&
                ( (!rlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH)) || (dist_vec_squared(gAgent.getPositionGlobal(), pObj->getPositionGlobal() + LLVector3d(posOffset)) <= s_nFartouchDist * s_nFartouchDist) );
        }
        else if (!pObj->permYouOwner())
        {
            // Attachment worn by other - test for (2.d), (2.e) and (2.h)
            const LLUUID& idAttachAgent = static_cast<LLViewerObject*>(pObj->getRoot())->getID();
            fCanTouch =
                (
                    ( (!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHATTACH) && !rlvHandler.hasBehaviour(RLV_BHVR_TOUCHATTACHOTHER)) ||
                      (rlvHandler.isException(RLV_BHVR_TOUCHATTACH, idRoot, ERlvExceptionCheck::Permissive) || rlvHandler.isException(RLV_BHVR_TOUCHATTACH, idAttachAgent, ERlvExceptionCheck::Permissive)) ) &&
                    (!rlvHandler.isException(RLV_BHVR_TOUCHATTACHOTHER, idAttachAgent))
                ) &&
                ( (!rlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH)) || (dist_vec_squared(gAgent.getPositionGlobal(), pObj->getPositionGlobal() + LLVector3d(posOffset)) <= s_nFartouchDist * s_nFartouchDist) );
        }
        else if (!pObj->isHUDAttachment())
        {
            // Regular attachment worn by this avie - test for (3.d), (3.e) and (3.h)
            fCanTouch =
                ((!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHATTACH)) || (rlvHandler.isException(RLV_BHVR_TOUCHATTACH, idRoot, ERlvExceptionCheck::Permissive))) &&
                ((!rlvHandler.hasBehaviour(RLV_BHVR_TOUCHATTACHSELF)) || (rlvHandler.isException(RLV_BHVR_TOUCHATTACH, idRoot, ERlvExceptionCheck::Permissive)));
        }
        else
        {
            // HUD attachment - test for (4.g)
            fCanTouch = (!hasBehaviour(RLV_BHVR_TOUCHHUD)) || (rlvHandler.isException(RLV_BHVR_TOUCHHUD, idRoot, ERlvExceptionCheck::Permissive));
        }
    }
    // Post-check for (1/2/3/4i)
    if ( (!fCanTouch) && (hasBehaviour(RLV_BHVR_TOUCHME)) )
        fCanTouch = rlvHandler.hasBehaviourRoot(idRoot, RLV_BHVR_TOUCHME);
    return fCanTouch;
}
