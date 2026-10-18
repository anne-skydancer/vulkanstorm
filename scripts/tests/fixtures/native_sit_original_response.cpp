void process_avatar_sit_response(LLMessageSystem *mesgsys, void **user_data)
{
    LLVector3 sitPosition;
    LLQuaternion sitRotation;
    LLUUID sitObjectID;
    bool use_autopilot;
    mesgsys->getUUIDFast(_PREHASH_SitObject, _PREHASH_ID, sitObjectID);
    mesgsys->getBOOLFast(_PREHASH_SitTransform, _PREHASH_AutoPilot, use_autopilot);
    mesgsys->getVector3Fast(_PREHASH_SitTransform, _PREHASH_SitPosition, sitPosition);
    mesgsys->getQuatFast(_PREHASH_SitTransform, _PREHASH_SitRotation, sitRotation);
    LLVector3 camera_eye;
    mesgsys->getVector3Fast(_PREHASH_SitTransform, _PREHASH_CameraEyeOffset, camera_eye);
    LLVector3 camera_at;
    mesgsys->getVector3Fast(_PREHASH_SitTransform, _PREHASH_CameraAtOffset, camera_at);
    bool force_mouselook;
    mesgsys->getBOOLFast(_PREHASH_SitTransform, _PREHASH_ForceMouselook, force_mouselook);

    if (isAgentAvatarValid() && dist_vec_squared(camera_eye, camera_at) > CAMERA_POSITION_THRESHOLD_SQUARED)
    {
        gAgentCamera.setSitCamera(sitObjectID, camera_eye, camera_at);
    }

    gAgentCamera.setForceMouselook(force_mouselook);
    // Forcing turning off flying here to prevent flying after pressing "Stand"
    // to stand up from an object. See EXT-1655.
    gAgent.setFlying(false);

    LLViewerObject* object = gObjectList.findObject(sitObjectID);
    if (object)
    {
        LLVector3 sit_spot = object->getPositionAgent() + (sitPosition * object->getRotation());
        if (!use_autopilot || (isAgentAvatarValid() && gAgentAvatarp->isSitting() && gAgentAvatarp->getRoot() == object->getRoot()))
        {
            //we're already sitting on this object, so don't autopilot
        }
        else
        {
            gAgent.startAutoPilotGlobal(gAgent.getPosGlobalFromAgent(sit_spot), "Sit", &sitRotation, near_sit_object, NULL, 0.5f);
        }
    }
    else
    {
        LL_WARNS("Messaging") << "Received sit approval for unknown object " << sitObjectID << LL_ENDL;
    }
}