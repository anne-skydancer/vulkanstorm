// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lllocallighttable.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lllocallighttable_test.cpp
// The screen rectangle of a light's sphere and the 32 x 32 tile list the local light ratio pass reads.

#include "lllocallighttable.h"

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    // a perspective projection (column major, OpenGL clip space), 90 degrees, square, near 0.1, far 100
    void projection(float out[16])
    {
        const float n = 0.1f, f = 100.f;
        for (int i = 0; i < 16; ++i) out[i] = 0.f;
        out[0] = 1.f;
        out[5] = 1.f;
        out[10] = -(f + n) / (f - n);
        out[11] = -1.f;
        out[14] = -2.f * f * n / (f - n);
    }

    void test_rect()
    {
        using namespace LLLocalLightTable;
        float p[16];
        projection(p);
        const float ahead[3] = { 0.f, 0.f, -10.f };
        Rect r = sphereScreenRect(ahead, 1.f, p, 1280, 720);
        check(!r.empty(), "a light ahead is on screen");
        // 90 degrees: x = +-1 at z = -9 and z = -11 projects to +-1/9 and +-1/11 of the half width; the box is the larger
        const float half = 1.f / 9.f * 0.5f * 1280.f;
        check(r.x0 <= 640 - int(half) + 1 && r.x1 >= 640 + int(half) - 1, "its rectangle spans the sphere horizontally");
        check(r.x0 > 400 && r.x1 < 880, "and is small");
        const float behind[3] = { 0.f, 0.f, 10.f };
        r = sphereScreenRect(behind, 1.f, p, 1280, 720);
        check(r.x0 == 0 && r.y0 == 0 && r.x1 == 1280 && r.y1 == 720, "a light behind the camera covers the screen (conservative)");
        const float inside[3] = { 0.f, 0.f, -1.f };
        r = sphereScreenRect(inside, 5.f, p, 1280, 720);
        check(r.x0 == 0 && r.x1 == 1280, "a camera inside the light's box covers the screen");
        const float far_left[3] = { -60.f, 0.f, -10.f };
        r = sphereScreenRect(far_left, 1.f, p, 1280, 720);
        check(r.empty(), "a light far off to the side is off screen");
        const float huge[3] = { 1.0e30f, 0.f, -10.f };
        r = sphereScreenRect(huge, 1.f, p, 1280, 720);
        check(r.empty() || (r.x0 >= 0 && r.x1 <= 1280), "a huge coordinate is clamped before the integer cast");
        const float edge[3] = { -10.5f, 0.f, -10.f };
        r = sphereScreenRect(edge, 1.f, p, 1280, 720);
        check(!r.empty() && r.x0 == 0, "a light across the left edge is clamped to it");
    }

    void test_tiles()
    {
        using namespace LLLocalLightTable;
        Tiles t;
        t.reset(1920, 1080);
        check(t.tilesX() == 60 && t.tilesY() == 34, "1920 x 1080 is 60 x 34 tiles (the last row partial)");
        check(t.rows() == 60 * 34 && t.bytes() == size_t(60 * 34 * TILE_ROW), "a row of 33 bytes per tile");
        Rect r;
        r.x0 = 31; r.y0 = 0; r.x1 = 33; r.y1 = 1;   // two pixels across a tile border
        check(t.add(5, r), "a rectangle is added");
        check(t.count(0, 0) == 1 && t.count(1, 0) == 1 && t.count(2, 0) == 0 && t.count(0, 1) == 0, "it lists the two tiles it touches");
        check(t.light(0, 0, 0) == 5 && t.light(1, 0, 0) == 5, "with its index");
        r.x0 = 0; r.y0 = 0; r.x1 = 1920; r.y1 = 1080;
        t.add(7, r);
        check(t.count(0, 0) == 2 && t.light(0, 0, 1) == 7, "indices keep their order of adding");
        check(t.count(59, 33) == 1 && t.light(59, 33, 0) == 7, "the full screen reaches the last, partial tile");
        Rect none;
        check(!t.add(1, none), "an empty rectangle adds nothing");
        check(!t.add(MAX_LIGHTS, r) && !t.add(-1, r), "an index that does not fit a byte is refused");
        Tiles u;
        u.reset(64, 64);
        Rect one;
        one.x1 = 64; one.y1 = 64;
        for (int i = 0; i < MAX_PER_TILE + 3; ++i)
        {
            u.add(i, one);
        }
        check(u.count(0, 0) == MAX_PER_TILE && u.light(0, 0, MAX_PER_TILE - 1) == MAX_PER_TILE - 1, "a tile holds at most 32 lights, the first ones added");
        check(u.overflow() == 3 * 4, "the lights that did not fit are counted (3 lights over 4 tiles)");
        check(u.dropped(MAX_PER_TILE) && u.dropped(MAX_PER_TILE + 2) && !u.dropped(0) && !u.dropped(MAX_PER_TILE - 1), "the lights that did not fit are reported");
        u.reset(64, 64);
        check(u.count(0, 0) == 0 && u.overflow() == 0 && !u.dropped(MAX_PER_TILE), "reset clears");
        // clipped to a rectangle: only its tiles are filled
        Tiles c;
        c.reset(256, 256);
        Rect clip;
        clip.x0 = 64; clip.y0 = 32; clip.x1 = 128; clip.y1 = 96;
        c.setClip(clip);
        Rect all;
        all.x1 = 256; all.y1 = 256;
        c.add(3, all);
        check(c.count(2, 1) == 1 && c.count(3, 2) == 1 && c.count(0, 0) == 0 && c.count(4, 1) == 0 && c.count(2, 3) == 0, "a clipped list fills the clip's tiles only");
        Rect outside;
        outside.x0 = 200; outside.y0 = 200; outside.x1 = 220; outside.y1 = 220;
        check(!c.add(4, outside), "a light outside the clip is not listed");
        // taking the light that did not fit out frees the room: the lists made again hold the others
        Tiles d;
        d.reset(64, 64);
        for (int i = 0; i < MAX_PER_TILE + 1; ++i) d.add(i, one);
        check(d.dropped(MAX_PER_TILE), "the 33rd light is dropped");
        d.reset(64, 64);
        for (int i = 0; i < MAX_PER_TILE + 1; ++i) if (!(i == MAX_PER_TILE)) d.add(i, one);
        check(d.overflow() == 0 && d.count(0, 0) == MAX_PER_TILE, "without it every other light is listed");
    }
}

int main()
{
    test_rect();
    test_tiles();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lllocallighttable_test: ok\n");
    return 0;
}
