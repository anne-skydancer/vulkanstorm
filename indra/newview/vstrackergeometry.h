// Original LLTracker geometry and pulse order shared across peers. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "v4color.h"
#include "llrender.h"
#include <string>
inline F32 vs_tracker_pulse(F32 t,F32 z,bool tracking_avatar,const std::string& direction,bool cheesy)
{
    if (!cheesy)
    {
        return 0.f;
    }

    t *= F_PI;
    if ("DOWN" == direction)
    {
        z += t*64.f - 256.f;
    }
    else
    {
    z -= t*64.f - 256.f;
    }

    F32 a = cosf(z*F_PI/512.f)*10.0f;
    a = llmax(a, 9.9f);
    a -= 9.9f;
    a *= 10.f;
    return a;
}
template<class Sink>
void vs_tracker_shockwave(Sink& sink,F32 center_z,F32 t,S32 steps,LLColor4 color,bool cheesy)
{
    if (!cheesy)
    {
        return;
    }

    t *= 0.6284f/F_PI;

    t -= (F32) (S32) t;

    t = llmax(t, 0.5f);
    t -= 0.5f;
    t *= 2.0f;

    F32 radius = t*16536.f;

    // Inexact, but reasonably fast.
    F32 delta = F_TWO_PI / steps;
    F32 sin_delta = sin( delta );
    F32 cos_delta = cos( delta );
    F32 x = radius;
    F32 y = 0.f;

    LLColor4 ccol = LLColor4(1,1,1,(1.f-t)*0.25f);
    sink.begin(LLRender::TRIANGLE_FAN);
    sink.color4fv(ccol.mV);
    sink.vertex3f(0.f, 0.f, center_z);
    // make sure circle is complete
    steps += 1;

    color.mV[3] = (1.f-t*t);

    sink.color4fv(color.mV);
    while( steps-- )
    {
        // Successive rotations
        sink.vertex3f( x, y, center_z );
        F32 x_new = x * cos_delta - y * sin_delta;
        y = x * sin_delta +  y * cos_delta;
        x = x_new;
    }
    sink.end();
}
template<class Sink,class Clock>
void vs_tracker_beam(Sink& sink,LLVector3 pos_agent,const std::string& direction,LLColor4 fogged_color,F32 dist,Clock&& clock,const LLVector3& leftAxis,bool trackingAvatar,bool cheesy)
{
    const F32 MAX_HEIGHT = 5020.f;
    const U32 BEACON_ROWS = 256;

    U32 nRows;
    F32 height;
    F32 rowHeight;

    LLColor4 c_col, col_next, col_edge, col_edge_next;

    sink.matrixMode(LLRender::MM_MODELVIEW);
    sink.pushMatrix();

    if ("DOWN" == direction)
    {
        sink.translatef(pos_agent.mV[0], pos_agent.mV[1], pos_agent.mV[2]);
        vs_tracker_shockwave(sink,1024.f,clock(),32,fogged_color,cheesy);
        height = MAX_HEIGHT - pos_agent.mV[2];
    }
    else
    {
        sink.translatef(pos_agent.mV[0], pos_agent.mV[1], 0);
        height = pos_agent.mV[2];
    }

    // <FS> [FIRE-35333] OpenSim allows Z coordinates to be negative based on MinSimHeight
    if (height < 0.f)
        nRows = 0;
    else
        nRows = (U32)ceil((BEACON_ROWS * height) / MAX_HEIGHT);
    // </FS>
    if(nRows<2) nRows=2;
    rowHeight = height / nRows;

    sink.color4fv(fogged_color.mV);

    LLVector3 x_axis = leftAxis;
    F32 t = clock();

    F32 x = x_axis.mV[0];
    F32 y = x_axis.mV[1];
    // <FS> [FIRE-35333] OpenSim allows Z coordinates to be negative based on MinSimHeight
    //F32 z = 0.f;
    F32 z = llmin(height, 0.f);
    // </FS>
    F32 z_next;

    F32 a,an;
    F32 xa,xan;
    F32 ya,yan;

    bool tracking_avatar = trackingAvatar;

    sink.begin(LLRender::TRIANGLES);

    for (U32 i = 0; i < nRows; i++)
    {
        z_next = z + rowHeight;

        a = vs_tracker_pulse(t,z,tracking_avatar,direction,cheesy);
        an = vs_tracker_pulse(t,z_next,tracking_avatar,direction,cheesy);

        c_col = fogged_color + LLColor4(a, a, a, a);
        col_next = fogged_color + LLColor4(an, an, an, an);
        col_edge = fogged_color * LLColor4(a, a, a, 0.0f);
        col_edge_next = fogged_color * LLColor4(an, an, an, 0.0f);

        a = a + a + 1.f;
        an = an + an + 1.f;

        xa = x*a;
        ya = y*a;
        xan = x*an;
        yan = y*an;

        sink.color4fv(col_edge.mV);
        sink.vertex3f(-xa, -ya, z);

        sink.color4fv(col_next.mV);
        sink.vertex3f(0, 0, z_next);

        sink.color4fv(col_edge_next.mV);
        sink.vertex3f(-xan, -yan, z_next);


        sink.color4fv(col_edge.mV);
        sink.vertex3f(-xa, -ya, z);

        sink.color4fv(c_col.mV);
        sink.vertex3f(0, 0, z);

        sink.color4fv(col_next.mV);
        sink.vertex3f(0, 0, z_next);


        sink.color4fv(c_col.mV);
        sink.vertex3f(0, 0, z);

        sink.color4fv(col_edge_next.mV);
        sink.vertex3f(xan, yan, z_next);

        sink.color4fv(col_next.mV);
        sink.vertex3f(0, 0, z_next);


        sink.color4fv(c_col.mV);
        sink.vertex3f(0, 0, z);

        sink.color4fv(col_edge.mV);
        sink.vertex3f(xa, ya, z);

        sink.color4fv(col_edge_next.mV);
        sink.vertex3f(xan, yan, z_next);

        z += rowHeight;
    }

    sink.end();
    sink.popMatrix();
}
