void ReferenceSnapshot::draw()
{
    if (getCurrentImage() &&
        mPreviewImageEncoded.notNull() &&
        getSnapshotUpToDate())
    {
        LLColor4 bg_color(0.f, 0.f, 0.3f, 0.4f);
        gl_rect_2d(getRect(), bg_color);
        const LLRect& rect = getImageRect();
        LLRect shadow_rect = rect;
        shadow_rect.stretch(BORDER_WIDTH);
        gl_drop_shadow(shadow_rect.mLeft, shadow_rect.mTop, shadow_rect.mRight, shadow_rect.mBottom, LLColor4(0.f, 0.f, 0.f, mNeedsFlash ? 0.f :0.5f), 10);

        LLColor4 image_color(1.f, 1.f, 1.f, 1.f);
        gGL.color4fv(image_color.mV);
        gGL.getTexUnit(0)->bind(getCurrentImage());
        // calculate UV scale
        F32 uv_width = isImageScaled() ? 1.f : llmin((F32)getWidth() / (F32)getCurrentImage()->getWidth(), 1.f);
        F32 uv_height = isImageScaled() ? 1.f : llmin((F32)getHeight() / (F32)getCurrentImage()->getHeight(), 1.f);
        gGL.pushMatrix();
        {
            gGL.translatef((F32)rect.mLeft, (F32)rect.mBottom + TOP_PANEL_HEIGHT, 0.f);
            gGL.begin(LLRender::TRIANGLES);
            {
                gGL.texCoord2f(uv_width, uv_height);
                gGL.vertex2i(rect.getWidth(), rect.getHeight());

                gGL.texCoord2f(0.f, uv_height);
                gGL.vertex2i(0, rect.getHeight());

                gGL.texCoord2f(0.f, 0.f);
                gGL.vertex2i(0, 0);


                gGL.texCoord2f(uv_width, uv_height);
                gGL.vertex2i(rect.getWidth(), rect.getHeight());

                gGL.texCoord2f(0.f, 0.f);
                gGL.vertex2i(0, 0);

                gGL.texCoord2f(uv_width, 0.f);
                gGL.vertex2i(rect.getWidth(), 0);
            }
            gGL.end();
        }
        gGL.popMatrix();

        gGL.color4f(1.f, 1.f, 1.f, mFlashAlpha);
        gl_rect_2d(getRect());
        if (mNeedsFlash)
        {
            if (mFlashAlpha < 1.f)
            {
                mFlashAlpha = lerp(mFlashAlpha, 1.f, LLCriticalDamp::getInterpolant(0.02f));
            }
            else
            {
                mNeedsFlash = false;
            }
        }
        else
        {
            mFlashAlpha = lerp(mFlashAlpha, 0.f, LLCriticalDamp::getInterpolant(0.15f));
        }

        // Draw shining animation if appropriate.
        if (mShineCountdown > 0)
        {
            mShineCountdown--;
            if (mShineCountdown == 0)
            {
                mShineAnimTimer.start();
            }
        }
        else if (mShineAnimTimer.getStarted())
        {
            LL_DEBUGS("Snapshot") << "Drawing shining animation" << LL_ENDL;
            F32 shine_interp = llmin(1.f, mShineAnimTimer.getElapsedTimeF32() / SHINE_TIME);

            // draw "shine" effect
            LLRect local_rect(0, getRect().getHeight() + TOP_PANEL_HEIGHT, getRect().getWidth(), 0);
            LLLocalClipRect clip(local_rect);
            {
                // draw diagonal stripe with gradient that passes over screen
                S32 x1 = gViewerWindow->getWindowWidthScaled() * ll_round((clamp_rescale(shine_interp, 0.f, 1.f, -1.f - SHINE_WIDTH, 1.f)));
                S32 x2 = x1 + ll_round(gViewerWindow->getWindowWidthScaled() * SHINE_WIDTH);
                S32 x3 = x2 + ll_round(gViewerWindow->getWindowWidthScaled() * SHINE_WIDTH);
                S32 y1 = 0;
                S32 y2 = gViewerWindow->getWindowHeightScaled() + TOP_PANEL_HEIGHT;

                gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                gGL.begin(LLRender::TRIANGLES);
                {
                    gGL.color4f(1.f, 1.f, 1.f, 0.f);
                    gGL.vertex2i(x1, y1);
                    gGL.vertex2i(x1 + gViewerWindow->getWindowWidthScaled(), y2);
                    gGL.color4f(1.f, 1.f, 1.f, SHINE_OPACITY);
                    gGL.vertex2i(x2 + gViewerWindow->getWindowWidthScaled(), y2);

                    gGL.color4f(1.f, 1.f, 1.f, 0.f);
                    gGL.vertex2i(x1, y1);
                    gGL.color4f(1.f, 1.f, 1.f, SHINE_OPACITY);
                    gGL.vertex2i(x2 + gViewerWindow->getWindowWidthScaled(), y2);
                    gGL.vertex2i(x2, y1);


                    gGL.color4f(1.f, 1.f, 1.f, SHINE_OPACITY);
                    gGL.vertex2i(x2, y1);
                    gGL.vertex2i(x2 + gViewerWindow->getWindowWidthScaled(), y2);
                    gGL.color4f(1.f, 1.f, 1.f, 0.f);
                    gGL.vertex2i(x3 + gViewerWindow->getWindowWidthScaled(), y2);

                    gGL.color4f(1.f, 1.f, 1.f, SHINE_OPACITY);
                    gGL.vertex2i(x2, y1);
                    gGL.color4f(1.f, 1.f, 1.f, 0.f);
                    gGL.vertex2i(x3 + gViewerWindow->getWindowWidthScaled(), y2);
                    gGL.vertex2i(x3, y1);
                }
                gGL.end();
            }

            // if we're at the end of the animation, stop
            if (shine_interp >= 1.f)
            {
                mShineAnimTimer.stop();
            }
        }
    }

    // draw old image dropping away
    if (mFallAnimTimer.getStarted())
    {
        S32 old_image_index = (mCurImageIndex + 1) % 2;
        if (mViewerImage[old_image_index].notNull() && mFallAnimTimer.getElapsedTimeF32() < FALL_TIME)
        {
            LL_DEBUGS("Snapshot") << "Drawing fall animation" << LL_ENDL;
            F32 fall_interp = mFallAnimTimer.getElapsedTimeF32() / FALL_TIME;
            F32 alpha = clamp_rescale(fall_interp, 0.f, 1.f, 0.8f, 0.4f);
            LLColor4 image_color(1.f, 1.f, 1.f, alpha);
            gGL.color4fv(image_color.mV);
            gGL.getTexUnit(0)->bind(mViewerImage[old_image_index]);
            // calculate UV scale
            // *FIX get this to work with old image
            bool rescale = !mImageScaled[old_image_index] && mViewerImage[mCurImageIndex].notNull();
            F32 uv_width = rescale ? llmin((F32)mWidth[old_image_index] / (F32)mViewerImage[mCurImageIndex]->getWidth(), 1.f) : 1.f;
            F32 uv_height = rescale ? llmin((F32)mHeight[old_image_index] / (F32)mViewerImage[mCurImageIndex]->getHeight(), 1.f) : 1.f;
            gGL.pushMatrix();
            {
                LLRect& rect = mImageRect[old_image_index];
                gGL.translatef((F32)rect.mLeft, (F32)rect.mBottom - ll_round(getRect().getHeight() * 2.f * (fall_interp * fall_interp)), 0.f);
                gGL.rotatef(-45.f * fall_interp, 0.f, 0.f, 1.f);
                gGL.begin(LLRender::TRIANGLES);
                {
                    gGL.texCoord2f(uv_width, uv_height);
                    gGL.vertex2i(rect.getWidth(), rect.getHeight());

                    gGL.texCoord2f(0.f, uv_height);
                    gGL.vertex2i(0, rect.getHeight());

                    gGL.texCoord2f(0.f, 0.f);
                    gGL.vertex2i(0, 0);

                    gGL.texCoord2f(uv_width, uv_height);
                    gGL.vertex2i(rect.getWidth(), rect.getHeight());

                    gGL.texCoord2f(0.f, 0.f);
                    gGL.vertex2i(0, 0);

                    gGL.texCoord2f(uv_width, 0.f);
                    gGL.vertex2i(rect.getWidth(), 0);
                }
                gGL.end();
            }
            gGL.popMatrix();
        }
    }
}