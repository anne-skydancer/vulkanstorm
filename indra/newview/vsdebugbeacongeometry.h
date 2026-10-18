// Original LLDebugBeacon cross/cube vertex order, shared by both peers. LGPL-2.1.
#pragma once
#include "v3math.h"

template<class Sink>
void vs_debug_beacon_cross(Sink& sink,const LLVector3& center, F32 dx, F32 dy, F32 dz)
{
    sink.vertex3f(center.mV[VX] - dx, center.mV[VY], center.mV[VZ]);
    sink.vertex3f(center.mV[VX] + dx, center.mV[VY], center.mV[VZ]);
    sink.vertex3f(center.mV[VX], center.mV[VY] - dy, center.mV[VZ]);
    sink.vertex3f(center.mV[VX], center.mV[VY] + dy, center.mV[VZ]);
    sink.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] - dz);
    sink.vertex3f(center.mV[VX], center.mV[VY], center.mV[VZ] + dz);
}

template<class Sink>
void vs_debug_beacon_cube(Sink& sink,F32 width, const LLVector3& center)
{
    width = 0.5f * width;
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);

    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);

    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] + width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] + width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] - width ,center.mV[VY] - width,center.mV[VZ] - width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] + width);
    sink.vertex3f(center.mV[VX] + width ,center.mV[VY] - width,center.mV[VZ] - width);
}

// Original LLSky sun/moon seven-line geometry (the UI state is in the caller).
template<class Sink>
void vs_sun_moon_beacon(Sink& sink,const LLVector3& pos_agent,const LLVector3& direction)
{
    LLVector3 pos_end;
    for(S32 i=0;i<3;++i)pos_end.mV[i]=pos_agent.mV[i]+(50*direction.mV[i]);
    vs_debug_beacon_cross(sink,pos_agent,.5f,.5f,.5f);
    vs_debug_beacon_cross(sink,pos_end,2.f,2.f,2.f);
    sink.vertex3fv(pos_agent.mV);sink.vertex3fv(pos_end.mV);
}
