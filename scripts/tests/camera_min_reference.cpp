bool Camera::referenceMin(F32 &obj_min_distance)
{
    bool soft_limit = false; // is the bounding box to be treated literally (volumes) or as an approximation (avatars)

    if (!mFocusObject || mFocusObject->isDead() ||
        mFocusObject->isMesh() ||
        isDisableCameraConstraints())
    {
        obj_min_distance = 0.f;
        return true;
    }

    if (mFocusObject->mDrawable.isNull())
    {
#ifdef LL_RELEASE_FOR_DOWNLOAD
        LL_WARNS() << "Focus object with no drawable!" << LL_ENDL;
#else
        mFocusObject->dump();
        LL_ERRS() << "Focus object with no drawable!" << LL_ENDL;
#endif
        obj_min_distance = 0.f;
        return true;
    }

    LLQuaternion inv_object_rot = ~mFocusObject->getRenderRotation();
    LLVector3 target_offset_origin = mFocusObjectOffset;
    LLVector3 camera_offset_target(getCameraPositionAgent() - gAgent.getPosAgentFromGlobal(mFocusTargetGlobal));

    // convert offsets into object local space
    camera_offset_target.rotVec(inv_object_rot);
    target_offset_origin.rotVec(inv_object_rot);

    // push around object extents based on target offset
    LLVector3 object_extents = mFocusObject->getScale();
    if (mFocusObject->isAvatar())
    {
        // fudge factors that lets you zoom in on avatars a bit more (which don't do FOV zoom)
        object_extents.mV[VX] *= AVATAR_ZOOM_MIN_X_FACTOR;
        object_extents.mV[VY] *= AVATAR_ZOOM_MIN_Y_FACTOR;
        object_extents.mV[VZ] *= AVATAR_ZOOM_MIN_Z_FACTOR;
        soft_limit = true;
    }
    LLVector3 abs_target_offset = target_offset_origin;
    abs_target_offset.abs();

    LLVector3 target_offset_dir = target_offset_origin;

    bool target_outside_object_extents = false;

    for (U32 i = VX; i <= VZ; i++)
    {
        if (abs_target_offset.mV[i] * 2.f > object_extents.mV[i] + OBJECT_EXTENTS_PADDING)
        {
            target_outside_object_extents = true;
        }
        if (camera_offset_target.mV[i] > 0.f)
        {
            object_extents.mV[i] -= target_offset_origin.mV[i] * 2.f;
        }
        else
        {
            object_extents.mV[i] += target_offset_origin.mV[i] * 2.f;
        }
    }

    // don't shrink the object extents so far that the object inverts
    object_extents.clamp(0.001f, F32_MAX);

    // move into first octant
    LLVector3 camera_offset_target_abs_norm = camera_offset_target;
    camera_offset_target_abs_norm.abs();
    // make sure offset is non-zero
    camera_offset_target_abs_norm.clamp(0.001f, F32_MAX);
    camera_offset_target_abs_norm.normalize();

    // find camera position relative to normalized object extents
    LLVector3 camera_offset_target_scaled = camera_offset_target_abs_norm;
    camera_offset_target_scaled.mV[VX] /= object_extents.mV[VX];
    camera_offset_target_scaled.mV[VY] /= object_extents.mV[VY];
    camera_offset_target_scaled.mV[VZ] /= object_extents.mV[VZ];

    if (camera_offset_target_scaled.mV[VX] > camera_offset_target_scaled.mV[VY] &&
        camera_offset_target_scaled.mV[VX] > camera_offset_target_scaled.mV[VZ])
    {
        if (camera_offset_target_abs_norm.mV[VX] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VX] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VX] * 0.5f / camera_offset_target_abs_norm.mV[VX];
        }
    }
    else if (camera_offset_target_scaled.mV[VY] > camera_offset_target_scaled.mV[VZ])
    {
        if (camera_offset_target_abs_norm.mV[VY] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VY] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VY] * 0.5f / camera_offset_target_abs_norm.mV[VY];
        }
    }
    else
    {
        if (camera_offset_target_abs_norm.mV[VZ] < 0.001f)
        {
            obj_min_distance = object_extents.mV[VZ] * 0.5f;
        }
        else
        {
            obj_min_distance = object_extents.mV[VZ] * 0.5f / camera_offset_target_abs_norm.mV[VZ];
        }
    }

    LLVector3 object_split_axis;
    LLVector3 target_offset_scaled = target_offset_origin;
    target_offset_scaled.abs();
    target_offset_scaled.normalize();
    target_offset_scaled.mV[VX] /= object_extents.mV[VX];
    target_offset_scaled.mV[VY] /= object_extents.mV[VY];
    target_offset_scaled.mV[VZ] /= object_extents.mV[VZ];

    if (target_offset_scaled.mV[VX] > target_offset_scaled.mV[VY] &&
        target_offset_scaled.mV[VX] > target_offset_scaled.mV[VZ])
    {
        object_split_axis = LLVector3::x_axis;
    }
    else if (target_offset_scaled.mV[VY] > target_offset_scaled.mV[VZ])
    {
        object_split_axis = LLVector3::y_axis;
    }
    else
    {
        object_split_axis = LLVector3::z_axis;
    }

    LLVector3 camera_offset_object(getCameraPositionAgent() - mFocusObject->getPositionAgent());


    F32 camera_offset_clip = camera_offset_object * object_split_axis;
    F32 target_offset_clip = target_offset_dir * object_split_axis;

    // target has moved outside of object extents
    // check to see if camera and target are on same side
    if (target_outside_object_extents)
    {
        if (camera_offset_clip > 0.f && target_offset_clip > 0.f)
        {
            return false;
        }
        else if (camera_offset_clip < 0.f && target_offset_clip < 0.f)
        {
            return false;
        }
    }

    // clamp obj distance to diagonal of 10 by 10 cube
    obj_min_distance = llmin(obj_min_distance, 10.f * F_SQRT3);

    obj_min_distance += LLViewerCamera::getInstance()->getNear() + (soft_limit ? 0.1f : 0.2f);

    return true;
}