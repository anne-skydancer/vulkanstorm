// Shared animesh legal extent/offset and joint scaling rules. LGPL-2.1.
#pragma once
#include "v3math.h"
#include "lljoint.h"
#include <algorithm>
#include <cmath>
struct VSControlConstraints
{
    LLVector3 position;
    F32       scale = 1;
};
inline VSControlConstraints vs_control_constraints(LLVector3 official, const LLVector3* bounds, const LLVector3& previousPosition,
                                                   F32 previousScale, bool attached, F32 maxOffset, F32 maxSize)
{
    VSControlConstraints result;
    if (!bounds || !box_valid_and_non_zero(bounds) || !std::isfinite(previousScale) || previousScale <= 0)
        return result;
    const LLVector3 unshifted[] = { bounds[0] - previousPosition, bounds[1] - previousPosition };
    const LLVector3 dimensions  = bounds[1] - bounds[0];
    const F32       size        = std::max({ dimensions.mV[0], dimensions.mV[1], dimensions.mV[2] });
    if (!attached)
    {
        const LLVector3 offset   = point_to_box_offset(official, unshifted);
        const F32       distance = offset.length();
        maxOffset                = std::max(maxOffset, 0.f);
        if (distance > maxOffset && distance > 0)
            result.position = offset * ((distance - maxOffset) / distance);
    }
    maxSize = std::max(maxSize, 1.f);
    if (size / previousScale > maxSize)
        result.scale = previousScale * maxSize / size;
    return result;
}
inline void vs_scale_avatar_joints(LLJoint* joint, F32 factor)
{
    if (!joint)
        return;
    joint->setScale(factor * joint->getScale());
    for (auto* child : joint->mChildren)
        vs_scale_avatar_joints(child, factor);
}
