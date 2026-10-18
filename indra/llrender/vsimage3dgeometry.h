/**
 * @file vsimage3dgeometry.h
 * @brief GL function implementations for immediate-mode gl drawing.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */
// Shared segmented world-billboard image geometry. LGPL-2.1.
#pragma once
#include "llrect.h"
#include "v3math.h"
// Emission order and arithmetic are the original gl_segmented_rect_3d_tex.
// The sink implements texCoord2f, vertex3f and vertex3fv; it owns publication.
template<class Sink>
void vs_segmented_image_3d(Sink& sink,const LLRectf& clip_rect,
    const LLRectf& center_uv_rect,const LLRectf& center_draw_rect,
    const LLVector3& width_vec,const LLVector3& height_vec)
{

    {
        // draw bottom left
        sink.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        sink.vertex3f(0.f, 0.f, 0.f);

        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, clip_rect.mBottom);
        sink.vertex3f(0.f, 0.f, 0.f);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        // draw bottom middle
        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw bottom right
        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        sink.texCoord2f(clip_rect.mRight, clip_rect.mBottom);
        sink.vertex3fv(width_vec.mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec).mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        // draw left
        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        // draw middle
        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw right
        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mBottom);
        sink.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mBottom * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        // draw top left
        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        sink.texCoord2f(clip_rect.mLeft, clip_rect.mTop);
        sink.vertex3fv((height_vec).mV);

        // draw top middle
        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight* width_vec + height_vec).mV);

        sink.texCoord2f(center_uv_rect.mLeft, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mLeft * width_vec + height_vec).mV);

        // draw top right
        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        sink.vertex3fv((width_vec + height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, center_uv_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight* width_vec + center_draw_rect.mTop * height_vec).mV);

        sink.texCoord2f(clip_rect.mRight, clip_rect.mTop);
        sink.vertex3fv((width_vec + height_vec).mV);

        sink.texCoord2f(center_uv_rect.mRight, clip_rect.mTop);
        sink.vertex3fv((center_draw_rect.mRight * width_vec + height_vec).mV);
    }



}
