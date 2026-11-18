// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llj2creduce.h
 * @brief The reduce factor (discard level) a J2C codestream can be decoded at.
 *
 * Free of viewer headers: LLImageJ2COJ::decodeImpl (llimagej2coj.cpp) applies the rule, and
 * indra/llimagej2coj/tests/llj2creduce_test.cpp checks it standalone against OpenJPEG.
 *
 * Why: a texture's first request asks for discard level 5, and OpenJPEG refuses a reduce factor at or
 * above the codestream's number of resolutions while it reads the header. The viewer's own encoder gives a
 * side below 32 px fewer than 6 resolutions (16 px: 5, 8 px: 4), so such a texture failed its first
 * decode, was removed from the cache and was marked missing for the session.
 */

#ifndef LL_LLJ2CREDUCE_H
#define LL_LLJ2CREDUCE_H

namespace LLJ2CReduce
{
    // The reduce factor to decode a request for `requested_discard` at, for a codestream whose component with
    // the fewest resolutions has `resolutions` of them (0: not known, nothing changes). A request for a level
    // coarser than the codestream has gets its coarsest, resolutions - 1; any other request is unchanged.
    constexpr int reduceFactor(int requested_discard, int resolutions)
    {
        return (resolutions > 0 && requested_discard >= resolutions) ? resolutions - 1 : requested_discard;
    }
}

#endif // LL_LLJ2CREDUCE_H
