// Shared system-avatar body LOD policy from LLVOAvatar::updateJointLODs.
// Copyright (c) Linden Research, Inc. LGPL-2.1.
#pragma once
#include <algorithm>

inline float vs_avatar_lod_area(float pixelArea, float factor, unsigned visibleAvatars, bool self, bool fullDetail)
{
    if (fullDetail)
        return 100000000.f;
    if (self)
        return pixelArea * .16f;
    const float lod     = factor * .7f + .3f;
    const float minimum = .25f + std::clamp(factor, 0.f, 1.f) * (.6f - .25f);
    const float load    = 1.f + std::clamp((float(visibleAvatars) - 8.f) / 17.f, 0.f, 1.f) * (minimum - 1.f);
    return pixelArea * .16f * lod * lod * load * load;
}
