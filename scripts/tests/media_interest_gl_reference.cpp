void GLMedia::calculateInterest()
{
    // Calculate distance from the avatar, for use in the proximity calculation.
    mProximityDistance = 0.0f;
    mProximityCamera = 0.0f;
    if(!mObjectList.empty())
    {
        // Just use the first object in the list.  We could go through the list and find the closest object, but this should work well enough.
        std::list< LLVOVolume* >::iterator iter = mObjectList.begin() ;
        LLVOVolume* objp = *iter ;
        assert(objp != NULL);

        // The distance calculation is invalid for HUD attachments -- leave both mProximityDistance and mProximityCamera at 0 for them.
        if(!objp->isHUDAttachment())
        {
            LLVector3d obj_global = objp->getPositionGlobal() ;
            LLVector3d agent_global = gAgent.getPositionGlobal() ;
            LLVector3d global_delta = agent_global - obj_global ;
            mProximityDistance = global_delta.magVecSquared();  // use distance-squared because it's cheaper and sorts the same.

            static LLUICachedControl<S32> mEarLocation("MediaSoundsEarLocation", 0);
            LLVector3d ear_position;
            switch(mEarLocation)
            {
            case 0:
            default:
                ear_position = gAgentCamera.getCameraPositionGlobal();
                break;

            case 1:
                ear_position = agent_global;
                break;
            }
            LLVector3d camera_delta = ear_position - obj_global;
            mProximityCamera = camera_delta.magVec();
        }
    }

    if(mNeedsMuteCheck)
    {
        // Check all objects this instance is associated with, and those objects' owners, against the mute list
        mIsMuted = false;

        std::list< LLVOVolume* >::iterator iter = mObjectList.begin() ;
        for(; iter != mObjectList.end() ; ++iter)
        {
            LLVOVolume *obj = *iter;
            assert(obj);
            if (!obj) continue;
            if(LLMuteList::getInstance() &&
               LLMuteList::getInstance()->isMuted(obj->getID()))
            {
                mIsMuted = true;
            }
            else
            {
                // We won't have full permissions data for all objects.  Attempt to mute objects when we can tell their owners are muted.
                if (LLSelectMgr::getInstance())
                {
                    LLPermissions* obj_perm = LLSelectMgr::getInstance()->findObjectPermissions(obj);
                    if(obj_perm)
                    {
                        if(LLMuteList::getInstance() &&
                           LLMuteList::getInstance()->isMuted(obj_perm->getOwner()))
                            mIsMuted = true;
                    }
                }
            }
        }

        mNeedsMuteCheck = false;
    }
}