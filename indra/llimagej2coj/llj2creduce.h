// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llj2creduce.h
 * @brief The reduce factor (discard level) a J2C codestream can be decoded at.
 *
 * Free of viewer headers: LLImageJ2COJ::decodeImpl (llimagej2coj.cpp) applies the rule,
 * indra/llimagej2coj/tests/llj2creduce_test.cpp checks it standalone, and
 * indra/llimagej2coj/tests/llj2creduce_openjpeg_test.cpp checks it against OpenJPEG.
 *
 * Why: a texture's first request asks for discard level 5, and OpenJPEG refuses a reduce factor at or
 * above the codestream's number of resolutions while it reads the header. The viewer's own encoder gives a
 * side below 32 px fewer than 6 resolutions (16 px: 5, 8 px: 4), so such a texture failed its first
 * decode, was removed from the cache and was marked missing for the session. Its coarsest level is a strip
 * one pixel thin, so decoding it there costs nothing; a large codestream with few resolutions, which only a
 * crafted asset has, would instead be decoded at nearly full size from the first 600 bytes (an 8192 x 8192
 * one with one resolution: about 1 GB in OpenJPEG), so the rule keeps its request, which fails as before.
 */

#ifndef LL_LLJ2CREDUCE_H
#define LL_LLJ2CREDUCE_H

namespace LLJ2CReduce
{
    // Pixels of a width x height image at reduce factor `level` (0 to 30): OpenJPEG decodes each side as
    // ceil(side / 2^level)
    constexpr long long levelPixels(long long width, long long height, int level)
    {
        return ((width + (1LL << level) - 1) >> level) * ((height + (1LL << level) - 1) >> level);
    }

    // The reduce factor to decode a request for `requested_discard` at, for a width x height codestream whose
    // component with the fewest resolutions has `resolutions` of them (0: not known, nothing changes).
    // - A request for a level the codestream has is unchanged.
    // - A request for a coarser level gets the coarsest the codestream has, resolutions - 1, when that level
    //   has no more pixels than the requested level of the largest image the viewer takes (`max_image_size`
    //   square, MAX_IMAGE_SIZE): it then costs no more than any request for that level can.
    // - Otherwise the request is unchanged, and the decode fails as it did before the rule.
    constexpr int reduceFactor(int requested_discard, int resolutions, int width, int height, int max_image_size)
    {
        if (resolutions <= 0 || requested_discard < resolutions || requested_discard > 30)
        {
            return requested_discard;
        }
        const int coarsest = resolutions - 1;
        const long long side = max_image_size >> requested_discard;
        return levelPixels(width, height, coarsest) <= side * side ? coarsest : requested_discard;
    }
}

#endif // LL_LLJ2CREDUCE_H
