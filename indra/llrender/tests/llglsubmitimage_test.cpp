// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the image comparison behind the in-frame identical-image check (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/llrender indra/llrender/tests/llglsubmitimage_test.cpp
// The comparison is the gate of every stage of the OpenGL modernisation, so it has to see one unit of
// difference in one channel of one pixel, in 8 bit and in float images, and report where.

#include "llglsubmitstats.h"

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
