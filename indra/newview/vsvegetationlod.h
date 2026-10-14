// Shared vegetation LOD policy from LLVOTree/LLVOGrass. Copyright Linden Research, Inc.
// LGPL-2.1.
#pragma once
#include "vsvolumelod.h"

inline bool vs_vegetation_stopped(F32 factor)
{ return factor < F_ALMOST_ZERO; }

inline S32 vs_tree_lod(F32 appAngle, F32 factor, F32 distance, F32 diameter, F32 billboardScale, F32 billboardRatio, F32 distanceFactor)
{
    if (vs_vegetation_stopped(factor))
        return 4;
    const F32     angle = appAngle * factor;
    constexpr F32 thresholds[]{ 30, 20, 15, F_ALMOST_ZERO };
    S32           detail = 4;
    for (S32 level = 0; level < 4; ++level)
        if (angle > thresholds[level])
        {
            detail = level;
            break;
        }
    const F32 size             = billboardScale * billboardRatio * diameter;
    const F32 adjustedDistance = llmax(distance * distanceFactor * (F_PI / 3.f), .01f);
    const S32 volume           = LLVolumeLODGroup::getDetailFromTan(ll_round(factor * 64 * size / adjustedDistance, .01f));
    return llmin(4, llmax(detail, 3 - volume));
}
