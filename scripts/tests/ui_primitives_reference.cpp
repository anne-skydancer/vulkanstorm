// Frozen OpenGL reference vertex sequences from a26af168cf; LGPL-2.1.
namespace Reference
{
void gl_arc_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled, F32 start_angle, F32 end_angle)
{
    if (end_angle < start_angle)
    {
        end_angle += F_TWO_PI;
    }

    gGL.pushUIMatrix();
    {
        gGL.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = (end_angle - start_angle) / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = cosf(start_angle) * radius;
        F32 y = sinf(start_angle) * radius;

        if (filled)
        {
            gGL.begin(LLRender::TRIANGLE_FAN);
            gGL.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            gGL.begin(LLRender::LINE_STRIP);
        }

        while( steps-- )
        {
            // Successive rotations
            gGL.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        gGL.end();
    }
    gGL.popUIMatrix();
}

void gl_circle_2d(F32 center_x, F32 center_y, F32 radius, S32 steps, bool filled)
{
    gGL.pushUIMatrix();
    {
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.translateUI(center_x, center_y, 0.f);

        // Inexact, but reasonably fast.
        F32 delta = F_TWO_PI / steps;
        F32 sin_delta = sin( delta );
        F32 cos_delta = cos( delta );
        F32 x = radius;
        F32 y = 0.f;

        if (filled)
        {
            gGL.begin(LLRender::TRIANGLE_FAN);
            gGL.vertex2f(0.f, 0.f);
            // make sure circle is complete
            steps += 1;
        }
        else
        {
            gGL.begin(LLRender::LINE_LOOP);
        }

        while( steps-- )
        {
            // Successive rotations
            gGL.vertex2f( x, y );
            F32 x_new = x * cos_delta - y * sin_delta;
            y = x * sin_delta +  y * cos_delta;
            x = x_new;
        }
        gGL.end();
    }
    gGL.popUIMatrix();
}

void gl_corners_2d(S32 left, S32 top, S32 right, S32 bottom, S32 length, F32 max_frac)
{
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    length = llmin((S32)(max_frac*(right - left)), length);
    length = llmin((S32)(max_frac*(top - bottom)), length);
    gGL.begin(LLRender::LINES);
    gGL.vertex2i(left, top);
    gGL.vertex2i(left + length, top);

    gGL.vertex2i(left, top);
    gGL.vertex2i(left, top - length);

    gGL.vertex2i(left, bottom);
    gGL.vertex2i(left + length, bottom);

    gGL.vertex2i(left, bottom);
    gGL.vertex2i(left, bottom + length);

    gGL.vertex2i(right, top);
    gGL.vertex2i(right - length, top);

    gGL.vertex2i(right, top);
    gGL.vertex2i(right, top - length);

    gGL.vertex2i(right, bottom);
    gGL.vertex2i(right - length, bottom);

    gGL.vertex2i(right, bottom);
    gGL.vertex2i(right, bottom + length);
    gGL.end();
}

void gl_washer_2d(F32 outer_radius, F32 inner_radius, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = F_TWO_PI / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius;
    F32 y1 = 0.f;
    F32 x2 = inner_radius;
    F32 y2 = 0.f;

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    gGL.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            gGL.color4fv(outer_color.mV);
            gGL.vertex2f( x1, y1 );
            gGL.color4fv(inner_color.mV);
            gGL.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    gGL.end();
}

void gl_washer_segment_2d(F32 outer_radius, F32 inner_radius, F32 start_radians, F32 end_radians, S32 steps, const LLColor4& inner_color, const LLColor4& outer_color)
{
    const F32 DELTA = (end_radians - start_radians) / steps;
    const F32 SIN_DELTA = sin( DELTA );
    const F32 COS_DELTA = cos( DELTA );

    F32 x1 = outer_radius * cos( start_radians );
    F32 y1 = outer_radius * sin( start_radians );
    F32 x2 = inner_radius * cos( start_radians );
    F32 y2 = inner_radius * sin( start_radians );

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.begin( LLRender::TRIANGLE_STRIP  );
    {
        steps += 1; // An extra step to close the circle.
        while( steps-- )
        {
            gGL.color4fv(outer_color.mV);
            gGL.vertex2f( x1, y1 );
            gGL.color4fv(inner_color.mV);
            gGL.vertex2f( x2, y2 );

            F32 x1_new = x1 * COS_DELTA - y1 * SIN_DELTA;
            y1 = x1 * SIN_DELTA +  y1 * COS_DELTA;
            x1 = x1_new;

            F32 x2_new = x2 * COS_DELTA - y2 * SIN_DELTA;
            y2 = x2 * SIN_DELTA +  y2 * COS_DELTA;
            x2 = x2_new;
        }
    }
    gGL.end();
}

void gl_rect_2d_simple( S32 width, S32 height )
{
    gGL.begin( LLRender::TRIANGLES );
        gGL.vertex2i(width, height);
        gGL.vertex2i(0, height);
        gGL.vertex2i(0, 0);

        gGL.vertex2i(width, height);
        gGL.vertex2i(0, 0);
        gGL.vertex2i(width, 0);
    gGL.end();
}
}
