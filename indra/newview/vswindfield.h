// Shared bounded simulator-wind interpolation. LGPL-2.1.
#pragma once
#include "v3math.h"
#include <algorithm>
#include <cmath>
#include <span>
inline LLVector3 vs_wind_velocity(std::span<const float, 256> x, std::span<const float, 256> y, const LLVector3& position, float width,
                                  float height)
{
    if (!position.isFinite() || !std::isfinite(width) || !std::isfinite(height) || width <= 0 || height <= 0)
        return LLVector3::zero;
    float px = std::max(position.mV[0], 0.f), py = std::max(position.mV[1], 0.f);
    if (px >= width)
        px = std::fmod(px, width);
    if (py >= height)
        py = std::fmod(py, height);
    px *= 16 / width;
    py *= 16 / height;
    const unsigned i = std::min(unsigned(px), 15u), j = std::min(unsigned(py), 15u), k = i + j * 16;
    const float    dx = px - i, dy = py - j;
    auto           sample = [&](std::span<const float, 256> values)
    {
        if (i == 15 || j == 15)
            return values[k];
        return values[k] * (1 - dx) * (1 - dy) + values[k + 1] * dx * (1 - dy) + values[k + 16] * dy * (1 - dx) + values[k + 17] * dx * dy;
    };
    return LLVector3(sample(x), sample(y), 0) * 2.f; // WIND_SCALE_HACK in the reference viewer.
}
