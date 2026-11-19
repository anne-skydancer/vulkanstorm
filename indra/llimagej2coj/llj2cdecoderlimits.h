// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The Second Life resource limits of an OpenJPEG decoder (package 2.5.4.44499299587 and later). The profile caps a
// codestream at 1,024 tiles, 8 components, 64 layers, 2,048 pixels per decoded side and 256 MiB per decode, and
// OpenJPEG checks it from the headers before it allocates. Set it after opj_setup_decoder and before
// opj_read_header, on every decoder the viewer creates, in the header pass and in the decode pass.

#ifndef LL_LLJ2CDECODERLIMITS_H
#define LL_LLJ2CDECODERLIMITS_H

#include "openjpeg.h"

#ifndef OPJ_HAVE_DECODER_LIMITS
#error "OpenJPEG package 2.5.4.44499299587 or later is required (opj_decoder_set_limits)"
#endif

namespace LLJ2CDecoderLimits
{
    // False if the limits could not be set; the caller then refuses the decode, since a decoder without the
    // limits would take any header at its word.
    inline bool applySLTexture(opj_codec_t* decoder)
    {
        opj_decoder_limits_t limits;
        return decoder && opj_decoder_limits_init(&limits, OPJ_DECODER_LIMITS_SL_TEXTURE) &&
               opj_decoder_set_limits(decoder, &limits);
    }
}

#endif // LL_LLJ2CDECODERLIMITS_H
