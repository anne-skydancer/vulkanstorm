// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llpointshadowpool.h and llindexskip.h (no viewer libraries, no GL), the point lights' shadow faces (stage L4):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview -I indra/llrender indra/newview/tests/llpointshadowpool_test.cpp
// Plan: reports/point-light-l4-2026-11-18/plan.md, section 5; doc: doc/shadows/point-light-shadows.md.
//   P-1   the face pick against a brute-force major axis on 10^6 directions, the exact ties, and the face coordinates against the axes
//   P-1b  the render's view and projection put a point where the lookup's uv and depth say (faceView, faceProjection, faceTexCoord)
//   P-2   seams: every tap of a receiver on a face edge stays inside its tile at the filter's cap for N = 128 .. 1024
//   P-3   the face size from the lamp, one size per lamp, the valve's point floor (128) and the projectors' unchanged floor (64)
//   P-4   capacity: 170 lamps at the floor fit the 4096 atlas; 59 lamps at the floor use 35 %
//   P-5   draw-then-swap: a lamp's faces resized one at a time keep valid rects that never overlap, and the old rect serves until then
//   P-8   the emitter's faces left out of a draw that batches them with other prims (llindexskip::split against a brute force)
//   P-9   the pose of a lamp's faces: its centre; a prim spinning about any axis keeps it; the radius the faces are drawn for (a growth
//         redraws, a drop after 5 s, a candle never)
//   P-10  the merged selection over the two pools: clashing raw keys stay two items, the cap holds but for one progress pick, and
//         neither pool starves
//   P-11  the own avatar's every-frame lamps: at most four, the strongest, none without light
//   P-12  a waiting lamp takes a placed lamp's faces only when that one is farther by the reach's hysteresis, never back and forth
//   P-13  the own avatar's every-frame faces within half of A_max
//   P-14  a first fill (one item per face) leaves the avatar layers their share; a moving lamp runs beside the own avatar's faces
//   P-15  the change index leaves a lamp's own emitter out of its faces (and only its own)
//   Pack  the lookup's packed face rects round-trip exactly for every rect of the 4096 atlas

#include "llpointshadowpool.h"
#include "llindexskip.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <vector>

using namespace llprojectorpool;
using namespace llpointpool;

namespace
{
    int failures = 0;

    void check(bool cond, const char* what, double a = 0.0, double b = 0.0)
    {
        if (!cond)
        {
            std::printf("FAIL %s (%g, %g)\n", what, a, b);
            ++failures;
        }
    }

    // the brute force: the first axis (x, y, z) whose magnitude is the largest; the positive face for a component >= 0
    int bruteFace(const float d[3])
    {
        const float m = std::max(std::fabs(d[0]), std::max(std::fabs(d[1]), std::fabs(d[2])));
        for (int i = 0; i < 3; ++i)
        {
            if (std::fabs(d[i]) == m)
            {
                return 2 * i + (d[i] >= 0.f ? 0 : 1);
            }
        }
        return -1;
    }

    float dot3(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }

    void testPick()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        int wrong = 0, coord = 0;
        for (int n = 0; n < 1000000; ++n)
        {
            float d[3] = { u(rng), u(rng), u(rng) };
            if ((n & 7) == 0)
            {
                // ties: copy one component's magnitude onto another, with either sign
                const int a = n % 3, b = (n / 3) % 3;
                d[b] = (n & 8 ? -1.f : 1.f) * std::fabs(d[a]);
            }
            if ((n & 63) == 1)
            {
                d[n % 3] = (n & 64) ? -0.f : 0.f; // signed zeros
            }
            float xyw[3];
            const int f = pickFace(d, xyw);
            wrong += f != bruteFace(d);
            const FaceAxes& ax = kFaceAxes[f];
            coord += xyw[0] != dot3(ax.right, d) || xyw[1] != dot3(ax.up, d) || xyw[2] != dot3(ax.fwd, d);
        }
        check(wrong == 0, "P-1 the face pick is the brute force's major axis", wrong);
        check(coord == 0, "P-1 the face coordinates are the axes' dot products", coord);
        // the exact ties of the rule: x before y before z, a zero to the positive face
        const float ties[][3] = { { 1, 1, 1 }, { -1, 1, 1 }, { 0, 1, 1 }, { 0, 0, 1 }, { 0, 0, -1 }, { -0.f, 0, 0 }, { 0, -1, -1 }, { 2, -2, 1 } };
        const int want[] = { 0, 1, 2, 4, 5, 0, 3, 0 };
        for (size_t i = 0; i < sizeof(want) / sizeof(want[0]); ++i)
        {
            float xyw[3];
            check(pickFace(ties[i], xyw) == want[i], "P-1 a tie goes to x, then y, a zero to the positive face", double(i), pickFace(ties[i], xyw));
        }
        // every face is a rotation: right x up = -fwd, unit axes
        for (const FaceAxes& a : kFaceAxes)
        {
            const float c[3] = { a.right[1] * a.up[2] - a.right[2] * a.up[1], a.right[2] * a.up[0] - a.right[0] * a.up[2], a.right[0] * a.up[1] - a.right[1] * a.up[0] };
            check(c[0] == -a.fwd[0] && c[1] == -a.fwd[1] && c[2] == -a.fwd[2], "P-1 every face's axes are a rotation (det +1)");
        }
    }

    // the render's matrices against the lookup's coordinates: a point's clip position through faceView and faceProjection, biased to [0, 1],
    // is faceTexCoord's (u, v, z)
    void testMatrices()
    {
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        const float centre[3] = { 120.5f, -33.25f, 22.f };
        double worst_uv = 0.0, worst_z = 0.0;
        for (uint32_t n : { 128u, 256u, 512u, 1024u })
        {
            const double t = faceTanHalf(n);
            const float near_m = kNearM, far_m = faceFar(6.f);
            float proj[16];
            faceProjection(t, near_m, far_m, proj);
            const FaceDepth fd = faceDepth(near_m, far_m);
            for (int k = 0; k < 20000; ++k)
            {
                const float r = 0.05f + 8.5f * (0.5f + 0.5f * u(rng));
                float d[3] = { u(rng), u(rng), u(rng) };
                const float l = std::sqrt(dot3(d, d));
                if (l < 1e-3f)
                {
                    continue;
                }
                float p[3];
                for (int i = 0; i < 3; ++i)
                {
                    d[i] *= r / l;
                    p[i] = centre[i] + d[i];
                }
                float xyw[3];
                const int f = pickFace(d, xyw);
                float view[16];
                faceView(f, centre, view);
                double e[4] = { 0, 0, 0, 0 };
                for (int row = 0; row < 4; ++row)
                {
                    e[row] = double(view[row]) * p[0] + double(view[4 + row]) * p[1] + double(view[8 + row]) * p[2] + double(view[12 + row]);
                }
                double c[4];
                for (int row = 0; row < 4; ++row)
                {
                    c[row] = proj[row] * e[0] + proj[4 + row] * e[1] + proj[8 + row] * e[2] + proj[12 + row] * e[3];
                }
                float uvz[3];
                faceTexCoord(xyw, t, fd, uvz);
                // in metres at the receiver: the matrices are float in agent space (a centre 120 m out rounds to a few micrometres), so
                // the two agree to the agent coordinates' precision; a uv step is 2 w t metres, a depth step c1 / (c0 - z)^2 metres of w
                const double w = xyw[2];
                worst_uv = std::max(worst_uv, std::fabs((c[0] / c[3]) * 0.5 + 0.5 - uvz[0]) * 2.0 * w * t);
                worst_uv = std::max(worst_uv, std::fabs((c[1] / c[3]) * 0.5 + 0.5 - uvz[1]) * 2.0 * w * t);
                // the depth: within a few float ulps of 1 (the projection's float elements) plus what the point's agent coordinates'
                // rounding (2e-5 m) moves it by (dz / dw = c1 / w^2): the ratio stays below 1
                const double z_render = (c[2] / c[3]) * 0.5 + 0.5;
                worst_z = std::max(worst_z, std::fabs(z_render - uvz[2]) / (2.5e-7 + fd.c1 / (w * w) * 2e-5));
                check(std::fabs(c[3] - xyw[2]) < 1e-4 * std::max(1.0, double(r)), "P-1b w is the axial distance", c[3], xyw[2]);
            }
        }
        check(worst_uv < 5e-5, "P-1b the render's uv is the lookup's (m at the receiver)", worst_uv);
        check(worst_z < 1.0, "P-1b the render's depth is the lookup's (float ulps plus the agent coordinates' rounding)", worst_z);
    }

    // P-2: every texel a receiver's taps read stays inside its face's tile at the cap (14 texels): the quads span floor(tc - r) .. floor(tc + r) + 1
    // per axis, tc = uv N - 0.5, and the shader's edge test (tc - r < 1 or tc + r > N - 2) never fires (bit for bit an unbounded map)
    void testSeams()
    {
        std::mt19937 rng(3);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        const float r = kPointLamp.kernel_max;
        for (uint32_t n : { 128u, 256u, 512u, 1024u })
        {
            const double t = faceTanHalf(n);
            const FaceDepth fd = faceDepth(kNearM, 9.f);
            int out = 0, edge = 0;
            double lo = 1e9, hi = -1e9;
            for (int k = 0; k < 200000; ++k)
            {
                // directions on and near the cube's edges and corners (two or three components of equal magnitude), and random ones
                float d[3] = { u(rng), u(rng), u(rng) };
                if (k & 1)
                {
                    const int a = k % 3, b = (k / 3) % 3;
                    d[b] = (k & 2 ? -1.f : 1.f) * std::fabs(d[a]) * (1.f - 1e-6f * float(k % 5));
                }
                if ((k % 7) == 0)
                {
                    d[0] = d[1] = d[2] = (k & 4) ? -0.5f : 0.5f;
                }
                float xyw[3];
                pickFace(d, xyw);
                if (!(xyw[2] > 0.f))
                {
                    continue;
                }
                float uvz[3];
                faceTexCoord(xyw, t, fd, uvz);
                for (int a = 0; a < 2; ++a)
                {
                    const double tc = double(uvz[a]) * n - 0.5;
                    lo = std::min(lo, tc);
                    hi = std::max(hi, tc);
                    const double first = std::floor(tc - r), last = std::floor(tc + r) + 1.0;
                    out += first < 0.0 || last > double(n) - 1.0;
                    edge += (tc - r) < 1.0 || (tc + r) > double(n) - 2.0;
                }
            }
            check(out == 0, "P-2 every tap stays inside its face's tile", n, out);
            check(edge == 0, "P-2 no tap reaches the tile's clamped edge (the lookup equals an unbounded map's)", n, edge);
            check(lo >= kGuard - 0.5 - 1e-3 && hi <= n - kGuard - 0.5 + 1e-3, "P-2 a receiver lies in its face's interior", lo, hi);
        }
    }

    void testSize()
    {
        uint32_t prev = 0;
        for (double r = 0.1; r <= 20.0; r += 0.05)
        {
            const uint32_t n = faceBaseSize(r);
            check(n >= kFaceMin && n <= kFaceMax && (n & (n - 1)) == 0, "P-3 a face's size is a power of two in [128, 1024]", r, n);
            check(n >= prev, "P-3 a larger lamp never gets smaller faces", r, n);
            prev = n;
        }
        check(faceBaseSize(0.5) == 512, "P-3 a 0.5 m lamp: 512 (375 interior texels of 4 mm at 0.75 m, plus the guards)", faceBaseSize(0.5));
        check(faceBaseSize(10.0) == kFaceMax, "P-3 a 10 m lamp: the largest face", faceBaseSize(10.0));
        check(faceBaseSize(std::nan("")) == kFaceMin, "P-3 a NaN radius: the floor", faceBaseSize(std::nan("")));
        check(std::fabs(faceTanHalf(128) - 64.0 / 48.0) < 1e-12 && std::fabs(faceTanHalf(1024) - 512.0 / 496.0) < 1e-12, "P-3 the face's tangent from its guard");
        // the valve with the faces' floor: a point face never goes below 128, at any pressure
        for (int p = 0; p <= 12; ++p)
        {
            for (uint32_t base : { 128u, 256u, 512u, 1024u })
            {
                for (int band = 0; band <= kMaxBand; ++band)
                {
                    ValveTile t;
                    t.base = base;
                    t.band = band;
                    t.floor = kFaceMin;
                    check(valveSize(t, p) >= kFaceMin, "P-3 a face never goes below its floor", p, valveSize(t, p));
                    ValveTile pt = t;
                    pt.floor = kMinTile;
                    const uint32_t want = std::max(kMinTile, base >> valveShift(p, band));
                    check(valveSize(pt, p) == want, "P-3 a projector tile keeps its floor of 64", p, valveSize(pt, p));
                }
            }
        }
        // the ceiling of a set of faces at the floor is 0 (nothing to shrink) and the level controller never passes it
        std::vector<ValveTile> faces(6 * 200);
        for (ValveTile& t : faces)
        {
            t.base = 1024;
            t.band = 0;
            t.floor = kFaceMin;
        }
        LevelController lc;
        lc.update(faces, 4096, 0.016f);
        check(lc.level() == lc.ceiling(), "P-3 200 lamps of 1024 faces: the valve goes to its ceiling", lc.level(), lc.ceiling());
        for (const ValveTile& t : faces)
        {
            check(valveSize(t, lc.level()) == kFaceMin, "P-3 ... where every face is at the floor", valveSize(t, lc.level()));
            break;
        }
        // one band per lamp: a lamp's faces are sized alike whatever the pressure
        std::vector<ValveTile> lamp(6);
        for (ValveTile& t : lamp)
        {
            t.base = faceBaseSize(3.0);
            t.band = 2;
            t.floor = kFaceMin;
        }
        for (int p = 0; p <= 10; ++p)
        {
            for (const ValveTile& t : lamp)
            {
                check(valveSize(t, p) == valveSize(lamp[0], p), "P-3 a lamp's faces have one size", p);
            }
        }
    }

    void testCapacity()
    {
        BuddyAtlas atlas(4096);
        int placed = 0;
        for (int i = 0; i < 170 * 6; ++i)
        {
            Rect r;
            placed += atlas.alloc(kFaceMin, r) ? 1 : 0;
        }
        check(placed == 170 * 6, "P-4 170 lamps of six 128 faces fit the 4096 atlas", placed);
        std::vector<ValveTile> tiles(59 * 6);
        for (ValveTile& t : tiles)
        {
            t.base = kFaceMin;
            t.floor = kFaceMin;
        }
        const double u = demand(tiles, 0, 4096);
        check(u > 0.34 && u < 0.36, "P-4 59 lamps at the floor use 35 % of the atlas", u);
        // the point atlas' tier (llpointpool::pointAtlasTier, the function ensureAllocated calls): D3's two tiers, never past the packing
        check(pointAtlasTier(4000, 16384) == 4096 && pointAtlasTier(2048, 16384) == 2048 && pointAtlasTier(0, 0) == 4096,
              "P-4 the point atlas: 2048 below 3 GB, 4096 from 3 GB or unknown");
        check(atlasTier(24000, 32768, true) == kAtlasLarge && pointAtlasTier(24000, 32768) == 4096,
              "P-4 never 8192, where the projectors' atlas is (24 GB of dedicated memory): the faces' packing holds up to 4096");
        check(pointAtlasTier(16000, 2048) == 2048 && pointAtlasTier(16000, 1024) == 2048,
              "P-4 within GL_MAX_TEXTURE_SIZE, never below the smallest tier");
        for (uint32_t vram : { 0u, 1024u, 3071u, 3072u, 8192u, 11264u, 24000u, 100000u })
        {
            for (uint32_t max_tex : { 0u, 2048u, 4096u, 8192u, 16384u, 32768u })
            {
                const uint32_t t = pointAtlasTier(vram, max_tex);
                check((t == 2048 || t == 4096) && t <= kPackAtlasMax, "P-4 every point atlas packs its face rects", vram, t);
            }
        }
        check(atlasBytes(4096) == 128ull << 20, "P-4 the 4096 point atlases take 128 MiB");
    }

    // P-5: a lamp's six faces shrink one render at a time (draw-then-swap per face): each face frees its old rect and takes a block of the
    // new size in its own render, the rects never overlap, and a face not drawn yet keeps serving its old rect
    void testDrawThenSwap()
    {
        BuddyAtlas atlas(4096);
        std::vector<Rect> rects(6);
        for (Rect& r : rects)
        {
            check(atlas.alloc(512, r), "P-5 the lamp's faces at 512");
        }
        for (int f = 0; f < 6; ++f)
        {
            const Rect old = rects[f];
            check(atlas.largestAfterFree(old) >= 256, "P-5 a shrink always finds its block");
            atlas.free(old);
            Rect nr;
            check(atlas.alloc(256, nr), "P-5 the shrunk face is placed");
            rects[f] = nr;
            for (int g = 0; g < 6; ++g)
            {
                for (int h = g + 1; h < 6; ++h)
                {
                    const Rect& a = rects[g];
                    const Rect& b = rects[h];
                    const bool overlap = a.x < b.x + b.size && b.x < a.x + a.size && a.y < b.y + b.size && b.y < a.y + a.size;
                    check(!overlap, "P-5 the faces' rects never overlap during a resize", g, h);
                }
            }
        }
        // and back up: a growth waits for a free block of its new size (largestAfterFree), it never takes a smaller one
        for (int f = 0; f < 6; ++f)
        {
            if (atlas.largestAfterFree(rects[f]) >= 512)
            {
                atlas.free(rects[f]);
                check(atlas.alloc(512, rects[f]), "P-5 a growth takes the block it found");
            }
        }
    }

    void testSkip()
    {
        using llindexskip::Range;
        std::mt19937 rng(5);
        int wrong = 0;
        for (int k = 0; k < 20000; ++k)
        {
            const uint32_t offset = rng() % 300, count = 3 * (1 + rng() % 100);
            std::vector<Range> skip;
            const int n = int(rng() % 4);
            for (int i = 0; i < n; ++i)
            {
                const uint32_t b = 3 * (rng() % 150);
                skip.push_back({ b, uint32_t(b + 3 * (rng() % 30)) });
            }
            std::vector<char> want(600, 0), got(600, 0);
            bool any = false;
            for (uint32_t i = offset; i < offset + count; ++i)
            {
                bool left = false;
                for (const Range& r : skip)
                {
                    left = left || (i >= r.begin && i < r.end);
                }
                want[i] = left ? 0 : 1;
                any = any || left;
            }
            std::vector<std::pair<uint32_t, uint32_t>> parts;
            const bool split = llindexskip::split(offset, count, skip, parts);
            if (!split)
            {
                for (uint32_t i = offset; i < offset + count; ++i)
                {
                    got[i] = 1;
                }
            }
            uint32_t last_end = 0;
            for (const auto& p : parts)
            {
                wrong += p.second == 0 || p.first < last_end; // ascending, no empty part
                last_end = p.first + p.second;
                for (uint32_t i = p.first; i < p.first + p.second; ++i)
                {
                    wrong += got[i]++ != 0; // no index drawn twice
                }
            }
            wrong += split != any;
            wrong += want != got;
        }
        check(wrong == 0, "P-8 the emitter's indices are left out, every other index is drawn once, in order", wrong);
        std::vector<std::pair<uint32_t, uint32_t>> parts;
        check(!llindexskip::split(0, 30, { { 30, 60 } }, parts) && parts.empty(), "P-8 a range past the draw changes nothing");
        check(llindexskip::split(0, 30, { { 0, 30 } }, parts) && parts.empty(), "P-8 a draw of the emitter alone draws nothing");
        check(llindexskip::split(0, 30, { { 9, 12 } }, parts) && parts.size() == 2 && parts[0] == std::make_pair(0u, 9u) && parts[1] == std::make_pair(12u, 18u),
              "P-8 an emitter inside a batch splits it in two");
    }

    void testPose()
    {
        const float c[3] = { 10.f, 20.f, 30.f };
        const Pose a = lampPose(42, c);
        for (int i = 0; i < 4; ++i)
        {
            check(a.rot[i] == (i == 3 ? 1.f : 0.f), "P-9 a lamp's faces have no rotation in their pose");
        }
        check(a.radius == 0.f, "P-9 the radius is no part of the pose (faceRadius has its own rule)");
        check(!poseChanged(a, lampPose(42, c)), "P-9 a prim spinning about any axis keeps its faces (its rotation is no input)");
        const float moved[3] = { 10.002f, 20.f, 30.f };
        const float nudged[3] = { 10.0005f, 20.f, 30.f };
        check(poseChanged(a, lampPose(42, moved)) && !poseChanged(a, lampPose(42, nudged)), "P-9 a centre moved 2 mm redraws, 0.5 mm does not");
        check(poseChanged(a, lampPose(43, c)), "P-9 another region is another pose");

        // the radius the faces are drawn for (llpointpool::faceRadius): a growth redraws at once, a drop only once it has held 5 s, a
        // candle that varies its radius never redraws
        double below = -1.0;
        check(faceRadius(0.f, 3.f, below, 0.0) == 3.f, "P-9 no faces yet: the lamp's radius");
        check(faceRadius(3.f, 3.005f, below, 0.0) == 3.f && faceRadius(3.f, 2.995f, below, 0.0) == 3.f, "P-9 within 1 cm: the drawn radius");
        check(faceRadius(3.f, 3.2f, below, 0.0) == 3.2f, "P-9 a radius 20 cm larger: the faces end before the light, drawn again");
        below = -1.0;
        bool redrawn = false;
        for (int k = 0; k < 600; ++k) // 10 s of a candle: 2.5 .. 3.0 m every frame at 60 fps, never below for 5 s in a row
        {
            const float r = 2.5f + 0.5f * float((k * 7919) % 101) / 100.f;
            redrawn = redrawn || faceRadius(3.f, r, below, k / 60.0) != 3.f;
        }
        check(!redrawn, "P-9 a candle varying its radius inside the drawn one never redraws");
        below = -1.0;
        check(faceRadius(3.f, 1.f, below, 100.0) == 3.f && faceRadius(3.f, 1.f, below, 104.9) == 3.f, "P-9 a drop: the drawn faces serve for 5 s");
        check(faceRadius(3.f, 1.f, below, 105.0) == 1.f, "P-9 ... then the faces of the smaller radius are drawn");
        below = -1.0;
        check(faceRadius(3.f, std::nan(""), below, 0.0) == 3.f, "P-9 a NaN radius keeps the drawn faces");
    }

    // P-12: a waiting lamp takes the faces of a placed one only when that one is farther by the reach's hysteresis
    void testEvict()
    {
        check(evicts(5.f, 30.f) && !evicts(5.f, 6.f) && !evicts(30.f, 32.f) && evicts(30.f, 33.1f), "P-12 farther by max(2 m, 10 %) only");
        check(!evicts(std::nan(""), 100.f) && evicts(5.f, std::nan("")), "P-12 a NaN distance is the farthest: never evicts, always evicted");
        // no pair trades back and forth: if a evicts b, b never evicts a
        std::mt19937 rng(17);
        std::uniform_real_distribution<float> d(0.f, 140.f);
        int both = 0;
        for (int k = 0; k < 100000; ++k)
        {
            const float a = d(rng), b = d(rng);
            both += evicts(a, b) && evicts(b, a);
        }
        check(both == 0, "P-12 an eviction is never undone by the evicted lamp", both);
    }

    // P-13: the own avatar's every-frame faces within half of A_max
    void testOwnBudget()
    {
        std::vector<OwnFace> faces = { { 0, faceKey(1, 0), 3 }, { 0, faceKey(1, 5), 2 }, { 1, faceKey(2, 0), 4 }, { 2, faceKey(3, 1), 6 },
                                       { 3, faceKey(4, 0), 1 } };
        std::vector<bool> every = ownFacesWithinBudget(faces);
        check(every[0] && every[1] && every[2] && !every[3] && every[4], "P-13 the strongest lamps' faces while they fit 12 draws (3 + 2 + 4, 6 waits, 1 fits)");
        uint32_t used = 0;
        for (size_t i = 0; i < faces.size(); ++i)
        {
            used += every[i] ? std::max(faces[i].draws, 1u) : 0u;
        }
        check(used <= kOwnAvatarDrawsMax && kOwnAvatarDrawsMax == kAvatarDrawsMax / 2, "P-13 at most half of A_max", used);
        // a crowd under the strongest lamp: its face (24 avatars) goes by the deadline, the other lamps' faces still run
        std::vector<OwnFace> crowd = { { 0, faceKey(1, 5), 24 }, { 1, faceKey(2, 5), 2 } };
        every = ownFacesWithinBudget(crowd);
        check(!every[0] && every[1], "P-13 a face of 24 avatars is not an every-frame face; the others' still are");
        // 4 lamps x 6 faces of the own avatar alone: 12 run
        std::vector<OwnFace> alone;
        for (uint32_t l = 0; l < 4; ++l)
        {
            for (uint32_t f = 0; f < 6; ++f)
            {
                alone.push_back({ l, faceKey(l + 1, f), 1 });
            }
        }
        size_t n = 0;
        for (bool b : ownFacesWithinBudget(alone))
        {
            n += b ? 1 : 0;
        }
        check(n == kOwnAvatarDrawsMax, "P-13 the own avatar alone: twelve faces every frame", double(n));
    }

    // P-10: the merged selection. The selection itself is selectFrame's; the pools only pack their keys.
    void testMerged()
    {
        // (a) the two pools' raw keys clash (a projector's lamp 1, cell 0 and a point lamp 2, face 0 are both 16): packed with their pools
        // they are two items, both picked, distinct, in a fixed order (the key breaks the tie), and each unpacks to its pool and key
        {
            check(faceKey(2, 0) == (1u << 4 | 0u), "P-10 the clash is real: point lamp 2, face 0 is the projectors' lamp 1, cell 0");
            std::vector<StaticItem> st(2);
            st[0].key = 16;
            st[0].pool = kPoolProjectors;
            st[1].key = faceKey(2, 0);
            st[1].pool = kPoolPoints;
            for (StaticItem& it : st)
            {
                it.pending = true;
                it.last_cpu_ms = 0.07f;
                it.age_frames = 3;
                it.cam_dist = 5.f;
            }
            const FramePicks p = selectFrame(st, {}, kFrameCapMs, kAvatarDrawsMax, true);
            std::vector<StaticItem> rev = { st[1], st[0] };
            const FramePicks q = selectFrame(rev, {}, kFrameCapMs, kAvatarDrawsMax, true);
            check(p.fixed.size() == 2 && p.fixed[0] != p.fixed[1], "P-10 clashing raw keys: both items picked, two packed keys", double(p.fixed.size()));
            check(p.fixed == q.fixed, "P-10 ... in a fixed order, whatever the lists' order");
            int pools = 0;
            for (uint32_t k : p.fixed)
            {
                check(unpackKey(k) == 16u, "P-10 ... each unpacks to its raw key", unpackKey(k));
                pools |= 1 << unpackPool(k);
            }
            check(pools == 3, "P-10 ... and to its own pool", pools);
            // the same in the dynamic list: two avatar layers with one raw key
            std::vector<DynamicItem> dy(2);
            dy[0].key = 16;
            dy[0].pool = kPoolProjectors;
            dy[1].key = faceKey(2, 0);
            dy[1].pool = kPoolPoints;
            for (DynamicItem& it : dy)
            {
                it.avatar = it.layer = true;
                it.avatar_draws = 1;
                it.age_frames = 1;
                it.cam_dist = 4.f;
                it.last_cpu_ms = 0.05f;
            }
            const FramePicks d = selectFrame({}, dy, kFrameCapMs, kAvatarDrawsMax, true);
            check(d.dynamic.size() == 2 && d.dynamic[0] != d.dynamic[1], "P-10 clashing raw keys in the dynamic list: both run", double(d.dynamic.size()));
        }

        // (b) and (c): two pools of static work over frames: the predicted spend stays inside the cap but for one progress pick, and every
        // item of both pools is served within the age bound
        struct Work
        {
            uint32_t pool, key;
            float ms;
            uint32_t since = 0;
        };
        std::vector<Work> work;
        for (uint32_t i = 0; i < 40; ++i)
        {
            work.push_back({ kPoolProjectors, i * 16, 0.07f });
            work.push_back({ kPoolPoints, faceKey(i + 1, i % 6), 0.07f });
        }
        uint32_t worst_wait = 0, over = 0;
        std::set<uint64_t> served;
        for (uint32_t frame = 1; frame <= 200 && !work.empty(); ++frame)
        {
            std::vector<StaticItem> st;
            for (const Work& w : work)
            {
                StaticItem it;
                it.key = w.key;
                it.pool = w.pool;
                it.pending = true;
                it.last_cpu_ms = w.ms;
                it.age_frames = frame - 1;
                it.cam_dist = float(w.key % 13);
                st.push_back(it);
            }
            const FramePicks p = selectFrame(st, {}, kFrameCapMs, kAvatarDrawsMax, true);
            over += p.predicted_ms > kFrameCapMs + 1e-4f && p.fixed.size() > 1;
            std::vector<Work> left;
            for (const Work& w : work)
            {
                const uint32_t packed = packKey(w.pool, w.key);
                if (std::find(p.fixed.begin(), p.fixed.end(), packed) != p.fixed.end())
                {
                    served.insert(packed);
                    worst_wait = std::max(worst_wait, frame);
                }
                else
                {
                    left.push_back(w);
                }
            }
            work.swap(left);
        }
        check(over == 0, "P-10 two pools never exceed the cap but for one progress pick", over);
        check(work.empty() && served.size() == 80, "P-10 every item of both pools is served", double(work.size()), double(served.size()));
        check(worst_wait <= (80 + 6) / 7 + 1, "P-10 neither pool starves: the age order serves all within ceil(N / per frame)", worst_wait);
        check(unpackPool(packKey(kPoolPoints, faceKey(kLampIds - 1, 5))) == kPoolPoints && unpackKey(packKey(kPoolPoints, faceKey(kLampIds - 1, 5))) == faceKey(kLampIds - 1, 5),
              "P-10 the largest face key packs and unpacks");
    }

    // P-14: the point lamps' work beside the avatar layers (review B1). A first fill is one item per face (a face's cost), never a lamp's
    // six faces as one item, so the avatar layers keep their share; a lamp that moves runs as the overdue progress pick even beside the own
    // avatar's every-frame faces.
    void testFillAndMovers()
    {
        auto layers = [](uint32_t n, float ms) {
            std::vector<DynamicItem> dy;
            for (uint32_t i = 0; i < n; ++i)
            {
                DynamicItem it;
                it.pool = kPoolProjectors;
                it.key = i * 16 + 1;
                it.avatar = it.layer = true;
                it.avatar_draws = 1;
                it.age_frames = 1;
                it.cam_dist = 3.f;
                it.last_cpu_ms = ms;
                dy.push_back(it);
            }
            return dy;
        };
        // (a) a login fill: 55 lamps' faces, each its own item, beside six other avatars' layers (0.04 ms each)
        for (float face_ms : { coldItemMs(kDefaultItemMs), 0.07f, 0.125f })
        {
            std::vector<StaticItem> fill;
            for (uint32_t l = 1; l <= 55; ++l)
            {
                for (uint32_t f = 0; f < kFaces; ++f)
                {
                    StaticItem it;
                    it.pool = kPoolPoints;
                    it.key = faceKey(l, f);
                    it.never_drawn = true;
                    it.pending = true;
                    it.cam_dist = 100.f + float(l);
                    it.last_cpu_ms = face_ms;
                    fill.push_back(it);
                }
            }
            const FramePicks p = selectFrame(fill, layers(6, 0.04f), kFrameCapMs, kAvatarDrawsMax, true);
            check(p.dynamic.size() == 6, "P-14 a fill leaves the six avatar layers their share", face_ms, double(p.dynamic.size()));
            check(!p.fixed.empty() && p.predicted_ms <= kFrameCapMs + 1e-4f, "P-14 ... and draws faces in what is left, inside the cap",
                  double(p.fixed.size()), p.predicted_ms);
        }
        // the cold prediction of a face is the scheduler's prior, not a placeholder above it
        check(coldItemMs(kDefaultItemMs) == kDefaultItemMs, "P-14 a face never drawn is predicted at kDefaultItemMs");

        // (b) a lamp moving every frame (six faces, 0.42 ms) next to the own avatar's every-frame faces and nothing static: it runs once it
        // is overdue (the progress pick beside the own faces), not never
        const float moving_ms = 6.f * 0.07f;
        uint32_t age = 1, ran = 0, worst_gap = 0, gap = 0;
        for (uint32_t frame = 0; frame < 120; ++frame)
        {
            std::vector<DynamicItem> dy = layers(2, 0.05f);
            for (DynamicItem& it : dy)
            {
                it.own = true;
            }
            DynamicItem lamp;
            lamp.pool = kPoolPoints;
            lamp.key = faceKey(9, kLampItem);
            lamp.age_frames = age;
            lamp.cam_dist = 2.f;
            lamp.last_cpu_ms = moving_ms;
            dy.push_back(lamp);
            const FramePicks p = selectFrame({}, dy, kFrameCapMs, kAvatarDrawsMax, true);
            const bool run = std::find(p.dynamic.begin(), p.dynamic.end(), packKey(kPoolPoints, lamp.key)) != p.dynamic.end();
            check(std::count_if(p.dynamic.begin(), p.dynamic.end(), [](uint32_t k) { return unpackPool(k) == kPoolProjectors; }) == 2,
                  "P-14 the own faces run every frame beside it");
            ran += run ? 1 : 0;
            gap = run ? 0 : gap + 1;
            worst_gap = std::max(worst_gap, gap);
            age = run ? 1 : age + 1;
        }
        check(ran >= 120 / kDynamicDeadlineFrames - 1 && worst_gap < kDynamicDeadlineFrames, "P-14 a moving lamp beside the own faces runs every 4th frame at least",
              ran, worst_gap);
        // alone, it fits the cap: every frame
        std::vector<DynamicItem> one(1);
        one[0].pool = kPoolPoints;
        one[0].key = faceKey(9, kLampItem);
        one[0].age_frames = 1;
        one[0].last_cpu_ms = moving_ms;
        check(selectFrame({}, one, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 1, "P-14 a moving lamp alone runs every frame");
        // once the frame has spent, the cap holds without exception: it waits
        one[0].age_frames = 9;
        check(selectFrame({}, one, 0.2f, kAvatarDrawsMax, false).dynamic.empty(), "P-14 no overrun once the frame has spent");
    }

    // P-15: the change index leaves a face's own emitter out (TileVolume::ignore): the bulb's LOD switch or alpha marks the other lamps'
    // faces it lies in, never its own lamp's (which never draw it)
    void testIgnoreSource()
    {
        int emitter_a = 1, emitter_b = 2, other = 3; // only their addresses: the sources
        const float centre_a[3] = { 0.f, 0.f, 0.f }, centre_b[3] = { 1.5f, 0.f, 0.f };
        std::vector<TileVolume> tiles;
        for (int f = 0; f < 6; ++f)
        {
            TileVolume a, b;
            a.key = faceKey(1, uint32_t(f));
            a.pyramid = facePyramid(centre_a, f, float(faceTanHalf(256)), kNearM, 6.f);
            a.ignore = &emitter_a;
            b.key = faceKey(2, uint32_t(f));
            b.pyramid = facePyramid(centre_b, f, float(faceTanHalf(256)), kNearM, 6.f);
            b.ignore = &emitter_b;
            tiles.push_back(a);
            tiles.push_back(b);
        }
        ChangeIndex index;
        index.rebuild(tiles);
        DirtyList list;
        Aabb bulb; // lamp 1's emitter: a 6 cm bulb around its centre
        for (int i = 0; i < 3; ++i)
        {
            bulb.lo[i] = -0.03f;
            bulb.hi[i] = 0.03f;
        }
        list.push(bulb, Cause::LodUp, &emitter_a);
        std::vector<Hit> hits;
        index.apply(list.swapForApply(), false, hits);
        int own = 0, theirs = 0;
        for (const Hit& h : hits)
        {
            (lampOf(h.key) == 1 ? own : theirs) += 1;
        }
        check(own == 0 && theirs >= 1, "P-15 the emitter's own change leaves its lamp's faces alone and marks the other lamp's", own, theirs);
        // the same box from another prim (a shade around the bulb) marks every face it meets, its own lamp's included
        list.push(bulb, Cause::LodUp, &other);
        index.apply(list.swapForApply(), false, hits);
        own = 0;
        for (const Hit& h : hits)
        {
            own += lampOf(h.key) == 1 ? 1 : 0;
        }
        check(own == 6, "P-15 another prim's change marks all six faces it meets", own);
        // an identical box of another source right after is kept (one tile may leave out one source and not the other)
        list.push(bulb, Cause::Edit, &emitter_a);
        list.push(bulb, Cause::Edit, &other);
        list.push(bulb, Cause::Edit, &other);
        check(list.pending() == 2, "P-15 the list keeps one box per source in a row", double(list.pending()));
        (void)emitter_b;
    }

    void testOwn()
    {
        std::vector<std::pair<uint32_t, float>> lamps = { { 5, 0.2f }, { 3, 0.9f }, { 8, 0.f }, { 1, 0.5f }, { 2, 0.9f }, { 9, 0.1f }, { 4, 0.05f } };
        std::vector<uint32_t> rank;
        const std::vector<bool> every = ownEveryFrame(lamps, &rank);
        const std::vector<bool> want = { true, true, false, true, true, false, false };
        check(every == want, "P-11 the four strongest lamps (a tie by lamp id), none without light");
        check(rank[4] == 0 && rank[1] == 1 && rank[3] == 2 && rank[0] == 3 && rank[5] == 4, "P-11 the others in order of strength");
        std::vector<std::pair<uint32_t, float>> dark = { { 1, 0.f }, { 2, 0.f } };
        const std::vector<bool> none = ownEveryFrame(dark);
        check(!none[0] && !none[1], "P-11 a lamp that does not reach the avatar runs nothing every frame");
        size_t count = 0;
        std::vector<std::pair<uint32_t, float>> many;
        for (uint32_t i = 0; i < 55; ++i)
        {
            many.push_back({ i, 1.f + float(i) });
        }
        for (bool b : ownEveryFrame(many))
        {
            count += b ? 1 : 0;
        }
        check(count == kOwnEveryFrameLamps, "P-11 55 lamps: four run every frame", double(count));
        // the shader's attenuation: 2 (1 - clamp((d / s + f) / (1 + f)))^2
        check(std::fabs(lampIrradiance(1.f, 0.f, 3.f, 0.f) - 2.f) < 1e-6f && lampIrradiance(1.f, 3.f, 3.f, 0.f) == 0.f &&
              std::fabs(lampIrradiance(0.5f, 1.5f, 3.f, 1.f) - 0.5f * 2.f * std::pow(1.f - 0.75f, 2.f)) < 1e-6f,
              "P-11 the irradiance is the deferred pass's attenuation");
    }

    void testPack()
    {
        int wrong = 0;
        for (uint32_t n = kFaceMin; n <= kFaceMax; n <<= 1)
        {
            for (uint32_t y = 0; y + n <= 4096; y += n)
            {
                for (uint32_t x = 0; x + n <= 4096; x += n)
                {
                    const Rect r{ x, y, n };
                    const float p = packFaceRect(r);
                    Rect back;
                    wrong += !(p >= 1.f) || !unpackFaceRect(p, back) || !(back == r) || float(int(p)) != p || p >= 32768.f;
                }
            }
        }
        check(wrong == 0, "Pack every face rect of the 4096 atlas round-trips exactly", wrong);
        Rect none;
        check(!unpackFaceRect(packFaceRect(Rect{ 0, 0, 64 }), none) && !unpackFaceRect(0.f, none), "Pack a rect below the floor, or none, is no rect");
        check(packFaceRect(Rect{ 4096, 0, 128 }) == 0.f, "Pack a rect outside the 4096 atlas is no rect");
    }

    // the faces' volumes hold their interior directions, and the pick's face volume holds the point (the change index's input)
    void testVolumes()
    {
        std::mt19937 rng(13);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        const float centre[3] = { 5.f, 6.f, 7.f };
        const float far_m = faceFar(4.f);
        int missed = 0;
        for (int k = 0; k < 50000; ++k)
        {
            float d[3] = { u(rng), u(rng), u(rng) };
            const float l = std::sqrt(dot3(d, d));
            if (l < 1e-3f)
            {
                continue;
            }
            const float r = 0.05f + (far_m - 0.1f) * (0.5f + 0.5f * u(rng)) * 0.55f;
            float xyw[3];
            const int f = pickFace(d, xyw);
            Aabb box;
            for (int i = 0; i < 3; ++i)
            {
                box.lo[i] = box.hi[i] = centre[i] + d[i] * r / l;
            }
            const Pyramid py = facePyramid(centre, f, float(faceTanHalf(256)), kNearM, far_m);
            missed += intersects(py, box) ? 0 : 1;
        }
        check(missed == 0, "volumes: a point lies in its face's volume", missed);
        const float cam[3] = { 5.f, 6.f, 17.f };
        check(std::fabs(lampDistance(cam, centre, 6.f) - 4.f) < 1e-5f && lampDistance(centre, centre, 6.f) == 0.f, "volumes: the camera's distance to the lamp's sphere");
    }
}

int main()
{
    testPick();
    testMatrices();
    testSeams();
    testSize();
    testCapacity();
    testDrawThenSwap();
    testSkip();
    testPose();
    testEvict();
    testOwnBudget();
    testMerged();
    testFillAndMovers();
    testIgnoreSource();
    testOwn();
    testPack();
    testVolumes();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("llpointshadowpool: all checks passed\n");
    return 0;
}
