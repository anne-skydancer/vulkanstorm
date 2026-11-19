// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the image comparison behind the in-frame identical-image check (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/llrender indra/llrender/tests/llglsubmitimage_test.cpp
// The comparison is the gate of every stage of the OpenGL modernisation, so it has to see one unit of
// difference in one channel of one pixel, in 8 bit and in float images, and report where.

#include "llglsubmitstats.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

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

    float fromBits(uint32_t bits)
    {
        float f;
        std::memcpy(&f, &bits, 4);
        return f;
    }

    uint32_t toBits(float f)
    {
        uint32_t b;
        std::memcpy(&b, &f, 4);
        return b;
    }
}

int main()
{
    using namespace LLGLSubmitImage;

    // 8 bit RGBA, 5 x 3
    {
        std::vector<uint8_t> a(5 * 3 * 4, 7), b = a;
        Diff d = compare(a.data(), b.data(), 5, 3, 4, false);
        check(d.identical() && d.pixels == 15 && d.firstX == -1, "identical 8 bit images");

        b[(1 * 5 + 3) * 4 + 2] = 8; // pixel (3, 1), blue, one unit
        d = compare(a.data(), b.data(), 5, 3, 4, false);
        check(!d.identical() && d.mismatchedPixels == 1, "one unit in one channel is seen");
        check(d.firstX == 3 && d.firstY == 1, "and located");
        check(d.maxDiff[2] == 1 && d.maxDiff[0] == 0 && d.maxDiff[1] == 0 && d.maxDiff[3] == 0, "in the right channel");

        b[0] = 200; // pixel (0, 0), red
        d = compare(a.data(), b.data(), 5, 3, 4, false);
        check(d.mismatchedPixels == 2 && d.firstX == 0 && d.firstY == 0 && d.maxDiff[0] == 193, "two pixels, the first in scan order");
    }

    // float RGBA: the lowest bit of the mantissa is one ulp
    {
        std::vector<float> a(4 * 4 * 4, 0.5f), b = a;
        Diff d = compare(a.data(), b.data(), 4, 4, 4, true);
        check(d.identical(), "identical float images");

        b[(2 * 4 + 1) * 4 + 0] = fromBits(toBits(0.5f) + 1);
        d = compare(a.data(), b.data(), 4, 4, 4, true);
        check(d.mismatchedPixels == 1 && d.maxDiff[0] == 1 && d.firstX == 1 && d.firstY == 2, "one ulp is seen and located");
    }

    // float depth, one channel: the ulp distance across a power of two and across zero
    {
        std::vector<float> a(2 * 2, 1.0f), b = a;
        b[3] = fromBits(toBits(1.0f) - 1); // the float just below 1.0
        Diff d = compare(a.data(), b.data(), 2, 2, 1, true);
        check(d.mismatchedPixels == 1 && d.maxDiff[0] == 1, "one ulp across a power of two");

        std::vector<float> z(1, 0.0f), nz(1, -0.0f);
        d = compare(z.data(), nz.data(), 1, 1, 1, true);
        check(d.mismatchedPixels == 1 && d.maxDiff[0] == 1, "+0 and -0 differ by one (bitwise, not numerically)");

        std::vector<float> n1(1, fromBits(0x7fc00001u)), n2(1, fromBits(0x7fc00002u));
        d = compare(n1.data(), n2.data(), 1, 1, 1, true);
        check(d.mismatchedPixels == 1, "NaNs with different payloads differ");
        d = compare(n1.data(), n1.data(), 1, 1, 1, true);
        check(d.identical(), "the same NaN is identical to itself");
    }

    // a bad channel count is a failure, not a pass
    {
        std::vector<uint8_t> a(16, 0);
        Diff d = compare(a.data(), a.data(), 2, 2, 5, false);
        check(!d.identical(), "an unsupported channel count never passes");
    }

    // 8 bit images of 2 and 3 channels, and a non-square float image: the strides must not mix rows or channels
    {
        std::vector<uint8_t> a(4 * 2 * 3, 9), b = a;
        b[(1 * 4 + 2) * 3 + 1] = 10; // pixel (2, 1), green
        Diff d = compare(a.data(), b.data(), 4, 2, 3, false);
        check(d.mismatchedPixels == 1 && d.firstX == 2 && d.firstY == 1 && d.maxDiff[1] == 1 && d.maxDiff[0] == 0 && d.maxDiff[2] == 0,
              "3 channel 8 bit");

        std::vector<uint8_t> c(5 * 3 * 2, 0), e = c;
        e[(2 * 5 + 4) * 2 + 1] = 255; // pixel (4, 2), second channel
        d = compare(c.data(), e.data(), 5, 3, 2, false);
        check(d.mismatchedPixels == 1 && d.firstX == 4 && d.firstY == 2 && d.maxDiff[1] == 255 && d.maxDiff[0] == 0, "2 channel 8 bit");

        std::vector<float> f(7 * 2 * 4, 0.25f), g = f; // 7 wide, 2 high
        g[(1 * 7 + 6) * 4 + 3] = fromBits(toBits(0.25f) + 3);
        d = compare(f.data(), g.data(), 7, 2, 4, true);
        check(d.mismatchedPixels == 1 && d.firstX == 6 && d.firstY == 1 && d.maxDiff[3] == 3 && d.pixels == 14, "non-square float");
    }

    // the run tracker and the bucket mapping behind the mergeable-run histogram
    {
        using namespace LLGLSubmitMath;
        const int expect[] = {0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5};
        bool ok = true;
        for (uint32_t len = 0; len < 20; ++len)
        {
            ok = ok && runBucket(len) == expect[len];
        }
        check(ok, "run buckets: 1, 2, 3-4, 5-8, 9-16, 17+");

        int a = 0, b = 0;
        RunTracker t;
        auto c1 = t.draw(&a, 1);
        check(c1.len == 0, "the first draw closes nothing");
        check(t.draw(&a, 1).len == 0 && t.draw(&a, 1).len == 0, "same record and epoch continue the run");
        auto c2 = t.draw(&a, 2); // a state change since the last draw
        check(c2.rec == &a && c2.len == 3, "an epoch change closes the run of 3");
        auto c3 = t.draw(&b, 2); // another scope, same epoch
        check(c3.rec == &a && c3.len == 1, "a record change closes the run of 1");
        auto c4 = t.close();
        check(c4.rec == &b && c4.len == 1, "close returns the open run");
        check(t.close().len == 0, "and only once");
    }

    // runsNoUniform (G6r-0): a uniform between two equal draws splits today's run but not the run per-draw records would
    // give; a program, texture, cap or vertex format change splits both
    {
        using namespace LLGLSubmitMath;
        int scope = 0;
        RunEpochs e;
        RunTracker all, noUniform;
        uint32_t runs = 0, runsNoUniform = 0;
        auto draw = [&]()
        {
            runs += all.draw(&scope, e.all).len ? 1 : 0;
            runsNoUniform += noUniform.draw(&scope, e.noUniform).len ? 1 : 0;
        };
        auto close = [&]()
        {
            runs += all.close().len ? 1 : 0;
            runsNoUniform += noUniform.close().len ? 1 : 0;
        };
        draw();
        e.uniform();
        draw();
        close();
        check(runs == 2 && runsNoUniform == 1, "a uniform between two equal draws: runs 2, runsNoUniform 1");

        runs = runsNoUniform = 0;
        draw();
        e.stateChange();
        draw();
        e.uniform();
        draw();
        close();
        check(runs == 3 && runsNoUniform == 2, "a state change splits both runs, a uniform only today's");
        check(runsNoUniform <= runs, "runsNoUniform never exceeds runs");
    }

    // the copy census: size classes, run buckets, the exclusion order and the exactness guard
    {
        using namespace LLGLSubmitMath;
        check(censusSizeClass(0) == 0 && censusSizeClass(63) == 0 && censusSizeClass(64) == 1 && censusSizeClass(255) == 1 &&
              censusSizeClass(256) == 2 && censusSizeClass(575) == 2 && censusSizeClass(576) == 3 && censusSizeClass(65535) == 3,
              "size classes: under 64, 64-255, 256-575, 576+");
        const int expect[] = {0, 0, 1, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 5};
        bool ok = true;
        for (uint32_t n = 0; n < 18; ++n)
        {
            ok = ok && censusRunBucket(n) == expect[n];
        }
        ok = ok && censusRunBucket(32) == 5 && censusRunBucket(33) == 6 && censusRunBucket(64) == 6 && censusRunBucket(65) == 7 &&
             censusRunBucket(100000) == 7;
        check(ok, "census run buckets: 1, 2, 3-4, 5-8, 9-16, 17-32, 33-64, 65+");

        CensusFacts f;
        check(censusExclusion(f) == CENSUS_NO_KEY, "no key: nothing to share");
        f.hasKey = true;
        check(censusExclusion(f) == CENSUS_ELIGIBLE, "a plain keyed face is eligible");
        f.numbersSafe = false;
        check(censusExclusion(f) == CENSUS_NUMERIC, "a transform outside the guard stays baked");
        f.selected = true;
        check(censusExclusion(f) == CENSUS_SELECTED, "the first failing rule is reported");
        f.blended = true;
        f.rigged = true;
        check(censusExclusion(f) == CENSUS_RIGGED, "rigged comes before blended");
        f.hud = true;
        check(censusExclusion(f) == CENSUS_HUD, "a HUD attachment before anything else but the key");
        f.hasKey = false;
        check(censusExclusion(f) == CENSUS_NO_KEY, "no key before any rule");
        ok = true;
        for (int r = 0; r < CENSUS_EXCLUSIONS; ++r)
        {
            ok = ok && std::strcmp(censusExclusionName(r), "?") != 0;
        }
        check(ok && std::strcmp(censusExclusionName(CENSUS_EXCLUSIONS), "?") == 0, "every exclusion has a name");

        const float guard = std::ldexp(1.0f, -50);
        check(censusNumberSafe(0.0f) && censusNumberSafe(-0.0f) && censusNumberSafe(1.0f) && censusNumberSafe(-3.0e38f),
              "zero and ordinary numbers pass the guard");
        check(censusNumberSafe(guard) && censusNumberSafe(-guard), "2^-50 passes");
        check(!censusNumberSafe(fromBits(toBits(guard) - 1)) && !censusNumberSafe(1.0e-20f) && !censusNumberSafe(fromBits(1u)),
              "below 2^-50 and subnormals fail");
        check(!censusNumberSafe(fromBits(0x7f800000u)) && !censusNumberSafe(fromBits(0xff800000u)) && !censusNumberSafe(fromBits(0x7fc00000u)),
              "infinities and NaN fail");
    }

    // the copy census key: equal inputs, equal keys; any input the bake reads changes it; never 0
    {
        using namespace LLGLSubmitMath;
        CensusKeyInputs a;
        a.volume = 0x1234;
        a.face = 2;
        a.vertices = 300;
        a.indices = 900;
        a.formatMask = 0x3f;
        a.scaleS = a.scaleT = 1.f;
        a.color[3] = 1.f;
        const uint64_t base = censusKey(a);
        check(base != 0 && censusKey(a) == base, "a key is stable and not 0");
        std::vector<CensusKeyInputs> variants(15, a);
        variants[0].volume++;
        variants[1].face++;
        variants[2].vertices++;
        variants[3].indices++;
        variants[4].formatMask ^= 1;
        variants[5].scaleS = 2.f;
        variants[6].scaleT = 2.f;
        variants[7].offsetS = 0.5f;
        variants[8].offsetT = 0.5f;
        variants[9].rotation = 1.f;
        variants[10].color[0] = 0.5f;
        variants[11].glow = 0.1f;
        variants[12].material[1] = 7;
        variants[13].rigged = true;
        variants[14].offsetS = -0.0f; // -0 and +0 are kept apart (an offset of -0 can keep a -0 coordinate)
        bool all_differ = true;
        for (const CensusKeyInputs& v : variants)
        {
            all_differ = all_differ && censusKey(v) != base;
        }
        check(all_differ, "every input the bake reads changes the key");
        CensusKeyInputs s = a;
        s.skinHash = 99;
        s.textureIndex = 3;
        check(censusKey(s) == base, "a static face's key ignores the skin and the texture index");
        CensusKeyInputs r1 = a, r2 = a;
        r1.rigged = r2.rigged = true;
        r2.skinHash = 5;
        CensusKeyInputs r3 = r1;
        r3.textureIndex = 1;
        check(censusKey(r1) != censusKey(r2) && censusKey(r1) != censusKey(r3), "a rigged face's key holds its skin and texture index");
    }

    // one draw item under the two policies: instanced = eligible with 64 vertices or more
    {
        using namespace LLGLSubmitMath;
        auto face = [](uint64_t key, uint32_t vertices, uint8_t exclusion = CENSUS_ELIGIBLE)
        {
            CensusFace f;
            f.key = key;
            f.vertices = vertices;
            f.exclusion = exclusion;
            return f;
        };
        CensusItem it = censusItem(nullptr, 0);
        check(it.segments == 0 && it.sets == 0 && !it.mixed && it.instancedVertices == 0, "an empty item");

        const CensusFace same[] = {face(1, 100), face(1, 100), face(1, 100)};
        it = censusItem(same, 3);
        check(it.segments == 1 && it.sets == 1 && !it.mixed && it.instancedVertices == 300, "three copies of one key: one set");

        const CensusFace two[] = {face(1, 100), face(1, 100), face(2, 80)};
        it = censusItem(two, 3);
        check(it.segments == 1 && it.sets == 2 && !it.mixed, "two keys: two sets, one segment");

        const CensusFace mixed[] = {face(3, 500, CENSUS_SELECTED), face(1, 100), face(1, 100), face(4, 20)};
        it = censusItem(mixed, 4);
        check(it.segments == 3 && it.sets == 1 && it.mixed && it.instancedVertices == 200,
              "baked, instanced, baked: three segments under P0, the item baked under P1");

        const CensusFace alternating[] = {face(1, 100), face(5, 63), face(1, 100)};
        it = censusItem(alternating, 3);
        check(it.segments == 3 && it.sets == 2 && it.mixed, "a small eligible face is baked and splits the set");
    }

    // runs of one template in list order (N-SF1): eligible faces of one key, any size, inside one scope
    {
        using namespace LLGLSubmitMath;
        int scopeA = 0, scopeB = 0;
        CensusFace k1, k2, out;
        k1.key = 1;
        k1.vertices = 40;
        k2.key = 2;
        k2.vertices = 300;
        out.key = 1;
        out.vertices = 40;
        out.exclusion = CENSUS_BLENDED;
        CensusRunTracker t;
        check(t.face(&scopeA, k1).len == 0 && t.face(&scopeA, k1).len == 0 && t.face(&scopeA, k1).len == 0, "a run of three");
        CensusRunTracker::Closed c = t.face(&scopeA, k2);
        check(c.rec == &scopeA && c.len == 3 && c.vertices == 40, "another key closes it, with its size");
        c = t.face(&scopeA, out);
        check(c.len == 1 && c.vertices == 300, "an ineligible face closes the run");
        check(t.face(&scopeA, out).len == 0, "and opens none");
        check(t.face(&scopeA, k1).len == 0, "a new run starts");
        c = t.face(&scopeB, k1);
        check(c.rec == &scopeA && c.len == 1, "a scope change closes the run");
        c = t.close();
        check(c.rec == &scopeB && c.len == 1 && t.close().len == 0, "close returns the open run once");
    }

    // window averaging
    {
        using namespace LLGLSubmitMath;
        check(perFrameMean(300.0, 120.0) == 2.5, "mean per frame");
        check(perFrameMean(5.0, 0.0) == 0.0, "no frames, no division by zero");
    }

    // ordering of the mapped bits
    check(orderedFloatBits(toBits(-1.0f)) < orderedFloatBits(toBits(0.0f)), "ordered bits: negative below zero");
    check(orderedFloatBits(toBits(1.0f)) < orderedFloatBits(toBits(2.0f)), "ordered bits: increasing");

    if (failures)
    {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("llglsubmitimage_test: all checks passed\n");
    return 0;
}
