// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the rule in llj2creduce.h (no libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/llimagej2coj indra/llimagej2coj/tests/llj2creduce_test.cpp
// The same rule against OpenJPEG's decoder: llj2creduce_openjpeg_test.cpp.
// A texture's first request asks for discard level 5. A codestream with fewer resolutions than that decodes at
// its coarsest level when that level is no larger than level 5 of the largest image the viewer takes (4096,
// MAX_IMAGE_SIZE): every texture the viewer's encoder makes with a side below 32 px (a strip one pixel thin at
// its coarsest level), and nothing large. Any other request is passed on unchanged.

#include "llj2creduce.h"

#include <cstdio>

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

    constexpr int MAX_IMAGE_SIZE = 4096;   // llimage.h
    constexpr int FIRST_REQUEST = 5;       // MAX_DISCARD_LEVEL: a texture of unknown size asks for it first

    constexpr int firstRequest(int width, int height, int resolutions)
    {
        return LLJ2CReduce::reduceFactor(FIRST_REQUEST, resolutions, width, height, MAX_IMAGE_SIZE);
    }

    // The viewer's encoder (JPEG2KEncode::encode): 1 + floor(log2(the smaller side)) resolutions when a side fits
    // one 64 px tile (below 128 px), 6 otherwise
    int encoderResolutions(int width, int height)
    {
        const int smaller = width < height ? width : height;
        if (width >= 128 && height >= 128)
        {
            return 6;
        }
        int log2 = 0;
        while ((2 << log2) <= smaller)
        {
            ++log2;
        }
        return 1 + log2;
    }

    void test_level_pixels()
    {
        static_assert(LLJ2CReduce::levelPixels(16, 16, 4) == 1, "16 x 16 at level 4 is 1 x 1");
        check(LLJ2CReduce::levelPixels(512, 16, 4) == 32, "512 x 16 at level 4 is 32 x 1");
        check(LLJ2CReduce::levelPixels(24, 24, 4) == 4, "sides round up: 24 x 24 at level 4 is 2 x 2");
        check(LLJ2CReduce::levelPixels(8192, 8192, 0) == 8192LL * 8192LL, "no overflow for a large level");
        check(LLJ2CReduce::levelPixels(65535, 65535, 0) == 65535LL * 65535LL, "nor at the largest 16-bit size");
    }

    void test_unchanged_requests()
    {
        check(firstRequest(1024, 1024, 6) == 5, "6 resolutions: discard 5 unchanged (every texture in the cache)");
        check(LLJ2CReduce::reduceFactor(4, 5, 16, 16, MAX_IMAGE_SIZE) == 4
              && LLJ2CReduce::reduceFactor(0, 5, 16, 16, MAX_IMAGE_SIZE) == 0, "levels the codestream has are unchanged");
        check(firstRequest(16, 16, 0) == 5, "resolutions not known: unchanged");
        check(LLJ2CReduce::reduceFactor(-1, 5, 16, 16, MAX_IMAGE_SIZE) == -1, "a negative request is passed on unchanged");
        check(LLJ2CReduce::reduceFactor(40, 5, 16, 16, MAX_IMAGE_SIZE) == 40, "an out-of-range request is passed on unchanged");
    }

    void test_small_textures_decode_at_their_coarsest()
    {
        static_assert(LLJ2CReduce::reduceFactor(5, 5, 16, 16, 4096) == 4, "16 x 16: discard 5 becomes 4");
        check(firstRequest(16, 16, 5) == 4, "16 x 16 (5 resolutions): discard 5 becomes 4");
        check(firstRequest(8, 8, 4) == 3, "8 x 8 (4 resolutions): discard 5 becomes 3");
        check(firstRequest(4, 4, 3) == 2, "4 x 4 (3 resolutions): discard 5 becomes 2");
        check(firstRequest(512, 16, 5) == 4, "512 x 16 strip: discard 5 becomes 4");

        // every size the viewer's encoder gives fewer than 6 resolutions, up to MAX_IMAGE_SIZE: its coarsest level
        bool all = true;
        for (int width = 4; width <= MAX_IMAGE_SIZE; width *= 2)
        {
            for (int height = 4; height <= MAX_IMAGE_SIZE; height *= 2)
            {
                const int resolutions = encoderResolutions(width, height);
                if (resolutions < FIRST_REQUEST + 1)
                {
                    all = all && firstRequest(width, height, resolutions) == resolutions - 1;
                }
            }
        }
        check(all, "every texture the encoder makes with a side below 32 px decodes at its coarsest level");
        check(encoderResolutions(16, 16) == 5 && encoderResolutions(64, 64) == 7 && encoderResolutions(128, 128) == 6,
              "the encoder's resolution rule as modelled");
    }

    void test_large_codestreams_keep_their_request()
    {
        // a crafted 8192 x 8192 codestream with 1 resolution: its coarsest level is the whole image, 1 GB in
        // OpenJPEG for four components; its request is kept and fails as before
        check(firstRequest(8192, 8192, 1) == 5, "8192 x 8192 with 1 resolution: discard 5 unchanged");
        check(firstRequest(4096, 4096, 1) == 5, "4096 x 4096 with 1 resolution: discard 5 unchanged");
        check(firstRequest(8192, 8192, 3) == 5, "8192 x 8192 with 3 resolutions (2048 x 2048 at its coarsest): unchanged");
        check(firstRequest(256, 256, 1) == 5, "256 x 256 with 1 resolution: unchanged");

        // the bound: level 5 of a 4096 image, 128 x 128
        check(firstRequest(128, 128, 1) == 0, "128 x 128 with 1 resolution: at the bound, decoded at its only level");
        check(firstRequest(256, 128, 1) == 5, "256 x 128 with 1 resolution: past the bound, unchanged");
        check(firstRequest(256, 256, 2) == 1, "256 x 256 with 2 resolutions: 128 x 128 at its coarsest, decoded there");

        // a finer request allows a larger level, as large as that request can be for any image
        check(LLJ2CReduce::reduceFactor(1, 1, 2048, 2048, MAX_IMAGE_SIZE) == 0, "discard 1 of a 2048 codestream with 1 resolution");
        check(LLJ2CReduce::reduceFactor(1, 1, 4096, 4096, MAX_IMAGE_SIZE) == 1, "discard 1 of a 4096 codestream with 1 resolution: unchanged");
    }
}

int main()
{
    test_level_pixels();
    test_unchanged_requests();
    test_small_textures_decode_at_their_coarsest();
    test_large_codestreams_keep_their_request();
    if (failures == 0)
    {
        std::printf("llj2creduce_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
