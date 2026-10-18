LLVector3d Camera::referenceTargets(LLVector3d camera_target_global,const LLVector3d& focus_target_global)
{
    LLVector3d camera_pos_global;
    mCameraVirtualPositionAgent = gAgent.getPosAgentFromGlobal(camera_target_global);

    // perform field of view correction
    mCameraFOVZoomFactor = calcCameraFOVZoomFactor();
    camera_target_global = focus_target_global + (camera_target_global - focus_target_global) * (1.f + mCameraFOVZoomFactor);

    gAgent.setShowAvatar(true); // can see avatar by default

    // Adjust position for animation
    if (mCameraAnimating)
    {
        F32 time = mAnimationTimer.getElapsedTimeF32();

        // yet another instance of critically damped motion, hooray!
        // F32 fraction_of_animation = 1.f - pow(2.f, -time / CAMERA_ZOOM_HALF_LIFE);

        // linear interpolation
        F32 fraction_of_animation = time / mAnimationDuration;

        bool isfirstPerson = mCameraMode == CAMERA_MODE_MOUSELOOK;
        bool wasfirstPerson = mLastCameraMode == CAMERA_MODE_MOUSELOOK;
        F32 fraction_animation_to_skip;

        if (mAnimationCameraStartGlobal == camera_target_global)
        {
            fraction_animation_to_skip = 0.f;
        }
        else
        {
            LLVector3d cam_delta = mAnimationCameraStartGlobal - camera_target_global;
            fraction_animation_to_skip = HEAD_BUFFER_SIZE / (F32)cam_delta.magVec();
        }
        F32 animation_start_fraction = (wasfirstPerson) ? fraction_animation_to_skip : 0.f;
        F32 animation_finish_fraction =  (isfirstPerson) ? (1.f - fraction_animation_to_skip) : 1.f;

        if (fraction_of_animation < animation_finish_fraction)
        {
            if (fraction_of_animation < animation_start_fraction || fraction_of_animation > animation_finish_fraction )
            {
                gAgent.setShowAvatar(false);
            }

            // ...adjust position for animation
            F32 smooth_fraction_of_animation = llsmoothstep(0.0f, 1.0f, fraction_of_animation);
            camera_pos_global = lerp(mAnimationCameraStartGlobal, camera_target_global, smooth_fraction_of_animation);
            mFocusGlobal = lerp(mAnimationFocusStartGlobal, focus_target_global, smooth_fraction_of_animation);
        }
        else
        {
            // ...animation complete
            mCameraAnimating = false;

            camera_pos_global = camera_target_global;
            mFocusGlobal = focus_target_global;

            gAgent.endAnimationUpdateUI();
            gAgent.setShowAvatar(true);
        }

        if (isAgentAvatarValid() && (mCameraMode != CAMERA_MODE_MOUSELOOK))
        {
            gAgentAvatarp->updateAttachmentVisibility(mCameraMode);
        }
    }
    else
    {
        camera_pos_global = camera_target_global;
        mFocusGlobal = focus_target_global;
        gAgent.setShowAvatar(true);
    }

    // smoothing
    if (true)
    {
        LLVector3d agent_pos = gAgent.getPositionGlobal();
        LLVector3d camera_pos_agent = camera_pos_global - agent_pos;
        // Sitting on what you're manipulating can cause camera jitter with smoothing.
        // This turns off smoothing while editing. -MG
        bool in_build_mode = LLToolMgr::getInstance()->inBuildMode();
        mCameraSmoothingStop = mCameraSmoothingStop || in_build_mode;

        if (cameraThirdPerson() && !mCameraSmoothingStop)
        {
            const F32 SMOOTHING_HALF_LIFE = 0.02f;

            F32 smoothing = LLSmoothInterpolation::getInterpolant(gSavedSettings.getF32("CameraPositionSmoothing") * SMOOTHING_HALF_LIFE, false);

            if (mFocusOnAvatar && !mFocusObject) // we differentiate on avatar mode
            {
                // for avatar-relative focus, we smooth in avatar space -
                // the avatar moves too jerkily w/r/t global space to smooth there.

                LLVector3d delta = camera_pos_agent - mCameraSmoothingLastPositionAgent;
                if (delta.magVec() < MAX_CAMERA_SMOOTH_DISTANCE)  // only smooth over short distances please
                {
                    camera_pos_agent = lerp(mCameraSmoothingLastPositionAgent, camera_pos_agent, smoothing);
                    camera_pos_global = camera_pos_agent + agent_pos;
                }
            }
            else
            {
                LLVector3d delta = camera_pos_global - mCameraSmoothingLastPositionGlobal;
                if (delta.magVec() < MAX_CAMERA_SMOOTH_DISTANCE) // only smooth over short distances please
                {
                    camera_pos_global = lerp(mCameraSmoothingLastPositionGlobal, camera_pos_global, smoothing);
                }
            }
        }

        mCameraSmoothingLastPositionGlobal = camera_pos_global;
        mCameraSmoothingLastPositionAgent = camera_pos_agent;
        mCameraSmoothingStop = false;
    }


    mCameraCurrentFOVZoomFactor = lerp(mCameraCurrentFOVZoomFactor, mCameraFOVZoomFactor, LLSmoothInterpolation::getInterpolant(FOV_ZOOM_HALF_LIFE));

//  LL_INFOS() << "Current FOV Zoom: " << mCameraCurrentFOVZoomFactor << " Target FOV Zoom: " << mCameraFOVZoomFactor << " Object penetration: " << mFocusObjectDist << LL_ENDL;

    return camera_pos_global;
}
