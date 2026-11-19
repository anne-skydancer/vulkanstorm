// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llprojectorshadowpool.h (no viewer libraries, no GL):
//   Options: --table (the cadence table of the C1 commit), --bench-index (the change-index bench, not run in CI).
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llprojectorshadowpool_test.cpp
// Spec: reports/projector-pool-2026-11-13/design-final.md, section 5 stage P1:
//   T1  256 lights x 2048^2 fit at the floor
//   T2  one 2048^2 lamp crossing the range never oscillates the level; 0.60 / 0.45 / 3 s holds
//   T3  the same set gives the same layout, whatever the input order
//   T4  repack plan: same-size moves are copies, every new rect is inside the atlas, no overlaps
//   T5  box against six planes vs a brute-force reference; the 8 m grid gives the same hits
//   T6  a dirty box pushed during a render lands in the next frame (double buffer)
//   T7  scheduler: never-drawn first, then oldest first by frames (distance only breaks a tie), a fixed per-frame cap,
//       every cheap tile every frame, staleness bounded by ceil(N / tiles per frame), avatar items never exceed A_max
//   T8  the 8192 atlas tier: tier choice by VRAM, dedicated memory and GL_MAX_TEXTURE_SIZE, the buddy allocator, repack and level
//       controller at 8192, and the L1 filter rules (the radius between the 1.5-texel floor and the 12-texel cap, the 4.5 cm
//       emitter base times the softness, the point lights' prepared set: the C++ twins of the shader's numbers)
//   P3  the change causes, the dirty list's bound, the change index against brute force, the mover rules, the LOD pin's books,
//       the tile verdict, and a static scene that draws nothing
//   T7Differential  the partial-sort selection against the full sort of the former one (about 18,000 comparisons)
//   P3IndexSubset   the change index over a subset (the pending tiles left out) gives the full set's hits minus those keys
//   BoxFilter       the per-lamp box filter keeps every box that touches a tile of the lamp, in order, whatever the bounds
//   Cells (L2)      the size from the lamp alone (guard included), the distance bands and their hysteresis, the pressure valve (no
//                   size change while the atlas has room; farthest first over the budget; the ceiling; no hold for empty steps), and
//                   the block a draw-then-swap resize gets (BuddyAtlas::largestAfterFree)
//   ScriptLamps (S2) the hold of a lamp switched off, black or at radius 0 (holdOffFrame: blinkers keep their tiles, a day and night
//                   switch gives them back once); the silhouette key's inputs (silhouetteFaceMix: each moves the key, nothing else is one;
//                   L3: the RGB of a face the blend pass draws is one)
//   TextureEntryRebuilds (S2) a tint, emissive or material that leaves the silhouette alone pushes nothing at its rebuild either
//                   (the texture-entry mark), in a static or an active linkset (the bridge's markMoved and its move, the follow-up
//                   review's B1); a silhouette change pushes once, wherever the setter's markTextured runs (L3: a tint blinker on a
//                   blended face, once a toggle); a new LOD, a scale, markForUpdate, a move around a tint and the linkset's own move still push
//   Placement (L2)  a GL-free replay of layout() and renderTile()'s resize: an arrival that finds no room waits for the shrinks and the
//                   pressure ends at the rule's (review B1); a shortfall only at the ceiling; after a compaction the free texels hold
//                   every arrival set and growth they cover; a growth blocked by fragmentation gets one compaction after its hold (Q1)
// plus the size rule and the level controller (the C++ twin of the spec's rules; P2's one-tile size and global level are twins here).

#include "llprojectorshadowpool.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <random>
#include <set>
#include <string>

using namespace llprojectorpool;

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

    const float kPi = 3.14159265358979f;

    bool rectInside(const Rect& r, uint32_t atlas)
    {
        return uint64_t(r.x) + r.size <= atlas && uint64_t(r.y) + r.size <= atlas;
    }
    bool rectOverlap(const Rect& a, const Rect& b)
    {
        return a.x < b.x + b.size && b.x < a.x + a.size && a.y < b.y + b.size && b.y < a.y + a.size;
    }

    // Twins of P2's rules, which the viewer no longer calls (L2 sizes every tile with the valve): the square lamp of one tile, the demand
    // of tiles that all shrink alike, and the global level, which is the valve with every tile in the farthest band.
    // N_base = clamp(pow2_round(2 * tan(fov / 2) * d_ref / 4 mm), 128, 2048)
    uint32_t baseSize(float radius, float fov_radians)
    {
        // tan() is non-monotone past pi: clamp the half angle to [0, 1.55] rad, far beyond any real fov
        const double half = std::min(std::max(0.5 * double(fov_radians), 0.0), 1.55);
        return tileBaseSize(2.0 * std::tan(half), double(radius), 0);
    }
    // U(s) = sum N_i(s)^2 / A^2
    double demand(const std::vector<uint32_t>& bases, int level, uint32_t atlas)
    {
        double sum = 0.0;
        for (uint32_t b : bases)
        {
            const double n = double(levelSize(b, level));
            sum += n * n;
        }
        const double a = double(atlas);
        return sum / (a * a);
    }
    bool updateLevel(LevelController& lc, const std::vector<uint32_t>& bases, uint32_t atlas, float dt_seconds)
    {
        std::vector<ValveTile> tiles(bases.size());
        for (size_t i = 0; i < bases.size(); ++i)
        {
            tiles[i].base = bases[i];
        }
        return lc.update(tiles, atlas, dt_seconds);
    }
    constexpr int kMaxPressure = kMaxBand + kMaxLevel; // every band at kMaxLevel octaves
    constexpr float kFloorMovingFps = 25.f;           // the owner's floor moving (the cap's effect on it, testP4Caps)

    std::vector<LayoutRequest> requestsFor(const std::vector<uint32_t>& bases, int level)
    {
        std::vector<LayoutRequest> r(bases.size());
        for (size_t i = 0; i < bases.size(); ++i)
        {
            r[i].key = uint32_t(i + 1);
            r[i].size = levelSize(bases[i], level);
        }
        return r;
    }

    // ---- size rule ----
    void testSizeRule()
    {
        // a 90 degree lamp of radius >= 2.7 m gets 2048, of 1 m gets 1024 (spec 4.2 example)
        check(baseSize(2.7f, kPi / 2) == 2048, "size 90deg r=2.7", baseSize(2.7f, kPi / 2));
        check(baseSize(8.0f, kPi / 2) == 2048, "size 90deg r=8");
        check(baseSize(1.0f, kPi / 2) == 1024, "size 90deg r=1", baseSize(1.0f, kPi / 2));
        // clamps
        check(baseSize(0.01f, 0.05f) == 128, "size lower clamp");
        check(baseSize(100.f, 3.0f) == 2048, "size upper clamp");
        // only the light decides, never the camera: no distance argument exists. Monotone in radius.
        uint32_t prev = 0;
        for (float r = 0.1f; r < 10.f; r += 0.1f)
        {
            const uint32_t n = baseSize(r, 1.2f);
            check(n >= prev, "size monotone in radius", r, n);
            check((n & (n - 1)) == 0, "size power of two", n);
            prev = n;
        }
        // level size: halves per level, floor 64
        check(levelSize(2048, 0) == 2048 && levelSize(2048, 1) == 1024 && levelSize(2048, 5) == 64, "level halves");
        check(levelSize(2048, 9) == 64 && levelSize(128, 3) == 64, "floor 64");
        check(levelSize(128, 0) == 128, "level 0 is base");
    }

    // ---- T1 ----
    void testT1()
    {
        const std::vector<uint32_t> bases(256, 2048u);
        const uint32_t atlas = 4096;
        LevelController lc;
        updateLevel(lc, bases, atlas, 0.016f);
        check(lc.level() > 0, "T1 level shrinks for 256 big lamps", lc.level());
        check(demand(bases, lc.level(), atlas) <= kShrinkAbove, "T1 demand under threshold", demand(bases, lc.level(), atlas));
        // and every lamp is actually placed at that level, and also at the floor
        for (int level : { lc.level(), kMaxLevel })
        {
            const RepackPlan plan = planRepack(requestsFor(bases, level), atlas);
            check(plan.ok, "T1 256 lamps placed", level, double(plan.unplaced.size()));
        }
        // at the floor 256 lamps use 6.25 % of a 4096 atlas; even 2048^2 atlases and 4096 floors fit
        check(demand(bases, kMaxLevel, 4096) < 0.07, "T1 floor demand", demand(bases, kMaxLevel, 4096));
        check(planRepack(requestsFor(std::vector<uint32_t>(256, 2048u), kMaxLevel), 2048).ok, "T1 floor fits 2048 atlas");
        const RepackPlan full = planRepack(requestsFor(std::vector<uint32_t>(4096, 2048u), kMaxLevel), 4096);
        check(full.ok, "T1 4096 floor tiles fill a 4096 atlas");
        const RepackPlan over = planRepack(requestsFor(std::vector<uint32_t>(4097, 2048u), kMaxLevel), 4096);
        check(!over.ok && over.unplaced.size() == 1, "T1 one over fails cleanly", double(over.unplaced.size()));
    }

    // ---- T8 ----
    void testT8()
    {
        // the tier: 2048 below 3 GB, 4096, 8192 from 12 GB, never above what the driver can make
        check(atlasTier(0, 16384, true) == 4096, "T8 unknown VRAM gets the default tier");
        check(atlasTier(2048, 16384, true) == 2048, "T8 2 GB gets 2048");
        check(atlasTier(3071, 16384, true) == 2048 && atlasTier(3072, 16384, true) == 4096, "T8 the 3 GB edge");
        check(atlasTier(8192, 16384, true) == 4096, "T8 8 GB gets 4096");
        // a "12 GB" card reports a little under 12288 MB: the edge is 11 GB (11264 MB)
        check(atlasTier(11263, 16384, true) == 4096 && atlasTier(11264, 16384, true) == 8192, "T8 the 11 GB edge");
        check(atlasTier(12200, 16384, true) == 8192, "T8 a 12 GB card reporting 12200 MB gets 8192");
        check(atlasTier(24576, 16384, true) == 8192, "T8 24 GB gets 8192");
        // shared memory (a DXGI budget of system RAM) never gets 8192
        check(atlasTier(24576, 16384, false) == 4096 && atlasTier(12288, 16384, false) == 4096, "T8 memory not known to be dedicated stays at 4096");
        check(atlasTier(2048, 16384, false) == 2048, "T8 the small tier does not depend on dedicated memory");
        check(atlasTier(24576, 4096, true) == 4096, "T8 GL_MAX_TEXTURE_SIZE 4096 limits it");
        check(atlasTier(24576, 2048, true) == 2048 && atlasTier(24576, 1024, true) == 2048, "T8 never below the smallest tier");
        check(atlasTier(24576, 0, true) == 8192, "T8 unknown max texture size does not limit");
        check(atlasBytes(2048) >> 20 == 32 && atlasBytes(4096) >> 20 == 128 && atlasBytes(8192) >> 20 == 512, "T8 MiB of S + L");

        const uint32_t atlas = 8192;
        BuddyAtlas b(atlas);
        check(b.atlasSize() == 8192, "T8 buddy takes 8192", b.atlasSize());
        // sixteen 2048 tiles fill it; a seventeenth does not fit; the floor holds 128 * 128 tiles
        std::vector<Rect> rects;
        Rect r;
        for (int i = 0; i < 16; ++i)
        {
            check(b.alloc(2048, r) && rectInside(r, atlas), "T8 sixteen 2048 tiles fit", i);
            rects.push_back(r);
        }
        check(!b.alloc(64, r), "T8 a full atlas has no room for a 64 tile");
        bool clean = true;
        for (size_t i = 0; i < rects.size(); ++i)
            for (size_t j = i + 1; j < rects.size(); ++j) clean = clean && !rectOverlap(rects[i], rects[j]);
        check(clean, "T8 the sixteen tiles do not overlap");
        for (const Rect& q : rects) b.free(q);
        check(b.alloc(8192, r) && r.x == 0 && r.y == 0 && r.size == 8192, "T8 freed tiles merge back into one 8192 block");

        check(planRepack(requestsFor(std::vector<uint32_t>(16384, 2048u), kMaxLevel), atlas).ok, "T8 16384 floor tiles fill an 8192 atlas");
        const RepackPlan over = planRepack(requestsFor(std::vector<uint32_t>(16385, 2048u), kMaxLevel), atlas);
        check(!over.ok && over.unplaced.size() == 1, "T8 one over fails cleanly", double(over.unplaced.size()));

        // the level: nine 2048 lamps (U = 0.5625) stay at level 0, the tenth shrinks; at 4096 the third already does
        LevelController lc;
        updateLevel(lc, std::vector<uint32_t>(9, 2048u), atlas, 0.016f);
        check(lc.level() == 0, "T8 nine 2048 lamps stay at level 0 on 8192", lc.level());
        updateLevel(lc, std::vector<uint32_t>(10, 2048u), atlas, 0.016f);
        check(lc.level() == 1, "T8 the tenth shrinks", lc.level());
        LevelController l4;
        updateLevel(l4, std::vector<uint32_t>(3, 2048u), 4096, 0.016f);
        check(l4.level() == 1, "T8 the same on 4096: the third lamp shrinks", l4.level());
        for (uint32_t n : { 5u, 12u, 40u, 100u })
        {
            LevelController c;
            const std::vector<uint32_t> bases(n, 2048u);
            updateLevel(c, bases, atlas, 0.016f);
            check(demand(bases, c.level(), atlas) <= kShrinkAbove, "T8 demand under threshold", n, demand(bases, c.level(), atlas));
            check(planRepack(requestsFor(bases, c.level()), atlas).ok, "T8 placed at the chosen level", n);
        }

        // a repack from level 0 to 1 and a compaction at 8192: moves inside the atlas, copies for equal sizes
        std::mt19937 rng(8);
        std::vector<uint32_t> bases;
        for (int i = 0; i < 40; ++i) bases.push_back(128u << (rng() % 5));
        const RepackPlan old_plan = planRepack(requestsFor(bases, 0), atlas);
        check(old_plan.ok, "T8 old plan ok");
        std::vector<LayoutRequest> req = requestsFor(bases, 1);
        for (size_t i = 0; i < req.size(); ++i)
        {
            req[i].has_old = true;
            req[i].old_rect = old_plan.placed[i];
            req[i].drawn = true;
        }
        const RepackPlan plan = planRepack(req, atlas);
        bool ok = plan.ok && plan.moves.size() == req.size();
        for (const Move& m : plan.moves)
        {
            ok = ok && m.copy == (m.src.size == m.dst.size) && rectInside(m.dst, atlas) && rectInside(m.src, atlas);
        }
        check(ok, "T8 repack 0 -> 1 at 8192: every move inside, copy iff equal size");
        for (size_t i = 0; i < plan.placed.size() && ok; ++i)
        {
            ok = rectInside(plan.placed[i], atlas) && plan.placed[i].size == req[i].size;
            for (size_t j = i + 1; j < plan.placed.size() && ok; ++j) ok = !rectOverlap(plan.placed[i], plan.placed[j]);
        }
        check(ok, "T8 new rects inside and disjoint at 8192");
        // the repack plan's own allocator at 8192 reports its size, and a tile past 8192 is refused
        check(plan.buddy.atlasSize() == 8192, "T8 the plan's buddy is the 8192 allocator", plan.buddy.atlasSize());
        BuddyAtlas big(atlas);
        check(!big.alloc(16384, r), "T8 a tile larger than the atlas is refused");

        // L1: the filter runs at the physical radius between the floor (1.5 texels) and the cap (12), per axis; no cap in metres, so
        // a coarser tile (a level step) only changes how many texels the same physical radius is
        check(kProjectorLamp.kernel_min == 1.5f && kProjectorLamp.kernel_max == 12.0f, "L1 projector filter: floor 1.5 texels, cap 12 (the shader's)");
        check(filterRadiusTexels(kProjectorLamp, 0.2f) == 1.5f && filterRadiusTexels(kProjectorLamp, 0.0f) == 1.5f, "L1 a penumbra under 1.5 texels is filtered at the floor");
        check(filterRadiusTexels(kProjectorLamp, 4.25f) == 4.25f, "L1 between floor and cap: the physical radius itself");
        check(filterRadiusTexels(kProjectorLamp, 40.0f) == 12.0f, "L1 the cap: 12 texels");
        check(filterRadiusTexels(kProjectorLamp, std::numeric_limits<float>::quiet_NaN()) == 1.5f &&
                  filterRadiusTexels(kProjectorLamp, std::numeric_limits<float>::infinity()) == 12.0f && filterRadiusTexels(kProjectorLamp, -3.0f) == 1.5f,
              "L1 bad radii stay in [floor, cap]");
        {
            // a level step halves the texels per metre: the same physical radius is half as many texels (no metric cap holds it)
            const float c1 = 0.25f, k = 1.0f, depth = 0.05f, light = emitterExtent(kProjectorLamp, 1.0f); // 4.6 and 2.3 texels
            const float r1024 = penumbraTexelsPerDepth(light, k, 1024, c1) * depth, r512 = penumbraTexelsPerDepth(light, k, 512, c1) * depth;
            check(std::fabs(r1024 - 2.f * r512) < 1e-4f && filterRadiusTexels(kProjectorLamp, r1024) == r1024 && filterRadiusTexels(kProjectorLamp, r512) == r512,
                  "L1 a level step halves the radius in texels, the radius in metres stays", r1024, r512);
        }
        // the emitter's extent: the projector lamp's 4.5 cm base times the softness (RenderShadowLightSize), whatever the prim
        check(emitterExtent(kProjectorLamp, 1.0f) == 0.045f, "L1 emitter extent is 4.5 cm at softness 1 (the slider's top)");
        check(emitterExtent(kProjectorLamp, 0.0f) == 0.0f, "L1 a softness of 0 is a zero-size emitter (hard shadows; the filter keeps its 1.5 texel floor)");
        check(std::fabs(emitterExtent(kProjectorLamp, kSoftnessDefault) - 0.01035f) < 1e-7f, "L1 emitter extent at the default softness 0.23 is 1.035 cm",
              emitterExtent(kProjectorLamp, kSoftnessDefault));
        check(std::fabs(emitterExtent(kProjectorLamp, 0.5f) - 0.0225f) < 1e-8f, "L1 emitter extent scales with the softness");
        check(emitterExtent(kProjectorLamp, std::numeric_limits<float>::quiet_NaN()) == emitterExtent(kProjectorLamp, kSoftnessDefault) &&
                  emitterExtent(kProjectorLamp, std::numeric_limits<float>::infinity()) == emitterExtent(kProjectorLamp, kSoftnessDefault) &&
                  emitterExtent(kProjectorLamp, -1.0f) == emitterExtent(kProjectorLamp, kSoftnessDefault),
              "L1 a negative or non-finite softness gets the default");
        // the point lights' set (stage L4): the same lamp base and floor, the cap its 16-texel face guard less the bilinear footprint
        check(kPointLamp.emitter_m == kProjectorLamp.emitter_m && kPointLamp.kernel_min == kProjectorLamp.kernel_min && kPointLamp.kernel_max + 2.0f <= 16.0f &&
                  kPointLamp.kernel_max >= kProjectorLamp.kernel_max,
              "L1 point-light set: same base and floor, cap within a 16-texel guard");
        check(float(kCellGuard) >= kProjectorLamp.kernel_max + 2.0f, "L1 a cell's guard covers the projector cap plus the bilinear footprint");
        // the penumbra scale per axis: (0.5 / c1) * light * k * N, the x axis from light.x and k.x
        check(std::fabs(penumbraTexelsPerDepth(0.1f, 2.0f, 1024, 0.5f) - 204.8f) < 1e-3f, "T8 penumbra scale x", penumbraTexelsPerDepth(0.1f, 2.0f, 1024, 0.5f));
        check(std::fabs(penumbraTexelsPerDepth(0.02f, 0.5f, 1024, 0.5f) - 10.24f) < 1e-3f, "T8 penumbra scale y (axes not swapped)", penumbraTexelsPerDepth(0.02f, 0.5f, 1024, 0.5f));
    }

    // ---- T2 ----
    void testT2()
    {
        const uint32_t atlas = 4096;
        // two 2048^2 lamps: U = 0.5, level 0. A third: U = 0.75 > 0.60 -> level 1.
        std::vector<uint32_t> two(2, 2048u), three(3, 2048u);
        LevelController lc;
        check(!updateLevel(lc, two, atlas, 0.016f) && lc.level() == 0, "T2 two lamps level 0");
        check(updateLevel(lc, three, atlas, 0.016f) && lc.level() == 1, "T2 third lamp shrinks", lc.level());

        // the third lamp flickers across the range boundary (jittering camera distance); the
        // reach hysteresis plus the level hysteresis keep the level at 1: no oscillation
        const float reach = 128.f;
        bool in = false;
        int changes = 0, flips = 0;
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> jitter(-1.5f, 1.5f);
        for (int f = 0; f < 3000; ++f)
        {
            // a slow sweep across both thresholds (add at 128, drop beyond 140.8) plus jitter,
            // so the lamp really comes and goes; it also toggles every frame near the add distance
            const float dist = 135.f + 15.f * std::sin(float(f) * 0.01f) + jitter(rng);
            const bool now = inRange(in, dist, reach);
            if (now != in)
            {
                ++flips;
            }
            in = now;
            if (updateLevel(lc, in ? three : two, atlas, 0.016f))
            {
                ++changes;
            }
        }
        check(flips > 4, "T2 the lamp really crosses the range", flips);
        check(changes == 0, "T2 no level change while a lamp crosses the range", changes);
        check(lc.level() == 1, "T2 stays at level 1 (U(0) = 0.5 > 0.45)", lc.level());
        {
            // demand toggling every frame between 0.50 (two) and 0.75 (three): above 0.60 only
            // with the third lamp, so the level steps down once and then holds at the grow band
            LevelController tg;
            int ch = 0;
            for (int f = 0; f < 600; ++f)
            {
                ch += updateLevel(tg, (f & 1) ? three : two, atlas, 0.016f) ? 1 : 0;
            }
            check(ch <= 1, "T2 per-frame toggling changes the level at most once", ch);
        }

        // reach hysteresis itself: in at D, out only beyond D + max(2, 0.1 D)
        check(inRange(false, 128.f, 128.f) && !inRange(false, 128.01f, 128.f), "T2 add at D");
        check(inRange(true, 140.7f, 128.f) && !inRange(true, 140.9f, 128.f), "T2 drop at D + 0.1 D", 0, 0);
        check(inRange(true, 43.9f, 40.f) && !inRange(true, 44.1f, 40.f), "T2 10 percent band at D = 40");
        check(inRange(true, 11.9f, 10.f) && !inRange(true, 12.1f, 10.f), "T2 2 m band for small D");

        // 0.60 / 0.45 / 3 s: 0.75 shrinks; 38 x 512^2 (0.59) holds; 40 x 512^2 (0.625) shrinks
        std::vector<uint32_t> exact60(1, 2048u);
        exact60.insert(exact60.end(), 8, 1024u);           // 0.25 + 8 * 0.0625 = 0.75
        LevelController a;
        updateLevel(a, exact60, atlas, 0.016f);
        check(a.level() == 1, "T2 0.75 shrinks", a.level());
        std::vector<uint32_t> u38(38, 512u);
        LevelController b;
        updateLevel(b, u38, atlas, 0.016f);
        check(demand(u38, 0, atlas) < kShrinkAbove && b.level() == 0, "T2 0.59 holds", demand(u38, 0, atlas));
        std::vector<uint32_t> u40(40, 512u);               // 0.625
        LevelController c;
        updateLevel(c, u40, atlas, 0.016f);
        check(demand(u40, 0, atlas) > kShrinkAbove && c.level() == 1, "T2 0.625 shrinks", c.level());

        // grow: from level 1, U(0) must be <= 0.45 for 3 s
        std::vector<uint32_t> low(28, 512u);                // 28/64 = 0.4375 <= 0.45 at level 0
        LevelController g;
        updateLevel(g, u40, atlas, 0.016f);
        check(g.level() == 1, "T2 start at 1");
        float t = 0.f;
        bool grew = false;
        while (t < 2.95f)
        {
            grew = updateLevel(g, low, atlas, 0.05f) || grew;
            t += 0.05f;
        }
        check(!grew && g.level() == 1, "T2 no grow before 3 s", t);
        for (int i = 0; i < 4; ++i)
        {
            updateLevel(g, low, atlas, 0.05f);
        }
        check(g.level() == 0, "T2 grows after 3 s", g.level());
        // a spike above 0.45 resets the hold
        std::vector<uint32_t> mid(30, 512u);                // 30/64 = 0.469 > 0.45: no grow
        LevelController r2;
        updateLevel(r2, u40, atlas, 0.016f);
        for (int i = 0; i < 50; ++i) updateLevel(r2, low, atlas, 0.05f);  // 2.5 s
        updateLevel(r2, mid, atlas, 0.05f);                                // reset
        for (int i = 0; i < 50; ++i) updateLevel(r2, low, atlas, 0.05f);  // 2.5 s again
        check(r2.level() == 1, "T2 interrupted hold does not grow", r2.level());
        // (a tile that finds no room is no input since L2: it waits for the shrinks being drawn, testPlacement)
        // never past the max level
        LevelController m;
        updateLevel(m, std::vector<uint32_t>(10000, 2048u), atlas, 0.016f);
        check(m.level() == kMaxLevel, "T2 level capped", m.level());
    }

    // ---- T3 ----
    void testT3()
    {
        std::mt19937 rng(11);
        std::vector<LayoutRequest> reqs;
        const uint32_t sizes[] = { 512, 256, 256, 128, 64, 64 };
        for (uint32_t i = 0; i < 120; ++i)
        {
            LayoutRequest q;
            q.key = 1000 + i * 3;
            q.size = sizes[rng() % 6];
            reqs.push_back(q);
        }
        const RepackPlan a = planRepack(reqs, 4096);
        check(a.ok, "T3 plan ok");
        for (int trial = 0; trial < 20; ++trial)
        {
            std::vector<LayoutRequest> shuffled = reqs;
            std::shuffle(shuffled.begin(), shuffled.end(), rng);
            const RepackPlan b = planRepack(shuffled, 4096);
            bool same = b.ok;
            for (size_t i = 0; same && i < shuffled.size(); ++i)
            {
                // find shuffled[i] in reqs by key
                size_t j = 0;
                while (reqs[j].key != shuffled[i].key) ++j;
                same = b.placed[i] == a.placed[j];
            }
            check(same, "T3 same set, same layout", trial);
        }
        // run twice, identical
        const RepackPlan a2 = planRepack(reqs, 4096);
        bool same = true;
        for (size_t i = 0; i < reqs.size(); ++i) same = same && a.placed[i] == a2.placed[i];
        check(same, "T3 repeatable");
    }

    // ---- the allocator a plan hands back (the viewer adopts it instead of allocating again) ----
    void testPlanBuddy()
    {
        const uint32_t atlas = 1024;
        std::vector<LayoutRequest> reqs;
        const uint32_t sizes[] = { 256, 128, 128, 64, 64, 64, 512, 256 };
        uint32_t key = 1;
        uint64_t used = 0;
        for (uint32_t sz : sizes)
        {
            LayoutRequest q;
            q.key = key++;
            q.size = sz;
            reqs.push_back(q);
            used += uint64_t(sz) * sz;
        }
        RepackPlan plan = planRepack(reqs, atlas);
        check(plan.ok, "buddy plan ok");
        check(plan.buddy.freeTexels() == uint64_t(atlas) * atlas - used, "plan.buddy holds exactly the placed tiles", double(plan.buddy.freeTexels()));
        // a further allocation from the adopted allocator does not overlap any placed rect
        Rect extra;
        check(plan.buddy.alloc(64, extra), "plan.buddy still has room");
        for (const Rect& r : plan.placed)
        {
            const bool apart = extra.x + extra.size <= r.x || r.x + r.size <= extra.x || extra.y + extra.size <= r.y || r.y + r.size <= extra.y;
            check(apart, "plan.buddy hands out a rect clear of the placed ones");
        }
        // freeing every placed rect (and the extra one) gives the whole atlas back as one block
        plan.buddy.free(extra);
        for (const Rect& r : plan.placed)
        {
            plan.buddy.free(r);
        }
        check(plan.buddy.freeTexels() == uint64_t(atlas) * atlas && plan.buddy.largestFree() == atlas, "freeing all restores one whole-atlas block");
    }

    // ---- allocator ----
    void testBuddy()
    {
        BuddyAtlas b(1024);
        std::vector<Rect> got;
        Rect r;
        while (b.alloc(64, r)) got.push_back(r);
        check(got.size() == 256, "buddy fills with 64s", double(got.size()));
        check(b.freeTexels() == 0 && b.largestFree() == 0, "buddy full");
        for (size_t i = 0; i < got.size(); ++i)
            for (size_t j = i + 1; j < got.size(); ++j)
                if (rectOverlap(got[i], got[j])) { check(false, "buddy overlap"); return; }
        std::mt19937 rng(3);
        std::shuffle(got.begin(), got.end(), rng);
        for (const Rect& g : got) b.free(g);
        check(b.largestFree() == 1024 && b.freeTexels() == 1024u * 1024u, "buddy merges back to one block");
        check(b.alloc(1024, r) && r.x == 0 && r.y == 0, "buddy whole atlas again");
        check(!b.alloc(64, r), "buddy none left");
        check(!b.alloc(100, r) && !b.alloc(32, r) && !b.alloc(2048, r), "buddy rejects bad sizes");
        // random alloc/free keeps accounting exact
        BuddyAtlas c(2048);
        std::vector<Rect> live;
        uint64_t used = 0;
        for (int i = 0; i < 4000; ++i)
        {
            if (live.empty() || (rng() % 3) != 0)
            {
                const uint32_t s = 64u << (rng() % 5);
                if (c.alloc(s, r)) { live.push_back(r); used += uint64_t(s) * s; }
            }
            else
            {
                const size_t k = rng() % live.size();
                c.free(live[k]);
                used -= uint64_t(live[k].size) * live[k].size;
                live.erase(live.begin() + long(k));
            }
            if (c.freeTexels() + used != 2048ull * 2048ull) { check(false, "buddy accounting", i); return; }
        }
    }

    // ---- T4 ----
    void testT4()
    {
        std::mt19937 rng(5);
        const uint32_t atlas = 4096;
        std::vector<uint32_t> bases;
        for (int i = 0; i < 90; ++i) bases.push_back(128u << (rng() % 3));
        // old layout at level 0, new one at level 1; half of the tiles are drawn
        const RepackPlan old_plan = planRepack(requestsFor(bases, 0), atlas);
        check(old_plan.ok, "T4 old plan ok");
        std::vector<LayoutRequest> req = requestsFor(bases, 1);
        for (size_t i = 0; i < req.size(); ++i)
        {
            req[i].has_old = true;
            req[i].old_rect = old_plan.placed[i];
            req[i].drawn = (i % 2) == 0;
        }
        // some tiles keep their size (base 64 stays 64 at level 1: 128 -> 64 changes; make two same)
        req[0].size = req[0].old_rect.size;
        req[2].size = req[2].old_rect.size;
        const RepackPlan plan = planRepack(req, atlas);
        check(plan.ok, "T4 plan ok");
        size_t drawn = 0;
        for (const LayoutRequest& q : req) drawn += q.drawn ? 1 : 0;
        check(plan.moves.size() == drawn, "T4 one move per drawn tile", double(plan.moves.size()), double(drawn));
        for (const Move& m : plan.moves)
        {
            check(m.copy == (m.src.size == m.dst.size), "T4 same-size moves are copies, others blits", m.key);
            check(rectInside(m.dst, atlas), "T4 move target inside", m.key);
            check(rectInside(m.src, atlas), "T4 move source inside", m.key);
        }
        check(std::any_of(plan.moves.begin(), plan.moves.end(), [](const Move& m) { return m.copy; }), "T4 has a copy");
        check(std::any_of(plan.moves.begin(), plan.moves.end(), [](const Move& m) { return !m.copy; }), "T4 has a blit");
        bool inside = true, clean = true;
        for (size_t i = 0; i < plan.placed.size(); ++i)
        {
            inside = inside && rectInside(plan.placed[i], atlas) && plan.placed[i].size == req[i].size;
            for (size_t j = i + 1; j < plan.placed.size(); ++j) clean = clean && !rectOverlap(plan.placed[i], plan.placed[j]);
        }
        check(inside, "T4 every new rect inside the atlas, sized as asked");
        check(clean, "T4 no two new rects overlap");
        // compaction: a repack with no size change moves tiles but never resizes: all moves copies
        std::vector<LayoutRequest> same = requestsFor(bases, 0);
        for (size_t i = 0; i < same.size(); ++i)
        {
            same[i].has_old = true;
            same[i].old_rect = old_plan.placed[i];
            same[i].drawn = true;
        }
        const RepackPlan comp = planRepack(same, atlas);
        bool all_copy = comp.ok;
        for (const Move& m : comp.moves) all_copy = all_copy && m.copy;
        check(all_copy, "T4 compaction is copies only");
    }

    // ---- T5 ----
    struct V3 { float v[3]; };

    void testT5()
    {
        std::mt19937 rng(21);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        auto unit = [&](float* o) {
            do { o[0] = u(rng); o[1] = u(rng); o[2] = u(rng); } while (detail::dot(o, o) < 0.01f || detail::dot(o, o) > 1.f);
            detail::normalise(o);
        };
        const int kTiles = 60;
        std::vector<Pyramid> pyr;
        TileGrid grid;
        for (int i = 0; i < kTiles; ++i)
        {
            float o[3] = { u(rng) * 60.f, u(rng) * 60.f, u(rng) * 20.f };
            float f[3], r[3], up[3], tmp[3] = { 0, 0, 1 };
            unit(f);
            detail::cross(f, tmp, r);
            if (detail::dot(r, r) < 1e-4f) { tmp[0] = 1; tmp[2] = 0; detail::cross(f, tmp, r); }
            detail::normalise(r);
            detail::cross(r, f, up);
            detail::normalise(up);
            const float fov = 0.4f + 1.2f * (u(rng) * 0.5f + 0.5f);
            pyr.push_back(makePyramid(o, f, r, up, fov, 0.05f, 4.f + 20.f * (u(rng) * 0.5f + 0.5f)));
            grid.add(uint32_t(i), pyr.back().bounds);
        }
        int hits = 0, misses_checked = 0;
        std::vector<uint32_t> cand;
        for (int t = 0; t < 4000; ++t)
        {
            Aabb box;
            for (int i = 0; i < 3; ++i)
            {
                const float c = u(rng) * (i == 2 ? 20.f : 60.f);
                const float h = 0.05f + std::fabs(u(rng)) * 6.f;
                box.lo[i] = c - h;
                box.hi[i] = c + h;
            }
            grid.candidates(box, cand);
            std::set<uint32_t> via_grid;
            for (uint32_t i : cand)
            {
                if (intersects(pyr[i], box)) via_grid.insert(i);
            }
            for (int i = 0; i < kTiles; ++i)
            {
                // brute force A: the six-plane test over every tile, no grid. The grid may only
                // skip tiles whose bounds do not share a cell with the box (then no hit is possible).
                const bool brute = intersects(pyr[i], box) && overlaps(pyr[i].bounds, box);
                const bool grid_hit = via_grid.count(uint32_t(i)) != 0;
                if (brute != (grid_hit && overlaps(pyr[i].bounds, box)))
                {
                    check(false, "T5 grid vs brute force", t, i);
                    return;
                }
                // brute force B (no false negatives): sample points in the box; a point inside
                // all six planes means the test must say yes (and the grid must find it)
                bool point_inside = false;
                for (int s = 0; s < 40 && !point_inside; ++s)
                {
                    float p[3];
                    for (int k = 0; k < 3; ++k) p[k] = box.lo[k] + (box.hi[k] - box.lo[k]) * (u(rng) * 0.5f + 0.5f);
                    bool in = true;
                    for (const Plane& pl : pyr[i].planes) in = in && (detail::dot(pl.n, p) + pl.d >= 0.f);
                    point_inside = in;
                }
                if (point_inside)
                {
                    ++hits;
                    check(intersects(pyr[i], box), "T5 no false negative", t, i);
                    check(grid_hit, "T5 grid finds a true hit", t, i);
                }
                else if (!intersects(pyr[i], box))
                {
                    ++misses_checked;
                }
            }
        }
        check(hits > 50 && misses_checked > 1000, "T5 the test saw both hits and misses", hits, misses_checked);
        // a pyramid's own apex and a point just beyond the far plane
        {
            const float o[3] = { 0, 0, 0 }, f[3] = { 1, 0, 0 }, r[3] = { 0, -1, 0 }, up[3] = { 0, 0, 1 };
            const Pyramid p = makePyramid(o, f, r, up, kPi / 2, 0.1f, 10.f);
            const Aabb in = { { 4, -0.5f, -0.5f }, { 5, 0.5f, 0.5f } };
            const Aabb behind = { { -5, -0.5f, -0.5f }, { -4, 0.5f, 0.5f } };
            const Aabb beyond = { { 11, -0.5f, -0.5f }, { 12, 0.5f, 0.5f } };
            const Aabb aside = { { 4, 20, -0.5f }, { 5, 21, 0.5f } };
            check(intersects(p, in) && !intersects(p, behind) && !intersects(p, beyond) && !intersects(p, aside),
                  "T5 axis-aligned pyramid");
        }
    }

    // ---- T6 ----
    void testT6()
    {
        DirtyList d;
        Aabb a{ { 0, 0, 0 }, { 1, 1, 1 } }, b{ { 5, 5, 5 }, { 6, 6, 6 } }, c{ { 9, 9, 9 }, { 10, 10, 10 } };
        d.push(a);
        const std::vector<DirtyBox>& frame1 = d.swapForApply();
        check(frame1.size() == 1, "T6 frame 1 sees its push", double(frame1.size()));
        // pushes made while frame 1's tiles render
        d.push(b);
        d.push(c);
        check(frame1.size() == 1, "T6 a push during render does not enter the applied list", double(frame1.size()));
        check(d.pending() == 2, "T6 it waits", double(d.pending()));
        const std::vector<DirtyBox>& frame2 = d.swapForApply();
        check(frame2.size() == 2 && frame2[0].box.lo[0] == 5.f && frame2[1].box.lo[0] == 9.f, "T6 it lands next frame",
              double(frame2.size()));
        check(d.swapForApply().empty(), "T6 and is not delivered twice");
        // shiftObjects moves pending boxes
        d.push(a);
        d.translatePending(10.f, 0.f, -1.f);
        const std::vector<DirtyBox>& f4 = d.swapForApply();
        check(f4.size() == 1 && f4[0].box.lo[0] == 10.f && f4[0].box.hi[2] == 0.f, "T6 translate pending");
        // pose change thresholds
        Pose p0, p1;
        check(!poseChanged(p0, p1), "pose same");
        p1.pos[0] = 0.0005f;
        check(!poseChanged(p0, p1), "pose 0.5 mm is below the threshold");
        p1.pos[0] = 0.002f;
        check(poseChanged(p0, p1), "pose 2 mm changes");
        p1 = p0; p1.radius = 0.005f;
        check(!poseChanged(p0, p1), "radius 5 mm below");
        p1.radius = 0.02f;
        check(poseChanged(p0, p1), "radius 2 cm changes");
        p1 = p0; p1.region = 5;
        check(poseChanged(p0, p1), "region change");
        p1 = p0; for (int i = 0; i < 4; ++i) p1.rot[i] = -p0.rot[i];
        check(!poseChanged(p0, p1), "q and -q are the same rotation");
        p1 = p0; p1.fov = 0.001f;
        check(poseChanged(p0, p1), "fov changes");
    }

    // ---- T7 ----
    void testT7()
    {
        // never-drawn first, nearest first, before anything dirty; then oldest (frames) first
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, false, true, 1.f, 0.05f, 1 });      // dirty, near, 1 frame old
            items.push_back({ 2, true, false, 50.f, 0.05f, 0 });      // never drawn, far
            items.push_back({ 3, true, false, 5.f, 0.05f, 0 });       // never drawn, near
            items.push_back({ 4, false, true, 90.f, 0.05f, 50 });   // old
            const std::vector<uint32_t> o = selectStatic(items, 10.f);
            check(o.size() == 4 && o[0] == 3 && o[1] == 2, "T7 never-drawn first, nearest first");
            check(o.size() == 4 && o[2] == 4 && o[3] == 1, "T7 then oldest, then the younger");
        }
        // the age beats distance; the distance only breaks a tie
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, false, true, 2.f, 0.05f, 3 });
            items.push_back({ 2, false, true, 100.f, 0.05f, 9 });
            items.push_back({ 3, false, true, 90.f, 0.05f, 8 });
            const std::vector<uint32_t> o = selectStatic(items, 10.f);
            check(o.size() == 3 && o[0] == 2 && o[1] == 3 && o[2] == 1, "T7 oldest first, whatever the distance");
            std::vector<StaticItem> tie;
            tie.push_back({ 1, false, true, 2.f, 0.05f, 4 });
            tie.push_back({ 2, false, true, 1.f, 0.05f, 4 });
            const std::vector<uint32_t> o2 = selectStatic(tie, 10.f);
            check(o2.size() == 2 && o2[0] == 2, "T7 equal ages: the nearest first");
        }
        // cap and progress: at least one item even when it alone exceeds the cap; none when idle
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, true, false, 3.f, 0.9f, 0 });
            items.push_back({ 2, true, false, 4.f, 0.1f, 0 });
            const std::vector<uint32_t> o = selectStatic(items, 0.25f);
            check(o.size() == 1 && o[0] == 1, "T7 floor of one item over the cap", double(o.size()));
            std::vector<StaticItem> many;
            for (uint32_t i = 1; i <= 20; ++i) many.push_back({ i, false, true, float(i), 0.1f, 0 });
            const std::vector<uint32_t> o2 = selectStatic(many, 0.25f);
            check(o2.size() == 2, "T7 cap stops after two 0.1 ms items", double(o2.size()));
            std::vector<StaticItem> idle;
            idle.push_back({ 1, false, false, 1.f, 0.1f, 0 });
            check(selectStatic(idle, 0.25f).empty(), "T7 a static scene renders nothing");
        }
        // Simulated frames: every tile is pending each frame and its age is the frames since it was drawn (all start as drawn
        // at frame 0, so the first frames serve them by distance). The statistics start once every tile has been drawn once,
        // which is when the order has settled into a steady round robin. Returns the largest age at which a tile was picked
        // from then on, and whether any tile was picked twice before all had been picked once (a round).
        struct Sim { uint32_t worst = 0; bool twice = false; std::vector<uint32_t> served; };
        auto simulate = [](uint32_t n, float cost_ms, float cap_ms, uint32_t frames, bool near_first_distances) {
            std::vector<StaticItem> items;
            std::vector<uint32_t> drawn(n + 1, 0);
            for (uint32_t i = 1; i <= n; ++i)
            {
                items.push_back({ i, false, true, near_first_distances ? float(i) : float(n + 1 - i), cost_ms, 0 });
            }
            Sim sim;
            sim.served.assign(n + 1, 0);
            std::set<uint32_t> round;
            uint32_t drawn_once = 0;
            bool settled = false; // every tile has been drawn once (at the end of an earlier frame)
            for (uint32_t f = 1; f <= frames; ++f)
            {
                for (StaticItem& it : items) it.age_frames = f - drawn[it.key];
                const std::vector<uint32_t> o = selectStatic(items, cap_ms);
                for (uint32_t k : o)
                {
                    if (settled)
                    {
                        sim.worst = std::max(sim.worst, f - drawn[k]);
                        if (round.count(k)) sim.twice = true;
                        round.insert(k);
                        if (round.size() == n) round.clear();
                    }
                    if (drawn[k] == 0) ++drawn_once;
                    drawn[k] = f;
                    ++sim.served[k];
                }
                if (drawn_once == n) settled = true;
            }
            return sim;
        };
        // a near tile never starves a far one (one item per frame under the cap)
        {
            const Sim s2 = simulate(2, 0.2f, 0.25f, 200, true);
            check(s2.served[1] == 100 && s2.served[2] == 100, "T7 near and far tiles are served alike", s2.served[1], s2.served[2]);
            check(s2.worst <= 2 && !s2.twice, "T7 staleness bounded by ceil(2 / 1)", s2.worst);
        }
        // four cheap tiles: all four every frame under the fixed cap
        {
            std::vector<StaticItem> items;
            for (uint32_t i = 1; i <= 4; ++i) items.push_back({ i, false, true, float(i) * 3.f, 0.07f, 1 });
            check(selectStatic(items, kFrameCapMs).size() == 4, "T7 four cheap tiles all run every frame (fixed cap)");
            const Sim s4 = simulate(4, 0.07f, kFrameCapMs, 100, true);
            check(s4.worst == 1, "T7 four cheap tiles: every tile redrawn every frame", s4.worst);
        }
        // 30 tiles under a budget for 6: staleness at most 5 frames, nobody served twice before all once
        {
            const Sim s30 = simulate(30, 0.1f, 0.6f + 0.01f, 150, true);
            check(s30.worst <= 5, "T7 30 tiles: staleness at most 5 frames", s30.worst);
            check(!s30.twice, "T7 30 tiles: no tile twice before all once");
            const Sim far30 = simulate(30, 0.1f, 0.6f + 0.01f, 150, false);
            check(far30.worst <= 5 && !far30.twice, "T7 30 tiles: the same with the distances reversed", far30.worst);
        }
        // cold start with the viewer's real prior: the running average starts at 0.5 ms, above the whole cap, and a new
        // record's prediction is coldItemMs, so several never-drawn tiles are served in one frame, not one
        {
            check(coldItemMs(0.5f) <= kFrameCapMs / 4.f, "T7 the cold prediction is at most a quarter of the cap", coldItemMs(0.5f));
            check(coldItemMs(0.07f) == 0.07f, "T7 a measured average below the quarter is kept", coldItemMs(0.07f));
            std::vector<StaticItem> cold;
            for (uint32_t i = 1; i <= 20; ++i) cold.push_back({ i, true, false, float(i), coldItemMs(0.5f), 0 });
            const std::vector<uint32_t> o = selectStatic(cold, kFrameCapMs);
            check(o.size() >= 3 && o.size() <= 4, "T7 a 0.5 ms prior still serves several never-drawn tiles per frame", double(o.size()));
            check(o.size() >= 1 && o[0] == 1, "T7 the nearest never-drawn tile first");
            std::vector<StaticItem> old_prior;
            for (uint32_t i = 1; i <= 20; ++i) old_prior.push_back({ i, true, false, float(i), 0.5f, 0 });
            check(selectStatic(old_prior, kFrameCapMs).size() == 1, "T7 (the uncapped 0.5 ms prior would serve one tile)");
        }
        // cold start: 20 never-drawn tiles with no history (cost 0, NaN) do not all run in one frame
        {
            std::vector<StaticItem> cold;
            for (uint32_t i = 1; i <= 20; ++i) cold.push_back({ i, true, false, float(i), 0.f, 0 });
            const size_t n = selectStatic(cold, 0.25f).size();
            check(n >= 4 && n <= 6, "T7 cold start is capped (about T_s / default)", double(n));
            for (StaticItem& it : cold) it.last_cpu_ms = std::numeric_limits<float>::quiet_NaN();
            check(selectStatic(cold, 0.25f).size() == n, "T7 a NaN prediction is the default", 0);
            for (StaticItem& it : cold) it.cam_dist = std::numeric_limits<float>::quiet_NaN();
            check(selectStatic(cold, 0.25f).size() == n, "T7 NaN distances sort without UB", 0);
        }
        // dynamic: A_max and the CPU cap, the 4-frame deadline, no floor
        {
            std::vector<DynamicItem> items;
            for (uint32_t i = 1; i <= 40; ++i) items.push_back({ i, true, 0, float(i), 0.005f, 1 });
            const std::vector<uint32_t> o = selectDynamic(items, 10.f, 24);
            check(o.size() == 24, "T7 avatar items never exceed A_max", double(o.size()));
            uint32_t avatars = 0;
            for (uint32_t k : o) { for (const DynamicItem& it : items) if (it.key == k) avatars += it.avatar_draws; }
            check(avatars <= 24, "T7 avatar draws <= A_max", avatars);
            check(o[0] == 1, "T7 nearest first");
            items[30].age_frames = 4;               // key 31, far, at the deadline
            const std::vector<uint32_t> o2 = selectDynamic(items, 10.f, 24);
            check(o2[0] == 31, "T7 age deadline beats distance (dynamic)", o2[0]);
            {
                const std::vector<uint32_t> o4 = selectDynamic(items, 0.001f, 24);
                check(o4.size() == 1 && o4[0] == 31, "T7 only the overdue item runs when nothing fits the cap", o4.size());
            }
            items[30].age_frames = 0;
            check(selectDynamic(items, 0.001f, 24).empty(), "T7 nothing fits the cap: a young face of another avatar waits for its deadline (only the own avatar runs every frame)");
            check(selectDynamic(items, 0.001f, 24, false).empty(), "T7 no floor: with the frame spent nothing runs");
            std::vector<DynamicItem> big{ { 1, true, 0, 1.f, 0.01f, 30 }, { 2, true, 0, 2.f, 0.01f, 5 } };
            const std::vector<uint32_t> o3 = selectDynamic(big, 10.f, 24);
            check(o3.size() == 1 && o3[0] == 1,
                  "T7 an item over A_max draws its nearest A_max, it is not starved", o3.size());
            std::vector<DynamicItem> alone{ { 7, true, 9, 1.f, 0.01f, 30 } };
            check(selectDynamic(alone, 10.f, 24).size() == 1, "T7 a lone item with 30 avatars still runs");
            std::vector<DynamicItem> costly{ { 8, false, 9, 1.f, 0.5f, 0 } };
            check(selectDynamic(costly, 0.3f, 24).size() == 1, "T7 an overdue item over the CPU cap runs");
            std::vector<DynamicItem> costly_young{ { 8, false, 0, 1.f, 0.5f, 0 } };
            check(selectDynamic(costly_young, 0.3f, 24).empty(), "T7 a young moving-lamp item over the CPU cap waits for its deadline (only avatar faces run over the share every frame)");
            std::vector<DynamicItem> costly_face{ { 8, true, 0, 1.f, 0.5f, 1 } };
            check(selectDynamic(costly_face, 0.3f, 24).empty(), "T7 a young face of another avatar over the CPU cap waits for its deadline");
            costly_face[0].own = true;
            check(selectDynamic(costly_face, 0.3f, 24).size() == 1, "T7 the own avatar's face over the CPU cap runs alone, every frame");
            check(selectDynamic(costly_face, 0.3f, 24, false).size() == 1, "T7 ... even when the frame has spent");
            std::vector<DynamicItem> lamps{ { 1, false, 0, 1.f, 0.1f, 0 }, { 2, false, 0, 2.f, 0.1f, 0 },
                                            { 3, false, 0, 3.f, 0.1f, 0 }, { 4, false, 0, 4.f, 0.1f, 0 } };
            check(selectDynamic(lamps, 0.3f, 24).size() == 3, "T7 lamps follow the CPU cap, not A_max");
            // an avatar item with avatar_draws left at 0 still counts one draw against A_max
            std::vector<DynamicItem> av_items{ { 1, true, 0, 1.f, 0.01f, 0 }, { 2, true, 0, 2.f, 0.01f, 0 } };
            check(selectDynamic(av_items, 10.f, 1).size() == 1, "T7 an avatar item with no draw count still costs one of A_max");
            // a bad frame time never advances or poisons the grow hold; every case starts from a fresh level 1
            {
                const std::vector<uint32_t> one{ 2048 };
                auto fresh = [&]() {
                    LevelController lc;
                    updateLevel(lc, std::vector<uint32_t>(3, 2048), 4096, 0.016f);   // level 1
                    return lc;
                };
                auto stable = [&](LevelController& lc, int frames, float dt) {
                    for (int i = 0; i < frames; ++i) updateLevel(lc, one, 4096, dt);
                };
                LevelController ref = fresh();
                const int shrunk = ref.level();
                stable(ref, 11, 0.25f);
                check(ref.level() == shrunk, "T2 11 frames of 0.25 s are not yet 3 s", ref.level());
                stable(ref, 1, 0.25f);
                check(ref.level() < shrunk, "T2 12 frames of 0.25 s are 3 s: the level grows", ref.level());
                LevelController nan = fresh();
                stable(nan, 10, std::numeric_limits<float>::quiet_NaN());
                check(nan.level() == shrunk, "T2 a NaN frame time does not advance the hold", nan.level());
                stable(nan, 12, 0.25f);
                check(nan.level() < shrunk, "T2 a NaN frame time does not poison the hold", nan.level());
                LevelController neg = fresh();
                stable(neg, 1, -5.f);
                stable(neg, 12, 0.25f);
                check(neg.level() < shrunk, "T2 a negative frame time does not run the hold backwards", neg.level());
                LevelController stall = fresh();
                stable(stall, 1, 100.f);   // one stall counts as one short frame, not 3 s of stability
                check(stall.level() == shrunk, "T2 a long stall is not 3 s of stable demand", stall.level());
            }
        }
    }
void testBadInput()
{
    TileGrid grid;
    Aabb a;
    for (int i = 0; i < 3; ++i) { a.lo[i] = 0.f; a.hi[i] = 4.f; }
    grid.add(0, a);
    Aabb b;
    for (int i = 0; i < 3; ++i) { b.lo[i] = 100.f; b.hi[i] = 104.f; }
    grid.add(1, b);
    std::vector<uint32_t> cand;
    Aabb huge;
    for (int i = 0; i < 3; ++i) { huge.lo[i] = -1.0e6f; huge.hi[i] = 1.0e6f; }
    grid.candidates(huge, cand);
    check(cand.size() == 2, "T5 a 1e6 m box returns every tile, without looping", double(cand.size()));
    Aabb nan = a;
    nan.hi[1] = std::numeric_limits<float>::quiet_NaN();
    grid.candidates(nan, cand);
    check(cand.size() == 2, "T5 a NaN box returns every tile");
    Aabb inf = a;
    inf.lo[0] = -std::numeric_limits<float>::infinity();
    grid.candidates(inf, cand);
    check(cand.size() == 2, "T5 an infinite box returns every tile");
    grid.add(2, huge);                       // a world-sized tile goes to the wide list
    grid.candidates(a, cand);
    check(cand.size() == 2 && cand[0] == 0 && cand[1] == 2, "T5 a wide tile is a candidate of every query");
    Aabb far = a;
    for (int i = 0; i < 3; ++i) { far.lo[i] = 3.0e38f; far.hi[i] = 3.4e38f; }
    grid.candidates(far, cand);              // clamped, no UB, wide tile only
    check(cand.size() == 1 && cand[0] == 2, "T5 an extreme box is clamped");
    // atlas that is not a power of two never hands out a rect outside it
    BuddyAtlas atlas(3000);
    Rect r1, r2;
    check(atlas.atlasSize() == 2048, "buddy: 3000 rounds down to 2048", atlas.atlasSize());
    check(atlas.alloc(2048, r1) && !atlas.alloc(2048, r2), "buddy: the rounded atlas holds one 2048 tile");
    check(baseSize(1.f, 3.1415927f) >= kMinBase && baseSize(1.f, 3.1415927f) <= kMaxBase, "size rule: fov near pi stays in range");
    check(baseSize(1.f, 1.0e30f) <= kMaxBase, "size rule: huge fov stays in range");
}

// ---- P3: causes, the change index, the mover rules, the LOD pin's books, the tile verdict ----
Pyramid pyramidAt(float x, float y, float z, float fov, float radius)
{
    const float o[3] = { x, y, z }, f[3] = { 0, 0, -1 }, r[3] = { 1, 0, 0 }, u[3] = { 0, 1, 0 };
    return makePyramid(o, f, r, u, fov, 0.1f, radius);
}

// The change index as it was before the flat scan (the 8 m grid rebuilt every frame): the oracle of the differential test
// and the baseline of the bench.
class GridChangeIndex
{
public:
    void rebuild(const std::vector<TileVolume>& tiles)
    {
        mTiles = tiles;
        mGrid.clear();
        for (uint32_t i = 0; i < mTiles.size(); ++i)
        {
            mGrid.add(i, mTiles[i].pyramid.bounds);
        }
    }
    void apply(const std::vector<DirtyBox>& boxes, bool everything, std::vector<Hit>& out)
    {
        out.clear();
        if (mTiles.empty())
        {
            return;
        }
        if (everything)
        {
            for (const TileVolume& t : mTiles)
            {
                out.push_back({ t.key, Cause::Other });
            }
            return;
        }
        std::vector<char> hit(mTiles.size(), 0);
        size_t remaining = mTiles.size();
        for (const DirtyBox& b : boxes)
        {
            if (remaining == 0)
            {
                break;
            }
            mGrid.candidates(b.box, mCand);
            for (uint32_t i : mCand)
            {
                if (!hit[i] && intersects(mTiles[i].pyramid, b.box))
                {
                    hit[i] = 1;
                    --remaining;
                    out.push_back({ mTiles[i].key, b.cause });
                }
            }
        }
    }

private:
    std::vector<TileVolume> mTiles;
    TileGrid mGrid;
    std::vector<uint32_t> mCand;
};

Pyramid randomPyramid(std::mt19937& rng, float world, float fov_lo, float fov_hi, float rad_lo, float rad_hi)
{
    std::uniform_real_distribution<float> pos(-world, world), fov(fov_lo, fov_hi), rad(rad_lo, rad_hi), dir(-1.f, 1.f);
    const float o[3] = { pos(rng), pos(rng), pos(rng) };
    float f[3] = { dir(rng), dir(rng), dir(rng) };
    if (f[0] * f[0] + f[1] * f[1] + f[2] * f[2] < 1e-3f)
    {
        f[2] = -1.f;
    }
    detail::normalise(f);
    float up_hint[3] = { 0.f, 1.f, 0.f };
    if (std::fabs(f[1]) > 0.9f)
    {
        up_hint[0] = 1.f;
        up_hint[1] = 0.f;
    }
    float r[3], u[3];
    detail::cross(f, up_hint, r);
    detail::normalise(r);
    detail::cross(r, f, u);
    const float fv = fov(rng); // named draws: the order of argument evaluation is unspecified
    const float rd = rad(rng);
    return makePyramid(o, f, r, u, fv, 0.1f, rd);
}

std::vector<DirtyBox> randomBoxes(std::mt19937& rng, size_t count, float world)
{
    std::uniform_real_distribution<float> pos(-world, world), ext(0.05f, 8.f);
    std::vector<DirtyBox> boxes;
    for (size_t i = 0; i < count; ++i)
    {
        Aabb b;
        for (int k = 0; k < 3; ++k)
        {
            const float c = pos(rng), e = ext(rng);
            b.lo[k] = c - e;
            b.hi[k] = c + e;
        }
        boxes.push_back({ b, Cause(rng() % unsigned(Cause::Count)) });
    }
    return boxes;
}

bool sameHits(const std::vector<Hit>& a, const std::vector<Hit>& b)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (a[i].key != b[i].key || a[i].cause != b[i].cause)
        {
            return false;
        }
    }
    return true;
}

// The flat-scan index gives the old grid index's hits exactly: keys, causes and order.
void testP3ChangeIndexDifferential()
{
    std::mt19937 rng(20261116);
    std::vector<Hit> got, want;
    int mismatches = 0, comparisons = 0;
    size_t total_hits = 0, nonempty = 0;
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    for (int scene = 0; scene < 120; ++scene)
    {
        const size_t n_tiles = rng() % 301;
        const size_t n_boxes = scene % 12 == 0 ? 5000 : rng() % 600;
        std::vector<TileVolume> tiles;
        for (size_t k = 0; k < n_tiles; ++k)
        {
            tiles.push_back({ uint32_t(1000 + k * 3), randomPyramid(rng, 80.f, 0.3f, 2.9f, 1.f, 20.f) });
        }
        if (scene % 5 == 0 && !tiles.empty())
        {
            tiles[rng() % tiles.size()].pyramid = randomPyramid(rng, 80.f, 0.3f, 2.9f, 1.f, 5000.f); // a world-sized tile
        }
        std::vector<DirtyBox> boxes = randomBoxes(rng, n_boxes, 90.f);
        if (scene % 3 == 0)
        {
            Aabb odd;
            for (int k = 0; k < 3; ++k) { odd.lo[k] = -1e6f; odd.hi[k] = 1e6f; }
            boxes.insert(boxes.begin() + long(boxes.size() / 2), { odd, Cause::Edit });
            for (int k = 0; k < 3; ++k) { odd.lo[k] = -3e38f; odd.hi[k] = 3e38f; }
            boxes.push_back({ odd, Cause::Mover });
            for (int k = 0; k < 3; ++k) { odd.lo[k] = nan; odd.hi[k] = 1.f; }
            boxes.insert(boxes.begin() + long(boxes.size() / 3), { odd, Cause::Lamp });
            for (int k = 0; k < 3; ++k) { odd.lo[k] = 0.f; odd.hi[k] = inf; }
            boxes.insert(boxes.begin() + long(boxes.size() / 4), { odd, Cause::Avatar });
        }
        GridChangeIndex old_index;
        ChangeIndex new_index;
        old_index.rebuild(tiles);
        new_index.rebuild(tiles);
        for (int every = 0; every < 2; ++every)
        {
            old_index.apply(boxes, every != 0, want);
            new_index.apply(boxes, every != 0, got);
            ++comparisons;
            total_hits += want.size();
            nonempty += want.empty() ? 0 : 1;
            if (!sameHits(got, want))
            {
                ++mismatches;
            }
        }
    }
    check(mismatches == 0, "P3 the flat-scan index gives the grid index's hits in the same order (mixed scenes: NaN, inf and 1e6 boxes)",
          mismatches, comparisons);
    check(total_hits > 0 && nonempty > size_t(comparisons) / 2, "P3 the differential scenes produce hits", total_hits, nonempty);

    // Each odd box alone, first and last (no catch-all box before it, so the 3e38 box meets unhit tiles), against finite tiles
    // and against tiles with NaN, inf and beyond-1e6 bounds, a zero-radius-like tile and a duplicate key.
    int odd_mismatches = 0, odd_comparisons = 0;
    Aabb odds[4];
    for (int k = 0; k < 3; ++k)
    {
        odds[0].lo[k] = -1e6f;  odds[0].hi[k] = 1e6f;
        odds[1].lo[k] = -3e38f; odds[1].hi[k] = 3e38f;
        odds[2].lo[k] = nan;    odds[2].hi[k] = 1.f;
        odds[3].lo[k] = 0.f;    odds[3].hi[k] = inf;
    }
    for (int scene = 0; scene < 64; ++scene)
    {
        const size_t n_tiles = 1 + rng() % 20;
        std::vector<TileVolume> tiles;
        for (size_t k = 0; k < n_tiles; ++k)
        {
            tiles.push_back({ uint32_t(1000 + k * 3), randomPyramid(rng, 80.f, 0.3f, 2.9f, 1.f, 20.f) });
        }
        Aabb& tb = tiles[rng() % tiles.size()].pyramid.bounds;
        for (int k = 0; k < 3; ++k)
        {
            switch (scene % 5)
            {
            case 1: tb.lo[k] = nan; tb.hi[k] = nan; break;
            case 2: tb.lo[k] = -inf; tb.hi[k] = inf; break;
            case 3: tb.lo[k] = -2e6f; tb.hi[k] = 2e6f; break;
            case 4: tb.hi[k] = tb.lo[k]; break; // zero extent
            default: break;
            }
        }
        if (n_tiles > 1)
        {
            tiles[1].key = tiles[0].key; // a duplicate key
        }
        for (const Aabb& odd : odds)
        {
            for (int layout = 0; layout < 3; ++layout)
            {
                std::vector<DirtyBox> boxes;
                const std::vector<DirtyBox> rest = randomBoxes(rng, layout == 0 ? 0 : 20, 90.f);
                if (layout == 1)
                {
                    boxes.push_back({ odd, Cause::Edit });
                }
                boxes.insert(boxes.end(), rest.begin(), rest.end());
                if (layout != 1)
                {
                    boxes.push_back({ odd, Cause::Mover });
                }
                GridChangeIndex old_index;
                ChangeIndex new_index;
                old_index.rebuild(tiles);
                new_index.rebuild(tiles);
                for (int every = 0; every < 2; ++every)
                {
                    old_index.apply(boxes, every != 0, want);
                    new_index.apply(boxes, every != 0, got);
                    ++odd_comparisons;
                    if (!sameHits(got, want))
                    {
                        ++odd_mismatches;
                    }
                }
            }
        }
    }
    check(odd_mismatches == 0, "P3 the flat-scan index matches the grid on odd boxes (1e6, 3e38, NaN, inf) and odd tiles", odd_mismatches,
          odd_comparisons);
}

// The cost of the change index on a frame that has dirty boxes (R tiles, B boxes): the old grid against the flat scan.
// Run with --bench-index (not run in CI). Rows: B boxes drawn over +/-70 m (near the tiles) and, marked far, over +/-400 m.
void benchChangeIndex()
{
    std::printf("change index, microseconds per frame (rebuild + apply): R tiles, B boxes\n");
    std::printf("%6s %6s %12s %12s %8s\n", "R", "B", "grid (old)", "scan (new)", "speedup");
    std::mt19937 rng(1);
    for (int R : { 20, 60, 150, 300 })
    {
        std::vector<TileVolume> tiles; // one lamp mix per R: a third of the tiles are 20 m lamps, the rest 8 m
        for (int k = 0; k < R; ++k)
        {
            tiles.push_back({ uint32_t(k + 1), randomPyramid(rng, 60.f, 0.5f, 2.2f, 1.f, k % 3 == 0 ? 20.f : 8.f) });
        }
        for (int variant = 0; variant < 2; ++variant)
        for (int B : variant == 0 ? std::vector<int>{ 0, 50, 500, 5000 } : std::vector<int>{ 500, 5000, 65536 })
        {
            const std::vector<DirtyBox> boxes = randomBoxes(rng, size_t(B), variant == 0 ? 70.f : 400.f);
            std::vector<Hit> hits;
            const int reps = std::max(3, 20000 / (R + B / 10 + 10));
            GridChangeIndex old_index;
            ChangeIndex new_index;
            double sink = 0, a = 1e30, b = 1e30; // the best of five trials of `reps` frames
            for (int trial = 0; trial < 5; ++trial)
            {
                auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < reps; ++i)
                {
                    old_index.rebuild(tiles);
                    old_index.apply(boxes, false, hits);
                    sink += double(hits.size());
                }
                auto t1 = std::chrono::steady_clock::now();
                for (int i = 0; i < reps; ++i)
                {
                    new_index.rebuild(tiles);
                    new_index.apply(boxes, false, hits);
                    sink += double(hits.size());
                }
                auto t2 = std::chrono::steady_clock::now();
                a = std::min(a, std::chrono::duration<double, std::micro>(t1 - t0).count() / reps);
                b = std::min(b, std::chrono::duration<double, std::micro>(t2 - t1).count() / reps);
            }
            std::printf("%6d %6d %12.1f %12.1f %7.1fx%s%s\n", R, B, a, b, a / std::max(b, 1e-3), variant ? " far" : "", sink < 0 ? "?" : "");
        }
    }
}

void testP3Causes()
{
    check(std::string(causeName(Cause::New)) == "new" && std::string(causeName(Cause::LodUp)) == "lod_up" &&
              std::string(causeName(Cause::AddRemove)) == "add_remove" && std::string(causeName(Cause::Edit)) == "edit" &&
              std::string(causeName(Cause::Mover)) == "mover" && std::string(causeName(Cause::Lamp)) == "lamp" &&
              std::string(causeName(Cause::Avatar)) == "avatar" && std::string(causeName(Cause::Lowres)) == "lowres" &&
              std::string(causeName(Cause::Other)) == "other",
          "P3 cause names");
    check(std::string(causeName(Cause::Count)) == "other", "P3 an out-of-range cause is other");
    CauseCounts c;
    c.add(Cause::Edit);
    c.add(Cause::Edit);
    c.add(Cause::LodUp);
    c.add(Cause::Count); // out of range counts as other, never writes past the array
    check(c.n[size_t(Cause::Edit)] == 2 && c.n[size_t(Cause::LodUp)] == 1 && c.n[size_t(Cause::Other)] == 1 && c.total() == 4,
          "P3 cause counts");
}

void testP3DirtyList()
{
    {
        DirtyList d;
        Aabb b;
        b.hi[0] = 1.f;
        d.push(b, Cause::Edit);
        d.push(b, Cause::Edit);
        check(d.pending() == 1, "P3 an identical box right after the last (the hooks of one drawable) is kept once");
        d.push(b, Cause::Mover);
        b.hi[1] = 2.f;
        d.push(b, Cause::Mover);
        check(d.pending() == 3, "P3 a different cause or box is kept");
    }
    DirtyList d;
    Aabb a{ { 0, 0, 0 }, { 1, 1, 1 } };
    d.push(a, Cause::Edit);
    d.push(a);
    const std::vector<DirtyBox>& f = d.swapForApply();
    check(f.size() == 2 && f[0].cause == Cause::Edit && f[1].cause == Cause::Other && !d.everything(), "P3 the box carries its cause");
    // a frame that loses boxes asks for everything, once; the cap is bounded
    for (size_t i = 0; i < DirtyList::kMaxPending + 100; ++i)
    {
        Aabb moving = a;
        moving.hi[0] += float(i); // distinct boxes: identical neighbours are merged
        d.push(moving, Cause::Edit);
    }
    check(d.pending() == DirtyList::kMaxPending, "P3 the pending list is bounded", double(d.pending()));
    const std::vector<DirtyBox>& g = d.swapForApply();
    check(g.size() == DirtyList::kMaxPending && d.everything(), "P3 an overflowed frame says everything");
    d.swapForApply();
    check(!d.everything(), "P3 the next frame starts clean");
    d.pushEverything();
    d.swapForApply();
    check(d.everything(), "P3 pushEverything");
    d.push(a);
    d.clear();
    check(d.pending() == 0 && d.swapForApply().empty() && !d.everything(), "P3 clear");
}

void testP3ChangeIndex()
{
    {
        // a wide projector (aspect 2) is as wide as its render frustum across and not wider along: the same region the cull sees
        const float o[3] = { 0, 0, 0 }, f[3] = { 0, 0, -1 }, r[3] = { 1, 0, 0 }, u[3] = { 0, 1, 0 };
        const Pyramid wide = makePyramid(o, f, r, u, 2.f * 1.0f, 1.0f, 0.1f, 20.f); // tan_x = 2, tan_y = 1
        auto box_at = [](float x, float y, float z) { Aabb b; b.lo[0] = x - .1f; b.hi[0] = x + .1f; b.lo[1] = y - .1f; b.hi[1] = y + .1f; b.lo[2] = z - .1f; b.hi[2] = z + .1f; return b; };
        check(intersects(wide, box_at(15.f, 0.f, -10.f)), "P3 the wide side of an aspect-2 volume holds a box at tan 1.5");
        check(!intersects(wide, box_at(0.f, 15.f, -10.f)), "P3 the narrow side does not hold a box at tan 1.5 (not widened)");
    }
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> pos(-60.f, 60.f), ext(0.2f, 6.f), fov(0.5f, 2.2f), rad(2.f, 20.f);
    std::vector<TileVolume> tiles;
    for (uint32_t k = 0; k < 80; ++k)
    {
        tiles.push_back({ 100 + k, pyramidAt(pos(rng), pos(rng), pos(rng), fov(rng), rad(rng)) });
    }
    ChangeIndex index;
    index.rebuild(tiles);
    check(index.size() == 80, "P3 index size");
    std::vector<Hit> hits;
    int mismatches = 0;
    for (int round = 0; round < 200; ++round)
    {
        std::vector<DirtyBox> boxes;
        for (int i = 0; i < 6; ++i)
        {
            Aabb b;
            const float cx = pos(rng), cy = pos(rng), cz = pos(rng), e = ext(rng);
            b.lo[0] = cx - e; b.hi[0] = cx + e; b.lo[1] = cy - e; b.hi[1] = cy + e; b.lo[2] = cz - e; b.hi[2] = cz + e;
            boxes.push_back({ b, i % 2 ? Cause::Edit : Cause::AddRemove });
        }
        index.apply(boxes, false, hits);
        // brute force: each tile, the first box that intersects it names the cause
        std::map<uint32_t, Cause> want;
        for (const DirtyBox& b : boxes)
        {
            for (const TileVolume& t : tiles)
            {
                if (!want.count(t.key) && intersects(t.pyramid, b.box))
                {
                    want[t.key] = b.cause;
                }
            }
        }
        std::map<uint32_t, Cause> got;
        for (const Hit& h : hits)
        {
            if (got.count(h.key))
            {
                ++mismatches; // a tile named twice
            }
            got[h.key] = h.cause;
        }
        if (got != want)
        {
            ++mismatches;
        }
    }
    check(mismatches == 0, "P3 the change index gives the brute-force hits, once per tile, with the first box's cause", mismatches);
    index.apply({}, false, hits);
    check(hits.empty(), "P3 no dirty box, no hit (a static scene)");
    index.apply({}, true, hits);
    check(hits.size() == 80 && hits[0].cause == Cause::Other, "P3 everything touches every tile");
    ChangeIndex none;
    none.rebuild({});
    none.apply({ DirtyBox() }, true, hits);
    check(hits.empty(), "P3 no tiles, no hits");
    // a world-sized box (far larger than any tile) is conservative, never a crash
    Aabb wide{ { -1e6f, -1e6f, -1e6f }, { 1e6f, 1e6f, 1e6f } };
    index.apply({ { wide, Cause::Edit } }, false, hits);
    check(hits.size() == 80, "P3 a world-sized box touches every tile", double(hits.size()));
    Aabb nan_box;
    nan_box.lo[0] = std::numeric_limits<float>::quiet_NaN();
    index.apply({ { nan_box, Cause::Edit } }, false, hits); // must not crash; a NaN box fails every plane test
    check(true, "P3 a NaN box does not crash (a smoke check: no hit count is asserted)");
}

void testP3MoverRules()
{
    ObjectFlags none, phys, mover;
    phys.physical = true;
    mover.mover = true;
    check(castsFixed(none, false), "P3 a plain linkset casts");
    check(!castsFixed(phys, false) && !castsFixed(mover, false), "P3 a physical object and a mover do not cast");
    check(castsFixed(phys, true) && castsFixed(mover, true), "P3 the lamp's own linkset is the fixture: it casts whatever its flags");
    check(hookSkips(mover, none, true) && hookSkips(phys, none, true) && hookSkips(none, mover, true) && hookSkips(none, phys, true),
          "P3 hooks skip active movers and their roots");
    check(!hookSkips(mover, none, false) && !hookSkips(phys, none, false) && !hookSkips(none, mover, false) && !hookSkips(none, phys, false),
          "P3 a mover at rest (static, drawn) is a fixed caster: its changes push");
    check(!hookSkips(mover, mover, true, true) && !hookSkips(phys, phys, true, true), "P3 the lamp's own linkset always pushes");
    check(!hookSkips(none, none, true), "P3 hooks keep ordinary objects");
    ServerUpdate u;
    check(!becomesMover(u), "P3 an empty update");
    u.pos_changed = true;
    check(!becomesMover(u), "P3 the creating update's position is not a move");
    u.created_seen = true;
    check(becomesMover(u), "P3 a later position change is a move");
    u.selected = true;
    check(!becomesMover(u), "P3 not while the agent edits the object");
    u = ServerUpdate();
    u.created_seen = true;
    u.rot_changed = true;
    check(becomesMover(u), "P3 a later rotation change is a move");
    u = ServerUpdate();
    u.is_root = true;
    u.velocity = true;
    check(becomesMover(u), "P3 a velocity on a root makes it a mover even in its first update");
    u.velocity = false;
    u.omega = true;
    check(becomesMover(u), "P3 so does an angular velocity");
    u.is_root = false;
    check(!becomesMover(u), "P3 a child's angular velocity is client spin: a known limit, not a mover");
    ObjectFlags a = none, b = none;
    b.physical = true;
    check(layerTransition(a, b) && layerTransition(b, a), "P3 physics on or off is a transition");
    ObjectFlags c = b;
    c.mover = true;
    check(!layerTransition(b, c), "P3 physical to physical-and-mover stays out of the layer: no transition");
    check(!layerTransition(a, a), "P3 no flag change, no transition");
    // the real flag values (llshadowdirtyfeed.cpp checks them against lldrawable.h)
    check(rebuildDirties(kRebuildVolume) && rebuildDirties(kRebuildRigged) && rebuildDirties(kRebuildPosition) && rebuildDirties(kRebuildAll),
          "P3 the flag filter: volume, rigged, a position-only rebuild and REBUILD_ALL pass");
    check(!rebuildDirties(kRebuildTcoord) && !rebuildDirties(kRebuildColor) && !rebuildDirties(kRebuildTcoord | kRebuildColor),
          "P3 texture coordinates and colour (and REBUILD_MATERIAL) do not");
    check(!rebuildDirties(kRebuildGeometry) && !rebuildDirties(0), "P3 REBUILD_GEOMETRY (markForUpdate, selection, bump) does not");
    check(rebuildDirties(kRebuildPosition | kRebuildTcoord), "P3 a position rebuild with one texcoord flag still passes (not the whole set)");
    {
        // the silhouette key (S2): what a prim's alpha faces cut out of a depth tile. llshadowdirtyfeed.cpp's silhouette_key folds one
        // SilhouetteFace per face with silhouetteFaceMix; an opaque single-sided face adds nothing
        const uint32_t opaque = kSilhouetteKeySeed; // no alpha face
        SilhouetteFace single;
        single.cutout = false;
        single.index = 3;
        single.alpha = 0.5f;
        single.texture[0] = 0x77;
        single.gltf = true;
        single.gltf_alpha_mode = 1;
        check(silhouetteFaceMix(opaque, single) == opaque,
              "S2 an opaque single-sided face is no input at all (silhouetteFaceMix itself: its index, texture, tint and material neither)");
        SilhouetteFace lace_face;
        lace_face.alpha = 0.5f;
        lace_face.texture[0] = 0x5a;
        lace_face.components = 4;
        const uint32_t lace = silhouetteFaceMix(opaque, lace_face);
        check(lace != opaque && silhouetteFaceMix(opaque, lace_face) == lace, "S2 an alpha face changes the key, the same inputs give the same key");
        // every input moves the key: the tint's alpha, the face, the texture and its channels, the mapping, the materials' alpha
        std::vector<SilhouetteFace> moved(16, lace_face);
        moved[0].alpha = 0.25f;
        moved[1].index = 1;
        moved[2].texture[15] = 1;
        moved[3].components = 2;
        moved[4].scale[1] = 2.f;
        moved[5].offset[0] = 0.5f;
        moved[6].rotation = 1.f;
        moved[7].texgen = 1;
        moved[8].material = true;
        moved[9].material = true;
        moved[9].alpha_mode = 2;
        moved[10].material = true;
        moved[10].alpha_cutoff = 128;
        moved[11].gltf = true;
        moved[12].gltf = true;
        moved[12].gltf_alpha_mode = 1;
        moved[13].gltf = true;
        moved[13].gltf_texture[3] = 9;
        moved[14].gltf = true;
        moved[14].gltf_scale[0] = 3.f;
        moved[15].gltf = true;
        moved[15].gltf_cutoff = 0.25f;
        moved.push_back(lace_face);
        moved[16].double_sided = true;
        std::set<uint32_t> keys{ lace };
        for (const SilhouetteFace& f : moved)
        {
            keys.insert(silhouetteFaceMix(opaque, f));
        }
        check(keys.size() == moved.size() + 1,
              "S2 each silhouette input (alpha, face, texture, channels, mapping, legacy and glTF alpha, double-sided) changes the key",
              double(keys.size()));
        // an opaque face is an input only when its glTF material is double-sided (the shadow pass then draws its back faces too): the flag
        // moves the key, its texture and tint do not
        SilhouetteFace two_sided;
        two_sided.cutout = false;
        two_sided.double_sided = true;
        SilhouetteFace two_sided_other = two_sided;
        two_sided_other.alpha = 0.5f;
        two_sided_other.texture[0] = 0x77;
        two_sided_other.gltf = true;
        two_sided_other.gltf_texture[1] = 3;
        check(silhouetteFaceMix(opaque, two_sided) != opaque, "S2 an opaque face turned double-sided changes the key");
        check(silhouetteFaceMix(opaque, two_sided_other) == silhouetteFaceMix(opaque, two_sided),
              "S2 an opaque double-sided face's texture, tint and base colour are no input");
        check(silhouetteFaceMix(silhouetteFaceMix(opaque, lace_face), moved[1]) != silhouetteFaceMix(opaque, lace_face),
              "S2 a second alpha face changes the key");
        const uint32_t lace_other_alpha = silhouetteFaceMix(opaque, moved[0]);
        check(texturedDirties(lace, lace_other_alpha, true), "P3 a new alpha of an alpha face dirties");
        check(texturedDirties(lace, opaque, true), "P3 a face turned opaque (alpha texture replaced by an RGB one) dirties: the hole closes");
        check(texturedDirties(opaque, lace, true), "P3 an opaque face turned alpha dirties: a hole opens");
        check(!texturedDirties(opaque, opaque, true), "P3 opaque to opaque (a plain texture swap, an animated texture) does not dirty");
        // fullbright, glow, shine, bump and the RGB of a face the blend pass does not draw are no input: a blinker that toggles them gives
        // the feed the same inputs, hence the same key, and redraws nothing
        check(!texturedDirties(lace, silhouetteFaceMix(opaque, lace_face), true), "S2 the same silhouette inputs (a blinker's fullbright, glow or RGB tint) do not dirty");
        SilhouetteFace mask_red = lace_face;
        mask_red.rgb[0] = 255;
        mask_red.rgb[1] = mask_red.rgb[2] = 0;
        check(silhouetteFaceMix(opaque, mask_red) == lace, "S2 the RGB of a face the blend pass does not draw (an alpha-masked one) is no input");
        // L3: a face the blend pass draws lets a lamp's light through coloured by its RGB (the light-through layer), so its RGB is an
        // input: a recolour redraws the tiles that see it, and a tint blinker on a blended face pushes, one push a toggle (by design)
        SilhouetteFace glass = lace_face;
        glass.tint = true;
        SilhouetteFace glass_red = glass;
        glass_red.rgb[1] = glass_red.rgb[2] = 0;
        SilhouetteFace glass_blue = glass;
        glass_blue.rgb[0] = glass_blue.rgb[1] = 0;
        const uint32_t k_glass = silhouetteFaceMix(opaque, glass);
        const uint32_t k_red = silhouetteFaceMix(opaque, glass_red);
        const uint32_t k_blue = silhouetteFaceMix(opaque, glass_blue);
        check(k_glass != lace && k_red != k_glass && k_blue != k_red && k_blue != k_glass,
              "L3 a blended face's RGB is an input: white, red and blue glass give three keys");
        check(texturedDirties(k_red, k_blue, true), "L3 a red pane recoloured blue dirties its tiles");
        check(silhouetteFaceMix(opaque, glass_red) == k_red, "L3 the same colour gives the same key");
        SilhouetteFace opaque_red = single;
        opaque_red.tint = true;
        opaque_red.rgb[1] = opaque_red.rgb[2] = 0;
        check(silhouetteFaceMix(opaque, opaque_red) == opaque, "L3 an opaque single-sided face is still no input, whatever its colour");
        check(texturedDirties(opaque, opaque, false) && texturedDirties(lace, lace, false), "P3 an unknown past (the prim predates tracking) pushes once, whatever it looks like now");
    }
}

void testP3PinBook()
{
    PinBook book;
    const Id128 o1{ 1, 1 }, o2{ 2, 2 }, o3{ 3, 3 };
    book.beginRender(10);
    check(book.touch(10, o1) && book.touch(10, o2), "P3 a first render pins what it meets");
    check(book.endRender(10).empty() && book.heldBy(10) == 2 && book.pinnedObjects() == 2, "P3 nothing is released after the first render");
    // convergence: the second render meets the same objects: no new pin, no release
    book.beginRender(10);
    check(!book.touch(10, o1) && !book.touch(10, o2), "P3 the second render pins nothing new");
    check(book.endRender(10).empty() && book.refs(o1) == 1, "P3 and releases nothing");
    // an object that left the light is released at the end of the render that did not meet it
    book.beginRender(10);
    book.touch(10, o1);
    book.touch(10, o3);
    const std::vector<Id128> gone = book.endRender(10);
    check(gone.size() == 1 && gone[0] == o2 && book.refs(o2) == 0 && book.refs(o3) == 1 && book.heldBy(10) == 2,
          "P3 an object not met again is released, a new one is held");
    // two tiles share an object: the count is two, and it falls with each tile
    book.beginRender(11);
    check(book.touch(11, o1), "P3 another tile pins the same object");
    book.endRender(11);
    check(book.refs(o1) == 2 && book.pinnedObjects() == 2, "P3 refcount per tile, distinct objects counted once");
    const std::vector<Id128> all = book.releaseTile(10);
    check(all.size() == 2 && book.refs(o1) == 1 && book.refs(o3) == 0 && book.heldBy(10) == 0, "P3 releasing a tile gives back everything it held");
    // an object that dies is dropped from every tile's books: a new object with the same id is pinned by its first render
    check(book.forget(o1) == 1 && book.refs(o1) == 0 && book.heldBy(11) == 0, "P3 a killed object leaves every book");
    book.beginRender(11);
    check(book.touch(11, o1) && book.refs(o1) == 1, "P3 the object re-created with the same id is pinned again");
    book.endRender(11);
    check(book.forget(Id128{ 9, 9 }) == 0, "P3 forgetting an unknown object does nothing");
    book.releaseTile(11);
    check(book.pinnedObjects() == 0, "P3 nothing stays pinned after every tile is released");
    check(book.releaseTile(99).empty() && book.endRender(99).empty(), "P3 an unknown tile releases nothing");
}

void testP3Verdict()
{
    TileFacts f;
    check(!classifyTile(f).needs_render, "P3 a static scene: a drawn tile with no change needs no render");
    f.never_drawn = true;
    TileVerdict v = classifyTile(f);
    check(v.needs_render && !v.dynamic && v.cause == Cause::New, "P3 a new tile is drawn by the static schedule");
    f.holds_avatar = true;
    f.lamp_moving = true;
    v = classifyTile(f);
    check(!v.dynamic && v.cause == Cause::New, "P3 even with an avatar, the first fill is the static schedule's");
    f = TileFacts();
    f.pending = true;
    f.pending_cause = Cause::LodUp;
    v = classifyTile(f);
    check(v.needs_render && !v.dynamic && v.cause == Cause::LodUp, "P3 a dirty tile keeps the cause of the dirty box");
    f = TileFacts();
    f.holds_avatar = true;
    v = classifyTile(f);
    check(!v.needs_render && v.needs_layer && v.dynamic && v.cause == Cause::Avatar, "P4 an avatar in the light: the avatar layer alone, dynamic (S is not drawn again)");
    f = TileFacts();
    f.avatar_in_layer = true;
    v = classifyTile(f);
    check(!v.needs_render && v.needs_layer && v.dynamic && v.cause == Cause::Avatar, "P4 the avatar left: one more layer pass restores L from S");
    f = TileFacts();
    f.lamp_moving = true;
    v = classifyTile(f);
    check(v.needs_render && v.dynamic && v.cause == Cause::Lamp, "P3 a moving lamp is a dynamic item");
    f = TileFacts();
    f.pose_stale = true;
    v = classifyTile(f);
    check(v.needs_render && !v.dynamic && v.cause == Cause::Lamp, "P3 a lamp that settled but whose tile is behind is a static item");
    f = TileFacts();
    f.lowres = true;
    check(classifyTile(f).cause == Cause::Lowres, "P3 a down-sized tile is drawn again");
    f.pending = true;
    f.pending_cause = Cause::Edit;
    check(classifyTile(f).cause == Cause::Edit, "P3 a change outranks the resize in the cause");
}

// Eight lamps whose volumes do not overlap, each drawn once, then no change and no avatar for as long as it stays so: the scheduler
// is handed nothing (the whole chain: dirty list, index over the drawn tiles, verdict, selection, with each tile's drawn and
// pending state modelled as the pool keeps it). One edit inside lamp 3's volume draws exactly lamp 3, in the frame after it.
void testP3StaticScene()
{
    constexpr uint32_t kLamps = 8;
    std::vector<TileVolume> tiles;
    for (uint32_t k = 0; k < kLamps; ++k)
    {
        tiles.push_back({ k + 1, pyramidAt(float(k) * 40.f, 0.f, 0.f, 1.5f, 8.f) }); // 40 m apart, 8 m deep: disjoint
    }
    ChangeIndex index;
    DirtyList dirty;
    bool drawn[kLamps] = {}, pending[kLamps] = {};
    Cause pending_cause[kLamps] = {};
    int fill_renders = 0, later_renders = 0, last_fill_frame = -1;
    std::vector<std::pair<int, uint32_t>> later; // (frame, tile) of every render after the fill
    for (int frame = 0; frame < 100; ++frame)
    {
        // the index covers the drawn tiles only, and only a frame with boxes needs it
        std::vector<TileVolume> drawn_tiles;
        for (uint32_t k = 0; k < kLamps; ++k)
        {
            if (drawn[k])
            {
                drawn_tiles.push_back(tiles[k]);
            }
        }
        index.rebuild(drawn_tiles);
        std::vector<Hit> hits;
        const std::vector<DirtyBox>& boxes = dirty.swapForApply();
        index.apply(boxes, dirty.everything(), hits);
        for (const Hit& h : hits)
        {
            if (!pending[h.key - 1])
            {
                pending[h.key - 1] = true;
                pending_cause[h.key - 1] = h.cause;
            }
        }
        std::vector<StaticItem> items;
        for (uint32_t k = 0; k < kLamps; ++k)
        {
            TileFacts f;
            f.never_drawn = !drawn[k];
            f.pending = pending[k];
            f.pending_cause = pending_cause[k];
            const TileVerdict v = classifyTile(f);
            if (v.needs_render && !v.dynamic)
            {
                items.push_back({ k + 1, f.never_drawn, true, float(k + 1), 0.05f, 0 });
            }
        }
        for (uint32_t key : selectStatic(items, kFrameCapMs))
        {
            const uint32_t k = key - 1;
            if (!drawn[k])
            {
                ++fill_renders;
                last_fill_frame = frame;
            }
            else
            {
                ++later_renders;
                later.push_back({ frame, k });
            }
            drawn[k] = true;
            pending[k] = false;
        }
        if (frame == 50)
        {
            // one edit inside lamp 3's volume (key 3, index 2); a camera that moved does not enter into it
            Aabb b;
            for (int i = 0; i < 3; ++i)
            {
                b.lo[i] = (tiles[2].pyramid.bounds.lo[i] + tiles[2].pyramid.bounds.hi[i]) * 0.5f;
                b.hi[i] = b.lo[i] + 0.1f;
            }
            dirty.push(b, Cause::Edit);
        }
    }
    check(fill_renders == int(kLamps), "P3 static scene: every lamp is filled once", fill_renders);
    check(last_fill_frame < 5, "P3 static scene: the cold fill ends within a few frames (the fixed cap)", last_fill_frame);
    check(later_renders == 1 && later.size() == 1 && later[0].first == 51 && later[0].second == 2,
          "P3 static scene: after the fill the one edit draws exactly lamp 3, in the next frame, and nothing else in 100 frames", later_renders);
}

// ---- P4: the avatar layer, the combined cap, the per-owner feed rules ----

std::vector<DynamicItem> faces(uint32_t n, float cost, uint32_t age, uint32_t pool = 0)
{
    std::vector<DynamicItem> v;
    for (uint32_t i = 1; i <= n; ++i)
    {
        DynamicItem it;
        it.key = i;
        it.avatar = true;
        it.age_frames = age;
        it.cam_dist = float(i);
        it.last_cpu_ms = cost;
        it.avatar_draws = 1;
        it.pool = pool;
        v.push_back(it);
    }
    return v;
}

std::vector<StaticItem> tiles(uint32_t n, float cost, uint32_t pool = 0)
{
    std::vector<StaticItem> v;
    for (uint32_t i = 1; i <= n; ++i)
    {
        StaticItem it;
        it.key = i;
        it.never_drawn = false;
        it.pending = true;
        it.cam_dist = float(i);
        it.last_cpu_ms = cost;
        it.age_frames = 1;
        it.pool = pool;
        v.push_back(it);
    }
    return v;
}

float predicted(const FramePicks& p, const std::vector<StaticItem>& st, const std::vector<DynamicItem>& dyn)
{
    float ms = 0.f;
    for (uint32_t k : p.dynamic)
    {
        for (const DynamicItem& it : dyn)
            if (packKey(it.pool, it.key) == k) ms += itemCost(it.last_cpu_ms);
    }
    for (uint32_t k : p.fixed)
    {
        for (const StaticItem& it : st)
            if (packKey(it.pool, it.key) == k) ms += itemCost(it.last_cpu_ms);
    }
    return ms;
}

void testP4Caps()
{
    // the constants: one combined cap, the floor's share of the standing floor's frame (L2), the dynamic layer's share and the static floor
    check(std::fabs(kFrameCapMs - kFloorShare * 1000.f / kFloorStandingFps) < 1e-3f, "P4 the combined cap is 3 % of the 60 fps floor's frame", kFrameCapMs);
    {
        const float standing = 1000.f / (1000.f / kFloorStandingFps + kFrameCapMs), moving = 1000.f / (1000.f / kFloorMovingFps + kFrameCapMs);
        check(standing > 58.f && moving > 24.6f, "P4 spent in full on a CPU-bound frame at the floor: about 58.3 fps standing, 24.7 moving", standing, moving);
    }
    check(std::fabs(kDynamicCapMs + kStaticCapMs - kFrameCapMs) < 1e-6f, "P4 the dynamic share and the static floor add up to the cap");
    check(kDynamicCapMs > kStaticCapMs, "P4 the dynamic layer has the priority share");

    // both kinds pending, many items of every cost: the picks of a frame never cost more than the cap
    {
        const float costs[] = { 0.03f, 0.05f, 0.07f, 0.1f, 0.12f, 0.2f };
        bool never_over = true, both_served = true;
        for (float cd : costs)
        {
            for (float cs : costs)
            {
                const std::vector<DynamicItem> dyn = faces(12, cd, 1);
                const std::vector<StaticItem> st = tiles(12, cs);
                const FramePicks p = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
                const float ms = predicted(p, st, dyn);
                const bool single = p.dynamic.size() + p.fixed.size() == 1;
                if (ms > kFrameCapMs + 1e-4f && !single) never_over = false;
                if (std::fabs(ms - p.predicted_ms) > 1e-4f) never_over = false;
                if (itemCost(cd) + itemCost(cs) <= kFrameCapMs && (p.dynamic.empty() || p.fixed.empty())) both_served = false;
            }
        }
        check(never_over, "P4 combined cap: the predicted cost of a frame never exceeds the cap when both kinds are pending");
        check(both_served, "P4 combined cap: both kinds are served whenever one of each fits");
    }
    // without the progress rule (something was already spent) the cap holds without exception, even for one big item
    {
        const std::vector<DynamicItem> dyn = faces(1, 0.6f, 9);   // overdue and over the cap alone
        const std::vector<StaticItem> st = tiles(1, 0.6f);
        const FramePicks p = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, false);
        check(p.dynamic.empty() && p.fixed.empty(), "P4 no progress pick once the frame has spent: an item over the cap waits");
        const FramePicks q = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(q.dynamic.size() + q.fixed.size() == 1, "P4 progress: an untouched frame runs one over-cap item alone, never two", double(q.dynamic.size() + q.fixed.size()));
        check(q.dynamic.size() == 1, "P4 progress: the dynamic item has the priority");
    }
    // a static tile above the whole cap is not starved by a standing avatar: after the wait it runs alone as the progress pick
    {
        const std::vector<DynamicItem> dyn = faces(3, 0.05f, 1);
        std::vector<StaticItem> st = tiles(1, 0.6f);
        const FramePicks young = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(young.dynamic.size() == 3 && young.fixed.empty(), "P4 a big static tile that has not waited yet leaves the cheap faces their frame");
        st[0].age_frames = kDynamicDeadlineFrames;
        const FramePicks old = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(old.fixed.size() == 1 && old.dynamic.empty(), "P4 a big static tile that has waited runs alone, the faces give way", double(old.fixed.size()));
        const FramePicks spent = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, false);
        check(spent.fixed.empty(), "P4 the big static tile does not run in a frame that has spent already");
    }
    // the in-frame gate (mayRunPick): the first static pick is exempt from the measured cap only when nothing else rendered this frame
    {
        // (a) the dynamic picks spent the ledger: the first static pick is refused
        FrameLedger spent;
        spent.begin(1);
        spent.add(kFrameCapMs + 0.01f);
        check(!mayRunPick(spent.left(kFrameCapMs), false, true, false), "P4 gate: a first static pick behind spent dynamic work is refused");
        // (b) the progress case from selectFrame: dynamic empty, ledger untouched, the head above the whole cap is allowed
        std::vector<StaticItem> st = tiles(1, 0.6f);
        st[0].age_frames = kDynamicDeadlineFrames;
        FrameLedger fresh;
        fresh.begin(2);
        const FramePicks pp = selectFrame(st, faces(3, 0.05f, 1), fresh.left(kFrameCapMs), kAvatarDrawsMax, fresh.untouched());
        const bool alone = fresh.untouched() && pp.dynamic.empty();
        check(alone && pp.fixed.size() == 1, "P4 gate: the progress case leaves dynamic empty, untouched ledger, one head");
        check(mayRunPick(0.f, false, true, alone), "P4 gate: the progress head is allowed past the cap");
        // (c) a second static pick with the ledger exhausted is refused, whatever `alone` says
        check(!mayRunPick(0.f, false, false, true), "P4 gate: a second static pick with the ledger exhausted is refused");
        check(!mayRunPick(0.f, true, true, true), "P4 gate: a dynamic pick is never exempt");
        check(mayRunPick(0.1f, false, false, false), "P4 gate: inside the cap everything runs");
    }
    // priority: the dynamic layer is served first, and the static tiles keep at least the floor
    {
        const std::vector<DynamicItem> dyn = faces(10, 0.05f, 1);
        const std::vector<StaticItem> st = tiles(10, 0.05f);
        const FramePicks p = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(p.dynamic.size() == 6, "P4 the dynamic layer takes its share: 0.30 ms / 0.05 ms = 6 faces", double(p.dynamic.size()));
        check(p.fixed.size() == 4, "P4 the static tiles get what is left, at least the floor: 0.20 ms / 0.05 ms = 4 tiles", double(p.fixed.size()));
        check(p.dynamic[0] == packKey(0, 1), "P4 the nearest face first (by position)");
        const FramePicks only_static = selectFrame(st, std::vector<DynamicItem>(), kFrameCapMs, kAvatarDrawsMax, true);
        check(only_static.fixed.size() == 10, "P4 without dynamic work the static tiles may take the whole cap: 0.5 / 0.05 = 10", double(only_static.fixed.size()));
        const FramePicks only_dyn = selectFrame(std::vector<StaticItem>(), dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(only_dyn.dynamic.size() == 10, "P4 the dynamic layer alone takes the whole cap when no static tile waits (C1): 0.5 / 0.05 = 10", double(only_dyn.dynamic.size()));
    }
    // a cap already spent by an earlier cache leaves less: the sum over both caches never exceeds the one cap
    {
        const std::vector<DynamicItem> dyn = faces(10, 0.05f, 1, 1);
        const std::vector<StaticItem> st = tiles(10, 0.05f, 1);
        FrameLedger ledger;
        ledger.begin(7);
        ledger.add(0.20f);
        const FramePicks p = selectFrame(st, dyn, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched());
        const float ms = predicted(p, st, dyn);
        check(ms + 0.20f <= kFrameCapMs + 1e-4f, "P4 a second cache spends only what the first left of the shared cap", ms);
        check(!p.dynamic.empty() || !p.fixed.empty(), "P4 and still serves something");
        check(unpackPool(p.dynamic.empty() ? p.fixed[0] : p.dynamic[0]) == 1 && unpackKey(p.dynamic.empty() ? p.fixed[0] : p.dynamic[0]) >= 1, "P4 picks come back with their pool and key");
        ledger.add(kFrameCapMs - 0.20f); // the rest of the cap
        const FramePicks none = selectFrame(st, dyn, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched());
        check(none.dynamic.empty() && none.fixed.empty(), "P4 a spent cap serves nothing for the next cache", double(none.dynamic.size() + none.fixed.size()));
        ledger.begin(8);
        check(ledger.untouched() && std::fabs(ledger.left(kFrameCapMs) - kFrameCapMs) < 1e-6f, "P4 the ledger starts every frame with the whole cap");
        ledger.begin(8);
        ledger.add(0.1f);
        ledger.begin(8);
        check(!ledger.untouched(), "P4 beginning the same frame again keeps what was spent (two caches may begin it)");
    }
    // the A_max ceiling: with a cheap face the avatar draws bind before the CPU share
    {
        const std::vector<DynamicItem> dyn = faces(40, 0.05f, 1);
        const std::vector<uint32_t> p = selectDynamic(dyn, 100.f, 24);
        check(p.size() == 24, "P4 the avatar draws never exceed A_max, whatever the CPU share", double(p.size()));
        check(selectFrame(std::vector<StaticItem>(), dyn, kFrameCapMs, 3, true).dynamic.size() == 3, "P4 a small A_max binds before the CPU share");
    }
    // static tiles' avatars are charged against A_max with the dynamic picks' (a cold fill cannot draw more than A_max in a frame)
    {
        auto drawsOf = [](const FramePicks& p, const std::vector<StaticItem>& st, const std::vector<DynamicItem>& dyn, uint32_t a_max) {
            uint32_t n = 0;
            for (uint32_t k : p.dynamic)
                for (const DynamicItem& d : dyn)
                    if (packKey(d.pool, d.key) == k) n += d.avatar ? std::min(std::max(d.avatar_draws, 1u), a_max) : 0u;
            for (uint32_t k : p.fixed)
                for (const StaticItem& s : st)
                    if (packKey(s.pool, s.key) == k) n += std::min(s.avatar_draws, a_max);
            return n;
        };
        std::vector<DynamicItem> dyn = faces(4, 0.05f, 1);
        std::vector<StaticItem> st = tiles(3, 0.05f);
        for (StaticItem& s : st) s.avatar_draws = 3;
        const FramePicks p = selectFrame(st, dyn, kFrameCapMs, 5, true);
        check(drawsOf(p, st, dyn, 5) <= 5, "P4 static plus dynamic avatar draws never exceed A_max", double(drawsOf(p, st, dyn, 5)));
        check(p.fixed.size() == 1, "P4 a static tile whose avatars do not fit the rest of A_max waits", double(p.fixed.size()));
        // the first static tile keeps its place (the dynamic layer gives up faces for it): nothing starves
        check(!p.fixed.empty() && unpackKey(p.fixed[0]) == 1, "P4 the head static tile with avatars is served");
        // a tile with more avatars than A_max draws A_max of them and runs alone as the exemption
        std::vector<StaticItem> big = tiles(1, 0.05f);
        big[0].avatar_draws = 100;
        const FramePicks q = selectFrame(big, faces(4, 0.05f, 1), kFrameCapMs, 3, true);
        check(q.fixed.size() == 1 && drawsOf(q, big, faces(4, 0.05f, 1), 3) <= 3, "P4 an avatar tile above A_max runs at A_max draws");
        // tiles without avatars are not charged
        std::vector<StaticItem> none = tiles(3, 0.05f);
        check(selectFrame(none, std::vector<DynamicItem>(), kFrameCapMs, 0, true).fixed.size() == 3, "P4 static tiles without avatars ignore A_max");
        // making room for the head's avatar draws drops avatar faces only, and only when the head is served
        {
            std::vector<DynamicItem> mix(3);
            for (uint32_t i = 0; i < 3; ++i) { mix[i].key = i + 1; mix[i].last_cpu_ms = 0.05f; mix[i].cam_dist = float(i); }
            mix[0].avatar = true;
            mix[0].avatar_draws = 3;
            std::vector<StaticItem> head = tiles(1, 0.05f);
            head[0].avatar_draws = 22;
            const FramePicks r = selectFrame(head, mix, kFrameCapMs, 24, true);
            check(r.dynamic.size() == 2 && r.fixed.size() == 1, "P4 only the avatar face yields to the head tile's draws");
            std::vector<StaticItem> big = tiles(1, 0.5f);
            big[0].avatar_draws = 22;
            big[0].never_drawn = false;
            big[0].age_frames = 0;
            const FramePicks r2 = selectFrame(big, mix, 0.2f, 24, false);
            check(r2.dynamic.size() == 3, "P4 dynamic faces are kept when the head is not served");
        }
    }
    // pool packing: the same key in two pools is two items
    check(packKey(1, 5) != packKey(0, 5) && unpackKey(packKey(1, 5)) == 5 && unpackPool(packKey(1, 5)) == 1, "P4 a packed key keeps its pool");
}

// The dynamic layer simulated over many frames: faces with an avatar, the nearest every frame, the rest by the 4-frame deadline.
void testP4Layer()
{
    // 12 lamps with an avatar in their light, a face costs 0.05 ms: 4 a frame by the dynamic share. Every face is served within
    // ceil(12 / 4) = 3 frames, and the nearest are served every frame as long as nothing is overdue.
    const uint32_t n = 12;
    std::vector<uint32_t> last(n + 1, 0);
    uint32_t worst = 0;
    bool far_served = true;
    for (uint32_t frame = 1; frame <= 200; ++frame)
    {
        std::vector<DynamicItem> items;
        for (uint32_t i = 1; i <= n; ++i)
        {
            DynamicItem it;
            it.key = i;
            it.avatar = true;
            it.age_frames = frame - last[i];
            it.cam_dist = float(i);
            it.last_cpu_ms = 0.05f;
            it.avatar_draws = 1;
            items.push_back(it);
        }
        const FramePicks p = selectFrame(std::vector<StaticItem>(), items, kFrameCapMs, kAvatarDrawsMax, true);
        for (uint32_t k : p.dynamic)
        {
            last[unpackKey(k)] = frame;
        }
        if (frame > 20)
        {
            for (uint32_t i = 1; i <= n; ++i)
            {
                worst = std::max(worst, frame - last[i]);
            }
        }
    }
    for (uint32_t i = 1; i <= n; ++i)
    {
        if (last[i] < 197) far_served = false;
    }
    check(far_served, "P4 layer: every face, near or far, is still served at the end of 200 frames");
    check(worst <= kDynamicDeadlineFrames, "P4 layer: no face waits longer than the deadline", worst);
    // the nearest alone (3 faces) run every frame
    std::vector<DynamicItem> few = faces(3, 0.05f, 1);
    bool every_frame = true;
    for (int frame = 0; frame < 50; ++frame)
    {
        if (selectFrame(std::vector<StaticItem>(), few, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() != 3) every_frame = false;
    }
    check(every_frame, "P4 layer: the avatar faces within the share run every frame");

    // the verdict: a static change and an avatar in the same tile: the fixed layer is redrawn (it includes the avatar layer)
    TileFacts f;
    f.pending = true;
    f.pending_cause = Cause::Edit;
    f.holds_avatar = true;
    const TileVerdict v = classifyTile(f);
    check(v.needs_render && !v.needs_layer && !v.dynamic, "P4 a tile with a change and an avatar is a static render (it draws the layer too)");
    // an avatar alone never makes the fixed layer stale
    TileFacts a;
    a.holds_avatar = true;
    a.avatar_in_layer = true;
    check(!classifyTile(a).needs_render, "P4 an avatar never dirties the static tile");
    // no avatar, nothing in L: a static scene stays free
    check(!classifyTile(TileFacts()).needs_layer, "P4 no avatar, nothing to restore: no item");
}

// The lag table of the pull request: min(A_max left, CPU cap / per-face cost), then ceil(faces / per frame)
void testP4Lag()
{
    // faces drawn every frame (the nearest) are part of A_max
    check(layerFacesPerFrame(kDynamicCapMs, 0.05f, kAvatarDrawsMax, 0) == 6, "P4 lag: 0.30 ms at 0.05 ms a face is 6 faces a frame", layerFacesPerFrame(kDynamicCapMs, 0.05f, kAvatarDrawsMax, 0));
    check(layerFacesPerFrame(kDynamicCapMs, 0.04f, kAvatarDrawsMax, 0) == 6, "P4 lag: a face below the scheduler floor is predicted at the floor");
    check(layerFacesPerFrame(kDynamicCapMs, 0.10f, kAvatarDrawsMax, 0) == 3, "P4 lag: 0.10 ms a face is 3 faces a frame");
    check(layerFacesPerFrame(kDynamicCapMs, 0.5f, kAvatarDrawsMax, 0) == 1, "P4 lag: a face over the share still runs alone");
    check(layerFacesPerFrame(10.f, 0.05f, kAvatarDrawsMax, 0) == 24, "P4 lag: A_max binds first with a large share");
    check(layerFacesPerFrame(10.f, 0.05f, kAvatarDrawsMax, 20) == 4, "P4 lag: A_max left after the every-frame draws");
    check(layerFacesPerFrame(10.f, 0.05f, kAvatarDrawsMax, 30) == 0, "P4 lag: no draw left");
    // the table of the PR: faces in range 4, 8, 12, 24 at 4 faces a frame; frames and ms at 60 and 120 fps
    check(layerLagFrames(4, 4) == 1 && layerLagFrames(8, 4) == 2 && layerLagFrames(12, 4) == 3 && layerLagFrames(24, 4) == 6, "P4 lag: 4 / 8 / 12 / 24 faces take 1 / 2 / 3 / 6 frames");
    check(layerLagFrames(5, 4) == 2 && layerLagFrames(0, 4) == 0, "P4 lag: rounding up, nothing to draw");
    // the simulation agrees with the formula: 24 faces, 4 a frame, the oldest served first
    {
        const uint32_t n = 24;
        std::vector<uint32_t> last(n + 1, 0);
        uint32_t worst = 0;
        for (uint32_t frame = 1; frame <= 300; ++frame)
        {
            std::vector<DynamicItem> items;
            for (uint32_t i = 1; i <= n; ++i)
            {
                DynamicItem it;
                it.key = i;
                it.avatar = true;
                it.age_frames = frame - last[i];
                it.cam_dist = float(i);
                it.last_cpu_ms = 0.05f;
                it.avatar_draws = 1;
                items.push_back(it);
            }
            for (uint32_t k : selectFrame(std::vector<StaticItem>(), items, kFrameCapMs, kAvatarDrawsMax, true).dynamic)
            {
                last[unpackKey(k)] = frame;
            }
            if (frame > 50)
            {
                for (uint32_t i = 1; i <= n; ++i) worst = std::max(worst, frame - last[i]);
            }
        }
        check(worst <= layerLagFrames(n, layerFacesPerFrame(kDynamicCapMs, 0.05f, kAvatarDrawsMax, 0)) + 1, "P4 lag: the simulated worst wait is the formula's (plus the frame it was seen in)", worst);
    }
}

void testP4Owners()
{
    // tracking per owner: the hooks run while any owner tracks; the pending boxes go only when the last owner leaves
    uint32_t mask = 0;
    const uint32_t proj = 1u, pts = 2u;
    uint32_t next = ownerMaskAfter(mask, proj, true);
    check(ownerEdge(mask, next) == OwnerEdge::First && next == proj, "P4 owners: the first owner switches the hooks on");
    mask = next;
    next = ownerMaskAfter(mask, pts, true);
    check(ownerEdge(mask, next) == OwnerEdge::Neither && next == (proj | pts), "P4 owners: a second owner changes nothing for the hooks");
    mask = next;
    next = ownerMaskAfter(mask, proj, false);
    check(ownerEdge(mask, next) == OwnerEdge::Neither && next == pts, "P4 owners: the projector pool releasing does not switch the hooks off or drop the point pool's boxes");
    mask = next;
    next = ownerMaskAfter(mask, pts, false);
    check(ownerEdge(mask, next) == OwnerEdge::Last && next == 0, "P4 owners: the last owner leaving switches them off and drops the boxes");
    check(ownerEdge(0, ownerMaskAfter(0, proj, false)) == OwnerEdge::Neither, "P4 owners: switching off what was never on is no edge");

    // lamp roots per owner: one owner's list does not replace the other's
    OwnerLists<int> roots;
    std::vector<int> a = { 5, 3, 3, 9 };
    std::vector<int> b = { 7, 3 };
    check(roots.set(0, a), "P4 roots: the first owner's list is new");
    check(roots.set(1, b), "P4 roots: the second owner's list is new");
    check(roots.contains(5) && roots.contains(9) && roots.contains(7) && roots.contains(3) && !roots.contains(4), "P4 roots: the lookup sees the union of both owners");
    check(roots.merged.size() == 4, "P4 roots: the union is sorted and de-duplicated", double(roots.merged.size()));
    std::vector<int> a2 = { 3, 5, 9 };
    check(!roots.set(0, a2), "P4 roots: the same list again changes nothing");
    std::vector<int> a3 = { 11 };
    roots.set(0, a3);
    check(!roots.contains(5) && roots.contains(11) && roots.contains(7) && roots.contains(3), "P4 roots: replacing one owner's list keeps the other's lamps");
    roots.clear(0);
    check(!roots.contains(11) && roots.contains(7) && roots.contains(3), "P4 roots: one owner released: its roots go, the other's stay");
    roots.clearAll();
    check(!roots.contains(7), "P4 roots: a reset clears every owner");

    // the frame's GL scope: one save and one restore however many caches render in the frame
    ScopeCounter scopes;
    int saves = 0, restores = 0;
    for (int pool = 0; pool < 3; ++pool)
    {
        const bool outer = scopes.enter();
        if (pool == 0) { /* the coordinator's scope */ }
        if (outer) ++saves;
        if (pool == 2)
        {
            for (int i = 0; i < 3; ++i)
            {
                if (scopes.leave()) ++restores;
            }
        }
    }
    check(saves == 1 && restores == 1, "P4 scope: nested scopes of three caches save once and restore once", saves, restores);
    ScopeCounter again;
    check(again.enter() && !again.enter() && !again.leave() && again.leave(), "P4 scope: the last leave restores");
}

// ---- The avatar layer's cadence (hotfix C1): faces drawn a frame and the worst gap, before and after ----
//
// One face = one lamp whose light holds an avatar. The simulation drives the scheduler the way LLProjectorShadows::update does: the
// pick (selectFrame), then the in-frame gate (mayRunPick, the dynamic share) with the measured cost, which is what each pick adds to
// the ledger. `legacy` is the release scheduler as it was before C1 (a fixed 0.20 ms share of a 0.35 ms cap, only an overdue face may run
// over it, lamp distance for the order, the last measured cost as the prediction); its pick is reproduced here from the old code, with
// the caps it had (kC1FrameCapMs, kC1DynamicCapMs; L2 set today's from the fps floor).
//   lamp_ties: every lamp at the same camera distance (0 indoors: the camera is inside every lamp's reach)
//   shared_avatar: every lamp holds the SAME avatar (the owner's own), so the avatar distance ties as well
constexpr float kC1FrameCapMs = 0.35f;   // the caps of the release scheduler before C1 (and before L2's floor-based budget)
constexpr float kC1DynamicCapMs = 0.20f;

struct CadenceResult
{
    double faces_per_frame = 0;
    uint32_t worst_gap = 0;
    uint32_t nearest_worst_gap = 0; // face 1, the one with the nearest avatar
};

std::vector<uint32_t> legacySelectDynamic(const std::vector<DynamicItem>& items, float t_d_ms, uint32_t a_max)
{
    std::vector<const DynamicItem*> order;
    for (const DynamicItem& it : items) order.push_back(&it);
    std::sort(order.begin(), order.end(), [](const DynamicItem* a, const DynamicItem* b) {
        const bool da = a->age_frames >= kDynamicDeadlineFrames, db = b->age_frames >= kDynamicDeadlineFrames;
        if (da != db) return da;
        if (da && a->age_frames != b->age_frames) return a->age_frames > b->age_frames;
        const float ca = sortDist(a->cam_dist), cb = sortDist(b->cam_dist);
        return ca != cb ? ca < cb : a->key < b->key;
    });
    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const DynamicItem* it : order)
    {
        const float cost = itemCost(it->last_cpu_ms);
        const uint32_t draws = it->avatar ? std::min(std::max(it->avatar_draws, 1u), a_max) : 0u;
        const bool overdue = it->age_frames >= kDynamicDeadlineFrames && out.empty();
        if ((spent + cost > t_d_ms && !overdue) || avatars + draws > a_max) continue;
        out.push_back(it->key);
        spent += cost;
        avatars += draws;
    }
    return out;
}

CadenceResult cadenceSim(uint32_t lamps, float cost, bool legacy, bool lamp_ties, bool shared_avatar, uint32_t frames = 600)
{
    std::vector<uint32_t> last(lamps + 1, 1);
    std::vector<float> measured(lamps + 1, 0.f); // lastAvatarMs: 0 until measured (itemCost floors it at kDefaultItemMs)
    std::vector<uint32_t> worst(lamps + 1, 0);
    uint64_t drawn = 0;
    const uint32_t warm = 40;
    for (uint32_t frame = 2; frame <= frames; ++frame)
    {
        std::vector<DynamicItem> items;
        for (uint32_t i = 1; i <= lamps; ++i)
        {
            DynamicItem it;
            it.key = i;
            it.avatar = true;
            it.age_frames = frame - last[i];
            // before: the lamp's distance (a tie indoors); after: the camera position's distance to the nearest avatar
            it.cam_dist = legacy ? (lamp_ties ? 0.f : float(i)) : (shared_avatar ? 3.f : float(i));
            it.last_cpu_ms = measured[i];
            it.avatar_draws = 1;
            items.push_back(it);
        }
        FrameLedger ledger;
        ledger.begin(frame);
        std::vector<uint32_t> picks;
        const float cap = legacy ? kC1FrameCapMs : kFrameCapMs;
        if (legacy)
        {
            const float left = ledger.left(cap);
            for (uint32_t k : legacySelectDynamic(items, std::min(kC1DynamicCapMs, left) + 1e-4f, kAvatarDrawsMax)) picks.push_back(k);
            float ms = 0.f;
            for (uint32_t k : picks) ms += itemCost(measured[k]);
            if (ms > left + 1e-4f && picks.size() != 1) picks.clear();
        }
        else
        {
            for (uint32_t k : selectFrame(std::vector<StaticItem>(), items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched()).dynamic)
                picks.push_back(unpackKey(k));
        }
        const float share = legacy ? kC1DynamicCapMs : dynamicShareMs(false, kFrameCapMs);
        float own = 0.f;
        bool first = true;
        for (uint32_t k : picks)
        {
            if (!mayRunPick(ledger.left(cap), true, first, false) || (!first && own >= share)) break;
            if (frame > warm) worst[k] = std::max(worst[k], frame - last[k]);
            last[k] = frame;
            measured[k] = legacy ? cost : smoothedCostMs(measured[k], cost);
            ledger.add(cost);
            own += cost;
            first = false;
            if (frame > warm) ++drawn;
        }
    }
    CadenceResult r;
    r.faces_per_frame = double(drawn) / double(frames - warm);
    for (uint32_t i = 1; i <= lamps; ++i) r.worst_gap = std::max(r.worst_gap, worst[i]);
    r.nearest_worst_gap = worst[1];
    return r;
}

void testP4Cadence(bool print_table)
{
    const float costs[] = { 0.04f, 0.07f, 0.10f, 0.15f, 0.20f, 0.30f };
    if (print_table)
    {
        std::printf("cadence, indoors (every lamp ties at distance 0), one avatar under all lamps: faces drawn a frame / worst gap in frames\n");
        std::printf("lamps  cost ms |   before        after\n");
    }
    for (uint32_t lamps = 1; lamps <= 5; ++lamps)
    {
        for (float cost : costs)
        {
            const CadenceResult before = cadenceSim(lamps, cost, true, true, true);
            const CadenceResult after = cadenceSim(lamps, cost, false, true, true);
            if (print_table)
            {
                std::printf("%5u  %7.2f | %5.2f / %-3u   %5.2f / %-3u\n", lamps, cost, before.faces_per_frame, before.worst_gap, after.faces_per_frame, after.worst_gap);
            }
            // never worse than before, in faces a frame and in the worst gap
            check(after.faces_per_frame + 1e-9 >= before.faces_per_frame, "C1 cadence: no fewer faces a frame than before", lamps, cost);
            check(after.worst_gap <= before.worst_gap, "C1 cadence: the worst gap is not longer than before", lamps, cost);
            // the deadline bound holds up to about 5 lamps in overload: the deadline serves one overdue face a frame, so with more
            // overdue faces than frames the wait is the number of lamps, not 4 (this table stops at 5)
            check(after.worst_gap <= kDynamicDeadlineFrames + 1, "C1 cadence: no face waits longer than the deadline (plus the frame it was seen in)", lamps, cost);
            // whatever fits the whole cap is drawn every frame
            const float need = float(lamps) * std::max(cost, kDefaultItemMs);
            if (need <= kFrameCapMs - 1e-3f)
            {
                check(after.worst_gap == 1, "C1 cadence: faces that fit the cap run every frame", lamps, cost);
            }
            if (lamps == 1)
            {
                check(after.worst_gap == 1 && after.faces_per_frame > 0.99, "C1 cadence: a single face, however heavy, runs every frame", cost);
            }
        }
    }
    // the cases of the survey (sim_out.md): 3 lamps at 0.07 ms had two every frame and the third every 4th; one lamp at 0.21 ms every 4th
    {
        const CadenceResult b = cadenceSim(3, 0.07f, true, true, true), a = cadenceSim(3, 0.07f, false, true, true);
        check(b.worst_gap == 4 && a.worst_gap == 1, "C1 cadence: 3 lamps at 0.07 ms: gap 4 before, every frame after", b.worst_gap, a.worst_gap);
        const CadenceResult b1 = cadenceSim(1, 0.21f, true, true, true), a1 = cadenceSim(1, 0.21f, false, true, true);
        check(b1.worst_gap == 4 && a1.worst_gap == 1, "C1 cadence: 1 lamp at 0.21 ms: gap 4 before, every frame after", b1.worst_gap, a1.worst_gap);
    }
    // indoors with avatars at different distances: the nearest avatar's face runs every frame, even when the lamps tie at 0 and
    // the faces do not all fit
    for (uint32_t lamps = 2; lamps <= 5; ++lamps)
    {
        for (float cost : costs)
        {
            const CadenceResult after = cadenceSim(lamps, cost, false, true, false);
            // every frame while the faces fit; in a real overload (more face cost than the cap holds) the 4-frame deadline, the safety
            // net, turns the service into a round robin and the nearest face waits like the others: bounded, never starved
            if (cost <= 0.10f || (lamps <= 4 && cost <= 0.15f) || lamps == 1)
            {
                check(after.nearest_worst_gap == 1, "C1 cadence: indoors the nearest avatar's face is drawn every frame", lamps, cost);
            }
            check(after.nearest_worst_gap <= kDynamicDeadlineFrames + 1, "C1 cadence: in an overload the nearest face still waits at most the deadline", lamps, cost);
        }
    }
    // the order: the avatar's distance first, then the age; the lamp's key decides nothing while ages differ
    {
        std::vector<DynamicItem> v = faces(3, 0.2f, 1);
        v[0].cam_dist = v[1].cam_dist = v[2].cam_dist = 3.f; // one avatar under three lamps
        v[0].age_frames = 1; v[1].age_frames = 3; v[2].age_frames = 2;
        const std::vector<uint32_t> o = selectDynamic(v, 10.f, kAvatarDrawsMax);
        check(o.size() == 3 && o[0] == 2 && o[1] == 3 && o[2] == 1, "C1 order: equal avatar distances are served oldest first, not by key");
        v[2].cam_dist = 1.f;
        check(selectDynamic(v, 10.f, kAvatarDrawsMax)[0] == 3, "C1 order: the nearest avatar first");
        v[0].age_frames = kDynamicDeadlineFrames;
        check(selectDynamic(v, 10.f, kAvatarDrawsMax)[0] == 1, "C1 order: an overdue face goes first (the safety net)");
    }
    // the share: the whole remaining cap without a waiting static tile, kDynamicCapMs with one
    check(std::fabs(dynamicShareMs(false, kFrameCapMs) - kFrameCapMs) < 1e-6f, "C1 share: no static tile waiting, the whole cap");
    check(std::fabs(dynamicShareMs(true, kFrameCapMs) - kDynamicCapMs) < 1e-6f, "C1 share: a static tile waiting, the fixed share");
    check(dynamicShareMs(true, 0.1f) == 0.1f && dynamicShareMs(false, -1.f) == 0.f, "C1 share: never more than what is left");
    {
        const std::vector<DynamicItem> dyn = faces(10, 0.05f, 1);
        check(selectFrame(std::vector<StaticItem>(), dyn, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 10, "C1 share: 0.5 / 0.05 = 10 faces without a static tile");
        check(selectFrame(tiles(1, 0.05f), dyn, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 6, "C1 share: with a static tile waiting, 0.30 / 0.05 = 6 faces");
    }
    // a heavy single face: alone, every frame; not over the cap when the frame has spent already
    {
        std::vector<DynamicItem> heavy = faces(1, 0.6f, 0);
        heavy[0].own = heavy[0].layer = true; // the own avatar's face (another avatar's heavy face waits for its deadline)
        const FramePicks p = selectFrame(std::vector<StaticItem>(), heavy, kFrameCapMs, kAvatarDrawsMax, true);
        check(p.dynamic.size() == 1, "C1 heavy: the own avatar's face over the whole cap runs alone, young or not");
        check(selectFrame(std::vector<StaticItem>(), heavy, 0.2f, kAvatarDrawsMax, false).dynamic.size() == 1, "C1 heavy: the own face runs even once the frame has spent");
        std::vector<DynamicItem> two = faces(2, 0.6f, 0);
        two[0].own = two[0].layer = true;
        check(selectFrame(std::vector<StaticItem>(), two, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 1, "C1 heavy: never two over-cap faces");
        // an over-cap face does not stop a face that fits when the frame has spent (the cheap one runs)
        std::vector<DynamicItem> mix = faces(2, 0.6f, 0);
        mix[1].last_cpu_ms = 0.06f;
        const FramePicks m = selectFrame(std::vector<StaticItem>(), mix, 0.2f, kAvatarDrawsMax, false);
        check(m.dynamic.size() == 1 && unpackKey(m.dynamic[0]) == 2, "C1 heavy: a face that fits still runs behind a heavy one when the frame has spent");
    }
    // the cost prediction: an EMA, a spike does not push a face out
    {
        float p = 0.08f;
        p = smoothedCostMs(p, 0.30f);
        check(p < kDynamicCapMs, "C1 smoothing: one spike of 0.30 ms leaves the prediction under the share", p);
        for (int i = 0; i < 40; ++i) p = smoothedCostMs(p, 0.30f);
        check(std::fabs(p - 0.30f) < 1e-3f, "C1 smoothing: a lasting change is followed", p);
        check(smoothedCostMs(0.1f, std::numeric_limits<float>::quiet_NaN()) == 0.1f && smoothedCostMs(0.1f, -1.f) == 0.1f, "C1 smoothing: a bad sample is ignored");
        check(smoothedCostMs(0.f, 0.2f) == 0.2f, "C1 smoothing: the first sample seeds it");
        // a fresh record predicts 0 (itemCost floors it at the default) and its first real sample is the prediction, not a blend with 0.05
        check(itemCost(0.f) == kDefaultItemMs && smoothedCostMs(0.f, 0.3f) == 0.3f, "C1 smoothing: a zero prediction is floored by the scheduler and seeded by the first sample");
        // a sample is clamped to twice the cap, so one hitch cannot make a face heavy for long
        check(kCostSampleMaxMs == 2.f * kFrameCapMs, "C1 smoothing: the clamp is twice the frame cap");
        check(smoothedCostMs(0.f, 50.f) == kCostSampleMaxMs, "C1 smoothing: a seed is clamped");
        float q = 0.08f;
        q = smoothedCostMs(q, 50.f);
        check(q <= 0.08f + kCostSmoothing * (kCostSampleMaxMs - 0.08f) + 1e-6f, "C1 smoothing: a 50 ms hitch counts as kCostSampleMaxMs (1 ms)", q);
        int frames_heavy = 0;
        while (q > kDynamicCapMs && frames_heavy < 100)
        {
            q = smoothedCostMs(q, 0.08f);
            ++frames_heavy;
        }
        check(frames_heavy <= 3, "C1 smoothing: after a hitch the face is not over the share for more than 3 frames", frames_heavy);
    }
    // the avatar's distance to the camera position (never the view direction)
    {
        Aabb b;
        for (int i = 0; i < 3; ++i) { b.lo[i] = 0.f; b.hi[i] = 2.f; }
        const float inside[3] = { 1.f, 1.f, 1.f }, out_x[3] = { 5.f, 1.f, 1.f }, corner[3] = { 5.f, 6.f, 1.f };
        check(pointBoxDistance(inside, b) == 0.f && pointBoxDistance(out_x, b) == 3.f && std::fabs(pointBoxDistance(corner, b) - 5.f) < 1e-5f, "C1 distance: point to box");
    }
}

// ---- The own avatar and the distance cadence (own-avatar hotfix) ----
//
// One own face (the user's avatar under a lamp), `near_n` faces whose nearest other avatar is 5 m from the camera position, `far_n` at
// 20 m. `before` is the C1 scheduler (selectDynamic as C1 left it: one 4-frame deadline, an avatar face may run over the share as the first
// pick, no own avatar), reproduced here; `after` is selectFrame. The measured cost of every face is `cost` (the prediction is exact, to
// isolate the schedule). Gaps are frames between two draws of the same face, after a warm-up.
struct OwnCadence
{
    uint32_t own_gap = 0, near_gap = 0, far_gap = 0;
    double faces_per_frame = 0;
};

std::vector<uint32_t> c1SelectDynamic(const std::vector<DynamicItem>& items, float t_d_ms, uint32_t a_max)
{
    std::vector<const DynamicItem*> order;
    for (const DynamicItem& it : items) order.push_back(&it);
    std::sort(order.begin(), order.end(), [](const DynamicItem* a, const DynamicItem* b) {
        const bool da = a->age_frames >= kDynamicDeadlineFrames, db = b->age_frames >= kDynamicDeadlineFrames;
        if (da != db) return da;
        if (da && a->age_frames != b->age_frames) return a->age_frames > b->age_frames;
        const float ca = sortDist(a->cam_dist), cb = sortDist(b->cam_dist);
        if (ca != cb) return ca < cb;
        return a->age_frames != b->age_frames ? a->age_frames > b->age_frames : a->key < b->key;
    });
    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const DynamicItem* it : order)
    {
        const float cost = itemCost(it->last_cpu_ms);
        const uint32_t draws = it->avatar ? std::min(std::max(it->avatar_draws, 1u), a_max) : 0u;
        const bool may_over = out.empty() && (it->avatar || it->age_frames >= kDynamicDeadlineFrames);
        if ((spent + cost > t_d_ms && !may_over) || avatars + draws > a_max) continue;
        out.push_back(it->key);
        spent += cost;
        avatars += draws;
    }
    return out;
}

OwnCadence ownCadenceSim(uint32_t near_n, uint32_t far_n, float cost, bool before, uint32_t frames = 800)
{
    const uint32_t total = 1 + near_n + far_n; // key 1 is the own face
    std::vector<uint32_t> last(total + 1, 1), worst(total + 1, 0);
    uint64_t drawn = 0;
    const uint32_t warm = 40;
    for (uint32_t frame = 2; frame <= frames; ++frame)
    {
        std::vector<DynamicItem> items;
        for (uint32_t i = 1; i <= total; ++i)
        {
            DynamicItem it;
            it.key = i;
            it.avatar = true;
            it.layer = true;
            it.own = !before && i == 1;
            it.age_frames = frame - last[i];
            it.cam_dist = i == 1 ? 2.f : (i <= 1 + near_n ? 5.f + 0.01f * float(i) : 20.f + 0.01f * float(i));
            it.last_cpu_ms = cost;
            it.avatar_draws = 1;
            items.push_back(it);
        }
        FrameLedger ledger;
        ledger.begin(frame);
        std::vector<uint32_t> picks;
        std::set<uint32_t> own;
        if (before)
        {
            const float left = ledger.left(kFrameCapMs);
            picks = c1SelectDynamic(items, left + 1e-4f, kAvatarDrawsMax);
            float ms = 0.f;
            for (size_t i = 0; i < picks.size(); ++i) ms += itemCost(cost);
            if (ms > left + 1e-4f && picks.size() != 1) picks.clear();
        }
        else
        {
            own.insert(1);
            for (uint32_t k : selectFrame(std::vector<StaticItem>(), items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched()).dynamic)
                picks.push_back(unpackKey(k));
        }
        float spent = 0.f;
        bool first = true;
        for (uint32_t k : picks)
        {
            const bool is_own = own.count(k) > 0;
            if (!mayRunPick(ledger.left(kFrameCapMs), true, first, false, is_own) || (!first && !is_own && spent >= kFrameCapMs)) break;
            if (frame > warm) worst[k] = std::max(worst[k], frame - last[k]);
            last[k] = frame;
            ledger.add(cost);
            spent += cost;
            first = false;
            if (frame > warm) ++drawn;
        }
    }
    OwnCadence r;
    r.faces_per_frame = double(drawn) / double(frames - warm);
    r.own_gap = worst[1];
    for (uint32_t i = 2; i <= total; ++i)
    {
        uint32_t& slot = i <= 1 + near_n ? r.near_gap : r.far_gap;
        slot = std::max(slot, worst[i]);
    }
    return r;
}

void testOwnAvatarCadence(bool print_table)
{
    // the deadline classes
    {
        DynamicItem it;
        it.avatar = true;
        it.layer = true;
        it.avatar_draws = 1;
        it.cam_dist = kNearAvatarM;
        check(dynamicDeadline(it) == kNearDeadlineFrames, "own cadence: an avatar at exactly kNearAvatarM is near (deadline 2)");
        it.cam_dist = kNearAvatarM + 0.01f;
        check(dynamicDeadline(it) == kDynamicDeadlineFrames, "own cadence: beyond kNearAvatarM the deadline is 4");
        it.cam_dist = std::numeric_limits<float>::infinity();
        check(dynamicDeadline(it) == kDynamicDeadlineFrames, "own cadence: an unknown distance is far");
        it.cam_dist = 1.f;
        it.avatar_draws = 0; // a face that only restores L from S
        check(dynamicDeadline(it) == kDynamicDeadlineFrames, "own cadence: a restore-only face keeps deadline 4");
        it.avatar_draws = 1;
        it.layer = false; // a moving lamp's tile
        check(dynamicDeadline(it) == kDynamicDeadlineFrames, "own cadence: a moving lamp's tile keeps deadline 4 whatever the distance");
        it.layer = true;
        it.own = true;
        check(dynamicDeadline(it) == 1, "own cadence: the own avatar's face has deadline 1");
    }
    if (print_table)
    {
        std::printf("\nown-avatar cadence under overload (one own face, N near <= 10 m, F far), worst gap in frames own/near/far, faces a frame\n");
        std::printf("near far  cost ms |   before (C1)            after\n");
    }
    const float costs[] = { 0.05f, 0.07f, 0.10f, 0.15f };
    for (uint32_t near_n : { 0u, 1u, 2u, 3u, 4u })
    {
        for (uint32_t far_n : { 0u, 2u, 4u, 8u })
        {
            for (float cost : costs)
            {
                const OwnCadence b = ownCadenceSim(near_n, far_n, cost, true), a = ownCadenceSim(near_n, far_n, cost, false);
                if (print_table)
                {
                    std::printf("%4u %3u  %7.2f | %2u/%2u/%2u %5.2f      %2u/%2u/%2u %5.2f\n", near_n, far_n, cost, b.own_gap, b.near_gap,
                                b.far_gap, b.faces_per_frame, a.own_gap, a.near_gap, a.far_gap, a.faces_per_frame);
                }
                // the own face: every frame in every scenario, however heavy the overload
                check(a.own_gap == 1, "own cadence: the own avatar's face is drawn every frame", double(near_n * 100 + far_n), cost);
                // slots a frame holds at this cost (the whole cap: no static tile waits); the own face takes one every frame, a near face
                // half of one, a far face a quarter. Where that fits, the near gap is <= 2 and the far gap <= 4; where it does not,
                // the faces share what is left and the gaps are what the budget gives (printed above)
                const float slots = std::floor(kFrameCapMs / std::max(cost, kDefaultItemMs) + 1e-4f);
                const float load = 1.f + 0.5f * float(near_n) + 0.25f * float(far_n);
                if (load + 1e-4f <= slots)
                {
                    check(a.near_gap <= kNearDeadlineFrames, "own cadence: near faces wait at most 2 frames where the budget holds it", double(near_n * 100 + far_n), cost);
                    check(a.far_gap <= kDynamicDeadlineFrames, "own cadence: far faces wait at most 4 frames where the budget holds it", double(near_n * 100 + far_n), cost);
                }
                // everything fits: every face every frame, as before
                if (float(1 + near_n + far_n) <= slots)
                {
                    check(a.near_gap <= 1 && a.far_gap <= 1 && a.own_gap == 1, "own cadence: when everything fits every face runs every frame", double(near_n * 100 + far_n), cost);
                    check(b.near_gap <= 1 && b.far_gap <= 1, "own cadence: ... as before", double(near_n * 100 + far_n), cost);
                }
            }
        }
    }
    // the own face runs past the cap and past an exhausted frame, alone or beside others, heavy or not
    {
        std::vector<DynamicItem> v = faces(3, 0.6f, 0); // all heavy (0.6 ms > the whole cap)
        v[1].own = true;
        v[1].layer = true;
        for (bool ok : { true, false })
        {
            for (float cap : { kFrameCapMs, 0.2f, 0.f, -0.1f })
            {
                const FramePicks p = selectFrame(std::vector<StaticItem>(), v, cap, kAvatarDrawsMax, ok);
                bool has_own = false;
                for (uint32_t k : p.dynamic) has_own = has_own || unpackKey(k) == 2;
                check(has_own && p.dynamic.size() == 1, "own cadence: a heavy own face runs alone, whatever the cap or the frame's spending", ok, cap);
            }
        }
        // two own faces (the own avatar under two lamps) both run, beside cheap faces when they fit
        std::vector<DynamicItem> w = faces(4, 0.05f, 0);
        w[0].own = w[1].own = true;
        const FramePicks p = selectFrame(std::vector<StaticItem>(), w, kFrameCapMs, kAvatarDrawsMax, true);
        check(p.dynamic.size() == 4 && unpackKey(p.dynamic[0]) <= 2 && unpackKey(p.dynamic[1]) <= 2, "own cadence: the own faces come first, all of them");
        // only the own faces remain when the cap is exhausted by an earlier cache
        const FramePicks q = selectFrame(std::vector<StaticItem>(), w, 0.f, kAvatarDrawsMax, false);
        check(q.dynamic.size() == 2, "own cadence: past an exhausted cap only the own faces run", q.dynamic.size());
    }
    // another avatar's heavy face is not exempt: over the share only once past its deadline (the own avatar alone has the exemption)
    {
        std::vector<DynamicItem> v = faces(1, 0.6f, 0);
        v[0].layer = true;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.empty(), "own cadence: a heavy young face of another avatar waits");
        v[0].age_frames = 2; // near deadline
        v[0].cam_dist = 3.f;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 1, "own cadence: ... and runs alone once past its deadline (near: 2)");
        v[0].cam_dist = 30.f;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.empty(), "own cadence: a far one is not due at 2");
        v[0].age_frames = kDynamicDeadlineFrames;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 1, "own cadence: ... but at 4");
    }
    // a moving lamp's tile keeps C1's rule: over the share only once overdue (4 frames), whatever its distance
    {
        std::vector<DynamicItem> v = faces(1, 0.6f, 2);
        v[0].avatar = false;
        v[0].cam_dist = 1.f;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.empty(), "own cadence: a heavy moving lamp's tile waits at 2 frames");
        v[0].age_frames = kDynamicDeadlineFrames;
        check(selectFrame(std::vector<StaticItem>(), v, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 1, "own cadence: ... runs alone at the deadline");
    }
    // the order under overload: own, then past-deadline (the more overdue first), then nearest
    {
        std::vector<DynamicItem> v = faces(4, 0.2f, 1);
        v[0].cam_dist = 30.f; v[0].age_frames = 5;           // far, 2 frames past the deadline
        v[1].cam_dist = 3.f;  v[1].age_frames = 2;           // near, due (1 past)
        v[2].cam_dist = 8.f;  v[2].age_frames = 1;           // near, not due
        v[3].cam_dist = 1.f;  v[3].own = true; v[3].age_frames = 1;
        for (DynamicItem& it : v) it.layer = true;
        const std::vector<uint32_t> o = selectDynamic(v, 10.f, kAvatarDrawsMax);
        check(o.size() == 4 && o[0] == 4 && o[1] == 1 && o[2] == 2 && o[3] == 3, "own cadence: own first, then the most overdue, then the nearest");
    }
    // the static guarantees stay: the head still runs beside the dynamic layer, and beside an own face too
    {
        std::vector<DynamicItem> v = faces(6, 0.05f, 1);
        for (DynamicItem& it : v) it.layer = true;
        v[0].own = true;
        const FramePicks p = selectFrame(tiles(1, 0.1f), v, kFrameCapMs, kAvatarDrawsMax, true);
        check(p.fixed.size() == 1, "own cadence: the static head keeps its place", p.fixed.size());
        check(!p.dynamic.empty() && unpackKey(p.dynamic[0]) == 1, "own cadence: ... and the own face stays first", p.dynamic.size());
        // a heavy own face (0.6 ms) leaves no room: an overdue head runs beside it as the progress pick
        std::vector<DynamicItem> h = faces(1, 0.6f, 1);
        h[0].own = h[0].layer = true;
        std::vector<StaticItem> st = tiles(1, 0.1f);
        st[0].age_frames = kDynamicDeadlineFrames;
        const FramePicks q = selectFrame(st, h, kFrameCapMs, kAvatarDrawsMax, true);
        check(q.dynamic.size() == 1 && q.fixed.size() == 1, "own cadence: an overdue static head runs beside a heavy own face (progress)");
        st[0].age_frames = 1;
        const FramePicks r = selectFrame(st, h, kFrameCapMs, kAvatarDrawsMax, true);
        check(r.dynamic.size() == 1 && r.fixed.empty(), "own cadence: a young head waits for room beside a heavy own face");
        // the head alone above the cap: overdue, it takes the progress pick and the own face stays
        std::vector<StaticItem> big = tiles(1, 0.6f);
        big[0].age_frames = kDynamicDeadlineFrames;
        std::vector<DynamicItem> o = faces(2, 0.05f, 1);
        o[0].own = o[0].layer = true;
        const FramePicks s = selectFrame(big, o, kFrameCapMs, kAvatarDrawsMax, true);
        check(s.fixed.size() == 1 && s.dynamic.size() == 1 && unpackKey(s.dynamic[0]) == 1, "own cadence: an over-cap overdue head drops the other faces, not the own one");
    }
}

// ---- The avatar layer's footprint copy (hotfix C3) ----
//
// A pure model of a tile's L and S depth: the copy and the draw are the ones the pool issues. The gate: after every pass, L equals
// what a whole-tile copy followed by the same scissored draw gives, bit for bit, everywhere (so in particular outside the rectangle).

// column-major projection * view of a camera at the origin looking down -z (fov 90 degrees, square, near 0.1, far 100)
void perspective90(float m[16])
{
    for (int i = 0; i < 16; ++i) m[i] = 0.f;
    const float n = 0.1f, f = 100.f;
    m[0] = 1.f;
    m[5] = 1.f;
    m[10] = -(f + n) / (f - n);
    m[11] = -1.f;
    m[14] = -2.f * f * n / (f - n);
}

Aabb box3(float x0, float y0, float z0, float x1, float y1, float z1)
{
    Aabb b;
    b.lo[0] = x0; b.lo[1] = y0; b.lo[2] = z0;
    b.hi[0] = x1; b.hi[1] = y1; b.hi[2] = z1;
    return b;
}

void testP4Footprint()
{
    float m[16];
    perspective90(m);
    const uint32_t n = 1000;
    // a box 1 m either side of the axis, 4 to 5 m ahead: at 4 m it spans +-0.25 of half the tile
    {
        TexelRect r;
        const bool known = boxFootprint(m, box3(-1.f, -1.f, -5.f, 1.f, 1.f, -4.f), n, r);
        check(known, "C3 footprint: a box ahead is known");
        const int32_t lo = int32_t(std::floor((0.5f - 0.125f) * 1000.f)) - kFootprintMarginTexels;
        const int32_t hi = int32_t(std::ceil((0.5f + 0.125f) * 1000.f)) + kFootprintMarginTexels;
        check(r.x0 == lo && r.x1 == hi && r.y0 == lo && r.y1 == hi, "C3 footprint: the projected extents plus the texel margin", r.x0, r.x1);
    }
    // a corner behind the eye plane: unknown, so the whole tile
    {
        TexelRect r;
        check(!boxFootprint(m, box3(-1.f, -1.f, -2.f, 1.f, 1.f, 1.f), n, r), "C3 footprint: a corner behind the eye plane is unknown");
        const std::vector<Aabb> boxes{ box3(-1.f, -1.f, -2.f, 1.f, 1.f, 1.f) };
        check(faceFootprint(m, boxes, n) == wholeTileRect(n), "C3 footprint: unknown falls back to the whole tile");
        float bad[16];
        for (int i = 0; i < 16; ++i) bad[i] = std::numeric_limits<float>::quiet_NaN();
        check(faceFootprint(bad, std::vector<Aabb>{ box3(-1.f, -1.f, -5.f, 1.f, 1.f, -4.f) }, n) == wholeTileRect(n), "C3 footprint: a non-finite matrix falls back to the whole tile");
    }
    // too large: the whole tile; no avatar: nothing; a clamped box outside the tile: nothing
    {
        check(faceFootprint(m, std::vector<Aabb>{ box3(-3.f, -3.f, -2.f, 3.f, 3.f, -1.5f) }, n) == wholeTileRect(n), "C3 footprint: a box over half the tile is the whole tile");
        check(faceFootprint(m, std::vector<Aabb>(), n).empty(), "C3 footprint: no avatar, no rectangle");
        TexelRect r;
        check(boxFootprint(m, box3(50.f, 0.f, -5.f, 52.f, 1.f, -4.f), n, r) && r.empty(), "C3 footprint: a box far off the tile's side covers nothing");
        const TexelRect two = faceFootprint(m, std::vector<Aabb>{ box3(-1.f, -1.f, -5.f, -0.5f, 0.f, -4.f), box3(0.5f, 0.f, -5.f, 1.f, 1.f, -4.f) }, n);
        TexelRect a, b;
        boxFootprint(m, box3(-1.f, -1.f, -5.f, -0.5f, 0.f, -4.f), n, a);
        boxFootprint(m, box3(0.5f, 0.f, -5.f, 1.f, 1.f, -4.f), n, b);
        check(two == unionRect(a, b), "C3 footprint: several avatars give the union");
    }
    // rectangle helpers
    {
        const TexelRect a{ 10, 10, 20, 20 }, b{ 15, 5, 30, 12 };
        check(unionRect(a, b) == TexelRect({ 10, 5, 30, 20 }), "C3 rect: union");
        check(unionRect(TexelRect(), a) == a && unionRect(a, TexelRect()) == a && unionRect(TexelRect(), TexelRect()).empty(), "C3 rect: an empty rectangle is the identity of the union");
        check(clampRect(TexelRect({ -5, -5, 2000, 7 }), 100) == TexelRect({ 0, 0, 100, 7 }) && clampRect(TexelRect({ 200, 0, 300, 5 }), 100).empty(), "C3 rect: clamped to the tile");
        check(layerCopyRect(false, a, b, 64) == wholeTileRect(64), "C3 copy: an unknown old rectangle copies the whole tile");
        check(layerCopyRect(true, a, b, 64) == unionRect(a, b), "C3 copy: the old and the new rectangle");
        check(layerCopyRect(true, a, TexelRect(), 64) == a && layerCopyRect(true, TexelRect(), TexelRect(), 64).empty(), "C3 copy: an avatar that left restores its old rectangle; nothing, nothing");
        const Aabb point = box3(0, 0, 0, 0, 0, 0), unit = box3(0, 0, 0, 1, 1, 2);
        const float nanf = std::numeric_limits<float>::quiet_NaN();
        check(std::fabs(footprintMarginM(0.f, 0, point) - kFootprintMarginM) < 1e-6f, "C3 margin: the base");
        check(std::fabs(footprintMarginM(0.f, 4, point) - (kFootprintMarginM + 4 * kFootprintStaleM)) < 1e-6f, "C3 margin: grows with the extents' refresh period");
        check(footprintMarginM(0.f, 20, point) > footprintMarginM(0.f, 4, point) + 0.6f, "C3 margin: 25 avatars (period 20) more than doubles the margin of 5");
        check(std::fabs(footprintMarginM(10.f, 0, point) - footprintMarginM(0.f, 0, point) - 10.f * kFootprintSpeedSeconds) < 1e-5f, "C3 margin: grows with the speed");
        check(std::fabs(footprintMarginM(0.f, 0, unit) - footprintMarginM(0.f, 0, point) - kFootprintSizeFraction * std::sqrt(6.f)) < 1e-5f, "C3 margin: grows with the box diagonal");
        check(footprintMarginM(nanf, -3, point) == kFootprintMarginM && std::isfinite(footprintMarginM(0.f, 4, box3(0, 0, 0, nanf, 1, 1))), "C3 margin: a bad speed, period or box adds nothing");
        check(inflateBox(box3(0, 0, 0, 1, 1, 1), 0.5f).lo[2] == -0.5f && inflateBox(box3(0, 0, 0, 1, 1, 1), 0.5f).hi[0] == 1.5f, "C3 margin: the box grows on every side");
    }

    // The gate: the incremental S -> L copy gives the whole-tile result bit for bit, over moving, growing, vanishing and unknown rectangles,
    // and with an invalidation (a repack) in between. A depth write is a LESS test against what L holds, within the scissor.
    {
        const uint32_t t = 64;
        std::mt19937 rng(12345);
        std::uniform_real_distribution<float> depth(0.f, 1.f);
        std::vector<float> S(t * t), L(t * t), ref(t * t);
        for (float& v : S) v = depth(rng);
        L = S;
        TexelRect prev; // L equals S: nothing drawn yet
        bool valid = true;
        bool exact = true, outside_is_S = true;
        uint32_t whole_copies = 0, partial_copies = 0;
        auto draw = [&](std::vector<float>& tile, const TexelRect& scissor, uint32_t seed) {
            std::mt19937 g(seed);
            for (int32_t y = scissor.y0; y < scissor.y1; ++y)
                for (int32_t x = scissor.x0; x < scissor.x1; ++x)
                {
                    const float d = depth(g);
                    if ((g() & 3u) != 0u && d < tile[size_t(y) * t + size_t(x)]) tile[size_t(y) * t + size_t(x)] = d;
                }
        };
        for (uint32_t frame = 0; frame < 500; ++frame)
        {
            TexelRect now; // the avatar moves, grows, vanishes, or is unknown (the whole tile)
            const uint32_t kind = rng() % 10;
            if (kind == 0) now = TexelRect();
            else if (kind == 1) now = wholeTileRect(t);
            else
            {
                const int32_t x = int32_t(rng() % 50), y = int32_t(rng() % 50), w = 1 + int32_t(rng() % 14), h = 1 + int32_t(rng() % 14);
                now = clampRect(TexelRect({ x, y, x + w, y + h }), t);
            }
            if (frame % 97 == 50) valid = false; // a repack: L is unknown
            if (!valid)
            {
                for (float& v : L) v = depth(rng); // unknown content: only the whole copy makes it S again
            }
            const TexelRect copy = layerCopyRect(valid, prev, now, t);
            if (copy == wholeTileRect(t)) ++whole_copies; else ++partial_copies;
            // the S -> L copy over `copy`
            for (int32_t y = copy.y0; y < copy.y1; ++y)
                for (int32_t x = copy.x0; x < copy.x1; ++x) L[size_t(y) * t + size_t(x)] = S[size_t(y) * t + size_t(x)];
            draw(L, now, frame + 1);
            // the reference: the whole tile copied, the same scissored draw
            ref = S;
            draw(ref, now, frame + 1);
            if (L != ref) exact = false;
            for (int32_t y = 0; y < int32_t(t); ++y)
                for (int32_t x = 0; x < int32_t(t); ++x)
                {
                    const bool in = x >= now.x0 && x < now.x1 && y >= now.y0 && y < now.y1;
                    if (!in && L[size_t(y) * t + size_t(x)] != S[size_t(y) * t + size_t(x)]) outside_is_S = false;
                }
            prev = now;
            valid = true;
        }
        check(exact, "C3 gate: L equals a whole-tile copy plus the same scissored draw, bit for bit, every pass");
        check(outside_is_S, "C3 gate: outside the footprint rectangle L is bit-exact S");
        check(partial_copies > 300 && whole_copies > 10, "C3 gate: the run covered partial and whole copies", partial_copies, whole_copies);
    }
}

void testP4(bool print_table)
{
    testP4Cadence(print_table);
    testOwnAvatarCadence(print_table);
    testP4Footprint();
    testP4Caps();
    testP4Layer();
    testP4Lag();
    testP4Owners();
}

// ---- T7 differential: selectStatic / selectFrame against the code before the hotfix (the full sort, the key maps) ----
// The oracle below is a copy of the previous implementation; the new one must pick the same keys in the same order and predict
// the same milliseconds bit for bit.
static std::vector<uint32_t> oracleSelectStatic(const std::vector<StaticItem>& items, float t_s_ms,
                                          uint32_t a_left = std::numeric_limits<uint32_t>::max())
{
    std::vector<const StaticItem*> never, rest;
    for (const StaticItem& it : items)
    {
        if (it.never_drawn)
        {
            never.push_back(&it);
        }
        else if (it.pending)
        {
            rest.push_back(&it);
        }
    }
    auto byDist = [](const StaticItem* a, const StaticItem* b) {
        const float da = sortDist(a->cam_dist), db = sortDist(b->cam_dist);
        return da != db ? da < db : a->key < b->key;
    };
    auto byAgeThenDist = [&byDist](const StaticItem* a, const StaticItem* b) {
        return a->age_frames != b->age_frames ? a->age_frames > b->age_frames : byDist(a, b);
    };
    std::sort(never.begin(), never.end(), byDist);
    std::sort(rest.begin(), rest.end(), byAgeThenDist);

    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const auto* list : { &never, &rest })
    {
        for (const StaticItem* it : *list)
        {
            const float cost = itemCost(it->last_cpu_ms);
            if (!out.empty() && (spent + cost > t_s_ms || avatars + it->avatar_draws > a_left))
            {
                return out; // over the cap or A_max: the order is kept, the rest waits
            }
            out.push_back(it->key);
            spent += cost;
            avatars += it->avatar_draws;
        }
    }
    return out;
}

static FramePicks oracleSelectFrame(const std::vector<StaticItem>& statics, const std::vector<DynamicItem>& dynamics, float cap_ms,
                              uint32_t a_max, bool progress_ok)
{
    FramePicks out;
    std::map<uint32_t, float> dyn_cost, st_cost; // the two lists have a key space each
    std::map<uint32_t, uint32_t> dyn_draws, st_draws;
    std::vector<DynamicItem> d = dynamics;
    for (DynamicItem& it : d)
    {
        it.key = packKey(it.pool, it.key);
        dyn_cost[it.key] = itemCost(it.last_cpu_ms);
        dyn_draws[it.key] = it.avatar ? std::min(std::max(it.avatar_draws, 1u), a_max) : 0u;
    }
    std::vector<StaticItem> st = statics;
    for (StaticItem& it : st)
    {
        it.key = packKey(it.pool, it.key);
        it.avatar_draws = std::min(it.avatar_draws, a_max);
        st_cost[it.key] = itemCost(it.last_cpu_ms);
        st_draws[it.key] = it.avatar_draws;
    }
    // a sum of float costs that equals the cap must fit it: the budgets carry a tolerance far below any cost (itemCost's floor is 0.05 ms)
    constexpr float kEps = 1e-4f;
    float left = cap_ms;
    std::set<uint32_t> own_keys; // the own avatar's faces: never dropped (every frame, whatever the cap says)
    for (const DynamicItem& it : d)
    {
        if (it.own)
        {
            own_keys.insert(it.key);
        }
    }
    auto dropLastOther = [&](std::vector<uint32_t>& list) { // drops the last pick that is not an own face; false when only own faces remain
        for (size_t i = list.size(); i > 0; --i)
        {
            if (!own_keys.count(list[i - 1]))
            {
                list.erase(list.begin() + (i - 1));
                return true;
            }
        }
        return false;
    };
    auto sumCost = [&](const std::vector<uint32_t>& list) {
        float ms = 0.f;
        for (uint32_t k : list)
        {
            ms += dyn_cost[k];
        }
        return ms;
    };
    if (left <= 0.f && !progress_ok)
    {
        out.dynamic = selectDynamic(d, 0.f, a_max, false); // only the own avatar's faces run past an exhausted cap
        out.predicted_ms = sumCost(out.dynamic);
        return out;
    }
    out.dynamic = selectDynamic(d, dynamicShareMs(!statics.empty(), left) + kEps, a_max, progress_ok);
    float dyn_ms = sumCost(out.dynamic);
    if (dyn_ms > left + kEps && !(progress_ok && out.dynamic.size() == 1))
    {
        // an over-cap item may only run alone, as the progress pick; the own avatar's faces stay (they are exempt)
        while (dyn_ms > left + kEps && dropLastOther(out.dynamic))
        {
            dyn_ms = sumCost(out.dynamic);
        }
    }
    left -= dyn_ms;
    uint32_t dyn_avatars = 0;
    for (uint32_t k : out.dynamic)
    {
        dyn_avatars += dyn_draws[k];
    }
    // the static schedule is never starved by the dynamic one: its first candidate (the order of selectStatic: never-drawn nearest,
    // then oldest) is guaranteed a place every frame when it fits the cap at all; the dynamic layer gives up its last faces for it
    if (!st.empty())
    {
        const std::vector<uint32_t> head = oracleSelectStatic(st, 0.f);
        const float head_ms = head.empty() ? 0.f : st_cost[head[0]];
        const uint32_t head_draws = head.empty() ? 0u : st_draws[head[0]];
        if (head_ms <= cap_ms + kEps)
        {
            while (head_ms > left + kEps && !out.dynamic.empty())
            {
                size_t i = out.dynamic.size();
                while (i > 0 && own_keys.count(out.dynamic[i - 1]))
                {
                    --i; // an own face is never given up
                }
                if (i == 0)
                {
                    break; // only own faces remain: the head waits for a frame with room
                }
                dyn_ms -= dyn_cost[out.dynamic[i - 1]];
                dyn_avatars -= dyn_draws[out.dynamic[i - 1]];
                out.dynamic.erase(out.dynamic.begin() + (i - 1));
                left = cap_ms - dyn_ms;
            }
        }
        else if (progress_ok && !head.empty())
        {
            // the head alone is above the cap: it takes the progress pick once it has waited (or has never been drawn)
            for (const StaticItem& it : st)
            {
                if (it.key == head[0] && (it.never_drawn || it.age_frames >= kDynamicDeadlineFrames))
                {
                    // the other dynamic picks give way; the own avatar's faces stay and the head runs beside them (a bounded overrun
                    // of that one item, so neither guarantee starves the other)
                    while (dropLastOther(out.dynamic))
                    {
                    }
                    dyn_ms = sumCost(out.dynamic);
                    dyn_avatars = 0;
                    for (uint32_t k : out.dynamic)
                    {
                        dyn_avatars += dyn_draws[k];
                    }
                    left = out.dynamic.empty() ? cap_ms : 0.f;
                    break;
                }
            }
        }
        // the head's avatar draws count against A_max too, but only where the head will be served (its CPU fits the cap, or it took the
        // progress pick above): the dynamic layer gives up its last avatar faces for it (head_draws <= a_max); faces without avatar draws
        // free nothing and stay
        if (head_ms <= cap_ms + kEps || out.dynamic.empty())
        {
            while (head_draws + dyn_avatars > a_max && dyn_avatars > 0)
            {
                size_t i = out.dynamic.size();
                while (i > 0 && (dyn_draws[out.dynamic[i - 1]] == 0 || own_keys.count(out.dynamic[i - 1])))
                {
                    --i;
                }
                if (i == 0)
                {
                    break; // only own faces hold the avatar draws: the head waits
                }
                const uint32_t k = out.dynamic[i - 1];
                dyn_ms -= dyn_cost[k];
                dyn_avatars -= dyn_draws[k];
                out.dynamic.erase(out.dynamic.begin() + (i - 1));
                left = cap_ms - dyn_ms;
            }
        }
    }
    // an overdue head that has no room beside the own avatar's faces runs beside them as the progress pick (one bounded overrun)
    bool head_beside_own = false;
    if (progress_ok && !st.empty() && !out.dynamic.empty())
    {
        const std::vector<uint32_t> head = oracleSelectStatic(st, 0.f);
        if (!head.empty() && st_cost[head[0]] > left + kEps)
        {
            for (const StaticItem& it : st)
            {
                head_beside_own = head_beside_own || (it.key == head[0] && (it.never_drawn || it.age_frames >= kDynamicDeadlineFrames));
            }
        }
    }
    out.fixed = oracleSelectStatic(st, std::max(left, 0.f) + kEps, a_max - dyn_avatars);
    float st_ms = 0.f;
    for (uint32_t k : out.fixed)
    {
        st_ms += st_cost[k];
    }
    if (st_ms > left + kEps && !(progress_ok && (out.dynamic.empty() || head_beside_own) && out.fixed.size() == 1))
    {
        // over what is left: only the single progress pick may be; any other overrun is cut back to what fits
        while (!out.fixed.empty() && st_ms > left + kEps)
        {
            st_ms -= st_cost[out.fixed.back()];
            out.fixed.pop_back();
        }
    }
    out.predicted_ms = dyn_ms + st_ms;
    return out;
}



std::vector<StaticItem> randomStatics(std::mt19937& rng, size_t n, int style)
{
    const float inf = std::numeric_limits<float>::infinity(), nan = std::numeric_limits<float>::quiet_NaN();
    const float costs[] = { 0.f, nan, inf, -inf, -1.f, 1e30f, 0.01f, 0.05f, 0.06f, 0.2f, 1.f, 5.f };
    std::vector<StaticItem> v(n);
    std::vector<uint32_t> keys(n);
    for (size_t i = 0; i < n; ++i)
    {
        keys[i] = uint32_t(i * 7 + 3);
    }
    std::shuffle(keys.begin(), keys.end(), rng);
    for (size_t i = 0; i < n; ++i)
    {
        StaticItem& it = v[i];
        it.key = keys[i];
        it.pool = style % 3 == 0 ? 0u : uint32_t(rng() % 3);
        it.never_drawn = rng() % (style % 2 ? 3 : 20) == 0;
        it.pending = rng() % 4 != 0;
        it.cam_dist = rng() % 9 == 0 ? nan : (rng() % 5 == 0 ? 0.f : float(rng() % 40)); // many ties
        it.last_cpu_ms = rng() % 3 == 0 ? costs[rng() % (sizeof(costs) / sizeof(costs[0]))] : float(rng() % 100) * 0.01f;
        it.age_frames = style % 4 == 0 ? 0u : uint32_t(rng() % 6); // ties on age
        it.avatar_draws = rng() % 3 == 0 ? uint32_t(rng() % 30) : 0u;
    }
    return v;
}

std::vector<DynamicItem> randomDynamics(std::mt19937& rng, size_t n)
{
    std::vector<DynamicItem> v(n);
    for (size_t i = 0; i < n; ++i)
    {
        DynamicItem& it = v[i];
        it.key = uint32_t(i * 5 + 1);
        it.pool = uint32_t(rng() % 3);
        it.avatar = rng() % 2 == 0;
        it.age_frames = uint32_t(rng() % 8);
        it.cam_dist = float(rng() % 30);
        it.last_cpu_ms = float(rng() % 60) * 0.01f;
        it.avatar_draws = uint32_t(rng() % 5);
        it.layer = rng() % 2 == 0;
        it.own = rng() % 12 == 0;
    }
    return v;
}

void testT7Differential()
{
    std::mt19937 rng(20261117);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    const float caps[] = { 0.f, 0.001f, 0.05f, 0.1f, 0.16f, 0.2f, 0.25f, 0.35f, 1.f, 1e-9f, -1.f, nan, inf, 1e9f };
    int mismatches = 0, iterations = 0, nonempty = 0;
    const size_t sizes[] = { 0, 1, 2, 3, 7, 40, 300, 2466, 5000 };
    for (int it = 0; it < 6000; ++it)
    {
        const size_t n = sizes[size_t(it) % (sizeof(sizes) / sizeof(sizes[0]))];
        const std::vector<StaticItem> statics = randomStatics(rng, n, it);
        // selectStatic: the caps, a_left of every kind
        const float t = caps[rng() % (sizeof(caps) / sizeof(caps[0]))];
        const uint32_t a_left = rng() % 3 == 0 ? std::numeric_limits<uint32_t>::max() : uint32_t(rng() % 60);
        const std::vector<uint32_t> want = oracleSelectStatic(statics, t, a_left);
        if (selectStatic(statics, t, a_left) != want)
        {
            ++mismatches;
        }
        ++iterations;
        nonempty += want.empty() ? 0 : 1;
        // selectFrame: the dynamic list, the cap left, own faces, progress both ways
        const std::vector<DynamicItem> dynamics = randomDynamics(rng, rng() % 4 == 0 ? 0 : rng() % 60);
        const float cap = caps[rng() % 8] + (rng() % 4 == 0 ? 0.f : float(rng() % 4) * 0.01f);
        for (int progress = 0; progress < 2; ++progress)
        {
            const uint32_t a_max = rng() % 2 ? 24u : uint32_t(rng() % 10);
            const FramePicks a = selectFrame(statics, dynamics, cap, a_max, progress != 0);
            const FramePicks b = oracleSelectFrame(statics, dynamics, cap, a_max, progress != 0);
            if (a.fixed != b.fixed || a.dynamic != b.dynamic || std::memcmp(&a.predicted_ms, &b.predicted_ms, sizeof(float)) != 0)
            {
                ++mismatches;
            }
            ++iterations;
        }
    }
    check(mismatches == 0, "T7 selectStatic and selectFrame equal the previous implementation (random sets to 5000 items)", mismatches, iterations);
    check(nonempty > 1000, "T7 the differential sets pick something", nonempty);
    std::printf("T7 differential: %d comparisons, %d mismatches\n", iterations, mismatches);
}

// P3: the index over a subset (the pending tiles left out, F1) gives the full set's hits minus the left-out keys, with the same causes.
void testP3IndexSubset()
{
    std::mt19937 rng(20261117);
    std::vector<Hit> full, part;
    int bad = 0, scenes = 0;
    for (int scene = 0; scene < 100; ++scene)
    {
        std::vector<TileVolume> all, sub;
        std::set<uint32_t> left_out;
        const size_t n = rng() % 400;
        for (size_t k = 0; k < n; ++k)
        {
            all.push_back({ uint32_t(100 + k * 3), randomPyramid(rng, 80.f, 0.3f, 2.9f, 1.f, 20.f) });
            if (rng() % 3 == 0)
            {
                left_out.insert(all.back().key);
            }
            else
            {
                sub.push_back(all.back());
            }
        }
        const std::vector<DirtyBox> boxes = randomBoxes(rng, rng() % 300, 90.f);
        ChangeIndex a, b;
        a.rebuild(all);
        b.rebuild(sub);
        for (int every = 0; every < 2; ++every)
        {
            a.apply(boxes, every != 0, full);
            b.apply(boxes, every != 0, part);
            std::vector<Hit> want;
            for (const Hit& h : full)
            {
                if (!left_out.count(h.key))
                {
                    want.push_back(h);
                }
            }
            ++scenes;
            bad += sameHits(part, want) ? 0 : 1;
        }
    }
    check(bad == 0, "F1 the index over the tiles that are not pending gives the full hits minus the pending keys, same causes", bad, scenes);
}

// F3: the per-lamp box filter keeps every box that touches a tile of the lamp, in order, whatever the bounds (NaN, inf, default pyramids).
void testBoxFilter()
{
    std::mt19937 rng(20261117);
    const float nan = std::numeric_limits<float>::quiet_NaN(), inf = std::numeric_limits<float>::infinity();
    int bad = 0, runs = 0;
    for (int scene = 0; scene < 400; ++scene)
    {
        std::vector<Pyramid> tiles;
        const size_t nt = 1 + rng() % 6;
        for (size_t k = 0; k < nt; ++k)
        {
            tiles.push_back(scene % 7 == 0 ? Pyramid() : randomPyramid(rng, 80.f, 0.3f, 2.9f, 1.f, 20.f)); // default pyramids too
        }
        if (scene % 5 == 1)
        {
            Aabb& b = tiles[rng() % nt].bounds;
            for (int k = 0; k < 3; ++k) { b.lo[k] = scene % 2 ? nan : -inf; b.hi[k] = inf; }
        }
        std::vector<Aabb> boxes;
        for (const DirtyBox& d : randomBoxes(rng, 40, 90.f))
        {
            boxes.push_back(d.box);
        }
        Aabb odd;
        for (int k = 0; k < 3; ++k) { odd.lo[k] = nan; odd.hi[k] = 1.f; }
        boxes.push_back(odd);
        for (int k = 0; k < 3; ++k) { odd.lo[k] = 0.f; odd.hi[k] = inf; }
        boxes.push_back(odd);
        BoxFilter f;
        for (const Pyramid& p : tiles)
        {
            f.add(p.bounds);
        }
        std::vector<Aabb> kept;
        f.select(boxes, kept);
        // per tile: the boxes that intersect it, as indices into `boxes` (brute force), against the same test over the kept list
        bool same = kept.size() <= boxes.size();
        for (const Pyramid& p : tiles)
        {
            std::vector<size_t> brute, filtered;
            for (size_t i = 0; i < boxes.size(); ++i)
            {
                if (intersects(p, boxes[i]))
                {
                    brute.push_back(i);
                }
            }
            // map each kept box back to its index in order
            size_t pos = 0;
            for (const Aabb& x : kept)
            {
                while (pos < boxes.size() && std::memcmp(&boxes[pos], &x, sizeof(Aabb)) != 0)
                {
                    ++pos;
                }
                if (pos < boxes.size() && intersects(p, x))
                {
                    filtered.push_back(pos);
                }
                ++pos;
            }
            same = same && brute == filtered;
        }
        ++runs;
        bad += same ? 0 : 1;
    }
    check(bad == 0, "F3 the lamp box filter keeps every box that touches a tile (NaN, inf and default pyramids included)", bad, runs);
}

void testP3()
{
    testP3Causes();
    testP3DirtyList();
    testP3ChangeIndex();
    testP3ChangeIndexDifferential();
    testP3MoverRules();
    testP3PinBook();
    testP3Verdict();
    testP3StaticScene();
}

// ---------------------------------------------------------------------------------------------
// Cells of a wide lamp, the tile's size from its lamp and the pressure valve (projector tile resolution, L2)
// ---------------------------------------------------------------------------------------------

bool pointInPyramid(const Pyramid& py, const float p[3])
{
    return pyramidDistance(py, p) == 0.f;
}

void testCellGrid()
{
    check(gridAxis(0.5) == 1 && gridAxis(1.0) == 1, "up to a 90 degree lamp: one cell");
    check(gridAxis(1.0 + 1e-9) == 1 && gridAxis(1.05) == 1, "just over 90 degrees: still one cell (ceil(tan - 0.05), up to tan 1.05)");
    check(gridAxis(1.0501) == 2, "past tan 1.05 (about 93 degrees): two cells");
    check(gridAxis(1.5) == 2 && gridAxis(std::tan(1.0)) == 2, "fov 2.0 (tan 1.557): 2 cells");
    check(gridAxis(std::tan(1.25)) == 3, "fov 2.5: 3 cells");
    check(gridAxis(std::tan(1.5)) == kMaxGrid && gridAxis(1e9) == kMaxGrid, "fov 3.0 and beyond: the cap");
    check(gridAxis(std::nan("")) == 1 && gridAxis(-1.0) == 1, "bad input: one cell");
    const CellGrid g = gridOf(std::tan(1.25), 1.0); // a wide prim: 3 across, 1 along
    check(g.gu == 3 && g.gv == 1 && g.cells() && g.count() == 3 && g.guard() == kCellGuard, "gridOf: per axis");
    check(!gridOf(1.0, 1.0).cells() && gridOf(1.0, 1.0).guard() == 0, "one tile has no guard");

    // the cells divide [-tan, tan] on each axis into equal parts, and the guard widens each side by its texels of the interior
    const double tx = std::tan(1.25), ty = std::tan(1.25) * 0.5;
    const CellGrid grid = gridOf(tx, ty);
    double span = 0.0;
    for (int i = 0; i < grid.gu; ++i)
    {
        const CellTan t = cellTanRect(grid, tx, ty, i, 0, 480, 0);
        span += t.x1 - t.x0;
        check(std::fabs((t.x1 - t.x0) - 2.0 * tx / grid.gu) < 1e-12, "a cell width");
        if (i > 0)
        {
            check(std::fabs(t.x0 - cellTanRect(grid, tx, ty, i - 1, 0, 480, 0).x1) < 1e-12, "neighbours share an edge");
        }
        const CellTan w = cellTanRect(grid, tx, ty, i, 0, 480, kCellGuard);
        check(std::fabs((t.x0 - w.x0) - (t.x1 - t.x0) * kCellGuard / 480.0) < 1e-12 && std::fabs((w.x1 - t.x1) - (t.x0 - w.x0)) < 1e-12,
              "the guard is kCellGuard interior texels on each side");
    }
    check(std::fabs(span - 2.0 * tx) < 1e-12, "the cells cover the image plane");
    check(cellInterior(512, kCellGuard) == 480 && cellInterior(64, kCellGuard) == 32 && cellInterior(64, 0) == 64, "the interior of a tile");
}

void testCellPyramids()
{
    const float origin[3] = { 1.f, 2.f, 3.f };
    const float fwd[3] = { 0.f, 0.f, -1.f }, right[3] = { 1.f, 0.f, 0.f }, up[3] = { 0.f, 1.f, 0.f };
    const double tx = std::tan(1.25), ty = std::tan(1.25);
    const float near_d = 0.3f, far_d = 6.f;
    // the symmetric pyramid is the general one with the plain tangents
    const Pyramid sym = makePyramid(origin, fwd, right, up, float(tx), float(ty), near_d, far_d);
    const Pyramid gen = makeFrustumPyramid(origin, fwd, right, up, float(-tx), float(tx), float(-ty), float(ty), near_d, far_d);
    for (int i = 0; i < 6; ++i)
    {
        check(sym.planes[i].d == gen.planes[i].d && sym.planes[i].n[0] == gen.planes[i].n[0] && sym.planes[i].n[1] == gen.planes[i].n[1] &&
                  sym.planes[i].n[2] == gen.planes[i].n[2],
              "makePyramid is the general pyramid with symmetric tangents");
    }
    const CellGrid grid = gridOf(tx, ty);
    std::vector<Pyramid> cells, wide;
    for (int j = 0; j < grid.gv; ++j)
    {
        for (int i = 0; i < grid.gu; ++i)
        {
            const CellTan t = cellTanRect(grid, tx, ty, i, j, 480, 0);
            cells.push_back(makeFrustumPyramid(origin, fwd, right, up, float(t.x0), float(t.x1), float(t.y0), float(t.y1), near_d, far_d));
            const CellTan w = cellTanRect(grid, tx, ty, i, j, 480, kCellGuard);
            wide.push_back(makeFrustumPyramid(origin, fwd, right, up, float(w.x0), float(w.x1), float(w.y0), float(w.y1), near_d, far_d));
        }
    }
    // the interiors partition the lamp: a point inside the lamp is in at least one cell (exactly one away from a seam), a point outside
    // is in none; the widened pyramids contain their interiors
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> u(-1.f, 1.f), d(0.2f, 6.5f);
    int seams = 0;
    for (int n = 0; n < 20000; ++n)
    {
        const float z = d(rng);
        const float p[3] = { origin[0] + u(rng) * float(tx) * 1.2f * z, origin[1] + u(rng) * float(ty) * 1.2f * z, origin[2] - z };
        const bool in_lamp = pointInPyramid(sym, p);
        int count = 0, wide_count = 0;
        for (size_t c = 0; c < cells.size(); ++c)
        {
            const bool in = pointInPyramid(cells[c], p);
            count += in;
            wide_count += pointInPyramid(wide[c], p);
            check(!in || pointInPyramid(wide[c], p), "the widened pyramid contains its interior");
        }
        if (in_lamp)
        {
            check(count >= 1, "a point in the lamp is in a cell");
            seams += count > 1;
        }
        else
        {
            check(count == 0, "a point outside the lamp is in no cell");
        }
        check(wide_count >= count, "the guard only adds");
    }
    check(seams < 200, "points on two cells are the seams only", seams);
    // a small box that lies in the neighbour guard, not in the cell itself, touches both widened pyramids (both tiles are dirtied by it)
    const CellTan t0 = cellTanRect(grid, tx, ty, 0, 0, 480, 0);
    const float zb = 3.f, xb = origin[0] + float(t0.x1) * zb + 0.002f * zb; // just past cell 0 right edge, inside its guard
    Aabb box;
    box.lo[0] = xb - 0.001f; box.hi[0] = xb + 0.001f;
    box.lo[1] = origin[1] - float(ty) * zb * 0.5f - 0.001f; box.hi[1] = origin[1] - float(ty) * zb * 0.5f + 0.001f; // inside the lower row
    box.lo[2] = origin[2] - zb - 0.001f; box.hi[2] = origin[2] - zb + 0.001f;
    check(intersects(wide[0], box) && intersects(cells[1], box) && intersects(wide[1], box), "an object in a neighbour guard dirties both cells");
    check(!intersects(cells[0], box), "... and is not in the first cell itself");
    // the distance: 0 inside, a lower bound outside (exact in front of a face)
    const float inside[3] = { origin[0], origin[1], origin[2] - 2.f };
    check(pyramidDistance(sym, inside) == 0.f, "inside: distance 0");
    const float behind[3] = { origin[0], origin[1], origin[2] + 1.5f }; // 1.5 m behind the apex, level with it
    check(pyramidDistance(sym, behind) > 0.5f, "behind the apex: outside", pyramidDistance(sym, behind));
    const float far_pt[3] = { origin[0], origin[1], origin[2] - far_d - 4.f };
    check(std::fabs(pyramidDistance(sym, far_pt) - 4.f) < 1e-3f, "beyond the far plane: its distance", pyramidDistance(sym, far_pt));
}

// L2: a tile's size from its lamp alone, the distance bands and the pressure valve
void testCellSizing()
{
    // the lamp's size: 4 mm texels at d_ref = min(1.5 r, 4 m) over the tile's wider axis, plus the guard of a cell; no camera input
    check(tileBaseSize(2.0, 2.7, 0) == 2048 && tileBaseSize(2.0, 1.0, 0) == 1024, "the size of a 90 degree tile (baseSize's rule)");
    check(baseSize(1.0f, kPi / 2) == tileBaseSize(2.0 * std::tan(0.5 * double(kPi / 2)), 1.0, 0) && baseSize(8.0f, 2.0f) == tileBaseSize(2.0 * std::tan(1.0), 8.0, 0),
          "baseSize is the one-tile square case");
    const double tan_w = 2.0 * std::tan(1.25) / 3.0; // a 3 x 3 cell of a 2.5 rad lamp
    check(tileBaseSize(tan_w, 8.0, kCellGuard) == 2048, "a wide lamp's cell at 4 m reference depth: 2048", tileBaseSize(tan_w, 8.0, kCellGuard));
    // the guard term: a cell's interior is its size less 2 guards, so the guard is added before the rounding
    check(tileBaseSize(0.16, 4.0, kCellGuard) == 256 && tileBaseSize(0.16, 4.0, 0) == 128, "the guard enters the size (interior 160 + 32 rounds to 256, without it 128)",
          tileBaseSize(0.16, 4.0, kCellGuard), tileBaseSize(0.16, 4.0, 0));
    check(tileBaseSize(std::nan(""), 4.0, 0) == kMinBase && tileBaseSize(1.0, std::nan(""), 0) == kMinBase && tileBaseSize(-1.0, 4.0, 0) == kMinBase &&
              tileBaseSize(1e30, 4.0, 0) == kMaxBase && tileBaseSize(1.0, 1e30, kCellGuard) <= kMaxBase,
          "bad input stays in [kMinBase, kMaxBase]");

    // the bands: octaves of the camera position's distance, from 8 m; a band moves only half an octave past its bound
    check(bandOf(0.f) == 0 && bandOf(7.99f) == 0 && bandOf(8.f) == 1 && bandOf(15.99f) == 1 && bandOf(16.f) == 2 && bandOf(63.f) == 3 && bandOf(64.f) == kMaxBand &&
              bandOf(1e9f) == kMaxBand,
          "the bands");
    check(bandOf(std::numeric_limits<float>::quiet_NaN()) == kMaxBand && bandOf(std::numeric_limits<float>::infinity()) == kMaxBand && bandOf(-3.f) == 0,
          "a bad distance: NaN and infinity are the farthest band, a negative one the nearest");
    check(nextBand(1, 22.f) == 1 && nextBand(1, 23.f) == 2 && nextBand(2, 11.5f) == 2 && nextBand(2, 11.f) == 1, "a band moves half an octave past its bound");
    check(nextBand(kMaxBand, 1.f) == 0 && nextBand(0, 200.f) == kMaxBand, "a jump moves straight to the band");
    {
        // the fast path (no logarithm inside the widened bounds) is the same rule as the logarithms alone
        auto reference = [](int band, float d) {
            const int up = bandOf(d / kBandHysteresis), down = bandOf(d * kBandHysteresis);
            const int b = std::min(std::max(band, 0), kMaxBand);
            return up > b ? up : (down < b ? down : b);
        };
        std::mt19937 rng(5);
        std::uniform_real_distribution<float> dist(0.f, 200.f);
        int differ = 0;
        for (int n = 0; n < 200000; ++n)
        {
            const int b = int(rng() % (kMaxBand + 3)) - 1;
            const float d = n % 97 == 0 ? std::numeric_limits<float>::quiet_NaN() : (n % 89 == 0 ? std::numeric_limits<float>::infinity() : dist(rng));
            differ += nextBand(b, d) != reference(b, d);
        }
        for (int b = 0; b <= kMaxBand; ++b) // the bounds themselves, and a hair either side
        {
            for (const float edge : { bandLow(b) / kBandHysteresis, bandHigh(b) * kBandHysteresis })
            {
                for (const float d : { std::nextafter(edge, 0.f), edge, std::nextafter(edge, 1e9f) })
                {
                    differ += std::isfinite(d) && nextBand(b, d) != reference(b, d);
                }
            }
        }
        check(differ == 0, "nextBand's fast path equals the rule", differ);
    }
    {
        // a camera that stands at a bound and sways by +-25 % never changes the band
        int b = nextBand(kMaxBand, 16.f), changes = 0;
        for (int n = 0; n < 600; ++n)
        {
            const int nb = nextBand(b, 16.f * (1.f + 0.25f * std::sin(float(n) * 0.3f)));
            changes += nb != b;
            b = nb;
        }
        check(changes == 0, "swaying +-25 % at a bound changes no band", changes);
    }

    // the shift: farthest first, one octave per band per step of pressure
    check(valveShift(0, kMaxBand) == 0 && valveShift(1, kMaxBand) == 1 && valveShift(1, kMaxBand - 1) == 0 && valveShift(2, kMaxBand - 1) == 1, "the farthest band shrinks first");
    check(valveShift(kMaxPressure, 0) == kMaxLevel && valveShift(99, 3) == kMaxLevel && valveShift(-4, kMaxBand) == 0, "the shift stays in [0, kMaxLevel]");
    check(valveShift(kMaxBand + 1, 0) == 1 && valveShift(kMaxBand + 1, 1) == 2, "a tile one band farther is one octave smaller");

    // while the atlas has room the valve does nothing: the sizes are the lamps', whatever the camera does
    {
        std::vector<ValveTile> tiles;
        for (int i = 0; i < 6; ++i)
        {
            tiles.push_back({ 1024u, i, 0u }); // 6 x 1024^2 on 8192: 0.09 of the atlas
        }
        LevelController v;
        int changes = 0;
        std::mt19937 rng(11);
        for (int f = 0; f < 3600; ++f) // a minute at 60 fps, the camera walking: every tile's band jumps around
        {
            for (ValveTile& t : tiles)
            {
                t.band = int(rng() % (kMaxBand + 1));
            }
            changes += v.update(tiles, 8192, 1.f / 60.f);
            for (const ValveTile& t : tiles)
            {
                changes += valveSize(t, v.level()) != t.base;
            }
        }
        check(changes == 0 && v.level() == 0, "with room in the atlas no size ever changes, whatever the bands", changes);
    }
    // over the budget the far tiles shrink first and the near ones keep their size
    {
        std::vector<ValveTile> tiles;
        for (int i = 0; i < 12; ++i)
        {
            tiles.push_back({ 2048u, i < 4 ? 0 : (i < 8 ? 2 : kMaxBand), 0u }); // 12 x 2048^2 on 8192: demand 0.75
        }
        LevelController v;
        check(v.update(tiles, 8192, 1.f / 60.f) && v.level() > 0, "over the budget the pressure rises at once", v.level());
        check(demand(tiles, v.level(), 8192) <= kShrinkAbove, "and stops at or below the budget", demand(tiles, v.level(), 8192));
        check(valveSize(tiles[0], v.level()) == 2048 && valveSize(tiles[11], v.level()) < 2048, "the near tiles keep their size, the far ones shrink",
              valveSize(tiles[0], v.level()), valveSize(tiles[11], v.level()));
        check(valveSize(tiles[11], v.level()) <= valveSize(tiles[5], v.level()) && valveSize(tiles[5], v.level()) <= valveSize(tiles[0], v.level()),
              "never a far tile larger than a near one");
        // the near and middle tiles leave the light set: the pressure falls back after the hold (8 tiles would stay at 0.5 > kGrowAtMost)
        tiles.erase(tiles.begin(), tiles.begin() + 8); // the four far tiles: 4 x 2048^2 = 0.25 at P 0
        int steps = 0;
        const int was = v.level();
        for (int f = 0; f < 60 * 60 && v.level() > 0; ++f)
        {
            steps += v.update(tiles, 8192, 1.f / 60.f);
        }
        check(was > 0 && steps > 0 && v.level() == 0, "the pressure falls once the demand allows it", was, v.level());
        check(demand(tiles, v.level(), 8192) <= kShrinkAbove, "at a pressure that fits", demand(tiles, v.level(), 8192));
    }
    // the ceiling: no pressure past the point where every tile is at its floor (a step that changes nothing is never held)
    {
        std::vector<ValveTile> tiles(5000, ValveTile{ 2048u, kMaxBand, 0u }); // 5000 floor tiles do not fit a 4096 atlas at all (1.22)
        LevelController v;
        v.update(tiles, 4096, 1.f / 60.f);
        check(v.level() == v.ceiling() && v.ceiling() == kMaxLevel, "all far tiles: the ceiling is the old floor level", v.level(), v.ceiling());
        std::vector<ValveTile> near(5000, ValveTile{ 2048u, 0, 0u });
        LevelController w;
        w.update(near, 4096, 1.f / 60.f);
        check(w.level() == kMaxPressure && w.ceiling() == kMaxPressure, "all near tiles: the ceiling is the full pressure", w.level());
        // the near tiles move far: the pressure that changes nothing is dropped at once (no 3 s holds through the empty steps)
        w.update(tiles, 4096, 1.f / 60.f);
        check(w.level() == kMaxLevel, "a pressure past the new ceiling is dropped at once", w.level());
        check(pressureCeiling(std::vector<ValveTile>(3, ValveTile{ 128u, kMaxBand, 0u })) == 1 && pressureCeiling(std::vector<ValveTile>(3, ValveTile{ 64u, 0, 0u })) == 0,
              "small tiles reach their floor sooner; 64 never shrinks");
        check(pressureCeiling(std::vector<ValveTile>(3, ValveTile{ 2048u, 0, 512u })) == 0, "fixed tiles are not the valve's");
    }
    // a step down skips the steps that change nothing (a band with no tile)
    {
        std::vector<ValveTile> tiles(40, ValveTile{ 2048u, 0, 0u }); // only near tiles: the steps up to kMaxBand change nothing
        LevelController v;
        v.update(tiles, 8192, 1.f / 60.f);
        const int high = v.level();
        check(high > kMaxBand, "near tiles only: the pressure passes the empty steps at once", high);
        tiles.resize(4);
        float t = 0.f;
        while (v.level() > 0 && t < 60.f)
        {
            v.update(tiles, 8192, 1.f / 60.f);
            t += 1.f / 60.f;
        }
        check(v.level() == 0, "the pressure falls back to 0", v.level());
        check(t < kGrowHoldSeconds * float(high - kMaxBand) + 1.f, "one hold per step that changes a size, none for the empty ones", t, high);
    }
    // fixed tiles (a lamp switched off, a lamp's retiring cells) count at their size and are never resized
    {
        std::vector<ValveTile> tiles(10, ValveTile{ 2048u, kMaxBand, 0u });
        tiles[0].fixed = 2048u;
        LevelController v;
        v.update(tiles, 8192, 1.f / 60.f);
        check(valveSize(tiles[0], v.level()) == 2048u && valveSize(tiles[1], v.level()) < 2048u, "a fixed tile keeps its size, the others shrink for it");
    }
    // the old global level is the valve with every tile in the farthest band
    {
        const std::vector<uint32_t> bases = { 2048u, 1024u, 2048u, 512u, 2048u };
        std::vector<ValveTile> far;
        for (uint32_t b : bases)
        {
            far.push_back({ b, kMaxBand, 0u });
        }
        for (int level = 0; level <= kMaxLevel; ++level)
        {
            check(demand(far, level, 4096) == demand(bases, level, 4096), "all far: the valve's demand is the level's", level);
        }
    }

    // draw-then-swap needs the block a tile gets once its own rect is freed
    {
        BuddyAtlas b(4096);
        Rect r1, r2, r3, r4;
        check(b.alloc(1024, r1) && b.alloc(1024, r2) && b.alloc(1024, r3) && b.alloc(1024, r4), "four 1024 tiles in a quarter");
        check(b.largestFree() == 2048 && b.largestAfterFree(r1) == 2048, "three quarters free: a 2048 block, whatever is freed");
        Rect big;
        check(b.alloc(2048, big) && b.alloc(2048, big) && b.alloc(2048, big), "the rest filled with three 2048 tiles");
        check(b.largestFree() == 0 && b.largestAfterFree(r1) == 1024, "a full atlas: a tile can only get its own block back");
        b.free(r2);
        b.free(r3);
        b.free(r4);
        check(b.largestFree() == 1024 && b.largestAfterFree(r1) == 2048, "its three buddies free: freeing it merges a 2048 block");
        check(b.largestAfterFree(Rect{ 0, 0, 100 }) == b.largestFree(), "not a block size: the free blocks only");
    }
}

// The own avatar's every-frame rule: one cell per box (the one holding its centre), however many the box's width crosses
void testCellOwn()
{
    const float origin[3] = { 0.f, 0.f, 0.f }, fwd[3] = { 1.f, 0.f, 0.f }, right[3] = { 0.f, 1.f, 0.f }, up[3] = { 0.f, 0.f, 1.f };
    const double t = std::tan(1.25);
    const CellGrid grid = gridOf(t, t);
    std::vector<Pyramid> cores;
    for (int j = 0; j < grid.gv; ++j)
    {
        for (int i = 0; i < grid.gu; ++i)
        {
            const CellTan c = cellTanRect(grid, t, t, i, j, 480, 0);
            cores.push_back(makeFrustumPyramid(origin, fwd, right, up, float(c.x0), float(c.x1), float(c.y0), float(c.y1), 0.2f, 12.f));
        }
    }
    // a 1.8 m wide, 2 m tall box close to the apex, wider than several cells there, centred in one cell
    Aabb box;
    box.lo[0] = 0.25f; box.hi[0] = 0.6f;
    box.lo[1] = -0.9f; box.hi[1] = 0.9f;
    box.lo[2] = -1.0f; box.hi[2] = 1.0f;
    int overlapping = 0, holding = 0;
    for (const Pyramid& py : cores)
    {
        overlapping += intersects(py, box) ? 1 : 0;
        holding += cellHoldsOwn(py, box) ? 1 : 0;
    }
    check(overlapping > 4, "near the apex a box crosses more than 4 cells", overlapping);
    check(holding == 1, "exactly one cell holds the own avatar's centre", holding);
}

// S2: script-driven lamps. A lamp that goes off keeps its tiles for kOffHoldSeconds: LLProjectorShadows::holdOff calls holdOffFrame for
// each of its tiles every frame the lamp is seen off; the frame it is seen on clears the tile's `off`. What this checks is that rule
// over blink patterns; the light set's bookkeeping around it (the tiles marked seen, skipped by the scheduler, counted as fixed by the
// valve) is the GL class's and is not reached here.
void testScriptLamps()
{
    check(offHeld(10.0, 10.0) && offHeld(10.0, 10.0 + kOffHoldSeconds) && !offHeld(10.0, 10.0 + kOffHoldSeconds + 0.01), "S2 the hold lasts kOffHoldSeconds");
    check(!offHeld(std::nan(""), 1.0) && !offHeld(1.0, std::nan("")), "S2 a bad time ends the hold");
    {
        bool off = false;
        double since = 0.0;
        check(holdOffFrame(off, since, 7.0, true) && off && since == 7.0, "S2 the first frame seen off starts the hold");
        check(holdOffFrame(off, since, 9.0, true) && since == 7.0, "S2 later frames keep its start");
        check(!holdOffFrame(off, since, 9.0, false), "S2 a held tile out of range (by the reach it had when on) goes");
        check(!holdOffFrame(off, since, 7.0 + kOffHoldSeconds + 0.01, true), "S2 after the hold the tile goes");
    }
    // blinkers (0.03 s strobe, 0.1 s flasher, 1 s blinker, a broken tube's 2 s sleep) keep their tiles through every off phase; a day
    // and night switch gives them back once
    for (const double period : { 0.03, 0.1, 0.5, 1.0, 2.0, 60.0 })
    {
        bool off = false, have_tiles = true;
        double since = 0.0;
        int drops = 0;
        for (int f = 0; f < 60 * 120; ++f) // two minutes at 60 fps, on for one period, off for the next (60 s: on one minute, off the next)
        {
            const double now = f / 60.0;
            const bool lamp_on = (int(now / period) % 2) == 0;
            if (!lamp_on)
            {
                if (have_tiles && !holdOffFrame(off, since, now, true))
                {
                    have_tiles = false; // dropped like a lamp out of range
                    ++drops;
                }
            }
            else
            {
                off = false;       // the light set sees it on: its held tiles serve at once
                have_tiles = true; // (dropped ones are made again, with a fresh hold)
            }
        }
        if (period <= 2.0)
        {
            check(drops == 0, "S2 a blinking lamp keeps its tiles through every off phase", period, drops);
        }
        else
        {
            check(drops == 1, "S2 a lamp off for minutes gives its tiles back once", period, drops);
        }
    }
}

// S2, texture-entry rebuilds. LLVOVolume::setTEColor (dirtyMesh) and the material setters ask for REBUILD_ALL, which counts as a shape
// change; the feed marks such a request (textureEntryMarkAfterRequest), keeps or ends the mark with the next requests
// (textureEntryMarkAfterRebuild) and lets the rebuild's own pushes follow it (ownRebuildPushes). In an active linkset the prim's octree move
// marks its bridge moved, which is one of those pushes, and the bridge's move after the rebuilds follows the quiet record
// (quietBridgeAfterMove). This replays those rules in the order the feed's hooks run them, over what scripts and the viewer do to a prim:
// the setters in idle, LLDrawable::updateTexture's REBUILD_MATERIAL later in the same idle (LLPipeline::updateMove), then display's
// LLPipeline::updateGeom: the prim's rebuild (its octree move when it carries a shape flag, with the bridge's markMoved in an active
// linkset; its completion when it waited), then the bridge pass. The wiring of the hooks to these rules (TextureEntryRebuild, OwnRebuild,
// BridgeMoves, noteRebuild, noteTextured, noteMoved, noteDrawable) is the viewer's and not reached here.
namespace
{
    // where a texture-entry setter's markTextured runs: none (a tint's dirtyMesh), before its scope opens (LLViewerObject::setTEMaterialParams'
    // normal and specular maps; a cached material's synchronous callback in setTEMaterialID) or inside it
    enum class Textured { None, Before, Inside };

    struct FeedModel
    {
        bool marked = false;
        uint32_t queued = 0;                  // the queued rebuild's flags (0: none queued)
        uint32_t stored = kSilhouetteKeySeed; // the key the feed last saw
        bool known = true;
        bool bridged = false;                 // an active linkset: the prim lives in its bridge's octree
        bool root = false;                    // the prim is the linkset's root: the bridge's object is the prim's
        bool old_wiring = false;              // before the follow-up review's B1: the bridge's markMoved was counted as any push
        bool recorded = false;                // the bridge is on the quiet record
        bool pending = false;                 // the bridge waits on the moved list
        int pushes = 0;

        void textured(uint32_t now) // markTextured: a key change pushes once and stores the key
        {
            pushes += texturedDirties(stored, now, known) ? 1 : 0;
            stored = now;
            known = true;
        }
        void request(uint32_t flags) // markRebuild from anything but a texture-entry setter
        {
            marked = textureEntryMarkAfterRebuild(marked, flags);
            queued |= flags;
            if (rebuildDirties(flags))
            {
                marked = false; // noteDrawable outside the own rebuild
                ++pushes;
            }
        }
        void setter(uint32_t now, Textured where = Textured::Inside) // a texture-entry setter, its REBUILD_ALL in its scope
        {
            if (where == Textured::Before)
            {
                textured(now);
            }
            const bool queued_shape = (queued & kRebuildShape) != 0; // read when the scope opens
            if (where == Textured::Inside)
            {
                textured(now);
            }
            marked = textureEntryMarkAfterRequest(marked, queued_shape);
            queued |= kRebuildAll;
        }
        void moved() // markMoved, a put or a remove of the prim: a push outside the own rebuild
        {
            marked = false;
            ++pushes;
        }
        void bridgeMoved() // a markMoved of the bridge that is not the prim's own rebuild: the linkset's own move, another prim's
        {
            ++pushes; // the box it leaves
            if (root)
            {
                marked = false; // a push of the root's object outside the own rebuild
            }
            recorded = quietBridgeAfterMove(recorded, pending, false);
            pending = true;
        }
        bool ownPush(uint32_t now) // a push of the prim's own rebuild
        {
            const bool push = ownRebuildPushes(marked, stored, now, known);
            stored = now;
            known = true;
            pushes += push ? 1 : 0;
            return push;
        }
        void rebuildOwn(uint32_t now) // updateGeom: the prim's own rebuild
        {
            if (queued == 0)
            {
                return;
            }
            if (queued & kRebuildShape)
            {
                ownPush(now); // genBBoxes' octree move (in the bridge's octree when active)
                if (bridged && old_wiring)
                {
                    bridgeMoved(); // the move's markMoved of the bridge, counted
                }
                else if (bridged)
                {
                    const bool push = ownPush(now); // the move's markMoved of the bridge: the prim's rule
                    recorded = quietBridgeAfterMove(recorded, pending, !push);
                    pending = true;
                }
            }
            if (queued & (kRebuildVolume | kRebuildRigged))
            {
                ownPush(now); // the completion
            }
            marked = false; // the rebuild is done
            queued = 0;
        }
        void bridgePass() // updateGeom's bridge pass: a bridge off the quiet record pushes where it ended up
        {
            pushes += (pending && !recorded) ? 1 : 0;
            pending = recorded = false;
        }
        int rebuild(uint32_t now) // updateTexture's REBUILD_MATERIAL (still in idle), then display's updateGeom
        {
            const int before = pushes;
            if (queued != 0)
            {
                request(kRebuildTcoord | kRebuildColor);
            }
            rebuildOwn(now);
            bridgePass();
            return pushes - before;
        }
    };

    // a blinker toggled every 0.1 s for a minute: its pushes (tint: dirtyMesh alone; emissive: a glTF override; material: a legacy material
    // whose parameters arrive in the same frame)
    int blinkerPushes(bool bridged, bool root, bool old_wiring, const char* kind)
    {
        const uint32_t lace = 0x1234u;
        FeedModel m;
        m.stored = lace;
        m.bridged = bridged;
        m.root = root;
        m.old_wiring = old_wiring;
        for (int t = 0; t < 600; ++t)
        {
            if (kind[0] == 't')
            {
                m.setter(lace, Textured::None);
            }
            else if (kind[0] == 'e')
            {
                m.setter(lace);
            }
            else
            {
                m.setter(lace, Textured::Before);
                m.setter(lace);
            }
            m.rebuild(lace);
        }
        return m.pushes;
    }
}

void testTextureEntryRebuilds()
{
    check((kRebuildShape & kRebuildAll) == (kRebuildVolume | kRebuildPosition) && (kRebuildShape & kRebuildGeometry) == kRebuildPosition &&
              (kRebuildShape & (kRebuildTcoord | kRebuildColor)) == 0 && (kRebuildShape & kRebuildRigged) != 0,
          "S2 shape flags: REBUILD_ALL carries volume and position, REBUILD_GEOMETRY the position, REBUILD_MATERIAL none; rigging is one");
    const uint32_t lace = 0x1234u, opened = 0x5678u; // two silhouette keys
    // a tint, emissive or material blinker, every 0.1 s for a minute: no push at all, in a static linkset, as a child or as the root of an
    // active one (each was three a toggle before S2: the request, the octree move, the completion)
    for (const char* kind : { "tint", "emissive", "material" })
    {
        const int still = blinkerPushes(false, false, false, kind);
        const int child = blinkerPushes(true, false, false, kind);
        const int root = blinkerPushes(true, true, false, kind);
        check(still == 0 && child == 0 && root == 0, "S2 a blinker pushes nothing, static or in an active linkset (child, root)", double(still),
              double(child + root));
        // with the bridge's markMoved counted as any push (before the follow-up review's B1) the active linkset pushed twice a toggle (that
        // markMoved, the bridge pass) and three times for a root lamp (that markMoved ended the root's own mark, so its completion pushed)
        check(blinkerPushes(true, false, true, kind) == 1200 && blinkerPushes(true, true, true, kind) == 1800,
              "S2 B1: the old wiring pushes an active child blinker twice a toggle, a root three times", double(blinkerPushes(true, false, true, kind)),
              double(blinkerPushes(true, true, true, kind)));
    }
    // L3: a tint blinker on a blended face changes the face's RGB, an input of the key (the light-through layer's colour): its toggles push,
    // once each (the rebuild's first own push finds the new key and stores it), static, as a child or as the root of an active linkset
    for (const bool bridged : { false, true })
    {
        for (const bool root : { false, true })
        {
            if (root && !bridged)
            {
                continue;
            }
            const uint32_t red = 0x1234u, blue = 0x4321u;
            FeedModel m;
            m.stored = red;
            m.bridged = bridged;
            m.root = root;
            for (int t = 0; t < 600; ++t)
            {
                const uint32_t now = (t % 2) == 0 ? blue : red;
                m.setter(now, Textured::None);
                m.rebuild(now);
            }
            check(m.pushes == 600, "L3 a tint blinker on a blended face pushes once a toggle (static, active child, active root)", double(m.pushes),
                  double(bridged) + double(root));
        }
    }
    // a silhouette change through a setter pushes once, at its markTextured, wherever that runs; the rebuild's own pushes (and the bridge's)
    // find the key stored
    for (const Textured where : { Textured::Before, Textured::Inside })
    {
        for (const bool bridged : { false, true })
        {
            FeedModel m;
            m.stored = lace;
            m.bridged = bridged;
            m.setter(opened, where);
            check(m.pushes == 1, "S2 a silhouette change pushes once at the setter, its markTextured before or inside the scope", double(where == Textured::Before),
                  double(bridged));
            check(m.rebuild(opened) == 0, "S2 ... and its rebuild pushes nothing more (the tile drawn for the push comes after it)",
                  double(where == Textured::Before), double(bridged));
        }
    }
    for (const bool bridged : { false, true })
    {
        // what changes a shape still pushes around a tint: in an active linkset, the bridge's markMoved and the bridge pass as well
        const int extra = bridged ? 2 : 0;
        FeedModel m;
        m.stored = lace;
        m.bridged = bridged;
        m.request(kRebuildVolume); // a new LOD
        m.setter(lace, Textured::None);
        check(m.rebuild(lace) == 2 + extra, "S2 a tint after a new LOD: the LOD's rebuild still pushes", double(bridged));
        m.setter(lace, Textured::None);
        m.request(kRebuildVolume);
        check(m.rebuild(lace) == 2 + extra, "S2 a new LOD after a tint: its rebuild pushes", double(bridged));
        m.setter(lace, Textured::None);
        m.request(kRebuildPosition); // a scale change
        check(m.rebuild(lace) == 2 + extra, "S2 a scale after a tint: its rebuild pushes (the octree move and the completion)", double(bridged));
        m.setter(lace, Textured::None);
        m.request(kRebuildGeometry); // markForUpdate (selection, a new volume)
        check(m.rebuild(lace) == 2 + extra, "S2 markForUpdate after a tint: its rebuild pushes", double(bridged));
        m.setter(lace, Textured::None);
        m.moved();
        check(m.rebuild(lace) == 2 + extra, "S2 a move after a tint: the rebuild pushes where the prim ended up", double(bridged));
    }
    for (const bool root : { false, true })
    {
        // the linkset's own move (LLDrawable::updateXform's markMoved of the bridge, in idle before the rebuilds) with a tint: the bridge
        // pass still pushes where the linkset ended up. That move is a push of the root's object outside its own rebuild, so a root's tint
        // loses its mark and its rebuild pushes as well (conservative: the linkset's two boxes already cover it)
        FeedModel m;
        m.stored = lace;
        m.bridged = true;
        m.root = root;
        m.setter(lace, Textured::None);
        m.bridgeMoved();
        check(m.pushes == 1 && m.rebuild(lace) == (root ? 4 : 1),
              "S2 the linkset moved before a tint: the box it leaves and the one it ends in push (and a root's rebuild)", double(root),
              double(m.pushes));
        // another prim's counted move of the same bridge after the quiet rebuild, in the same updateGeom: the bridge pass pushes
        FeedModel n;
        n.stored = lace;
        n.bridged = true;
        n.root = root;
        n.setter(lace, Textured::None);
        n.request(kRebuildTcoord | kRebuildColor);
        n.rebuildOwn(lace);
        check(n.recorded && n.pushes == 0, "S2 the quiet rebuild puts the bridge on the record", double(root));
        n.bridgeMoved();
        n.bridgePass();
        check(n.pushes == 2 && !n.recorded && !n.pending, "S2 another prim's move after it: the bridge leaves the record and its pass pushes",
              double(root), double(n.pushes));
    }
    // the record over two prims of one linkset in one frame: two quiet rebuilds keep it, a counted move before or after either ends it
    check(quietBridgeAfterMove(quietBridgeAfterMove(false, false, true), true, true), "S2 two quiet rebuilds in one linkset keep the record");
    check(!quietBridgeAfterMove(quietBridgeAfterMove(false, false, false), true, true), "S2 a counted move first: a quiet one does not record");
    check(!quietBridgeAfterMove(true, true, false), "S2 a counted move after a quiet one ends the record");
    check(!quietBridgeAfterMove(false, true, true), "S2 a bridge already waiting for a counted move stays off the record");
    for (const bool bridged : { false, true })
    {
        FeedModel m;
        m.stored = lace;
        m.bridged = bridged;
        m.known = false; // the prim predates tracking
        m.setter(lace, Textured::None);
        check(m.rebuild(lace) == 1, "S2 a prim the feed has not seen pushes once, mark or not", double(bridged));
        m.setter(lace, Textured::None);
        check(m.rebuild(lace) == 0, "S2 ... and then follows the mark", double(bridged));
    }
}

// L2 placement. A GL-free replay of LLProjectorShadows::layout() (the valve, each tile's want, the arrivals placed the largest first, a
// compaction when the free texels hold them, arrivalShortfall, the growth hold) and of renderTile()'s resize (the rect freed, the new
// size allocated), built from the llprojectorpool rules those call, in layout()'s order. The renders: up to `per_frame` a frame, first
// fills first, then the resizes that are due, the longest-drawn first (the static schedule's order); a growth whose block an earlier
// resize of the frame took is not drawn (renderTile). Only the rects are modelled, not the GL copies of a compaction.
struct ReplayTile
{
    uint32_t key = 0, base = 0;
    int band = 0;
    bool placed = false, drawn = false;
    Rect rect;
    uint32_t want = 0;
    uint32_t drawn_frame = 0;
};

struct ReplayPool
{
    uint32_t atlas;
    BuddyAtlas buddy;
    LevelController valve;
    GrowthHold hold;
    std::map<uint32_t, ReplayTile> tiles;
    uint32_t frame = 1;
    int shortfall_frames = 0, compactions = 0, resizes = 0, renders = 0;
    explicit ReplayPool(uint32_t a) : atlas(a), buddy(a) {}

    bool due(const ReplayTile& t) const // LLProjectorShadows::resizeDue
    {
        return t.placed && t.want >= kMinTile && t.want != t.rect.size && (t.want < t.rect.size || buddy.largestAfterFree(t.rect) >= t.want);
    }
    void compact() // LLProjectorShadows::compact: every placed tile at its own size
    {
        std::vector<LayoutRequest> requests;
        std::vector<ReplayTile*> who;
        for (auto& e : tiles)
        {
            if (e.second.placed)
            {
                LayoutRequest q;
                q.key = e.first;
                q.size = e.second.rect.size;
                requests.push_back(q);
                who.push_back(&e.second);
            }
        }
        const RepackPlan plan = planRepack(requests, atlas);
        check(plan.ok, "L2 replay: a compaction always fits (the tiles fitted before)");
        buddy = plan.buddy;
        for (size_t i = 0; i < who.size(); ++i)
        {
            who[i]->rect = plan.placed[i];
        }
        ++compactions;
    }
    void layout(float dt)
    {
        std::vector<ValveTile> valve_tiles;
        std::vector<ReplayTile*> recs;
        for (auto& e : tiles)
        {
            valve_tiles.push_back({ e.second.base, e.second.band, 0u });
            recs.push_back(&e.second);
        }
        valve.update(valve_tiles, atlas, dt);
        for (size_t i = 0; i < recs.size(); ++i)
        {
            recs[i]->want = valveSize(valve_tiles[i], valve.level());
        }
        std::vector<ReplayTile*> waiting;
        for (auto& e : tiles)
        {
            if (!e.second.placed)
            {
                waiting.push_back(&e.second);
            }
        }
        std::sort(waiting.begin(), waiting.end(), [](const ReplayTile* a, const ReplayTile* b) { return a->want != b->want ? a->want > b->want : a->key < b->key; });
        auto place = [this](std::vector<ReplayTile*>& list) {
            std::vector<ReplayTile*> left;
            for (ReplayTile* t : list)
            {
                Rect r;
                if (buddy.alloc(t->want, r))
                {
                    t->rect = r;
                    t->placed = true;
                }
                else
                {
                    left.push_back(t);
                }
            }
            list.swap(left);
        };
        auto texels = [](const std::vector<ReplayTile*>& list) {
            uint64_t sum = 0;
            for (const ReplayTile* t : list)
            {
                sum += uint64_t(t->want) * t->want;
            }
            return sum;
        };
        place(waiting);
        if (!waiting.empty() && buddy.freeTexels() >= texels(waiting))
        {
            compact();
            place(waiting);
            check(waiting.empty(), "L2 replay: after a compaction the free blocks hold every arrival the free texels do");
        }
        if (!waiting.empty())
        {
            uint64_t shrinking = 0;
            for (const auto& e : tiles)
            {
                if (e.second.placed)
                {
                    shrinking += shrinkTexels(e.second.rect.size, e.second.want);
                }
            }
            if (arrivalShortfall(buddy.freeTexels(), shrinking, texels(waiting)))
            {
                ++shortfall_frames;
            }
        }
        bool blocked = false;
        for (const auto& e : tiles)
        {
            blocked = blocked || (e.second.placed && growthBlocked(buddy, e.second.rect, e.second.want));
        }
        if (hold.update(blocked, dt))
        {
            compact();
        }
    }
    void render(int per_frame)
    {
        std::vector<ReplayTile*> picks;
        for (auto& e : tiles)
        {
            if (e.second.placed && !e.second.drawn)
            {
                picks.push_back(&e.second);
            }
        }
        std::vector<ReplayTile*> resizing;
        for (auto& e : tiles)
        {
            if (e.second.drawn && due(e.second))
            {
                resizing.push_back(&e.second);
            }
        }
        std::stable_sort(resizing.begin(), resizing.end(), [](const ReplayTile* a, const ReplayTile* b) { return a->drawn_frame < b->drawn_frame; });
        picks.insert(picks.end(), resizing.begin(), resizing.end());
        int n = 0;
        for (ReplayTile* t : picks)
        {
            if (n >= per_frame)
            {
                break;
            }
            if (t->drawn && !due(*t))
            {
                continue; // its block went to an earlier growth of the frame: not drawn (renderTile)
            }
            if (due(*t))
            {
                buddy.free(t->rect);
                Rect r;
                check(buddy.alloc(t->want, r), "L2 replay: a due resize gets its block");
                t->rect = r;
                ++resizes;
            }
            t->drawn = true;
            t->drawn_frame = frame;
            ++n;
            ++renders;
        }
    }
    void step(int per_frame)
    {
        layout(1.f / 60.f);
        render(per_frame);
        ++frame;
    }
    void add(uint32_t key, uint32_t base, int band)
    {
        ReplayTile t;
        t.key = key;
        t.base = base;
        t.band = band;
        tiles[key] = t;
    }
    void drop(uint32_t key)
    {
        auto it = tiles.find(key);
        if (it != tiles.end())
        {
            if (it->second.placed)
            {
                buddy.free(it->second.rect);
            }
            tiles.erase(it);
        }
    }
    int unplaced() const
    {
        int n = 0;
        for (const auto& e : tiles)
        {
            n += e.second.placed ? 0 : 1;
        }
        return n;
    }
    int offWant() const
    {
        int n = 0;
        for (const auto& e : tiles)
        {
            n += e.second.placed && e.second.rect.size != e.second.want ? 1 : 0;
        }
        return n;
    }
    int ruleLevel() const // the valve's rule alone on the same tiles (a fresh controller: the shrink loop from 0)
    {
        std::vector<ValveTile> valve_tiles;
        for (const auto& e : tiles)
        {
            valve_tiles.push_back({ e.second.base, e.second.band, 0u });
        }
        LevelController fresh;
        fresh.update(valve_tiles, atlas, 1.f / 60.f);
        return fresh.level();
    }
};

void testPlacement()
{
    // the hold before a growth's compaction: kGrowHoldSeconds of stable frames, once per hold, reset by a frame without a blocked growth
    {
        GrowthHold h;
        int first = -1, count = 0;
        for (int f = 1; f <= 240; ++f)
        {
            if (h.update(true, 1.f / 60.f))
            {
                first = first < 0 ? f : first;
                ++count;
            }
        }
        check(first >= 180 && first <= 181, "L2 a blocked growth waits kGrowHoldSeconds (180 frames at 60 fps, float sums)", first);
        check(count == 1, "L2 ... then its compaction is due once: the hold starts again", count);
        GrowthHold r;
        for (int f = 0; f < 170; ++f)
        {
            r.update(true, 1.f / 60.f);
        }
        r.update(false, 1.f / 60.f);
        int late = 0;
        for (int f = 0; f < 170; ++f)
        {
            late += r.update(true, 1.f / 60.f);
        }
        check(late == 0, "L2 a frame without a blocked growth resets the hold");
        GrowthHold s;
        check(!s.update(true, 100.f) && !s.update(true, std::numeric_limits<float>::quiet_NaN()), "L2 a stall or a bad frame time is not 3 s of waiting");
    }
    check(shrinkTexels(1024, 512) == 1024u * 1024u - 512u * 512u && shrinkTexels(512, 1024) == 0 && shrinkTexels(512, 512) == 0, "L2 the texels a shrink gives back");
    check(!arrivalShortfall(10, 5, 15) && arrivalShortfall(10, 4, 15), "L2 a shortfall: the arrivals do not fit even once the shrinks are drawn");

    // after a compaction the free texels say what fits: every arrival set (the largest first) they hold is placed, and no growth whose
    // new size they hold is blocked (fragmented atlases from random allocations and frees)
    {
        std::mt19937 rng(77);
        int cases = 0, misplaced = 0, still_blocked = 0, fragmented = 0;
        for (int trial = 0; trial < 300; ++trial)
        {
            const uint32_t atlas = trial % 2 ? 4096u : 2048u;
            BuddyAtlas b(atlas);
            std::vector<Rect> live;
            for (int i = 0; i < 400; ++i)
            {
                Rect r;
                if (live.empty() || rng() % 3 != 0)
                {
                    if (b.alloc(64u << (rng() % 5), r))
                    {
                        live.push_back(r);
                    }
                }
                else
                {
                    const size_t k = rng() % live.size();
                    b.free(live[k]);
                    live.erase(live.begin() + long(k));
                }
            }
            for (const Rect& r : live) // a growth of each tile to twice its size: blocked before the compaction?
            {
                fragmented += growthBlocked(b, r, r.size * 2) ? 1 : 0;
            }
            std::vector<LayoutRequest> requests;
            for (size_t i = 0; i < live.size(); ++i)
            {
                LayoutRequest q;
                q.key = uint32_t(i + 1);
                q.size = live[i].size;
                requests.push_back(q);
            }
            const RepackPlan plan = planRepack(requests, atlas);
            if (!plan.ok)
            {
                check(false, "L2 a compaction of what fitted fits", trial);
                continue;
            }
            for (size_t i = 0; i < live.size(); ++i)
            {
                for (uint32_t want = plan.placed[i].size * 2; want <= atlas; want *= 2)
                {
                    if (uint64_t(want) * want <= plan.buddy.freeTexels())
                    {
                        ++cases;
                        still_blocked += growthBlocked(plan.buddy, plan.placed[i], want) ? 1 : 0;
                    }
                }
            }
            BuddyAtlas after = plan.buddy;
            uint64_t left = after.freeTexels();
            std::vector<uint32_t> arrivals;
            while (true)
            {
                const uint32_t s = 64u << (rng() % 6);
                if (uint64_t(s) * s > left)
                {
                    break;
                }
                arrivals.push_back(s);
                left -= uint64_t(s) * s;
            }
            std::sort(arrivals.rbegin(), arrivals.rend());
            for (uint32_t s : arrivals)
            {
                Rect r;
                misplaced += after.alloc(s, r) ? 0 : 1;
            }
        }
        check(fragmented > 0, "L2 the random atlases do fragment (growths blocked although the texels would hold them)", fragmented);
        check(cases > 1000 && still_blocked == 0, "L2 after a compaction no growth the free texels hold is blocked", cases, still_blocked);
        check(misplaced == 0, "L2 after a compaction every arrival set the free texels hold is placed", misplaced);
    }

    // B1 (review): a lamp switched on near the camera while far tiles hold the atlas. Its tiles wait for the far tiles' shrinks and the
    // pressure ends where the rule puts it (a step for the waiting arrival left the near tiles an octave coarser for good)
    for (const int per_frame : { 4, 1000 })
    {
        ReplayPool p(4096);
        uint32_t key = 1;
        for (int i = 0; i < 38; ++i)
        {
            p.add(key++, 1024, 2);
        }
        for (int f = 0; f < 600; ++f)
        {
            p.step(per_frame);
        }
        check(p.valve.level() == 3 && p.offWant() == 0, "L2 B1: 38 far tiles settle at pressure 3", p.valve.level());
        const uint32_t first_near = key;
        for (const uint32_t base : { 2048u, 1024u, 1024u, 1024u })
        {
            p.add(key++, base, 0);
        }
        int waited = -1;
        for (int f = 0; f < 60 * 30; ++f)
        {
            p.step(per_frame);
            if (waited < 0 && p.unplaced() == 0)
            {
                waited = f;
            }
        }
        check(p.valve.level() == p.ruleLevel() && p.valve.level() == 4, "L2 B1: the pressure is the rule's, not one step above", p.valve.level(), p.ruleLevel());
        check(p.tiles[first_near].rect.size == 2048 && p.tiles[first_near + 1].rect.size == 1024, "L2 B1: the near tiles keep their lamp's size",
              p.tiles[first_near].rect.size, p.tiles[first_near + 1].rect.size);
        check(waited >= 0 && waited <= 3 && p.offWant() == 0, "L2 B1: the arrivals wait a few frames for the shrinks, then every tile has its size", waited);
        check(p.shortfall_frames == 0, "L2 B1: no shortfall below the ceiling", p.shortfall_frames);
    }

    // over the budget even at the ceiling: a shortfall no render frees (5000 tiles do not fit a 4096 atlas at their floor)
    {
        ReplayPool p(4096);
        for (uint32_t i = 1; i <= 5000; ++i)
        {
            p.add(i, 2048, 0);
        }
        for (int f = 0; f < 120; ++f)
        {
            p.step(1000);
        }
        check(p.valve.level() == p.valve.ceiling() && p.unplaced() > 0 && p.shortfall_frames > 0, "L2 a shortfall exists only at the ceiling, with tiles left out",
              p.unplaced(), p.shortfall_frames);
    }

    // Q1 (audit): fragmentation that blocks a growth. A 2048 lamp and three far 1024 ones at pressure 0; a burst of near lamps fills the
    // atlas while the old tiles still hold their rects; a second burst; then the scene leaves but for one tile in each quadrant. The 2048
    // lamp comes near: the free texels hold its 2048 four times over, no quadrant is free. It gets a compaction after the hold.
    {
        ReplayPool p(4096);
        uint32_t key = 1;
        const uint32_t g = key;
        p.add(key++, 2048, kMaxBand);
        for (int i = 0; i < 3; ++i)
        {
            p.add(key++, 1024, kMaxBand);
        }
        for (int f = 0; f < 120; ++f)
        {
            p.step(6);
        }
        for (int i = 0; i < 64; ++i)
        {
            p.add(key++, 128, 0);
        }
        for (int f = 0; f < 600; ++f)
        {
            p.step(6);
        }
        for (int i = 0; i < 128; ++i)
        {
            p.add(key++, 256, 0);
        }
        for (int f = 0; f < 600; ++f)
        {
            p.step(6);
        }
        bool kept[4] = { false, false, false, false };
        std::vector<uint32_t> leave;
        for (const auto& e : p.tiles)
        {
            const int q = int(e.second.rect.x / 2048) + 2 * int(e.second.rect.y / 2048);
            if (e.first == g)
            {
                continue;
            }
            if (e.second.placed && !kept[q])
            {
                kept[q] = true; // the first tile found in each quadrant stays (in the 2048 lamp's quadrant, one besides it)
                continue;
            }
            leave.push_back(e.first);
        }
        for (uint32_t k : leave)
        {
            p.drop(k);
        }
        p.tiles[g].band = 0;
        const int compactions = p.compactions;
        int blocked_frames = 0, grown_at = -1;
        for (int f = 0; f < 60 * 10; ++f)
        {
            p.step(6);
            const ReplayTile& t = p.tiles[g];
            blocked_frames += growthBlocked(p.buddy, t.rect, t.want) ? 1 : 0;
            if (grown_at < 0 && t.rect.size == 2048)
            {
                grown_at = f;
            }
        }
        check(kept[0] && kept[1] && kept[2] && kept[3], "L2 Q1: every quadrant holds a tile");
        check(blocked_frames >= 170, "L2 Q1: the growth is blocked by fragmentation alone (the case exists)", blocked_frames);
        check(grown_at >= 170 && grown_at <= 200 && p.compactions == compactions + 1, "L2 Q1: after the hold one compaction lets it grow", grown_at,
              p.compactions - compactions);
    }
}

void testCells()
{
    testCellGrid();
    testCellPyramids();
    testCellSizing();
    testCellOwn();
}
} // namespace

int main(int argc, char** argv)
{
    const bool print_table = argc > 1 && std::string(argv[1]) == "--table"; // the cadence table of the C1 commit
    if (argc > 1 && std::string(argv[1]) == "--bench-index")
    {
        benchChangeIndex();
        return 0;
    }
    testSizeRule();
    testBuddy();
    testT1();
    testT2();
    testT3();
    testPlanBuddy();
    testT4();
    testT5();
    testT6();
    testT7();
    testT7Differential();
    testP3IndexSubset();
    testBoxFilter();
    testT8();
    testBadInput();
    testP3();
    testP4(print_table);
    testCells();
    testScriptLamps();
    testTextureEntryRebuilds();
    testPlacement();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("llprojectorshadowpool_test: all checks passed\n");
    return 0;
}
