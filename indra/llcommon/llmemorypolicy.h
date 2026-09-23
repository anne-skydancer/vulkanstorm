/** Runtime memory policy. Adapted from Alchemy (LGPL-2.1-only). */
#pragma once

#include <algorithm>

namespace LLMemoryPolicy
{
inline float pressureFactor(float current, float free_mb, float dt)
{
    const float target = 1.f + std::clamp((256.f - free_mb) / 224.f, 0.f, 1.f);
    if (target < current && free_mb <= 406.f)
    {
        return current;
    }
    const float step = (target > current ? 0.5f : 0.1f) * std::max(dt, 0.f);
    return std::clamp(current + std::clamp(target - current, -step, step), 1.f, 2.f);
}

inline float sceneFactor(float allocated_mb, float physical_mb, float heap_mb,
                         float low_mb, float high_mb, bool automatic)
{
    if (automatic)
    {
        low_mb = std::max(750.f, physical_mb * 0.25f);
        high_mb = std::max(2048.f, physical_mb * 0.6f);
    }
    low_mb = std::max(low_mb, 0.f);
    high_mb = std::max(high_mb, low_mb + 1.f);
    const float cap = std::max(heap_mb * 0.9f, 1.f);
    if (high_mb > cap)
    {
        low_mb *= cap / high_mb;
        high_mb = cap;
    }
    return std::clamp((high_mb - allocated_mb) / std::max(high_mb - low_mb, 1.f), 0.f, 1.f);
}
}
