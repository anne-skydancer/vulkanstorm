/** Bounded face traversal, adapted from Alchemy (LGPL-2.1-only). */
#pragma once

#include <algorithm>
#include <cstdint>

// Keep both size and visibility over a rotation. A partial offscreen slice
// must not classify a texture still used by an onscreen face as offscreen.
struct LLTextureFaceScan
{
    std::uint32_t cursor = 0;
    std::uint32_t count = 0;
    float maximum = 0.f;
    float previousMaximum = 0.f;
    bool visible = false;
    bool previousVisible = false;

    void reset() { *this = {}; }

    std::uint32_t begin(std::uint32_t faces)
    {
        if (faces != count)
        {
            reset();
            count = faces;
        }
        return cursor;
    }

    std::uint32_t length() const { return std::min(32u, count - cursor); }

    void finish(std::uint32_t walked, float& size, bool& on_screen)
    {
        maximum = std::max(maximum, size);
        visible = visible || on_screen;
        cursor += walked;
        if (cursor >= count)
        {
            previousMaximum = maximum;
            previousVisible = visible;
            maximum = 0.f;
            visible = false;
            cursor = 0;
        }
        size = std::max(maximum, previousMaximum);
        on_screen = visible || previousVisible;
    }
};
