// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llj2cdecodeguard.h
 * @brief The J2C codestreams the OpenJPEG wrapper refuses before OpenJPEG allocates for them.
 *
 * Free of viewer headers: JPEG2KDecode::decode (llimagej2coj.cpp) applies it between opj_read_header and
 * opj_decode, LLImageJ2COJ::getMetadata applies its component rule to the header read (LLImageJ2CNeo, USE_NEOJ2K,
 * applies both to neo-j2k's header read), and
 * indra/llimagej2coj/tests/llj2cdecodeguard_openjpeg_test.cpp checks it against OpenJPEG.
 *
 * Why: opj_decode allocates a 4-byte sample for every pixel of every component at the decoded size before it
 * reads a single code-block, and the size and the component count come from the main header alone. A header of
 * about 130 bytes made it commit 4 GiB (16384 x 16384 with 4 components) to 8 GiB (8 components), and a texture
 * from the network, an image of a glTF asset or a file picked for upload reaches it.
 */

#ifndef LL_LLJ2CDECODEGUARD_H
#define LL_LLJ2CDECODEGUARD_H

namespace LLJ2CDecodeGuard
{
    // The longest side decoded, after the reduce factor. Defence in depth: the SL decoder profile (2048 px per
    // decoded side, 256 MiB, see llj2cdecoderlimits.h) is applied inside OpenJPEG and is the tighter bound
    constexpr unsigned long long MAX_DECODE_DIMENSION = 16384;
    // The most OpenJPEG may allocate for the decoded samples: 1 GiB (the SL profile allows 256 MiB)
    constexpr unsigned long long MAX_DECODE_BYTES = 1ull << 30;
    // OpenJPEG decodes every sample into an OPJ_INT32
    constexpr unsigned long long BYTES_PER_SAMPLE = 4;

    // A component count the viewer takes: 1 to `max_components` (MAX_IMAGE_COMPONENTS). LLImageBase keeps the
    // count in a signed byte, where a larger one would wrap (260 reads as 4).
    constexpr bool componentsSupported(unsigned long long components, unsigned long long max_components)
    {
        return components >= 1 && components <= max_components;
    }

    // One side of the image at reduce factor `reduce` (0 to 31): ceil(side / 2^reduce)
    constexpr unsigned long long reducedSide(unsigned long long side, unsigned reduce)
    {
        return (side + (1ull << reduce) - 1) >> reduce;
    }

    // Whether opj_decode may decode a `width` x `height` image (x1 - x0 by y1 - y0 of the reference grid) with
    // `components` components at reduce factor `reduce`: its components are supported, and after the reduce
    // neither side exceeds MAX_DECODE_DIMENSION and the samples take at most MAX_DECODE_BYTES. Subsampled
    // components are counted at the full size, which only overstates them.
    constexpr bool decodeAllowed(unsigned long long width, unsigned long long height, unsigned long long components,
                                 unsigned reduce, unsigned long long max_components)
    {
        if (!componentsSupported(components, max_components) || reduce > 31)
        {
            return false;
        }
        const unsigned long long reduced_width = reducedSide(width, reduce);
        const unsigned long long reduced_height = reducedSide(height, reduce);
        // the sides are at most 2^14 here, so the product of one component cannot overflow; dividing the budget
        // by the components keeps the comparison exact without multiplying by them
        return reduced_width <= MAX_DECODE_DIMENSION && reduced_height <= MAX_DECODE_DIMENSION
               && reduced_width * reduced_height * BYTES_PER_SAMPLE <= MAX_DECODE_BYTES / components;
    }
}

#endif // LL_LLJ2CDECODEGUARD_H
