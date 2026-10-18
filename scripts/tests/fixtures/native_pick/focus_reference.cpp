// Frozen original viewer algorithm; LGPL-2.1, like the viewer.
LLVector3 LLAgentCamera::calcFocusOffset(LLViewerObject *object, LLVector3 original_focus_point, S32 x, S32 y)
{
    LLMatrix4 obj_matrix = object->getRenderMatrix();
    LLQuaternion obj_rot = object->getRenderRotation();
    LLVector3 obj_pos = object->getRenderPosition();

    // if is avatar - don't do any funk heuristics to position the focal point
    // see DEV-30589
    if ((object->isAvatar() && !object->isRoot()) || (object->isAnimatedObject() && object->getControlAvatar()))
    {
        return original_focus_point - obj_pos;
    }
    if (object->isAvatar())
    {
        LLVOAvatar* av = object->asAvatar();
        return original_focus_point - av->getCharacterPosition();
    }

    LLQuaternion inv_obj_rot = ~obj_rot; // get inverse of rotation
    LLVector3 object_extents = object->getScale();

    // make sure they object extents are non-zero
    object_extents.clamp(0.001f, F32_MAX);

    // obj_to_cam_ray is unit vector pointing from object center to camera, in the coordinate frame of the object
    LLVector3 obj_to_cam_ray = obj_pos - LLViewerCamera::getInstance()->getOrigin();
    obj_to_cam_ray.rotVec(inv_obj_rot);
    obj_to_cam_ray.normalize();

    // obj_to_cam_ray_proportions are the (positive) ratios of
    // the obj_to_cam_ray x,y,z components with the x,y,z object dimensions.
    LLVector3 obj_to_cam_ray_proportions;
    obj_to_cam_ray_proportions.mV[VX] = llabs(obj_to_cam_ray.mV[VX] / object_extents.mV[VX]);
    obj_to_cam_ray_proportions.mV[VY] = llabs(obj_to_cam_ray.mV[VY] / object_extents.mV[VY]);
    obj_to_cam_ray_proportions.mV[VZ] = llabs(obj_to_cam_ray.mV[VZ] / object_extents.mV[VZ]);

    // find the largest ratio stored in obj_to_cam_ray_proportions
    // this corresponds to the object's local axial plane (XY, YZ, XZ) that is *most* facing the camera
    LLVector3 longest_object_axis;
    // is x-axis longest?
    if (obj_to_cam_ray_proportions.mV[VX] > obj_to_cam_ray_proportions.mV[VY]
        && obj_to_cam_ray_proportions.mV[VX] > obj_to_cam_ray_proportions.mV[VZ])
    {
        // then grab it
        longest_object_axis.setVec(obj_matrix.getFwdRow4());
    }
    // is y-axis longest?
    else if (obj_to_cam_ray_proportions.mV[VY] > obj_to_cam_ray_proportions.mV[VZ])
    {
        // then grab it
        longest_object_axis.setVec(obj_matrix.getLeftRow4());
    }
    // otherwise, use z axis
    else
    {
        longest_object_axis.setVec(obj_matrix.getUpRow4());
    }

    // Use this axis as the normal to project mouse click on to plane with that normal, at the object center.
    // This generates a point behind the mouse cursor that is approximately in the middle of the object in
    // terms of depth.
    // We do this to allow the camera rotation tool to "tumble" the object by rotating the camera.
    // If the focus point were the object surface under the mouse, camera rotation would introduce an undesirable
    // eccentricity to the object orientation
    LLVector3 focus_plane_normal(longest_object_axis);
    focus_plane_normal.normalize();

    LLVector3d focus_pt_global;
    gViewerWindow->mousePointOnPlaneGlobal(focus_pt_global, x, y, gAgent.getPosGlobalFromAgent(obj_pos), focus_plane_normal);
    LLVector3 focus_pt = gAgent.getPosAgentFromGlobal(focus_pt_global);

    // find vector from camera to focus point in object space
    LLVector3 camera_to_focus_vec = focus_pt - LLViewerCamera::getInstance()->getOrigin();
    camera_to_focus_vec.rotVec(inv_obj_rot);

    // find vector from object origin to focus point in object coordinates
    LLVector3 focus_offset_from_object_center = focus_pt - obj_pos;
    // convert to object-local space
    focus_offset_from_object_center.rotVec(inv_obj_rot);

    // We need to project the focus point back into the bounding box of the focused object.
    // Do this by calculating the XYZ scale factors needed to get focus offset back in bounds along the camera_focus axis
    LLVector3 clip_fraction;

    // for each axis...
    for (U32 axis = VX; axis <= VZ; axis++)
    {
        //...calculate distance that focus offset sits outside of bounding box along that axis...
        //NOTE: dist_out_of_bounds keeps the sign of focus_offset_from_object_center
        F32 dist_out_of_bounds;
        if (focus_offset_from_object_center.mV[axis] > 0.f)
        {
            dist_out_of_bounds = llmax(0.f, focus_offset_from_object_center.mV[axis] - (object_extents.mV[axis] * 0.5f));
        }
        else
        {
            dist_out_of_bounds = llmin(0.f, focus_offset_from_object_center.mV[axis] + (object_extents.mV[axis] * 0.5f));
        }

        //...then calculate the scale factor needed to push camera_to_focus_vec back in bounds along current axis
        if (llabs(camera_to_focus_vec.mV[axis]) < 0.0001f)
        {
            // don't divide by very small number
            clip_fraction.mV[axis] = 0.f;
        }
        else
        {
            clip_fraction.mV[axis] = dist_out_of_bounds / camera_to_focus_vec.mV[axis];
        }
    }

    LLVector3 abs_clip_fraction = clip_fraction;
    abs_clip_fraction.abs();

    // find axis of focus offset that is *most* outside the bounding box and use that to
    // rescale focus offset to inside object extents
    if (abs_clip_fraction.mV[VX] > abs_clip_fraction.mV[VY]
        && abs_clip_fraction.mV[VX] > abs_clip_fraction.mV[VZ])
    {
        focus_offset_from_object_center -= clip_fraction.mV[VX] * camera_to_focus_vec;
    }
    else if (abs_clip_fraction.mV[VY] > abs_clip_fraction.mV[VZ])
    {
        focus_offset_from_object_center -= clip_fraction.mV[VY] * camera_to_focus_vec;
    }
    else
    {
        focus_offset_from_object_center -= clip_fraction.mV[VZ] * camera_to_focus_vec;
    }

    // convert back to world space
    focus_offset_from_object_center.rotVec(obj_rot);

    // now, based on distance of camera from object relative to object size
    // push the focus point towards the near surface of the object when (relatively) close to the objcet
    // or keep the focus point in the object middle when (relatively) far
    // NOTE: leave focus point in middle of avatars, since the behavior you want when alt-zooming on avatars
    // is almost always "tumble about middle" and not "spin around surface point"
    {
        LLVector3 obj_rel = original_focus_point - object->getRenderPosition();

        //now that we have the object relative position, we should bias toward the center of the object
        //based on the distance of the camera to the focus point vs. the distance of the camera to the focus

        F32 relDist = llabs(obj_rel * LLViewerCamera::getInstance()->getAtAxis());
        F32 viewDist = dist_vec(obj_pos + obj_rel, LLViewerCamera::getInstance()->getOrigin());


        LLBBox obj_bbox = object->getBoundingBoxAgent();
        F32 bias = 0.f;

        // virtual_camera_pos is the camera position we are simulating by backing the camera off
        // and adjusting the FOV
        LLVector3 virtual_camera_pos = gAgent.getPosAgentFromGlobal(mFocusTargetGlobal + (getCameraPositionGlobal() - mFocusTargetGlobal) / (1.f + mCameraFOVZoomFactor));

        // if the camera is inside the object (large, hollow objects, for example)
        // leave focus point all the way to destination depth, away from object center
        if(!obj_bbox.containsPointAgent(virtual_camera_pos))
        {
            // perform magic number biasing of focus point towards surface vs. planar center
            bias = clamp_rescale(relDist/viewDist, 0.1f, 0.7f, 0.0f, 1.0f);
            obj_rel = lerp(focus_offset_from_object_center, obj_rel, bias);
        }

        focus_offset_from_object_center = obj_rel;
    }

    return focus_offset_from_object_center;
}
