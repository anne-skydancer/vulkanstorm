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
//   T7  scheduler: never-drawn first, 250 ms deadline beats distance, avatar items never exceed
//       A_max, no static starvation under a continuous dirty source
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
        // never-drawn first, nearest first, before anything stale or dirty
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, false, true, 10.f, 1.f, 0.05f });       // dirty, near
            items.push_back({ 2, true, false, 0.f, 50.f, 0.05f });       // never drawn, far
            items.push_back({ 3, true, false, 0.f, 5.f, 0.05f });        // never drawn, near
            items.push_back({ 4, false, true, 900.f, 90.f, 0.05f });     // stale
            const std::vector<uint32_t> o = selectStatic(items, 10.f);
            check(o.size() == 4 && o[0] == 3 && o[1] == 2, "T7 never-drawn first, nearest first");
            check(o.size() == 4 && o[2] == 4 && o[3] == 1, "T7 then stale, then the rest");
        }
        // the 250 ms deadline beats distance: a far item stale 300 ms precedes a near fresh one
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, false, true, 20.f, 2.f, 0.05f });
            items.push_back({ 2, false, true, 300.f, 100.f, 0.05f });
            items.push_back({ 3, false, true, 251.f, 90.f, 0.05f });
            const std::vector<uint32_t> o = selectStatic(items, 10.f);
            check(o.size() == 3 && o[0] == 2 && o[1] == 3 && o[2] == 1, "T7 stale beats distance, oldest first");
            std::vector<StaticItem> edge;
            edge.push_back({ 1, false, true, 249.f, 2.f, 0.05f });
            edge.push_back({ 2, false, true, 249.f, 1.f, 0.05f });
            const std::vector<uint32_t> o2 = selectStatic(edge, 10.f);
            check(o2.size() == 2 && o2[0] == 2, "T7 under 250 ms the distance rules");
        }
        // cap and progress: at least one item even when it alone exceeds the cap; none when idle
        {
            std::vector<StaticItem> items;
            items.push_back({ 1, true, false, 0.f, 3.f, 0.9f });
            items.push_back({ 2, true, false, 0.f, 4.f, 0.1f });
            const std::vector<uint32_t> o = selectStatic(items, 0.25f);
            check(o.size() == 1 && o[0] == 1, "T7 floor of one item over the cap", double(o.size()));
            std::vector<StaticItem> many;
            for (uint32_t i = 1; i <= 20; ++i) many.push_back({ i, false, true, 10.f, float(i), 0.1f });
            const std::vector<uint32_t> o2 = selectStatic(many, 0.25f);
            check(o2.size() == 2, "T7 cap stops after two 0.1 ms items", double(o2.size()));
            std::vector<StaticItem> idle;
            idle.push_back({ 1, false, false, 0.f, 1.f, 0.1f });
            check(selectStatic(idle, 0.25f).empty(), "T7 a static scene renders nothing");
        }
        // no starvation: a near source dirties item 1 every frame; a far item that became dirty
        // once is still served within 250 ms (+ a frame) of dirtying
        {
            const float frame_ms = 16.f;
            std::vector<StaticItem> items(2);
            items[0] = { 1, false, true, 0.f, 1.f, 0.2f };
            items[1] = { 2, false, true, 0.f, 90.f, 0.2f };
            float served_at = -1.f;
            float t = 0.f;
            for (int f = 0; f < 200 && served_at < 0.f; ++f, t += frame_ms)
            {
                items[0].pending = true;
                items[0].stale_ms = 0.f;           // re-dirtied every frame: always fresh and near
                const std::vector<uint32_t> o = selectStatic(items, 0.25f); // cap: one item per frame
                if (!o.empty() && o[0] == 2)
                {
                    served_at = t;
                    items[1].pending = false;
                }
                else
                {
                    items[1].stale_ms += frame_ms;
                }
            }
            check(served_at >= 0.f && served_at <= kStaleMs + 2 * frame_ms, "T7 far item is not starved", served_at);
        }
        // cold start: 20 never-drawn tiles with no history (cost 0, NaN) do not all run in one frame
        {
            std::vector<StaticItem> cold;
            for (uint32_t i = 1; i <= 20; ++i) cold.push_back({ i, true, false, 0.f, float(i), 0.f });
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
            // a bad frame time never advances or poisons the grow hold
            {
                const std::vector<uint32_t> one{ 2048 };
                LevelController lc;
                lc.update(std::vector<uint32_t>(3, 2048), 4096, 0.016f, false);   // level 1
                const int shrunk = lc.level();
                for (float dt : { std::numeric_limits<float>::quiet_NaN(), -5.f })
                {
                    for (int i = 0; i < 10; ++i) lc.update(one, 4096, dt, false);
                }
                check(lc.level() == shrunk, "T2 a NaN or negative frame time does not grow the level", lc.level());
                lc.update(one, 4096, 100.f, false);   // one stall counts as one short frame, not 3 s of stability
                check(lc.level() == shrunk, "T2 a long stall is not 3 s of stable demand", lc.level());
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
    testT4();
    testT5();
    testT6();
    testT7();
    testBadInput();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("llprojectorshadowpool_test: all checks passed\n");
    return 0;
}
