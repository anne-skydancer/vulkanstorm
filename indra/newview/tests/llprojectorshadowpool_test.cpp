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
// plus the size rule and the level controller (the C++ twin of the spec's rules).

#include "llprojectorshadowpool.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <set>

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
        // P2b: the emitter's extent: the prim's face, 2 cm .. 15 cm, never softer than P2's 15 cm
        check(emitterExtent(0.05f) == 0.05f && emitterExtent(0.001f) == kEmitterMin && emitterExtent(0.5f) == 0.15f && emitterExtent(5.0f) == 0.15f, "T8 emitter extent clamps the face to 2 cm .. 15 cm");
        check(emitterExtent(std::numeric_limits<float>::quiet_NaN()) == kEmitterMin && emitterExtent(std::numeric_limits<float>::infinity()) == kEmitterMin, "T8 a non-finite face gets the lower limit");
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
        const std::vector<Aabb>& frame1 = d.swapForApply();
        check(frame1.size() == 1, "T6 frame 1 sees its push", double(frame1.size()));
        // pushes made while frame 1's tiles render
        d.push(b);
        d.push(c);
        check(frame1.size() == 1, "T6 a push during render does not enter the applied list", double(frame1.size()));
        check(d.pending() == 2, "T6 it waits", double(d.pending()));
        const std::vector<Aabb>& frame2 = d.swapForApply();
        check(frame2.size() == 2 && frame2[0].lo[0] == 5.f && frame2[1].lo[0] == 9.f, "T6 it lands next frame",
              double(frame2.size()));
        check(d.swapForApply().empty(), "T6 and is not delivered twice");
        // shiftObjects moves pending boxes
        d.push(a);
        d.translatePending(10.f, 0.f, -1.f);
        const std::vector<Aabb>& f4 = d.swapForApply();
        check(f4.size() == 1 && f4[0].lo[0] == 10.f && f4[0].hi[2] == 0.f, "T6 translate pending");
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
            check(selectStatic(items, kStaticCapMs).size() == 4, "T7 four cheap tiles all run every frame (fixed cap)");
            const Sim s4 = simulate(4, 0.07f, kStaticCapMs, 100, true);
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
            check(coldItemMs(0.5f) <= kStaticCapMs / 4.f, "T7 the cold prediction is at most a quarter of the cap", coldItemMs(0.5f));
            check(coldItemMs(0.07f) == 0.07f, "T7 a measured average below the quarter is kept", coldItemMs(0.07f));
            std::vector<StaticItem> cold;
            for (uint32_t i = 1; i <= 20; ++i) cold.push_back({ i, true, false, float(i), coldItemMs(0.5f), 0 });
            const std::vector<uint32_t> o = selectStatic(cold, kStaticCapMs);
            check(o.size() >= 3 && o.size() <= 4, "T7 a 0.5 ms prior still serves several never-drawn tiles per frame", double(o.size()));
            check(o.size() >= 1 && o[0] == 1, "T7 the nearest never-drawn tile first");
            std::vector<StaticItem> old_prior;
            for (uint32_t i = 1; i <= 20; ++i) old_prior.push_back({ i, true, false, float(i), 0.5f, 0 });
            check(selectStatic(old_prior, kStaticCapMs).size() == 1, "T7 (the uncapped 0.5 ms prior would serve one tile)");
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
            check(selectDynamic(items, 0.001f, 24).empty(), "T7 no floor: nothing fits the cap, nothing runs");
            std::vector<DynamicItem> big{ { 1, true, 0, 1.f, 0.01f, 30 }, { 2, true, 0, 2.f, 0.01f, 5 } };
            const std::vector<uint32_t> o3 = selectDynamic(big, 10.f, 24);
            check(o3.size() == 1 && o3[0] == 1,
                  "T7 an item over A_max draws its nearest A_max, it is not starved", o3.size());
            std::vector<DynamicItem> alone{ { 7, true, 9, 1.f, 0.01f, 30 } };
            check(selectDynamic(alone, 10.f, 24).size() == 1, "T7 a lone item with 30 avatars still runs");
            std::vector<DynamicItem> costly{ { 8, false, 9, 1.f, 0.5f, 0 } };
            check(selectDynamic(costly, 0.3f, 24).size() == 1, "T7 an overdue item over the CPU cap runs");
            std::vector<DynamicItem> costly_young{ { 8, false, 0, 1.f, 0.5f, 0 } };
            check(selectDynamic(costly_young, 0.3f, 24).empty(), "T7 a young item over the CPU cap waits");
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
} // namespace

int main()
{
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
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("llprojectorshadowpool_test: all checks passed\n");
    return 0;
}
