// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llstochasticlayerrank.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llstochasticlayerrank_test.cpp
// Four coincident blended faces of one object (the layers of a mesh eye, stacked decals; static or rigged,
// the rule is the same) against the creator's face order, through a model of the stochastic passes on a
// 24-bit depth buffer:
//   stochastic depth: each face writes its depth (plus its polygon offset) at the samples its mask covers,
//                     depth test LEQUAL, in any draw order;
//   accumulation:     the ordinary program, every sample, a face passes where its depth (with its offset)
//                     is at or in front of the stochastic depth;
//   glow:             like the accumulation, with the glow's offset.
// The noise is the difference between a face's depth in the accumulation or the glow and its depth in the
// stochastic depth (other programs, other vertex shader files). Without the rank all four faces pass
// everywhere (the pixel is their average, the bug); with it only the faces no later face covers pass, from
// -2 to +1 units of noise in the accumulation and from -2 to +2 in the glow.

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
    constexpr uint32_t ALL = (1u << SAMPLES) - 1;

    int32_t unitsOf(float base, unsigned rank)
    {
        return (int32_t)LLStochasticLayerRank::units(base, rank);
    }

    // the ranks of n faces given as an array in the creator's order, as the viewer's run of one object's faces
    void assignArray(const LLStochasticLayerRank::Box* boxes, std::size_t n, uint8_t* ranks)
    {
        LLStochasticLayerRank::assign(n,
            [&](std::size_t i) { return boxes[i]; },
            [&](std::size_t i, uint8_t rank) { ranks[i] = rank; });
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

    // every face passes exactly at the samples no later face covers (where the face itself is the top layer,
    // and where nothing covers the sample)
    bool exactlyFaceOrder(const std::array<uint32_t, FACES>& mask, const std::array<uint32_t, FACES>& got)
    {
        for (int f = 0; f < FACES; ++f)
        {
            uint32_t later = 0;
            for (int g = f + 1; g < FACES; ++g)
            {
                later |= mask[g];
            }
            if (got[f] != (ALL & ~later))
            {
                return false;
            }
        }
        return true;
    }

    void test_ranks()
    {
        using namespace LLStochasticLayerRank;
        const Box eye = { { -1, -1, 0 }, { 1, 1, 0.5f } };
        const Box lid = { { -1, -1, 0 }, { 1, 1, 0.6f } };
        // iris, pupil, a bigger lid layer, a decal on the iris: faces 0, 1 and 3 coincide
        const Box boxes[4] = { eye, eye, lid, eye };
        uint8_t ranks[4];
        assignArray(boxes, 4, ranks);
        check(ranks[0] == 0 && ranks[1] == 1 && ranks[2] == 0 && ranks[3] == 2, "rank = coincident faces before it, in face order");

        Box lone[3] = { eye, lid, { { 0, 0, 0 }, { 1, 1, 1 } } };
        assignArray(lone, 3, ranks);
        check(ranks[0] == 0 && ranks[1] == 0 && ranks[2] == 0, "faces with distinct extents have no rank");

        const float nan = std::numeric_limits<float>::quiet_NaN();
        const Box bad = { { nan, nan, nan }, { nan, nan, nan } };
        const Box with_nan[2] = { bad, bad };
        assignArray(with_nan, 2, ranks);
        check(ranks[0] == 0 && ranks[1] == 0, "a face without extents coincides with nothing");

        Box many[40];
        uint8_t big[40];
        std::fill(many, many + 40, eye);
        assignArray(many, 40, big);
        check(MAX_RANK == 7 && big[6] == 6 && big[7] == MAX_RANK && big[39] == MAX_RANK,
              "the rank is clamped at 7 (a mesh has at most 8 faces)");

        check(units(UNITS_DEPTH, 0) == -1.f && units(UNITS_ACCUM, 0) == -2.f && units(UNITS_GLOW, 0) == -3.f,
              "rank 0 keeps the passes' offsets (-1, -2, -3)");
        check(units(UNITS_DEPTH, 3) == -16.f && units(UNITS_ACCUM, 3) == -17.f && units(UNITS_GLOW, 3) == -18.f,
              "five units per rank: -1 - 5r, -2 - 5r, -3 - 5r");
        check(units(UNITS_DEPTH, MAX_RANK) == -36.f, "the largest pull is 36 units in front of the surface");
    }

    void test_four_coincident_faces()
    {
        using namespace LLStochasticLayerRank;

        // creator's order 0 < 1 < 2 < 3, alpha 1: every face covers every sample
        const std::array<uint32_t, FACES> opaque = { ALL, ALL, ALL, ALL };
        const std::array<unsigned, FACES> unranked = { 0, 0, 0, 0 };
        const std::array<unsigned, FACES> ranked = { 0, 1, 2, 3 };

        std::array<int, FACES> order = { 0, 1, 2, 3 };
        bool ordersAgree = true;
        bool bugShown = false;
        do
        {
            // today: all four pass at every sample, the pixel is the average of the four
            const auto before = passes(unranked, opaque, order, UNITS_ACCUM, 0);
            bugShown = before[0] == ALL && before[1] == ALL && before[2] == ALL && before[3] == ALL;
            // the rank: only the last face passes anywhere
            const auto after = passes(ranked, opaque, order, UNITS_ACCUM, 0);
            ordersAgree = ordersAgree && after[0] == 0 && after[1] == 0 && after[2] == 0 && after[3] == ALL;
        } while (std::next_permutation(order.begin(), order.end()));
        check(bugShown, "without the rank all four coincident faces share every sample (the average)");
        check(ordersAgree, "with the rank the face last in the creator's order wins every sample, in all 24 draw orders");

        // the margins (alpha 1): the winner passes while the noise is at most +1 in the accumulation and +2 in
        // the glow (as without the rank); every face below it, the one just below included, fails from -2
        // units of noise up, in both
        bool accumHolds = true;
        for (int32_t noise = -2; noise <= 1; ++noise)
        {
            const auto got = passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, noise);
            accumHolds = accumHolds && got[3] == ALL && got[2] == 0 && got[1] == 0 && got[0] == 0;
        }
        check(accumHolds, "accumulation, -2 to +1 units of noise: the winner on every sample, the faces below on none");
        bool glowHolds = true;
        for (int32_t noise = -2; noise <= 2; ++noise)
        {
            const auto got = passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, noise);
            glowHolds = glowHolds && got[3] == ALL && got[2] == 0 && got[1] == 0 && got[0] == 0;
        }
        check(glowHolds, "glow, -2 to +2 units of noise: the winner's glow on every sample, none from the faces below");
        // where the margins end: the face one rank below shows at -4 in the accumulation and at -3 in the glow
        check(passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, -3)[2] == 0 &&
              passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_ACCUM, -4)[2] == ALL,
              "the accumulation of the face one below holds to -3 units");
        check(passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, -2)[2] == 0 &&
              passes(ranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, -3)[2] == ALL,
              "the glow of the face one below holds to -2 units");
        // what the two units per rank of the first version left: the glow one rank below on every sample at no noise
        {
            const int32_t stored = SURFACE + (int32_t)(UNITS_DEPTH - 2.f * 1.f);
            const int32_t below_glow = SURFACE + (int32_t)(UNITS_GLOW - 2.f * 0.f);
            check(below_glow <= stored, "two units per rank let the glow of the face one below through (the fixed leak)");
        }
        const auto glow0 = passes(unranked, opaque, { 0, 1, 2, 3 }, UNITS_GLOW, 0);
        check(glow0[0] == ALL, "unranked faces keep today's glow offset");

        // a winner that covers some samples: the faces below show where it does not, each where no later face
        // covers, in every draw order and through the margins
        const std::array<uint32_t, FACES> partial = { ALL, 0x3Cu, 0x0Fu, 0x81u };
        order = { 0, 1, 2, 3 };
        bool exact = true;
        do
        {
            for (int32_t noise = -2; noise <= 1; ++noise)
            {
                exact = exact && exactlyFaceOrder(partial, passes(ranked, partial, order, UNITS_ACCUM, noise));
            }
            for (int32_t noise = -2; noise <= 2; ++noise)
            {
                exact = exact && exactlyFaceOrder(partial, passes(ranked, partial, order, UNITS_GLOW, noise));
            }
        } while (std::next_permutation(order.begin(), order.end()));
        check(exact, "a face passes exactly at the samples no later face covers (partial coverage, all orders, the margins)");
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
