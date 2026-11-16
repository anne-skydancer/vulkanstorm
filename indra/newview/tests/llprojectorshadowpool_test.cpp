// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llprojectorshadowpool.h (no viewer libraries, no GL):
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
//       controller at 8192, and the P2b size rules (the radius cap and the emitter extent in the shader's and C++ side's twin)
//   P3  the change causes, the dirty list's bound, the grid index against brute force, the mover rules, the LOD pin's books,
//       the tile verdict, and a static scene that draws nothing
// plus the size rule and the level controller (the C++ twin of the spec's rules).

#include "llprojectorshadowpool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
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
        lc.update(bases, atlas, 0.016f, false);
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
        lc.update(std::vector<uint32_t>(9, 2048u), atlas, 0.016f, false);
        check(lc.level() == 0, "T8 nine 2048 lamps stay at level 0 on 8192", lc.level());
        lc.update(std::vector<uint32_t>(10, 2048u), atlas, 0.016f, false);
        check(lc.level() == 1, "T8 the tenth shrinks", lc.level());
        LevelController l4;
        l4.update(std::vector<uint32_t>(3, 2048u), 4096, 0.016f, false);
        check(l4.level() == 1, "T8 the same on 4096: the third lamp shrinks", l4.level());
        for (uint32_t n : { 5u, 12u, 40u, 100u })
        {
            LevelController c;
            const std::vector<uint32_t> bases(n, 2048u);
            c.update(bases, atlas, 0.016f, false);
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

        // P2b: the radius cap in metres: 0.04 m * k * N / w texels, clamped to 1 .. 12, per axis (x and y not swapped)
        check(std::fabs(radiusCapTexels(2.0f, 1024, 10.0f) - 8.192f) < 1e-4f, "T8 cap k 2, N 1024, w 10 m = 8.192 texels", radiusCapTexels(2.0f, 1024, 10.0f));
        check(std::fabs(radiusCapTexels(0.5f, 1024, 10.0f) - 2.048f) < 1e-4f, "T8 cap k 0.5, N 1024, w 10 m = 2.048 texels", radiusCapTexels(0.5f, 1024, 10.0f));
        check(radiusCapTexels(2.0f, 1024, 10.0f) > radiusCapTexels(0.5f, 1024, 10.0f), "T8 the wider axis (larger k) gets the larger cap");
        check(radiusCapTexels(2.0f, 64, 100.0f) == 1.0f, "T8 a small far tile: at least one texel");
        check(radiusCapTexels(2.0f, 2048, 1.0f) == 12.0f, "T8 a large near tile: at most 12 texels");
        // a level step halves the texels per metre and so the cap, above the one texel floor
        check(std::fabs(radiusCapTexels(1.0f, 512, 4.0f) * 2.0f - radiusCapTexels(1.0f, 1024, 4.0f)) < 1e-5f, "T8 a level step halves the cap", radiusCapTexels(1.0f, 512, 4.0f));
        // the emitter's extent: a fixed 1 cm light times the softness (RenderShadowLightSize), whatever the prim
        check(emitterExtent(1.0f) == 0.01f && emitterExtent(0.0f) == 0.0f, "T8 emitter extent is 1 cm at softness 1");
        check(std::fabs(emitterExtent(kSoftnessDefault) - 0.0023f) < 1e-7f, "T8 emitter extent at the default softness 0.23 is 2.3 mm", emitterExtent(kSoftnessDefault));
        check(std::fabs(emitterExtent(0.5f) - 0.005f) < 1e-8f, "T8 emitter extent scales with the softness");
        check(emitterExtent(std::numeric_limits<float>::quiet_NaN()) == emitterExtent(kSoftnessDefault) && emitterExtent(std::numeric_limits<float>::infinity()) == emitterExtent(kSoftnessDefault)
              && emitterExtent(-1.0f) == emitterExtent(kSoftnessDefault), "T8 a negative or non-finite softness gets the default");
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
        check(!lc.update(two, atlas, 0.016f, false) && lc.level() == 0, "T2 two lamps level 0");
        check(lc.update(three, atlas, 0.016f, false) && lc.level() == 1, "T2 third lamp shrinks", lc.level());

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
            if (lc.update(in ? three : two, atlas, 0.016f, false))
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
                ch += tg.update((f & 1) ? three : two, atlas, 0.016f, false) ? 1 : 0;
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
        a.update(exact60, atlas, 0.016f, false);
        check(a.level() == 1, "T2 0.75 shrinks", a.level());
        std::vector<uint32_t> u38(38, 512u);
        LevelController b;
        b.update(u38, atlas, 0.016f, false);
        check(demand(u38, 0, atlas) < kShrinkAbove && b.level() == 0, "T2 0.59 holds", demand(u38, 0, atlas));
        std::vector<uint32_t> u40(40, 512u);               // 0.625
        LevelController c;
        c.update(u40, atlas, 0.016f, false);
        check(demand(u40, 0, atlas) > kShrinkAbove && c.level() == 1, "T2 0.625 shrinks", c.level());

        // grow: from level 1, U(0) must be <= 0.45 for 3 s
        std::vector<uint32_t> low(28, 512u);                // 28/64 = 0.4375 <= 0.45 at level 0
        LevelController g;
        g.update(u40, atlas, 0.016f, false);
        check(g.level() == 1, "T2 start at 1");
        float t = 0.f;
        bool grew = false;
        while (t < 2.95f)
        {
            grew = g.update(low, atlas, 0.05f, false) || grew;
            t += 0.05f;
        }
        check(!grew && g.level() == 1, "T2 no grow before 3 s", t);
        for (int i = 0; i < 4; ++i)
        {
            g.update(low, atlas, 0.05f, false);
        }
        check(g.level() == 0, "T2 grows after 3 s", g.level());
        // a spike above 0.45 resets the hold
        std::vector<uint32_t> mid(30, 512u);                // 30/64 = 0.469 > 0.45: no grow
        LevelController r2;
        r2.update(u40, atlas, 0.016f, false);
        for (int i = 0; i < 50; ++i) r2.update(low, atlas, 0.05f, false);  // 2.5 s
        r2.update(mid, atlas, 0.05f, false);                                // reset
        for (int i = 0; i < 50; ++i) r2.update(low, atlas, 0.05f, false);  // 2.5 s again
        check(r2.level() == 1, "T2 interrupted hold does not grow", r2.level());
        // placement failure asks for one more step
        LevelController p;
        check(p.update(two, atlas, 0.016f, true) && p.level() == 1, "T2 placement failure steps down", p.level());
        // never past the max level
        LevelController m;
        m.update(std::vector<uint32_t>(10000, 2048u), atlas, 0.016f, false);
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
                    lc.update(std::vector<uint32_t>(3, 2048), 4096, 0.016f, false);   // level 1
                    return lc;
                };
                auto stable = [&](LevelController& lc, int frames, float dt) {
                    for (int i = 0; i < frames; ++i) lc.update(one, 4096, dt, false);
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
    check(mismatches == 0, "P3 the grid index gives the brute-force hits, once per tile, with the first box's cause", mismatches);
    index.apply({}, false, hits);
    check(hits.empty(), "P3 no dirty box, no hit (a static scene)");
    index.apply({}, true, hits);
    check(hits.size() == 80 && hits[0].cause == Cause::Other, "P3 everything touches every tile");
    ChangeIndex none;
    none.rebuild({});
    none.apply({ DirtyBox() }, true, hits);
    check(hits.empty(), "P3 no tiles, no hits");
    // a world-sized box (more than the grid's cell cap) is conservative, never a crash
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
    check(texturedDirties(true, true, true), "P3 a retexture of a prim with an alpha face before and after dirties");
    check(texturedDirties(true, false, true), "P3 a face turned opaque (alpha texture replaced by an RGB one) dirties: the hole closes");
    check(texturedDirties(false, true, true), "P3 an opaque face turned alpha dirties: a hole opens");
    check(!texturedDirties(false, false, true), "P3 opaque to opaque (a plain texture swap, an animated texture) does not dirty");
    check(texturedDirties(false, false, false), "P3 an unknown past (the prim predates tracking) pushes once, even if it looks opaque now");
    check(texturedDirties(false, true, false) && texturedDirties(true, false, false), "P3 an unknown past with an alpha face now or before pushes");
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
    // the constants: one combined cap inside the +5 % allowance (about 0.4 ms), the dynamic layer's share and the static floor
    check(kFrameCapMs <= 0.40f, "P4 the combined cap is inside the 0.4 ms of the +5 % allowance", kFrameCapMs);
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
        const std::vector<DynamicItem> dyn = faces(1, 0.5f, 9);   // overdue and over the cap alone
        const std::vector<StaticItem> st = tiles(1, 0.5f);
        const FramePicks p = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, false);
        check(p.dynamic.empty() && p.fixed.empty(), "P4 no progress pick once the frame has spent: an item over the cap waits");
        const FramePicks q = selectFrame(st, dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(q.dynamic.size() + q.fixed.size() == 1, "P4 progress: an untouched frame runs one over-cap item alone, never two", double(q.dynamic.size() + q.fixed.size()));
        check(q.dynamic.size() == 1, "P4 progress: the dynamic item has the priority");
    }
    // a static tile above the whole cap is not starved by a standing avatar: after the wait it runs alone as the progress pick
    {
        const std::vector<DynamicItem> dyn = faces(3, 0.05f, 1);
        std::vector<StaticItem> st = tiles(1, 0.5f);
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
        std::vector<StaticItem> st = tiles(1, 0.5f);
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
        check(p.dynamic.size() == 4, "P4 the dynamic layer takes its share: 0.20 ms / 0.05 ms = 4 faces", double(p.dynamic.size()));
        check(p.fixed.size() == 3, "P4 the static tiles get what is left, at least the floor: 0.15 ms / 0.05 ms = 3 tiles", double(p.fixed.size()));
        check(p.dynamic[0] == packKey(0, 1), "P4 the nearest face first (by position)");
        const FramePicks only_static = selectFrame(st, std::vector<DynamicItem>(), kFrameCapMs, kAvatarDrawsMax, true);
        check(only_static.fixed.size() == 7, "P4 without dynamic work the static tiles may take the whole cap: 0.35 / 0.05 = 7", double(only_static.fixed.size()));
        const FramePicks only_dyn = selectFrame(std::vector<StaticItem>(), dyn, kFrameCapMs, kAvatarDrawsMax, true);
        check(only_dyn.dynamic.size() == 7, "P4 the dynamic layer alone takes the whole cap when no static tile waits (C1): 0.35 / 0.05 = 7", double(only_dyn.dynamic.size()));
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
        ledger.add(0.20f);
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
    check(layerFacesPerFrame(kDynamicCapMs, 0.05f, kAvatarDrawsMax, 0) == 4, "P4 lag: 0.20 ms at 0.05 ms a face is 4 faces a frame", layerFacesPerFrame(kDynamicCapMs, 0.05f, kAvatarDrawsMax, 0));
    check(layerFacesPerFrame(kDynamicCapMs, 0.04f, kAvatarDrawsMax, 0) == 4, "P4 lag: a face below the scheduler floor is predicted at the floor");
    check(layerFacesPerFrame(kDynamicCapMs, 0.10f, kAvatarDrawsMax, 0) == 2, "P4 lag: 0.10 ms a face is 2 faces a frame");
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
// the ledger. `legacy` is the release scheduler as it was before C1 (a fixed 0.20 ms share, only an overdue face may run over it,
// lamp distance for the order, the last measured cost as the prediction); its pick is reproduced here from the old code.
//   lamp_ties: every lamp at the same camera distance (0 indoors: the camera is inside every lamp's reach)
//   shared_avatar: every lamp holds the SAME avatar (the owner's own), so the avatar distance ties as well
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
        if (legacy)
        {
            const float left = ledger.left(kFrameCapMs);
            for (uint32_t k : legacySelectDynamic(items, std::min(kDynamicCapMs, left) + 1e-4f, kAvatarDrawsMax)) picks.push_back(k);
            float ms = 0.f;
            for (uint32_t k : picks) ms += itemCost(measured[k]);
            if (ms > left + 1e-4f && picks.size() != 1) picks.clear();
        }
        else
        {
            for (uint32_t k : selectFrame(std::vector<StaticItem>(), items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched()).dynamic)
                picks.push_back(unpackKey(k));
        }
        const float share = legacy ? kDynamicCapMs : dynamicShareMs(false, kFrameCapMs);
        float own = 0.f;
        bool first = true;
        for (uint32_t k : picks)
        {
            if (!mayRunPick(ledger.left(kFrameCapMs), true, first, false) || (!first && own >= share)) break;
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
        check(selectFrame(std::vector<StaticItem>(), dyn, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 7, "C1 share: 0.35 / 0.05 = 7 faces without a static tile");
        check(selectFrame(tiles(1, 0.05f), dyn, kFrameCapMs, kAvatarDrawsMax, true).dynamic.size() == 4, "C1 share: with a static tile waiting, 4 faces as before");
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
        check(p < 0.20f, "C1 smoothing: one spike of 0.30 ms leaves the prediction under the share", p);
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
        check(q <= 0.08f + kCostSmoothing * (kCostSampleMaxMs - 0.08f) + 1e-6f, "C1 smoothing: a 50 ms hitch counts as 0.7 ms", q);
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

void testP3()
{
    testP3Causes();
    testP3DirtyList();
    testP3ChangeIndex();
    testP3MoverRules();
    testP3PinBook();
    testP3Verdict();
    testP3StaticScene();
}
} // namespace

int main(int argc, char** argv)
{
    const bool print_table = argc > 1 && std::string(argv[1]) == "--table"; // the cadence table of the C1 commit
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
    testT8();
    testBadInput();
    testP3();
    testP4(print_table);
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("llprojectorshadowpool_test: all checks passed\n");
    return 0;
}
