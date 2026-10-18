// Frozen LLHUDIcon::render geometry/lifetime from OpenGL peer, LGPL-2.1.
bool LLHUDIcon::referenceQuad(std::array<LLVector3,4>& quad,F32& alpha)
{
    if (mHidden)
        return false;

    if (mSourceObject.isNull() || mImagep.isNull())
    {
        markDead();
        return false;
    }

    LLVector3 obj_position = mSourceObject->getRenderPosition();

    // put icon above object, and in front
    // RN: don't use drawable radius, it's fricking HUGE
    LLViewerCamera* camera = LLViewerCamera::getInstance();
    LLVector3 icon_relative_pos = (camera->getUpAxis() * ~mSourceObject->getRenderRotation());
    icon_relative_pos.abs();

    F32 distance_scale = llmin(mSourceObject->getScale().mV[VX] / icon_relative_pos.mV[VX],
        mSourceObject->getScale().mV[VY] / icon_relative_pos.mV[VY],
        mSourceObject->getScale().mV[VZ] / icon_relative_pos.mV[VZ]);
    F32 up_distance = 0.5f * distance_scale;
    LLVector3 icon_position = obj_position + (up_distance * camera->getUpAxis()) * 1.2f;

    LLVector3 icon_to_cam = LLViewerCamera::getInstance()->getOrigin() - icon_position;
    icon_to_cam.normVec();

    icon_position += icon_to_cam * mSourceObject->mDrawable->getRadius() * 1.1f;

    mDistance = dist_vec(icon_position, camera->getOrigin());

    F32 alpha_factor = clamp_rescale(mDistance, DIST_START_FADE, DIST_END_FADE, 1.f, 0.f);

    LLVector3 x_pixel_vec;
    LLVector3 y_pixel_vec;

    camera->getPixelVectors(icon_position, y_pixel_vec, x_pixel_vec);

    F32 scale_factor = 1.f;
    if (mAnimTimer.getElapsedTimeF32() < ANIM_TIME)
    {
        scale_factor = llmax(0.f, calc_bouncy_animation(mAnimTimer.getElapsedTimeF32() / ANIM_TIME));
    }

    F32 time_elapsed = mLifeTimer.getElapsedTimeF32();
    if (time_elapsed > MAX_VISIBLE_TIME)
    {
        markDead();
        return false;
    }

    if (time_elapsed > MAX_VISIBLE_TIME - FADE_OUT_TIME)
    {
        alpha_factor *= clamp_rescale(time_elapsed, MAX_VISIBLE_TIME - FADE_OUT_TIME, MAX_VISIBLE_TIME, 1.f, 0.f);
    }

    F32 image_aspect = (F32)mImagep->getFullWidth() / (F32)mImagep->getFullHeight() ;
    LLVector3 x_scale = image_aspect * (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * x_pixel_vec;
    LLVector3 y_scale = (F32)gViewerWindow->getWindowHeightScaled() * mScale * scale_factor * y_pixel_vec;

    LLVector3 lower_left = icon_position - (x_scale * 0.5f);
    LLVector3 lower_right = icon_position + (x_scale * 0.5f);
    LLVector3 upper_left = icon_position - (x_scale * 0.5f) + y_scale;
    LLVector3 upper_right = icon_position + (x_scale * 0.5f) + y_scale;

    quad={lower_left,lower_right,upper_left,upper_right};alpha=alpha_factor;return true;
}
