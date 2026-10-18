void LLToolGrabBase::startSpin()
{
    LLViewerObject* objectp = mGrabPick.getObject();
    if (!objectp)
    {
        return;
    }
    mSpinGrabbing = true;

    // Was saveSelectedObjectTransform()
    LLViewerObject *root = (LLViewerObject *)objectp->getRoot();
    mSpinRotation = root->getRotation();

    LLMessageSystem *msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_ObjectSpinStart);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_ObjectData);
    msg->addUUIDFast(_PREHASH_ObjectID, mGrabPick.mObjectID );
    msg->sendMessage( objectp->getRegion()->getHost() );
}
void LLToolGrabBase::stopSpin()
{
    mSpinGrabbing = false;

    LLViewerObject* objectp = mGrabPick.getObject();
    if (!objectp)
    {
        return;
    }

    LLMessageSystem *msg = gMessageSystem;
    switch(mMode)
    {
    case GRAB_ACTIVE_CENTER:
    case GRAB_NONPHYSICAL:
    case GRAB_LOCKED:
        msg->newMessageFast(_PREHASH_ObjectSpinStop);
        msg->nextBlockFast(_PREHASH_AgentData);
        msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID() );
        msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
        msg->nextBlockFast(_PREHASH_ObjectData);
        msg->addUUIDFast(_PREHASH_ObjectID, objectp->getID() );
        msg->sendMessage( objectp->getRegion()->getHost() );
        break;

    case GRAB_NOOBJECT:
    case GRAB_INACTIVE:
    default:
        // do nothing
        break;
    }
}