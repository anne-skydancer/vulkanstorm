/**
 * Shared reference immediate-mode UI geometry. Both renderer peers execute
 * these original vertex/color sequences; only the submission sink differs.
 * Derived from llrender2dutils.cpp, Copyright Linden Research, Inc.
 * SPDX-License-Identifier: LGPL-2.1-only
 */
#ifndef VS_UI_PRIMITIVE_H
#define VS_UI_PRIMITIVE_H
namespace VSUIPrimitive
{
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
}
#endif
