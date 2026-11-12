/**
 * @file lllocallighttable.h
 * @brief The light pool's local light table and its 32 x 32 screen tile list, without a renderer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#ifndef LL_LLLOCALLIGHTTABLE_H
#define LL_LLLOCALLIGHTTABLE_H

// Header-only and free of GL and viewer types, so that indra/newview/tests/lllocallighttable_test.cpp can check it
// standalone. The light pool publishes what the shadow pool's local light ratio pass (localLightRatioF.glsl) reads:
//  - the table: one row per local light, ROW_TEXELS RGBA32F texels (see LLLocalLightRow), uploaded as a 2D texture;
//  - the tile list: for each 32 x 32 pixel tile of the screen, the lights whose screen rectangle covers it, as
//    TILE_ROW bytes of an R8UI texture tilesX * TILE_ROW wide and tilesY high (tile (tx, ty) from x = TILE_ROW * tx at
//    row ty, which is the order data() has): the count, then the light indices. At most MAX_PER_TILE lights are listed per tile; the
//    lights that did not fit are counted (overflow) and leave the pixel's weight sum a little short, never wrong
//    otherwise. The lights that cast (the mapped projectors) are added first by the caller.
//
// Texel layout of a table row (all view space):
//   0  centre.xyz, size                  size is the light's radius (volume->getLightRadius() * 1.5)
//   1  colour.rgb (linear), falloff
//   2  origin.xyz, kind                  the projector's eye (its pass's proj_origin) or the centre; kind 1 a projector
//   3  slot, changed, 0, 0               slot: the projector map (0, 1) or -1; changed: its light moved since last frame
//   4-7 the projector matrix (view space to the light's texture space, column major); zero for a point light

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace LLLocalLightTable
{
    constexpr int TILE = 32;                       // pixels
    constexpr int MAX_PER_TILE = 32;
    constexpr int TILE_ROW = MAX_PER_TILE + 1;     // the count and the indices
    constexpr int MAX_LIGHTS = 256;                // the indices are bytes; RenderLocalLightCount's default
    constexpr int ROW_TEXELS = 8;

    struct Rect
    {
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;        // pixels, half open
        bool empty() const { return x1 <= x0 || y1 <= y0; }
    };

    // The screen rectangle (pixels) a light's sphere can reach: the bounding box of the sphere's view-space box through
    // the projection (column major, 16 floats), widened by a pixel. A box that reaches the camera plane (the camera is
    // inside it or the light is behind) covers the whole screen. Empty when the box lies off screen.
    inline Rect sphereScreenRect(const float centre[3], float radius, const float proj[16], int width, int height)
    {
        Rect full;
        full.x1 = width;
        full.y1 = height;
        float min_x = 1.0e30f, min_y = 1.0e30f, max_x = -1.0e30f, max_y = -1.0e30f;
        for (int i = 0; i < 8; ++i)
        {
            const float v[4] = { centre[0] + ((i & 1) ? radius : -radius), centre[1] + ((i & 2) ? radius : -radius),
                                 centre[2] + ((i & 4) ? radius : -radius), 1.f };
            float c[4];
            for (int r = 0; r < 4; ++r)
            {
                c[r] = proj[r] * v[0] + proj[4 + r] * v[1] + proj[8 + r] * v[2] + proj[12 + r] * v[3];
            }
            if (c[3] <= 1.0e-4f)
            {
                return full;
            }
            const float sx = (c[0] / c[3] * 0.5f + 0.5f) * float(width);
            const float sy = (c[1] / c[3] * 0.5f + 0.5f) * float(height);
            min_x = std::min(min_x, sx);
            max_x = std::max(max_x, sx);
            min_y = std::min(min_y, sy);
            max_y = std::max(max_y, sy);
        }
        Rect r;
        r.x0 = std::max(int(std::floor(min_x)) - 1, 0);
        r.y0 = std::max(int(std::floor(min_y)) - 1, 0);
        r.x1 = std::min(int(std::ceil(max_x)) + 1, width);
        r.y1 = std::min(int(std::ceil(max_y)) + 1, height);
        return r;
    }

    // The tile list of one frame
    class Tiles
    {
    public:
        // clears the lists and sizes the grid for a screen of width x height pixels
        void reset(int width, int height)
        {
            mTilesX = (std::max(width, 1) + TILE - 1) / TILE;
            mTilesY = (std::max(height, 1) + TILE - 1) / TILE;
            mData.assign(size_t(mTilesX) * size_t(mTilesY) * size_t(TILE_ROW), 0);
            mOverflow = 0;
        }

        // lists light `index` in every tile the rectangle touches; false when the rectangle is empty
        bool add(int index, const Rect& r)
        {
            if (r.empty() || index < 0 || index >= MAX_LIGHTS)
            {
                return false;
            }
            const int tx0 = r.x0 / TILE;
            const int ty0 = r.y0 / TILE;
            const int tx1 = std::min((r.x1 - 1) / TILE, mTilesX - 1);
            const int ty1 = std::min((r.y1 - 1) / TILE, mTilesY - 1);
            for (int ty = ty0; ty <= ty1; ++ty)
            {
                for (int tx = tx0; tx <= tx1; ++tx)
                {
                    uint8_t* row = &mData[(size_t(ty) * size_t(mTilesX) + size_t(tx)) * size_t(TILE_ROW)];
                    if (row[0] < MAX_PER_TILE)
                    {
                        row[1 + row[0]] = uint8_t(index);
                        ++row[0];
                    }
                    else
                    {
                        ++mOverflow;
                    }
                }
            }
            return true;
        }

        int tilesX() const { return mTilesX; }
        int tilesY() const { return mTilesY; }
        int rows() const { return mTilesX * mTilesY; }
        int count(int tx, int ty) const { return mData[(size_t(ty) * size_t(mTilesX) + size_t(tx)) * size_t(TILE_ROW)]; }
        int light(int tx, int ty, int i) const { return mData[(size_t(ty) * size_t(mTilesX) + size_t(tx)) * size_t(TILE_ROW) + 1 + size_t(i)]; }
        int overflow() const { return mOverflow; }
        const uint8_t* data() const { return mData.data(); }
        size_t bytes() const { return mData.size(); }

    private:
        int mTilesX = 0;
        int mTilesY = 0;
        int mOverflow = 0;
        std::vector<uint8_t> mData;
    };
}

#endif // LL_LLLOCALLIGHTTABLE_H
