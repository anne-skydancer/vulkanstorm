// Original LLAgent CPU navigation, b681403409. Linden Research, Inc.; LGPL-2.1.
void LLAgent::startAutoPilotGlobal(
    const LLVector3d &target_global,
    const std::string& behavior_name,
    const LLQuaternion *target_rotation,
    void (*finish_callback)(bool, void *),
    void *callback_data,
    F32 stop_distance,
    F32 rot_threshold,
    bool allow_flying)
{
    if (!isAgentAvatarValid())
    {
        return;
    }

    if (target_global.isExactlyZero())
    {
        LL_WARNS() << "Canceling attempt to start autopilot towards invalid position" << LL_ENDL;
        return;
    }

    // Are there any pending callbacks from previous auto pilot requests?
    if (mAutoPilotFinishedCallback)
    {
        mAutoPilotFinishedCallback(dist_vec(gAgent.getPositionGlobal(), mAutoPilotTargetGlobal) < mAutoPilotStopDistance, mAutoPilotCallbackData);
    }

    mAutoPilotFinishedCallback = finish_callback;
    mAutoPilotCallbackData = callback_data;
    mAutoPilotRotationThreshold = rot_threshold;
    mAutoPilotBehaviorName = behavior_name;
    mAutoPilotAllowFlying = allow_flying;

    LLVector3d delta_pos( target_global );
    delta_pos -= getPositionGlobal();
    F64 distance = delta_pos.magVec();
    LLVector3d trace_target = target_global;

    trace_target.mdV[VZ] -= 10.f;

    LLVector3d intersection;
    LLVector3 normal;
    LLViewerObject *hit_obj;
    F32 heightDelta = LLWorld::getInstance()->resolveStepHeightGlobal(NULL, target_global, trace_target, intersection, normal, &hit_obj);

    if (stop_distance > 0.f)
    {
        mAutoPilotStopDistance = stop_distance;
    }
    else
    {
        // Guess at a reasonable stop distance.
        mAutoPilotStopDistance = (F32) sqrt( distance );
        if (mAutoPilotStopDistance < 0.5f)
        {
            mAutoPilotStopDistance = 0.5f;
        }
    }

    if (mAutoPilotAllowFlying)
    {
        mAutoPilotFlyOnStop = getFlying();
    }
    else
    {
        mAutoPilotFlyOnStop = false;
    }

    if (distance > 30.0 && mAutoPilotAllowFlying)
    {
        setFlying(true);
    }

    if ( distance > 1.f &&
        mAutoPilotAllowFlying &&
        heightDelta > (sqrtf(mAutoPilotStopDistance) + 1.f))
    {
        setFlying(true);
        // Do not force flying for "Sit" behavior to prevent flying after pressing "Stand"
        // from an object. See EXT-1655.
        if ("Sit" != mAutoPilotBehaviorName)
            mAutoPilotFlyOnStop = true;
    }

    mAutoPilot = true;
    setAutoPilotTargetGlobal(target_global);

    if (target_rotation)
    {
        mAutoPilotUseRotation = true;
        mAutoPilotTargetFacing = LLVector3::x_axis * *target_rotation;
        mAutoPilotTargetFacing.mV[VZ] = 0.f;
        mAutoPilotTargetFacing.normalize();
    }
    else
    {
        mAutoPilotUseRotation = false;
    }

    mAutoPilotNoProgressFrameCount = 0;
}
void LLAgent::setAutoPilotTargetGlobal(const LLVector3d &target_global)
{
    if (mAutoPilot)
    {
        mAutoPilotTargetGlobal = target_global;

        // trace ray down to find height of destination from ground
        LLVector3d traceEndPt = target_global;
        traceEndPt.mdV[VZ] -= 20.f;

        LLVector3d targetOnGround;
        LLVector3 groundNorm;
        LLViewerObject *obj;

        LLWorld::getInstance()->resolveStepHeightGlobal(NULL, target_global, traceEndPt, targetOnGround, groundNorm, &obj);
        // Note: this might malfunction for sitting agent, since pelvis stays same, but agent's position becomes lower
        // But for autopilot to work we assume that agent is standing and ready to go.
        F64 target_height = llmax((F64)gAgentAvatarp->getPelvisToFoot(), target_global.mdV[VZ] - targetOnGround.mdV[VZ]);

        // clamp z value of target to minimum height above ground
        mAutoPilotTargetGlobal.mdV[VZ] = targetOnGround.mdV[VZ] + target_height;
        mAutoPilotTargetDist = (F32)dist_vec(gAgent.getPositionGlobal(), mAutoPilotTargetGlobal);
    }
}
void LLAgent::startFollowPilot(const LLUUID &leader_id, bool allow_flying, F32 stop_distance)
{
    mLeaderID = leader_id;
    if ( mLeaderID.isNull() ) return;

    LLViewerObject* object = gObjectList.findObject(mLeaderID);
    if (!object)
    {
        mLeaderID = LLUUID::null;
        return;
    }

    startAutoPilotGlobal(object->getPositionGlobal(),
                         std::string(), // behavior_name
                         NULL,          // target_rotation
                         NULL,          // finish_callback
                         NULL,          // callback_data
                         stop_distance,
                         0.03f,         // rotation_threshold
                         allow_flying);
}
void LLAgent::autoPilot(F32 *delta_yaw)
{
    if (mAutoPilot)
    {
        if (!mLeaderID.isNull())
        {
            LLViewerObject* object = gObjectList.findObject(mLeaderID);
            if (!object)
            {
                stopAutoPilot();
                return;
            }
            mAutoPilotTargetGlobal = object->getPositionGlobal();
        }

        if (!isAgentAvatarValid()) return;

        if (gAgentAvatarp->mInAir && mAutoPilotAllowFlying)
        {
            setFlying(true);
        }

        LLVector3 at;
        at.setVec(mFrameAgent.getAtAxis());
        LLVector3 target_agent = getPosAgentFromGlobal(mAutoPilotTargetGlobal);
        LLVector3 direction = target_agent - getPositionAgent();

        F32 target_dist = direction.magVec();

        if (target_dist >= mAutoPilotTargetDist)
        {
            mAutoPilotNoProgressFrameCount++;
            bool out_of_time = false;
            if (getFlying())
            {
                out_of_time = mAutoPilotNoProgressFrameCount > AUTOPILOT_MAX_TIME_NO_PROGRESS_FLY * gFPSClamped;
            }
            else
            {
                out_of_time = mAutoPilotNoProgressFrameCount > AUTOPILOT_MAX_TIME_NO_PROGRESS_WALK * gFPSClamped;
            }
            if (out_of_time)
            {
                stopAutoPilot();
                return;
            }
        }

        mAutoPilotTargetDist = target_dist;

        // Make this a two-dimensional solution
        at.mV[VZ] = 0.f;
        direction.mV[VZ] = 0.f;

        at.normalize();
        F32 xy_distance = direction.normalize();

        F32 yaw = 0.f;
        if (mAutoPilotTargetDist > mAutoPilotStopDistance)
        {
            yaw = angle_between(mFrameAgent.getAtAxis(), direction);
        }
        else if (mAutoPilotUseRotation)
        {
            // we're close now just aim at target facing
            yaw = angle_between(at, mAutoPilotTargetFacing);
            direction = mAutoPilotTargetFacing;
        }

        yaw = 4.f * yaw / gFPSClamped;

        // figure out which direction to turn
        LLVector3 scratch(at % direction);

        if (scratch.mV[VZ] > 0.f)
        {
            setControlFlags(AGENT_CONTROL_YAW_POS);
        }
        else
        {
            yaw = -yaw;
            setControlFlags(AGENT_CONTROL_YAW_NEG);
        }

        *delta_yaw = yaw;

        // Compute when to start slowing down
        F32 slow_distance;
        if (getFlying())
        {
            slow_distance = llmax(8.f, mAutoPilotStopDistance + 5.f);
        }
        else
        {
            slow_distance = llmax(3.f, mAutoPilotStopDistance + 2.f);
        }

        // If we're flying, handle autopilot points above or below you.
        if (getFlying() && xy_distance < AUTOPILOT_HEIGHT_ADJUST_DISTANCE)
        {
            if (isAgentAvatarValid())
            {
                F64 current_height = gAgentAvatarp->getPositionGlobal().mdV[VZ];
                F32 delta_z = (F32)(mAutoPilotTargetGlobal.mdV[VZ] - current_height);
                F32 slope = delta_z / xy_distance;
                if (slope > 0.45f && delta_z > 6.f)
                {
                    setControlFlags(AGENT_CONTROL_FAST_UP | AGENT_CONTROL_UP_POS);
                }
                else if (slope > 0.002f && delta_z > 0.5f)
                {
                    setControlFlags(AGENT_CONTROL_UP_POS);
                }
                else if (slope < -0.45f && delta_z < -6.f && current_height > AUTOPILOT_MIN_TARGET_HEIGHT_OFF_GROUND)
                {
                    setControlFlags(AGENT_CONTROL_FAST_UP | AGENT_CONTROL_UP_NEG);
                }
                else if (slope < -0.002f && delta_z < -0.5f && current_height > AUTOPILOT_MIN_TARGET_HEIGHT_OFF_GROUND)
                {
                    setControlFlags(AGENT_CONTROL_UP_NEG);
                }
            }
        }

        //  calculate delta rotation to target heading
        F32 delta_target_heading = angle_between(mFrameAgent.getAtAxis(), mAutoPilotTargetFacing);

        if (xy_distance > slow_distance && yaw < (F_PI / 10.f))
        {
            // walking/flying fast
            setControlFlags(AGENT_CONTROL_FAST_AT | AGENT_CONTROL_AT_POS);
        }
        else if (mAutoPilotTargetDist > mAutoPilotStopDistance)
        {
            // walking/flying slow
            U32 movement_flag = 0;

            if (at * direction > 0.9f)
            {
                movement_flag = AGENT_CONTROL_AT_POS;
            }
            else if (at * direction < -0.9f)
            {
                movement_flag = AGENT_CONTROL_AT_NEG;
            }

            if (getFlying())
            {
                // flying is too fast and has high inertia, artificially slow it down
                // Don't update flags too often, server might not react
                static U64 last_time_microsec = 0;
                U64 time_microsec = LLTimer::getTotalTime();
                U64 delta = time_microsec - last_time_microsec;
                // fly during ~0-40 ms, stop during ~40-250 ms
                if (delta > 250000) // 250ms
                {
                    // reset even if !movement_flag
                    last_time_microsec = time_microsec;
                }
                else if (delta > 40000) // 40 ms
                {
                    clearControlFlags(AGENT_CONTROL_AT_POS | AGENT_CONTROL_AT_POS);
                    movement_flag = 0;
                }
            }

            if (movement_flag)
            {
                setControlFlags(movement_flag);
            }
        }

        // check to see if we need to keep rotating to target orientation
        if (mAutoPilotTargetDist < mAutoPilotStopDistance)
        {
            setControlFlags(AGENT_CONTROL_STOP);
            if(!mAutoPilotUseRotation || (delta_target_heading < mAutoPilotRotationThreshold))
            {
                stopAutoPilot();
            }
        }
    }
}
void LLAgent::stopAutoPilot(bool user_cancel)
{
    if (mAutoPilot)
    {
        mAutoPilot = false;
        if (mAutoPilotUseRotation && !user_cancel)
        {
            resetAxes(mAutoPilotTargetFacing);
        }
        // Restore previous flying state before invoking mAutoPilotFinishedCallback to allow
        // callback function to change the flying state (like in near_sit_down_point()).
        // If the user cancelled, don't change the fly state
        if (!user_cancel)
        {
            setFlying(mAutoPilotFlyOnStop);
        }
        //NB: auto pilot can terminate for a reason other than reaching the destination
        if (mAutoPilotFinishedCallback)
        {
            mAutoPilotFinishedCallback(!user_cancel && dist_vec(gAgent.getPositionGlobal(), mAutoPilotTargetGlobal) < mAutoPilotStopDistance, mAutoPilotCallbackData);
            mAutoPilotFinishedCallback = NULL;
        }
        mLeaderID = LLUUID::null;

        setControlFlags(AGENT_CONTROL_STOP);

        if (user_cancel && !mAutoPilotBehaviorName.empty())
        {
            if (mAutoPilotBehaviorName == "Sit")
                LL_INFOS("Agent") << "Autopilot-Sit was canceled by user action" << LL_ENDL;
            else if (mAutoPilotBehaviorName == "Attach")
                LLNotificationsUtil::add("CancelledAttach");
            else
                LLNotificationsUtil::add("Cancelled");
        }
    }
}