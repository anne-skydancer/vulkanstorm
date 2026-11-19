// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lllightthrough.h, lamp shadows L3 (no viewer libraries, no GL):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lllightthrough_test.cpp
//   Rule       the light through one face: 1 at a = 0, 0 at a = 1, 1 - a for a black face, 1 - a^2 for a white one, never above
//              1, falling with the opacity and rising with the colour; the red pane and clear glass of the plan; the step at the solid
//              switch (0.98) at most 0.04; the invisible limit
//   Stack      stacked faces multiply, in any order
//   Reference  the receiver's reference at a texel is the depth lookup's: lowered by the receiver plane, never raised, at most rp_max
//   Tap        a footprint behind every caster and unblocked gets the casters' bilinear tau exactly, one in front of them all or with
//              none gets exactly its open weight (the filter's 1); a texel the depth atlas blocks leaves the tap (a frame's penumbra is
//              not brightened, whether or not the pane runs on under the frame); a mixed footprint lies between; the combine
//   Memory     32, 128 and 512 MiB at the 2048, 4096 and 8192 atlases: as much as the two depth atlases
//   Carry      a repack's moves: swaps, chains and cycles carried by the two-phase plan, applied to a model atlas; moved tiles without
//              a caster and resized tiles cleared, tiles that stay untouched

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

    void testRef()
    {
        // the depth lookup's reference (stSpotQuadTile): the receiver's depth less the bias, lowered by the plane, never raised
        check(texelRef(0.9f, 0.01f, 0.f, 0.05f) == 0.9f - 0.01f, "a flat receiver: its depth less the bias");
        check(texelRef(0.9f, 0.01f, 0.02f, 0.05f) == 0.9f - 0.01f, "a plane farther at the texel: never raised");
        check(texelRef(0.9f, 0.01f, -0.02f, 0.05f) == 0.9f - 0.01f + -0.02f, "a plane nearer at the texel: lowered by it");
        check(texelRef(0.9f, 0.01f, -0.5f, 0.05f) == 0.9f - 0.01f + -0.05f, "lowered by at most rp_max");
    }

    // the exact light through the open texels of a footprint: each open texel lets through its tau if its caster is in front of the
    // receiver, else all; a blocked texel lets through nothing and is not counted
    float exactTap(const float bw[4], const float tau[4], const float zt[4], const float s[4], const float ref[4], float& open)
    {
        float lit = 0.f;
        open = 0.f;
        for (int i = 0; i < 4; ++i)
        {
            if (s[i] >= ref[i])
            {
                open += bw[i];
                lit += bw[i] * (zt[i] < ref[i] ? tau[i] : 1.f);
            }
        }
        return lit;
    }

    void testTap()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> u(0.f, 1.f);
        for (int n = 0; n < 20000; ++n)
        {
            const float fx = u(rng), fy = u(rng);
            const float bw[4] = { (1.f - fx) * fy, fx * fy, fx * (1.f - fy), (1.f - fx) * (1.f - fy) };
            float tau[4], zt[4], s[4], ref[4];
            for (int i = 0; i < 4; ++i)
            {
                const bool has = u(rng) < 0.6f;
                tau[i] = has ? transmittance(u(rng), u(rng)) : 1.f;
                zt[i] = has ? 0.2f + 0.5f * u(rng) : kNoLayer;
                ref[i] = 0.95f - 0.01f * u(rng);
                s[i] = 1.f; // nothing opaque in front of the receiver
            }
            float tb = 0.f;
            for (int i = 0; i < 4; ++i)
            {
                tb += bw[i] * tau[i];
            }
            // behind every caster, nothing blocked: the casters' bilinear tau itself, and the tap's whole weight
            const Tap behind = throughTap(bw, zt, s, ref, tb);
            check(std::fabs(behind.lit - tb) < 1e-6f, "behind every caster: the bilinear tau", behind.lit, tb);
            check(std::fabs(behind.open - 1.f) < 1e-6f, "nothing blocked: the whole weight", behind.open);
            // in front of every caster: exactly its open weight (the filter's 1)
            const float front_ref[4] = { 0.1f, 0.1f, 0.1f, 0.1f };
            const Tap front = throughTap(bw, zt, s, front_ref, tb);
            check(front.lit == front.open, "in front of every caster: lit is the open weight exactly", front.lit, front.open);
            // opaque texels in front of the receiver, holding no caster there (the depth test keeps the layer out from under them, or
            // the pane stops at the frame): they leave the tap, and the rest is exact
            float s_frame[4], zt_frame[4], tau_frame[4];
            for (int i = 0; i < 4; ++i)
            {
                const bool blocked = u(rng) < 0.4f;
                s_frame[i] = blocked ? 0.15f : 1.f;
                zt_frame[i] = blocked ? kNoLayer : zt[i];
                tau_frame[i] = blocked ? 1.f : tau[i];
            }
            float tb_frame = 0.f;
            for (int i = 0; i < 4; ++i)
            {
                tb_frame += bw[i] * tau_frame[i];
            }
            float open = 0.f;
            const float exact = exactTap(bw, tau_frame, zt_frame, s_frame, ref, open);
            const Tap framed = throughTap(bw, zt_frame, s_frame, ref, tb_frame);
            check(std::fabs(framed.open - open) < 1e-6f, "the open weight is the unblocked texels'", framed.open, open);
            check(std::fabs(framed.lit - exact) < 2e-6f, "a frame without a pane under it: exact over the open texels", framed.lit, exact);
            // mixed (some casters behind the receiver, glass in front of opaque texels): lit within [0, open]
            const float mixed_ref[4] = { 0.45f, 0.45f, 0.45f, 0.45f };
            float mixed_s[4];
            for (int i = 0; i < 4; ++i)
            {
                mixed_s[i] = u(rng) < 0.3f ? 0.4f : 1.f;
            }
            const Tap mixed = throughTap(bw, zt, mixed_s, mixed_ref, tb);
            check(mixed.lit >= -1e-6f && mixed.lit <= mixed.open + 1e-6f, "a mixed footprint: lit within [0, open]", mixed.lit, mixed.open);
            const float value = combine(mixed.lit, mixed.open);
            check(value >= 0.f && value <= 1.f, "the combine within [0, 1]", value);
        }
        // a footprint with no caster: lit is the open weight exactly, the combine 1 exactly, and a visibility multiplied by it keeps its
        // bits (a tile cleared to no caster is the depth's alone)
        const float bw[4] = { 0.1f, 0.2f, 0.3f, 0.4f };
        const float none[4] = { kNoLayer, kNoLayer, kNoLayer, kNoLayer };
        const float clear[4] = { 1.f, 1.f, 1.f, 1.f };
        const float ref[4] = { 0.9f, 0.9f, 0.9f, 0.9f };
        const Tap empty = throughTap(bw, none, clear, ref, 1.f);
        check(empty.lit == empty.open, "no caster: lit is the open weight exactly");
        float lit = 0.f, open = 0.f;
        for (int k = 0; k < 16; ++k)
        {
            const float w = 1.f - 0.9f * (float(k) + 0.5f) / 16.f;
            lit += w * empty.lit;
            open += w * empty.open;
        }
        const float one = combine(lit, open);
        check(one == 1.f, "no caster: the filter is 1 exactly");
        for (float v : { 0.f, 0.123456789f, 0.5f, 0.999999f, 1.f })
        {
            check(sameBits(v * one, v), "a visibility times the empty filter keeps its bits", v);
        }
        // a receiver on the pane itself (its reference at the caster's depth) is in front: the strict test, no tint
        const float pane[4] = { 0.5f, 0.5f, 0.5f, 0.5f };
        const Tap on = throughTap(bw, pane, clear, pane, 0.4f);
        check(on.lit == on.open, "a receiver at the caster's depth is not behind it");
        // the frame's penumbra (the reason for the open weight): half the footprint under an opaque bar with no pane under it, half a
        // pane at 0.5 in front of the receiver. The tap passes the pane's 0.5 on its open half; without the weight it would read
        // (0.5 + 1) / 2 = 0.75 there and brighten the inside of the penumbra
        const float half_bw[4] = { 0.25f, 0.25f, 0.25f, 0.25f };
        const float half_zt[4] = { 0.4f, 0.4f, kNoLayer, kNoLayer };
        const float half_s[4] = { 1.f, 1.f, 0.3f, 0.3f };
        const float half_tb = 0.5f * 0.5f + 0.5f * 1.f;
        const Tap half = throughTap(half_bw, half_zt, half_s, ref, half_tb);
        check(std::fabs(combine(half.lit, half.open) - 0.5f) < 1e-6f, "the frame's penumbra passes the pane's own light", combine(half.lit, half.open));
        // no open tap: the filter has nothing to say (1)
        check(combine(0.f, 0.f) == 1.f, "no open tap: 1");
        check(combine(-1e-7f, 0.5f) == 0.f && combine(0.6f, 0.5f) == 1.f, "the combine is clamped to [0, 1]");
    }

    void testMemory()
    {
        check(layerBytes(2048) == 32ull << 20, "32 MiB at 2048", double(layerBytes(2048) >> 20));
        check(layerBytes(4096) == 128ull << 20, "128 MiB at 4096", double(layerBytes(4096) >> 20));
        check(layerBytes(8192) == 512ull << 20, "512 MiB at 8192", double(layerBytes(8192) >> 20));
        for (uint32_t edge : { 2048u, 4096u, 8192u })
        {
            check(layerBytes(edge) == llprojectorpool::atlasBytes(edge), "the layer is as big as the two depth atlases", edge);
        }
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
        // a tile whose size changes in the move has no content at its new size: its new rect is cleared, never copied (a copy of a
        // larger source would run over the neighbours of a smaller rect)
        const Rect big{ 0, 0, 8 }, small{ 8, 8, 4 };
        const std::vector<Move> resized{ { 7, big, small, false } };
        const CarryPlan resized_plan = planCarry(resized, [](uint32_t) { return true; });
        check(resized_plan.copies.empty() && resized_plan.clears.size() == 1 && resized_plan.clears[0] == small, "a resized tile is cleared at its new rect, not copied");
        const std::vector<Move> mislabelled{ { 8, big, small, true } };
        const CarryPlan mislabelled_plan = planCarry(mislabelled, [](uint32_t) { return true; });
        check(mislabelled_plan.copies.empty() && mislabelled_plan.clears.size() == 1, "a move between rects of two sizes is never copied, whatever its flag");
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
            const uint32_t atlas = 512;
            llprojectorpool::BuddyAtlas buddy(atlas);
            std::vector<llprojectorpool::LayoutRequest> requests;
            std::map<uint32_t, bool> h;
            Model layer(atlas);
            for (uint32_t key = 1; key <= 24; ++key)
            {
                const uint32_t size = llprojectorpool::kMinTile << (rng() % 3); // 64, 128, 256: sizes the buddy atlas takes
                Rect r;
                if (!buddy.alloc(size, r))
                {
                    continue;
                }
                llprojectorpool::LayoutRequest q;
                q.key = key;
                q.size = (rng() % 4) == 0 ? std::max(llprojectorpool::kMinTile, size >> 1) : size; // a quarter shrink in the repack (or keep the floor)
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
            const CarryPlan carried = planCarry(rp.moves, [&](uint32_t key) { return h[key]; });
            apply(layer, carried);
            for (size_t i = 0; i < kept.size(); ++i)
            {
                const uint32_t key = kept[i].key;
                const bool same_size = kept[i].size == kept[i].old_rect.size;
                check(layer.all(rp.placed[i], h[key] && same_size ? key : 0u), "every drawn tile reads its own layer after the repack, a resized one none", key);
            }
        }
    }
}

int main()
{
    testRule();
    testStack();
    testRef();
    testTap();
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
