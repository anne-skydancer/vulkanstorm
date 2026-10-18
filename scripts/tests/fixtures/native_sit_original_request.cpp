void handle_object_sit(LLViewerObject* object, const LLVector3& offset)
{
    // get object selection offset

//  if (object && object->getPCode() == LL_PCODE_VOLUME)
// [RLVa:KB] - Checked: 2010-03-06 (RLVa-1.2.0c) | Modified: RLVa-1.2.0c
    if ( (object && object->getPCode() == LL_PCODE_VOLUME) &&
         ((!rlv_handler_t::isEnabled()) || (RlvActions::canSit(object, offset))) )
// [/RLVa:KB]
    {
// [RLVa:KB] - Checked: 2010-08-29 (RLVa-1.2.1c) | Added: RLVa-1.2.1c
        if ( (gRlvHandler.hasBehaviour(RLV_BHVR_STANDTP)) && (isAgentAvatarValid()) )
        {
            if (gAgentAvatarp->isSitting())
            {
                gAgent.standUp();
                return;
            }
            gRlvHandler.setSitSource(gAgent.getPositionGlobal());
        }
// [/RLVa:KB]

        gMessageSystem->newMessageFast(_PREHASH_AgentRequestSit);
        gMessageSystem->nextBlockFast(_PREHASH_AgentData);
        gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
        gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        gMessageSystem->nextBlockFast(_PREHASH_TargetObject);
        gMessageSystem->addUUIDFast(_PREHASH_TargetID, object->mID);
        gMessageSystem->addVector3Fast(_PREHASH_Offset, offset);

        object->getRegion()->sendReliableMessage();
    }
}