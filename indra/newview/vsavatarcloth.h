// Reference avatarV.glsl cloth displacement in world-space CPU geometry. LGPL-2.1.
#pragma once
#include "v3math.h"
#include <algorithm>
#include <cmath>
struct VSAvatarCloth
{
    LLVector3 wind, eye, at;
    F32       strength = 0, frequency = 7, phase = 0;
};
inline F32 vs_avatar_cloth(const VSAvatarCloth& state, const LLVector3& position, LLVector3& normal, F32 coverage)
{
    const F32 facing = normal * state.wind;
    const F32 phase  = (facing - .015f * ((position - state.eye) * state.at)) * state.frequency - state.phase;
    auto      wave   = [](F32 angle)
    {
        const F32 square = angle * angle;
        return (((-.00018542f * square + .0083143f) * -square + .166666f) * -square + 1.f) * angle;
    };
    auto period = [](F32 value)
    {
        return (value - std::floor(value)) * 6.28318530f - 3.141592f;
    };
    const F32 bias         = (facing * 2 + 1) * state.strength;
    const F32 primary      = wave(period(phase * .159154943f)) * state.strength + bias + std::min(-.7f * normal.mV[2], .2f) * 1.5f;
    const F32 secondary    = wave(period(phase * .159154943f - 1.5707963f)) * state.strength + bias;
    const F32 displacement = std::max(primary * coverage, -1.f);
    normal += 2.f * (normal + state.wind * std::max(secondary * coverage, -1.f));
    normal.normalize();
    return displacement;
}
