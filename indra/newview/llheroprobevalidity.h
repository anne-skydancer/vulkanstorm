/** Hero probe layout and initialization bookkeeping; no GL dependencies. */
#pragma once

#include <cstdint>

namespace LLHeroProbeValidity
{
constexpr unsigned scratchLevels(unsigned resolution)
{
    if (resolution < 2 || (resolution & (resolution - 1))) return 0;
    unsigned levels = 0;
    while (resolution > 1) { ++levels; resolution >>= 1; }
    return levels;
}

// Nonzero hero weight covers glossiness > .75. Include the upper mip used
// by fractional LOD filtering, while preserving the existing LOD scale.
constexpr unsigned outputLevels(unsigned scratch_levels)
{
    const unsigned required = (scratch_levels + 3) / 4 + 1;
    return scratch_levels < required ? scratch_levels : required;
}

struct Contents
{
    std::uint8_t faces = 0;
    bool ready = false;

    void invalidate() { faces = 0; ready = false; }
    void completeFace(unsigned face) { if (face < 6) faces |= (1u << face); }
    bool canFilter() const { return faces == 0x3f; }
    void publish() { ready = canFilter(); }
};
}
