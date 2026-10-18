/**
 * Shared reference immediate-mode UI geometry. Both renderer peers execute
 * these original vertex/color sequences; only the submission sink differs.
 * Derived from llrender2dutils.cpp, Copyright Linden Research, Inc.
 * SPDX-License-Identifier: LGPL-2.1-only
 */
#ifndef VS_UI_PRIMITIVE_H
#define VS_UI_PRIMITIVE_H
#include "llrender.h"
#include "llrender2dutils.h"
#include <stdexcept>
namespace VSUIPrimitive
{
class NativePrimitive
{
    using Vertex = std::array<F32, 6>;
    U32 mMode = LLRender::TRIANGLES;
    std::size_t mCount = 0;
    Vertex mFirst{}, mPrevious{}, mBeforePrevious{};
    std::array<F32, 2> mOffset{};
    std::array<F32, 2> mSavedOffset{};
    bool mPushed = false;

    static void line(const Vertex& a, const Vertex& b) { LLRender2D::nativeLine(a, b); }
public:
    void pushUIMatrix()
    {
        if (mPushed) throw std::logic_error("Nested UI primitive transform");
        mSavedOffset = mOffset; mPushed = true;
    }
    void popUIMatrix()
    {
        if (!mPushed) throw std::logic_error("Unbalanced UI primitive transform");
        mOffset = mSavedOffset; mPushed = false;
    }
    void translateUI(F32 x, F32 y, F32 z)
    {
        if (z != 0.f) throw std::logic_error("UI primitive requires a planar transform");
        mOffset[0] += x; mOffset[1] += y;
    }
    void color4fv(const F32* color) { gGL.color4fv(color); }
    void begin(U32 mode) { mMode = mode; mCount = 0; }
    void vertex2fv(const F32* value) { vertex2f(value[0],value[1]); }
    void vertex2i(S32 x, S32 y) { vertex2f(F32(x), F32(y)); }
    void vertex2f(F32 x, F32 y)
    {
        const auto& c = LLRender2D::nativeColor();
        const Vertex v{x + mOffset[0], y + mOffset[1], c.mV[0], c.mV[1], c.mV[2], c.mV[3]};
        switch (mMode)
        {
        case LLRender::TRIANGLE_FAN:
            if (mCount >= 2) LLRender2D::nativeTriangle({mFirst, mPrevious, v});
            break;
        case LLRender::TRIANGLE_STRIP:
            if (mCount >= 2)
            {
                if (mCount & 1) LLRender2D::nativeTriangle({mPrevious, mBeforePrevious, v});
                else LLRender2D::nativeTriangle({mBeforePrevious, mPrevious, v});
            }
            break;
        case LLRender::TRIANGLES:
            if (mCount % 3 == 2) LLRender2D::nativeTriangle({mBeforePrevious, mPrevious, v});
            break;
        case LLRender::LINES:
            if (mCount & 1) line(mPrevious, v);
            break;
        case LLRender::LINE_STRIP:
        case LLRender::LINE_LOOP:
            if (mCount) line(mPrevious, v);
            break;
        default: throw std::logic_error("Unsupported UI primitive topology");
        }
        if (!mCount) mFirst = v;
        mBeforePrevious = mPrevious; mPrevious = v; ++mCount;
    }
    void end()
    {
        if (mMode == LLRender::LINE_LOOP && mCount > 1) line(mPrevious, mFirst);
    }
};

// Scoped submission of the same immediate UI vertices through either peer.
// This is deliberately local to 2D UI; gGL.begin still rejects native-world
// geometry accidentally submitted by an unwired OpenGL controller.
class Draw
{
    const bool mNative=LLRender2D::isNativeUI();
    NativePrimitive mPrimitive;
public:
    void begin(U32 mode) { if(mNative)mPrimitive.begin(mode);else gGL.begin(mode); }
    void end() { if(mNative)mPrimitive.end();else gGL.end(); }
    void vertex2f(F32 x,F32 y) {if(mNative)mPrimitive.vertex2f(x,y);else gGL.vertex2f(x,y);}
    void vertex2i(S32 x,S32 y) {if(mNative)mPrimitive.vertex2i(x,y);else gGL.vertex2i(x,y);}
    void vertex2fv(const F32* v) {vertex2f(v[0],v[1]);}
};

template<class Draw>
void gl_arc_2d(Draw& draw, F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle)
{
    if (end_angle < start_angle)
    {
        end_angle += F_TWO_PI;
    }

    draw.pushUIMatrix();
    {
        draw.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = (end_angle - start_angle) / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = cosf(start_angle) * radius;
        F32 y = sinf(start_angle) * radius;

        if (filled)
        {
            draw.begin(LLRender::TRIANGLE_FAN);
            draw.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            draw.begin(LLRender::LINE_STRIP);
        }

        while( steps-- )
        {
            // Successive rotations
            draw.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        draw.end();
    }
    draw.popUIMatrix();
}

template<class Draw>
void gl_circle_2d(Draw& draw, F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled)
{
    draw.pushUIMatrix();
    {
        draw.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = F_TWO_PI / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = radius;
        F32 y = 0.f;

        if (filled)
        {
            draw.begin(LLRender::TRIANGLE_FAN);
            draw.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            draw.begin(LLRender::LINE_LOOP);
        }

        while( steps-- )
        {
            // Successive rotations
            draw.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        draw.end();
    }
    draw.popUIMatrix();
}

template<class Draw>
void gl_corners_2d(Draw& draw, S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac)
{

    length = llmin((S32)(max_frac*(right - left)), length);
    length = llmin((S32)(max_frac*(top - bottom)), length);
    draw.begin(LLRender::LINES);
    draw.vertex2i(left, top);
    draw.vertex2i(left + length, top);

    draw.vertex2i(left, top);
    draw.vertex2i(left, top - length);

    draw.vertex2i(left, bottom);
    draw.vertex2i(left + length, bottom);

    draw.vertex2i(left, bottom);
    draw.vertex2i(left, bottom + length);

    draw.vertex2i(right, top);
    draw.vertex2i(right - length, top);

    draw.vertex2i(right, top);
    draw.vertex2i(right, top - length);

    draw.vertex2i(right, bottom);
    draw.vertex2i(right - length, bottom);

    draw.vertex2i(right, bottom);
    draw.vertex2i(right, bottom + length);
    draw.end();
}

template<class Draw>
void gl_washer_2d(Draw& draw, F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = F_TWO_PI / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius;
    F32 y1 = 0.f;
    F32 x2 = inner_radius;
    F32 y2 = 0.f;


    draw.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            draw.color4fv(outer_color.mV);
            draw.vertex2f( x1, y1 );
            draw.color4fv(inner_color.mV);
            draw.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    draw.end();
}

template<class Draw>
void gl_washer_segment_2d(Draw& draw, F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = (end_radians - start_radians) / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius * cos( start_radians );
    F32 y1 = outer_radius * sin( start_radians );
    F32 x2 = inner_radius * cos( start_radians );
    F32 y2 = inner_radius * sin( start_radians );

    draw.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            draw.color4fv(outer_color.mV);
            draw.vertex2f( x1, y1 );
            draw.color4fv(inner_color.mV);
            draw.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    draw.end();
}

template<class Draw>
void gl_rect_2d_simple(Draw& draw, S32 width, S32 height)
{
    draw.begin( LLRender::TRIANGLES );
        draw.vertex2i(width, height);
        draw.vertex2i(0, height);
        draw.vertex2i(0, 0);

        draw.vertex2i(width, height);
        draw.vertex2i(0, 0);
        draw.vertex2i(width, 0);
    draw.end();
}
template<class Draw>
void segmented_fragment(Draw& draw,const LLRect& rect,
    const S32 texture_width,
    const S32 texture_height,
    const S32 border_size,
    const F32 start_fragment,
    const F32 end_fragment,
    const U32 edges)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_UI;
    const S32 left = rect.mLeft;
    const S32 right = rect.mRight;
    const S32 top = rect.mTop;
    const S32 bottom = rect.mBottom;
    S32 width = llabs(right - left);
    S32 height = llabs(top - bottom);

    draw.pushUIMatrix();

    draw.translateUI((F32)left, (F32)bottom, 0.f);
    LLVector2 border_uv_scale((F32)border_size / (F32)texture_width, (F32)border_size / (F32)texture_height);

    if (border_uv_scale.mV[VX] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VX];
    }
    if (border_uv_scale.mV[VY] > 0.5f)
    {
        border_uv_scale *= 0.5f / border_uv_scale.mV[VY];
    }

    F32 border_scale = llmin((F32)border_size, (F32)width * 0.5f, (F32)height * 0.5f);
    LLVector2 border_width_left = ((edges & (~(U32)ROUNDED_RECT_RIGHT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_width_right = ((edges & (~(U32)ROUNDED_RECT_LEFT)) != 0) ? LLVector2(border_scale, 0.f) : LLVector2::zero;
    LLVector2 border_height_bottom = ((edges & (~(U32)ROUNDED_RECT_TOP)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 border_height_top = ((edges & (~(U32)ROUNDED_RECT_BOTTOM)) != 0) ? LLVector2(0.f, border_scale) : LLVector2::zero;
    LLVector2 width_vec((F32)width, 0.f);
    LLVector2 height_vec(0.f, (F32)height);

    F32 middle_start = border_scale / (F32)width;
    F32 middle_end = 1.f - middle_start;

    F32 u_min;
    F32 u_max;
    LLVector2 x_min;
    LLVector2 x_max;

    draw.begin(LLRender::TRIANGLES);
    {
        if (start_fragment < middle_start)
        {
            u_min = (start_fragment / middle_start)         * border_uv_scale.mV[VX];
            u_max = llmin(end_fragment / middle_start, 1.f) * border_uv_scale.mV[VX];
            x_min = (start_fragment / middle_start)         * border_width_left;
            x_max = llmin(end_fragment / middle_start, 1.f) * border_width_left;

            // draw bottom left
            draw.texCoord2f(u_min, 0.f);
            draw.vertex2fv(x_min.mV);

            draw.texCoord2f(border_uv_scale.mV[VX], 0.f);
            draw.vertex2fv(x_max.mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_min, 0.f);
            draw.vertex2fv(x_min.mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            // draw left
            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top left
            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(u_min, 1.f);
            draw.vertex2fv((x_min + height_vec).mV);
        }

        if (end_fragment > middle_start || start_fragment < middle_end)
        {
            x_min = border_width_left + ((llclamp(start_fragment, middle_start, middle_end) - middle_start)) * width_vec;
            x_max = border_width_left + ((llclamp(end_fragment, middle_start, middle_end) - middle_start)) * width_vec;

            // draw bottom middle
            draw.texCoord2f(border_uv_scale.mV[VX], 0.f);
            draw.vertex2fv(x_min.mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 0.f);
            draw.vertex2fv((x_max).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], 0.f);
            draw.vertex2fv(x_min.mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            // draw middle
            draw.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top middle
            draw.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(1.f - border_uv_scale.mV[VX], 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(border_uv_scale.mV[VX], 1.f);
            draw.vertex2fv((x_min + height_vec).mV);
        }

        if (end_fragment > middle_end)
        {
            u_min = 1.f         - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            u_max = 1.f         - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_uv_scale.mV[VX]);
            x_min = width_vec   - ((1.f - llmax(0.f, (start_fragment - middle_end) / middle_start)) * border_width_right);
            x_max = width_vec   - ((1.f - ((end_fragment - middle_end) / middle_start)) * border_width_right);

            // draw bottom right
            draw.texCoord2f(u_min, 0.f);
            draw.vertex2fv((x_min).mV);

            draw.texCoord2f(u_max, 0.f);
            draw.vertex2fv(x_max.mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_min, 0.f);
            draw.vertex2fv((x_min).mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            // draw right
            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(u_max, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + border_height_bottom).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_min, border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + border_height_bottom).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            // draw top right
            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_max + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(u_min, 1.f - border_uv_scale.mV[VY]);
            draw.vertex2fv((x_min + height_vec - border_height_top).mV);

            draw.texCoord2f(u_max, 1.f);
            draw.vertex2fv((x_max + height_vec).mV);

            draw.texCoord2f(u_min, 1.f);
            draw.vertex2fv((x_min + height_vec).mV);
        }
    }
    draw.end();

    draw.popUIMatrix();
}

}
#endif
