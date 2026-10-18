void gl_segmented_rect_3d_tex(const LLRectf& clip_rect, const LLRectf& center_uv_rect, const LLRectf& center_draw_rect,
                             const LLVector3& width_vec, const LLVector3& height_vec)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;

    gGL.begin(LLRender::TRIANGLES);
    {
        // draw bottom left
        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3f(0.f, 0.f, 0.f);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3f(0.f, 0.f, 0.f);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        // draw bottom middle
        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw bottom right
        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv(width_vec.mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw left
        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        // draw middle
        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw right
        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mBottom * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw top left
        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        gGL.texCoord2f(clip_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((height_vec).mV);

        // draw top middle
        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        // draw top right
        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mTop * height_vec).mV);

        gGL.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((width_vec + height_vec).mV);

        gGL.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        gGL.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);
    }
    gGL.end();

}
