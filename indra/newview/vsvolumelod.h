// Shared volume LOD selection from LLVOVolume. Copyright Linden Research, Inc.
// LGPL-2.1.
#pragma once
#include "llvolumemgr.h"
#include "llmath.h"
#include <cmath>

inline F32 vs_volume_lod_distance(F32 distance, F32 factor, F32 distanceFactor)
{
    distance *= distanceFactor;
    const F32 ramp = factor * 2;
    if (ramp > 0 && distance < ramp)
    {
        distance *= 1.f / ramp;
        distance *= distance;
        distance *= ramp;
    }
    return distance * (F_PI / 3.f);
}

inline S32 vs_volume_lod_detail(F32 distance, F32 radius, F32 factor, bool dynamic)
{
    if (dynamic)
        return LLVolumeLODGroup::getDetailFromTan(ll_round((factor * radius) / distance, .01f));
    return S32(llclamp(std::sqrt(radius) * factor * 4.f, 0.f, 3.f));
}

inline F32 vs_volume_rigged_lod_radius(F32 diagonal, bool controlAvatar)
{
    // Preserve the reference's historical full diagonal for resident rigging.
    return diagonal * (controlAvatar ? .5f : 1.f);
}
