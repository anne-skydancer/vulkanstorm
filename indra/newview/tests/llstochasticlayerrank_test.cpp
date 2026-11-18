// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llstochasticlayerrank.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llstochasticlayerrank_test.cpp
// Four coincident blended faces of one object (the layers of a mesh eye, stacked decals) against the
// creator's face order, through a model of the three stochastic passes on a 24-bit depth buffer:
//   stochastic depth: each face writes its depth (plus its polygon offset) at the samples its mask covers,
//                     depth test LEQUAL, in any draw order;
//   accumulation:     the ordinary program, every sample, a face passes where its depth (with its offset)
//                     is at or in front of the stochastic depth;
//   glow:             like the accumulation, with the glow's offset.
// Without the rank all four faces pass everywhere (the pixel is their average, the bug); with it only the
// faces no later face covers pass.

#include "llstochasticlayerrank.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>

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

    constexpr int SAMPLES = 8;
    constexpr int FACES = 4;
    constexpr int32_t SURFACE = 1 << 23;       // a depth value of the 24-bit buffer, the same for every coincident face
    constexpr int32_t FAR_DEPTH = (1 << 24) - 1;

    int32_t unitsOf(float base, unsigned rank)
    {
        return (int32_t)LLStochasticLayerRank::units(base, rank);
    }

    // The samples a face passes in the accumulation (or the glow), given the faces' ranks and coverage masks
    // and the order the depth pass drew them in (the result must not depend on it).
    std::array<uint32_t, FACES> passes(const std::array<unsigned, FACES>& rank, const std::array<uint32_t, FACES>& mask,
                                       const std::array<int, FACES>& order, float base, int32_t noise)
    {
        std::array<int32_t, SAMPLES> stored;
        stored.fill(FAR_DEPTH);
        for (int f : order)
        {
            const int32_t z = SURFACE + unitsOf(LLStochasticLayerRank::UNITS_DEPTH, rank[f]);
            for (int s = 0; s < SAMPLES; ++s)
            {
                if ((mask[f] >> s & 1) && z <= stored[s])
                {
                    stored[s] = z;
                }
            }
        }
        std::array<uint32_t, FACES> out{};
        for (int f = 0; f < FACES; ++f)
        {
            const int32_t z = SURFACE + unitsOf(base, rank[f]) + noise;
            for (int s = 0; s < SAMPLES; ++s)
            {
                if (z <= stored[s])
                {
                    out[f] |= 1u << s;
                }
            }
        }
        return out;
    }

    void test_ranks()
    {
        using namespace LLStochasticLayerRank;
        const Box eye = { { -1, -1, 0 }, { 1, 1, 0.5f } };
        const Box lid = { { -1, -1, 0 }, { 1, 1, 0.6f } };
        // iris, pupil, a bigger lid layer, a decal on the iris: faces 0, 1 and 3 coincide
        const Box boxes[4] = { eye, eye, lid, eye };
        uint8_t ranks[4];
        assign(boxes, 4, ranks);
        check(ranks[0] == 0 && ranks[1] == 1 && ranks[2] == 0 && ranks[3] == 2, "rank = coincident faces before it, in face order");

        Box lone[3] = { eye, lid, { { 0, 0, 0 }, { 1, 1, 1 } } };
        assign(lone, 3, ranks);
        check(ranks[0] == 0 && ranks[1] == 0 && ranks[2] == 0, "faces with distinct extents have no rank");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Box bad = { { nan, nan, nan }, { nan, nan, nan } };
        const Box with_nan[2] = { bad, bad };
        assign(with_nan, 2, ranks);
        check(ranks[0] == 0 && ranks[1] == 0, "a face without extents coincides with nothing");

        Box many[40];
        uint8_t big[40];
        std::fill(many, many + 40, eye);
        assign(many, 40, big);
        check(big[39] == MAX_RANK && big[31] == MAX_RANK && big[30] == 30, "the rank is clamped");

        check(units(UNITS_DEPTH, 0) == -1.f && units(UNITS_ACCUM, 0) == -2.f && units(UNITS_GLOW, 0) == -3.f,
              "rank 0 keeps the passes' offsets (-1, -2, -3)");
        check(units(UNITS_DEPTH, 3) == -7.f && units(UNITS_ACCUM, 3) == -8.f && units(UNITS_GLOW, 3) == -9.f,
              "two units per rank: -1 - 2r, -2 - 2r, -3 - 2r");
    }

    void test_four_coincident_faces()
    {
        using namespace LLStochasticLayerRank;
        const uint32_t all = (1u << SAMPLES) - 1;

        // creator's order 0 < 1 < 2 < 3, alpha 1: every face covers every sample
        const std::array<uint32_t, FACES> opaque = { all, all, all, all };
        const std::array<unsigned, FACES> unranked = { 0, 0, 0, 0 };
        const std::array<unsigned, FACES> ranked = { 0, 1, 2, 3 };

        std::array<int, FACES> order = { 0, 1, 2, 3 };
        bool ordersAgree = true;
        bool bugShown = false;
        do
        {
            // today: all four pass at every sample, the pixel is the average of the four
            const auto before = passes(unranked, opaque, order, UNITS_ACCUM, 0);
            bugShown = before[0] == all && before[1] == all && before[2] == all && before[3] == all;
            // the rank: only the last face passes anywhere
            const auto after = passes(ranked, opaque, order, UNITS_ACCUM, 0);
            ordersAgree = ordersAgree && after[0] == 0 && after[1] == 0 && after[2] == 0 && after[3] == all;
        } while (std::next_permutation(order.begin(), order.end()));
        check(bugShown, "without the rank all four coincident faces share every sample (the average)");
        check(ordersAgree, "with the rank the face last in the creator's order wins every sample, in all 24 draw orders");

        // a winner that covers some samples: the faces below show where it does not, each where no later face covers
        const std::array<uint32_t, FACES> partial = { all, 0x3Cu, 0x0Fu, 0x81u };
        order = { 0, 1, 2, 3 };
        bool exact = true;
        do
        {
            const auto got = passes(ranked, partial, order, UNITS_ACCUM, 0);
            for (int f = 0; f < FACES; ++f)
            {
                uint32_t later = 0;
                for (int g = f + 1; g < FACES; ++g)
                {
                    later |= partial[g];
                }
                exact = exact && got[f] == (all & ~later);
            }
        } while (std::next_permutation(order.begin(), order.end()));
        check(exact, "a face passes exactly at the samples no later face covers (partial coverage, all orders)");

        // the accumulation's margin: the winner passes whatever the last bit of its depth (one unit either way)
        check(passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, 0)[3] == all, "the winner passes at 0 units of depth noise");
        check(passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, 1)[3] == all, "the winner passes at +1 unit of depth noise");
        check(passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, 0)[2] == 0, "the face one rank below fails at the winner's samples");

        // the glow, other vertex shaders: the winner's glow passes at +1 unit; layers two or more below do not
        const auto glow = passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, 1);
        check(glow[3] == all, "the winner's glow passes at +1 unit of depth noise");
        check(glow[1] == 0 && glow[0] == 0, "the glow of a layer two or more ranks below does not show through");
        const auto glow0 = passes(unranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, 0);
        check(glow0[0] == all, "unranked faces keep today's glow offset");
    }
}

int main()
{
    test_ranks();
    test_four_coincident_faces();
    if (failures == 0)
    {
        std::printf("llstochasticlayerrank_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
