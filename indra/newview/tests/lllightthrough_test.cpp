// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lllightthrough.h, lamp shadows L3 (no viewer libraries, no GL):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lllightthrough_test.cpp
// Plan: reports/light-through-l3-2026-11-18/plan.md, section 8.
//   Rule       the light through one face: 1 at a = 0, 0 at a = 1, 1 - a for a black face, 1 - a^2 for a white one, never above
//              1, falling with the opacity and rising with the colour; the red pane and clear glass of the plan; the step at the solid
//              switch (0.98) at most 0.04; the invisible limit
//   Stack      stacked faces multiply, in any order
//   Tap        a receiver behind every caster of a footprint gets the bilinear tau exactly, in front of them all exactly 1, and a
//              footprint with no caster multiplies a visibility bit for bit; a mixed footprint lies between
//   Depth      the stored depth is never nearer the light than the caster whichever way the 16-bit conversion rounds, at most two
//              steps farther; the far plane stays "no caster"
//   Memory     24, 96 and 384 MiB at the 2048, 4096 and 8192 atlases
//   Carry      a repack's moves: swaps, chains and cycles carried by the two-phase plan, applied to a model atlas; moved tiles without
//              a caster cleared, tiles that stay untouched

#include "lllightthrough.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>

using namespace lllightthrough;
using llprojectorpool::Move;
using llprojectorpool::Rect;

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

    bool sameBits(float a, float b)
    {
        return std::memcmp(&a, &b, sizeof(float)) == 0;
    }

    void testRule()
    {
        // the limits
        for (float c : { 0.f, 0.25f, 0.5f, 1.f })
        {
            check(transmittance(0.f, c) == 1.f, "a = 0 lets all light through", c);
            check(transmittance(1.f, c) == 0.f, "a = 1 is opaque", c);
        }
        // black, white, bounds, monotonic
        for (int i = 0; i <= 1000; ++i)
        {
            const float a = i / 1000.f;
            check(std::fabs(transmittance(a, 0.f) - (1.f - a)) < 1e-6f, "a black face lets 1 - a through", a);
            check(std::fabs(transmittance(a, 1.f) - (1.f - a * a)) < 1e-6f, "a white face lets 1 - a^2 through", a);
            for (int j = 0; j <= 20; ++j)
            {
                const float c = j / 20.f;
                const float t = transmittance(a, c);
                check(t >= (1.f - a) - 1e-6f && t <= (1.f - a * a) + 1e-6f, "between 1 - a and 1 - a^2", a, c);
                check(t <= 1.f && t >= 0.f, "within [0, 1]", a, c);
                if (i > 0)
                {
                    check(t <= transmittance((i - 1) / 1000.f, c) + 1e-7f, "falls with the opacity", a, c);
                }
                if (j > 0)
                {
                    check(t >= transmittance(a, (j - 1) / 20.f) - 1e-7f, "rises with the colour", a, c);
                }
            }
        }
        // the plan's examples: a red pane at 0.6 throws red light; clear glass at 0.3 passes 0.91
        check(std::fabs(transmittance(0.6f, 1.f) - 0.64f) < 1e-6f, "red pane, red channel 0.64", transmittance(0.6f, 1.f));
        check(std::fabs(transmittance(0.6f, 0.f) - 0.40f) < 1e-6f, "red pane, green and blue 0.40", transmittance(0.6f, 0.f));
        check(std::fabs(transmittance(0.3f, 1.f) - 0.91f) < 1e-6f, "clear glass at 0.3 passes 0.91", transmittance(0.3f, 1.f));
        // the switch to solid: just below 0.98 at most 0.04 gets through, so the step to the depth layer's 0 cannot be seen
        const float below = std::nextafter(kSolidAlpha, 0.f);
        check(transmittance(below, 1.f) < 0.04f, "the step at the solid switch is under 0.04", transmittance(below, 1.f));
        check(isSolid(kSolidAlpha) && !isSolid(below), "solid from 0.98 up");
        // invisible: one 8-bit step of alpha, whose light rounds to within one 8-bit step of 1
        check(isInvisible(0.f) && isInvisible(kInvisibleAlpha) && !isInvisible(2.f / 255.f), "invisible at or below 1/255");
        check(isInvisible(std::nanf("")), "NaN alpha is invisible");
        for (float c : { 0.f, 1.f })
        {
            check(transmittance(kInvisibleAlpha, c) >= 1.f - 1.f / 255.f - 1e-7f, "an invisible face is within one step of 1", c);
        }
        // out of range and NaN inputs are clamped
        check(transmittance(-0.5f, 0.3f) == 1.f && transmittance(2.f, 0.3f) == 0.f, "a clamped to [0, 1]");
        check(transmittance(0.5f, 7.f) == transmittance(0.5f, 1.f) && transmittance(0.5f, -1.f) == transmittance(0.5f, 0.f), "c clamped to [0, 1]");
        check(transmittance(std::nanf(""), 0.5f) == 1.f, "NaN alpha lets the light through");
    }

    void testStack()
    {
        // the layer multiplies (glBlendFunc(GL_ZERO, GL_SRC_COLOR)): two panes of clear glass at 0.5 pass 0.75^2, a red and a green
        // pane pass the product per channel, and the order does not matter
        const float g = transmittance(0.5f, 1.f);
        check(std::fabs(g * g - 0.5625f) < 1e-6f, "two clear panes at 0.5 pass 0.5625", g * g);
        const float red[3] = { transmittance(0.6f, 1.f), transmittance(0.6f, 0.f), transmittance(0.6f, 0.f) };
        const float green[3] = { transmittance(0.4f, 0.f), transmittance(0.4f, 1.f), transmittance(0.4f, 0.f) };
        const float sheer[3] = { transmittance(0.3f, 0.9f), transmittance(0.3f, 0.9f), transmittance(0.3f, 0.8f) };
        for (int ch = 0; ch < 3; ++ch)
        {
            const float abc = red[ch] * green[ch] * sheer[ch];
            const float cba = sheer[ch] * green[ch] * red[ch];
            const float bca = green[ch] * sheer[ch] * red[ch];
            check(std::fabs(abc - cba) < 1e-6f && std::fabs(abc - bca) < 1e-6f, "the product does not depend on the order", abc, cba);
            check(abc <= std::min(red[ch], std::min(green[ch], sheer[ch])), "a stack lets through no more than its darkest face");
        }
        // the old dither passed the most opaque layer's light (two layers of 0.5 passed 0.5): the product is darker
        check(transmittance(0.5f, 0.f) * transmittance(0.5f, 0.f) < 0.5f, "two black sheers at 0.5 pass 0.25, not 0.5");
    }

    void testTap()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        for (int n = 0; n < 20000; ++n)
        {
            const float fx = u(rng), fy = u(rng);
            const float bw[4] = { (1.f - fx) * fy, fx * fy, fx * (1.f - fy), (1.f - fx) * (1.f - fy) };
            float tau[4], z[4];
            int casters = 0;
            for (int i = 0; i < 4; ++i)
            {
                const bool has = u(rng) < 0.6f;
                casters += has;
                tau[i] = has ? transmittance(u(rng), u(rng)) : 1.f;
                z[i] = has ? 0.2f + 0.5f * u(rng) : kNoLayer;
            }
            float tau_bilinear = 0.f;
            for (int i = 0; i < 4; ++i)
            {
                tau_bilinear += bw[i] * tau[i];
            }
            // behind every caster: the bilinear tau itself (texels without a caster hold 1)
            const float behind = throughTap(bw, z, 0.95f, tau_bilinear);
            check(std::fabs(behind - tau_bilinear) < 1e-6f || casters == 0, "behind every caster: the bilinear tau", behind, tau_bilinear);
            // in front of every caster: exactly 1
            check(throughTap(bw, z, 0.1f, tau_bilinear) == 1.f, "in front of every caster: exactly 1");
            // mixed: between the two
            const float mid = throughTap(bw, z, 0.45f, tau_bilinear);
            check(mid >= std::min(tau_bilinear, 1.f) - 1e-6f && mid <= 1.f, "a mixed footprint lies between", mid, tau_bilinear);
        }
        // a footprint with no caster: 1 exactly, and a visibility multiplied by it keeps its bits (the opaque-only tile is unchanged)
        const float bw[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        const float none[4] = { kNoLayer, kNoLayer, kNoLayer, kNoLayer };
        check(frontShare(bw, none, 0.9f) == 0.f, "no caster: no share in front");
        const float one = throughTap(bw, none, 0.9f, 1.f);
        check(one == 1.f, "no caster: the tap is 1 exactly");
        for (float v : { 0.f, 0.123456789f, 0.5f, 0.999999f, 1.f })
        {
            check(sameBits(v * one, v), "a visibility times the empty tap keeps its bits", v);
        }
        // a receiver on the pane itself (its reference at the caster's depth) is in front: the strict test, no tint
        const float pane[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        check(throughTap(bw, pane, 0.5f, 0.4f) == 1.f, "a receiver at the caster's depth is not behind it");
    }

    void testDepth()
    {
        // the 16-bit store: whichever neighbour the conversion picks, never nearer than the caster (to a millionth of a step) and at
        // most two steps farther; the far plane stays the "no caster" value
        std::mt19937 rng(11);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        double worst_near = 0.0, worst_far = 0.0;
        for (int n = 0; n < 200000; ++n)
        {
            const float z = n < 65536 ? float(n) / 65535.f : u(rng); // every exact step, then random depths
            if (z >= 1.f)
            {
                continue;
            }
            const double x = double(storedDepth(z)) * 65535.0;
            for (double q : { std::floor(x), std::ceil(x) })
            {
                const double stored = std::min(q, 65535.0) / 65535.0;
                worst_near = std::max(worst_near, (double(z) - stored) * 65535.0);
                worst_far = std::max(worst_far, (stored - double(z)) * 65535.0);
            }
        }
        check(worst_near < 1e-3, "the stored depth is never nearer than the caster (steps)", worst_near);
        check(worst_far <= 2.0 + 1e-3, "at most two steps farther", worst_far);
        check(storedDepth(1.f) == kNoLayer && storedDepth(std::nextafter(1.f, 0.f)) == kNoLayer, "the far plane stays no caster");
        check(storedDepth(-1.f) == kDepthStep, "a negative depth clamps to the near plane");
    }

    void testMemory()
    {
        check(layerBytes(2048) == 24ull << 20, "24 MiB at 2048", double(layerBytes(2048) >> 20));
        check(layerBytes(4096) == 96ull << 20, "96 MiB at 4096", double(layerBytes(4096) >> 20));
        check(layerBytes(8192) == 384ull << 20, "384 MiB at 8192", double(layerBytes(8192) >> 20));
        check(layerBytes(llprojectorpool::kAtlasDefault) * 4 == llprojectorpool::atlasBytes(llprojectorpool::kAtlasDefault) * 3,
              "the layer is 3/4 of the two depth atlases");
    }

    // A model of the layer: one value per texel (the key of the tile whose caster is there, 0: "no caster"), and the carry applied as
    // the GL half does it: every copy's source read first, then the clears, then the copies written.
    struct Model
    {
        uint32_t n;
        std::vector<uint32_t> t;
        explicit Model(uint32_t edge) : n(edge), t(size_t(edge) * edge, 0u) {}
        void fill(const Rect& r, uint32_t v)
        {
            for (uint32_t y = r.y; y < r.y + r.size; ++y)
                for (uint32_t x = r.x; x < r.x + r.size; ++x)
                    t[size_t(y) * n + x] = v;
        }
        std::vector<uint32_t> read(const Rect& r) const
        {
            std::vector<uint32_t> out;
            for (uint32_t y = r.y; y < r.y + r.size; ++y)
                for (uint32_t x = r.x; x < r.x + r.size; ++x)
                    out.push_back(t[size_t(y) * n + x]);
            return out;
        }
        void write(const Rect& r, const std::vector<uint32_t>& v)
        {
            size_t i = 0;
            for (uint32_t y = r.y; y < r.y + r.size; ++y)
                for (uint32_t x = r.x; x < r.x + r.size; ++x)
                    t[size_t(y) * n + x] = v[i++];
        }
        bool all(const Rect& r, uint32_t v) const
        {
            for (uint32_t x : read(r))
                if (x != v)
                    return false;
            return true;
        }
    };

    void apply(Model& m, const CarryPlan& plan)
    {
        std::vector<std::vector<uint32_t>> held;
        for (const Move& mv : plan.copies)
        {
            held.push_back(m.read(mv.src));
        }
        for (const Rect& r : plan.clears)
        {
            m.fill(r, 0u);
        }
        for (size_t i = 0; i < plan.copies.size(); ++i)
        {
            m.write(plan.copies[i].dst, held[i]);
        }
    }

    void testCarry()
    {
        // tiles of 4 texels in a 16-texel layer: A and B swap (a cycle), C moves into D's old rect while D moves on (a chain), E holds
        // no caster and moves into a rect that held a caster, F stays
        const Rect r0{ 0, 0, 4 }, r1{ 4, 0, 4 }, r2{ 8, 0, 4 }, r3{ 12, 0, 4 }, r4{ 0, 4, 4 }, r5{ 4, 4, 4 }, r6{ 8, 4, 4 };
        std::map<uint32_t, bool> holds{ { 1, true }, { 2, true }, { 3, true }, { 4, true }, { 5, false }, { 6, true } };
        Model m(16);
        m.fill(r0, 1); // A
        m.fill(r1, 2); // B
        m.fill(r2, 3); // C
        m.fill(r3, 4); // D
        m.fill(r4, 0); // E: no caster
        m.fill(r5, 6); // F
        m.fill(r6, 9); // a free rect that held a caster of a tile since dropped
        const std::vector<Move> moves{
            { 1, r0, r1, true }, { 2, r1, r0, true },          // swap
            { 3, r2, r3, true }, { 4, r3, r6, true },          // chain
            { 5, r4, r2, true },                               // E (no caster) into C's old rect
            { 6, r5, r5, true },                               // F stays
        };
        const CarryPlan plan = planCarry(moves, [&](uint32_t key) { return holds[key]; });
        check(plan.copies.size() == 4, "four tiles with a caster move", double(plan.copies.size()));
        check(plan.clears.size() == 1 && plan.clears[0] == r2, "the moved tile without a caster gets its new rect cleared");
        apply(m, plan);
        check(m.all(r1, 1) && m.all(r0, 2), "a swap is carried");
        check(m.all(r3, 3) && m.all(r6, 4), "a chain is carried");
        check(m.all(r2, 0), "the moved tile without a caster reads no caster");
        check(m.all(r5, 6), "a tile that stays keeps its layer");
        // the naive order (copy in place, move by move) loses a cycle: the reason for the two phases
        Model naive(16);
        naive.fill(r0, 1);
        naive.fill(r1, 2);
        for (const Move& mv : { moves[0], moves[1] })
        {
            naive.write(mv.dst, naive.read(mv.src));
        }
        check(!(naive.all(r1, 1) && naive.all(r0, 2)), "in-place copies would lose the swap");

        // random repacks of the pool's own planner: every tile that holds a caster ends with its content at its new rect, every other
        // drawn tile's new rect reads no caster
        std::mt19937 rng(3);
        for (int round = 0; round < 200; ++round)
        {
            const uint32_t atlas = 256;
            llprojectorpool::BuddyAtlas buddy(atlas);
            std::vector<llprojectorpool::LayoutRequest> requests;
            std::map<uint32_t, bool> h;
            Model layer(atlas);
            for (uint32_t key = 1; key <= 24; ++key)
            {
                const uint32_t size = 16u << (rng() % 3);
                Rect r;
                if (!buddy.alloc(size, r))
                {
                    continue;
                }
                llprojectorpool::LayoutRequest q;
                q.key = key;
                q.size = size;
                q.has_old = true;
                q.old_rect = r;
                q.drawn = true;
                requests.push_back(q);
                h[key] = (rng() % 2) == 0;
                layer.fill(r, h[key] ? key : 0u);
            }
            // drop every other tile (their rects keep stale casters), then repack the rest
            std::vector<llprojectorpool::LayoutRequest> kept;
            for (size_t i = 0; i < requests.size(); ++i)
            {
                if (i % 2 == 0)
                {
                    kept.push_back(requests[i]);
                }
                else
                {
                    layer.fill(requests[i].old_rect, 1000u + requests[i].key); // stale
                }
            }
            const llprojectorpool::RepackPlan rp = llprojectorpool::planRepack(kept, atlas);
            check(rp.ok, "the repack fits");
            const CarryPlan plan = planCarry(rp.moves, [&](uint32_t key) { return h[key]; });
            apply(layer, plan);
            for (size_t i = 0; i < kept.size(); ++i)
            {
                const uint32_t key = kept[i].key;
                check(layer.all(rp.placed[i], h[key] ? key : 0u), "every drawn tile reads its own layer after the repack", key);
            }
        }
    }
}

int main()
{
    testRule();
    testStack();
    testTap();
    testDepth();
    testMemory();
    testCarry();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("lllightthrough_test: all checks passed\n");
    return 0;
}
