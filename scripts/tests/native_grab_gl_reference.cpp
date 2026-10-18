// Frozen OpenGL/reference implementation from f2ddbb2aa7. LGPL-2.1.
void send_ObjectGrab_message(LLViewerObject* object, const LLPickInfo & pick, const LLVector3 &grab_offset)
{
    // <FS:Ansariel> Crash fix
    //if (!object) return;
    if (!object || !object->getRegion()) return;

    LLMessageSystem *msg = gMessageSystem;

    msg->newMessageFast(_PREHASH_ObjectGrab);
    msg->nextBlockFast( _PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast( _PREHASH_ObjectData);
    msg->addU32Fast(    _PREHASH_LocalID, object->mLocalID);
    msg->addVector3Fast(_PREHASH_GrabOffset, grab_offset);
    msg->nextBlock("SurfaceInfo");
    msg->addVector3("UVCoord", LLVector3(pick.mUVCoords));
    msg->addVector3("STCoord", LLVector3(pick.mSTCoords));
    msg->addS32Fast(_PREHASH_FaceIndex, pick.mObjectFace);
    msg->addVector3("Position", pick.mIntersection);
    msg->addVector3("Normal", pick.mNormal);
    msg->addVector3("Binormal", pick.mBinormal);
    msg->sendMessage( object->getRegion()->getHost());

    /*  Diagnostic code
    LL_INFOS() << "mUVCoords: " << pick.mUVCoords
            << ", mSTCoords: " << pick.mSTCoords
            << ", mObjectFace: " << pick.mObjectFace
            << ", mIntersection: " << pick.mIntersection
            << ", mNormal: " << pick.mNormal
            << ", mBinormal: " << pick.mBinormal
            << LL_ENDL;

    LL_INFOS() << "Avatar pos: " << gAgent.getPositionAgent() << LL_ENDL;
    LL_INFOS() << "Object pos: " << object->getPosition() << LL_ENDL;
    */
}
void send_ObjectDeGrab_message(LLViewerObject* object, const LLPickInfo & pick)
{
    // <FS:Ansariel> Crash fix
    //if (!object) return;
    if (!object || !object->getRegion()) return;

    LLMessageSystem *msg = gMessageSystem;

    msg->newMessageFast(_PREHASH_ObjectDeGrab);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_ObjectData);
    msg->addU32Fast(_PREHASH_LocalID, object->mLocalID);
    msg->nextBlock("SurfaceInfo");
    msg->addVector3("UVCoord", LLVector3(pick.mUVCoords));
    msg->addVector3("STCoord", LLVector3(pick.mSTCoords));
    msg->addS32Fast(_PREHASH_FaceIndex, pick.mObjectFace);
    msg->addVector3("Position", pick.mIntersection);
    msg->addVector3("Normal", pick.mNormal);
    msg->addVector3("Binormal", pick.mBinormal);
    msg->sendMessage(object->getRegion()->getHost());
}
