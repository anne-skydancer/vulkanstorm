// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llj2cdecodeguard.h against OpenJPEG (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/llimagej2coj $(pkg-config --cflags libopenjp2) indra/llimagej2coj/tests/llj2cdecodeguard_openjpeg_test.cpp $(pkg-config --libs libopenjp2)
// opj_decode allocates 4 bytes per sample of every component at the decoded size before it reads a code-block, and
// the main header alone sets that size: about 130 bytes made it commit 4 GiB (16384 x 16384, 4 components). The
// codestreams here are complete and conforming (one tile, 5/3 reversible, one layer, LRCP, every code-block empty,
// so they decode to mid-grey), built as the crafted streams of the 2026-11-18 audit were, and decoded with the calls
// of JPEG2KDecode::decode (llimagej2coj.cpp): the reduce factor is set before the header, and the guard runs between
// opj_read_header and opj_decode. The largest image it admits, 8192 x 8192 with 4 components, takes 1 GiB here.

#include "llj2cdecodeguard.h"

#include "openjpeg.h"

#include <algorithm>
#include <cstdint>
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

    constexpr unsigned long long MAX_IMAGE_COMPONENTS = 8; // llimage.h

    void put16(std::vector<unsigned char>& out, std::uint32_t value)
    {
        out.push_back(static_cast<unsigned char>(value >> 8));
        out.push_back(static_cast<unsigned char>(value));
    }

    void put32(std::vector<unsigned char>& out, std::uint32_t value)
    {
        put16(out, value >> 16);
        put16(out, value & 0xffff);
    }

    void marker(std::vector<unsigned char>& out, std::uint32_t code, const std::vector<unsigned char>& payload)
    {
        put16(out, code);
        put16(out, static_cast<std::uint32_t>(payload.size() + 2));
        out.insert(out.end(), payload.begin(), payload.end());
    }

    // A width x height codestream with `components` 8-bit unsigned components and `levels` decomposition levels: SOC,
    // SIZ (one tile), COD (LRCP, one layer, no colour transform, 64 x 64 code-blocks, 5/3 reversible), QCD (no
    // quantisation, 2 guard bits), one tile-part whose packets are all the one-byte empty packet, EOC
    std::vector<unsigned char> craft(std::uint32_t width, std::uint32_t height, std::uint32_t components,
                                     std::uint32_t levels = 5)
    {
        std::vector<unsigned char> out;
        put16(out, 0xff4f);

        std::vector<unsigned char> siz;
        put16(siz, 0);
        put32(siz, width);
        put32(siz, height);
        put32(siz, 0);
        put32(siz, 0);
        put32(siz, width);
        put32(siz, height);
        put32(siz, 0);
        put32(siz, 0);
        put16(siz, components);
        for (std::uint32_t c = 0; c < components; ++c)
        {
            siz.push_back(7); // 8 bits, unsigned
            siz.push_back(1);
            siz.push_back(1);
        }
        marker(out, 0xff51, siz);

        marker(out, 0xff52, { 0, 0, 0, 1, 0, static_cast<unsigned char>(levels), 4, 4, 0, 1 });

        std::vector<unsigned char> qcd(1 + 3 * levels + 1, static_cast<unsigned char>(9 << 3));
        qcd[0] = 0x40;
        marker(out, 0xff5c, qcd);

        const std::uint32_t packets = (levels + 1) * components;
        put16(out, 0xff90);
        put16(out, 10);
        put16(out, 0);
        put32(out, 12 + 2 + packets);
        out.push_back(0);
        out.push_back(1);
        put16(out, 0xff93);
        out.insert(out.end(), packets, 0);
        put16(out, 0xffd9);
        return out;
    }

    // A codestream in memory, read with the semantics of the wrapper's opj_read, opj_skip and opj_seek (EOF as -1)
    struct Stream
    {
        std::vector<unsigned char> bytes;
        OPJ_OFF_T offset = 0;
    };

    OPJ_SIZE_T streamRead(void* buffer, OPJ_SIZE_T count, void* user)
    {
        Stream* s = static_cast<Stream*>(user);
        if (s->offset < 0 || static_cast<OPJ_SIZE_T>(s->offset) >= s->bytes.size())
        {
            s->offset = static_cast<OPJ_OFF_T>(s->bytes.size());
            return static_cast<OPJ_SIZE_T>(-1);
        }
        const OPJ_SIZE_T n = std::min<OPJ_SIZE_T>(count, s->bytes.size() - static_cast<OPJ_SIZE_T>(s->offset));
        std::memcpy(buffer, s->bytes.data() + s->offset, n);
        s->offset += static_cast<OPJ_OFF_T>(n);
        return n;
    }

    OPJ_OFF_T streamSkip(OPJ_OFF_T count, void* user)
    {
        Stream* s = static_cast<Stream*>(user);
        const OPJ_OFF_T next = s->offset + count;
        if (next < 0 || next > static_cast<OPJ_OFF_T>(s->bytes.size()))
        {
            s->offset = std::clamp<OPJ_OFF_T>(next, 0, static_cast<OPJ_OFF_T>(s->bytes.size()));
            return static_cast<OPJ_OFF_T>(-1);
        }
        s->offset = next;
        return count;
    }

    OPJ_BOOL streamSeek(OPJ_OFF_T offset, void* user)
    {
        Stream* s = static_cast<Stream*>(user);
        if (offset < 0 || offset > static_cast<OPJ_OFF_T>(s->bytes.size()))
        {
            return OPJ_FALSE;
        }
        s->offset = offset;
        return OPJ_TRUE;
    }

    void quiet(const char*, void*)
    {
    }

    opj_stream_t* openStream(Stream& s)
    {
        opj_stream_t* stream = opj_stream_create(s.bytes.size(), OPJ_TRUE);
        opj_stream_set_user_data(stream, &s, nullptr);
        opj_stream_set_user_data_length(stream, s.bytes.size());
        opj_stream_set_read_function(stream, streamRead);
        opj_stream_set_skip_function(stream, streamSkip);
        opj_stream_set_seek_function(stream, streamSeek);
        return stream;
    }

    opj_codec_t* openDecoder(OPJ_UINT32 reduce)
    {
        opj_dparameters_t parameters;
        std::memset(&parameters, 0, sizeof(parameters));
        opj_set_default_decoder_parameters(&parameters);
        parameters.cp_reduce = 0;
        opj_codec_t* decoder = opj_create_decompress(OPJ_CODEC_J2K);
        opj_setup_decoder(decoder, &parameters);
        opj_set_error_handler(decoder, quiet, nullptr);
        opj_set_warning_handler(decoder, quiet, nullptr);
        opj_set_info_handler(decoder, quiet, nullptr);
        opj_set_decoded_resolution_factor(decoder, reduce); // before the header, as the wrapper
#if OPJ_VERSION_MAJOR > 2 || (OPJ_VERSION_MAJOR == 2 && OPJ_VERSION_MINOR >= 5)
        opj_decoder_set_strict_mode(decoder, OPJ_FALSE);
#endif
        return decoder;
    }

    // JPEG2KDecode::decode: the header, the guard, and opj_decode only when the guard admits the image
    struct Decoded
    {
        bool header = false;  // opj_read_header succeeded
        bool admitted = false; // the guard let opj_decode run
        bool decoded = false; // opj_decode succeeded
        unsigned width = 0;
        unsigned height = 0;
        unsigned components = 0;
        bool grey = false;    // every component holds mid-grey (the empty code-blocks)
    };

    Decoded decode(const std::vector<unsigned char>& codestream, OPJ_UINT32 reduce)
    {
        Decoded result;
        Stream in{ codestream, 0 };
        opj_codec_t* decoder = openDecoder(reduce);
        opj_stream_t* stream = openStream(in);
        opj_image_t* image = nullptr;
        result.header = opj_read_header(stream, decoder, &image);
        if (result.header)
        {
            const unsigned long long width = (image->x1 > image->x0) ? (image->x1 - image->x0) : 0;
            const unsigned long long height = (image->y1 > image->y0) ? (image->y1 - image->y0) : 0;
            result.admitted = LLJ2CDecodeGuard::decodeAllowed(width, height, image->numcomps, reduce, MAX_IMAGE_COMPONENTS);
        }
        if (result.admitted)
        {
            result.decoded = opj_decode(decoder, stream, image) && image->numcomps;
            opj_end_decompress(decoder, stream);
        }
        if (result.decoded)
        {
            result.width = image->comps[0].w;
            result.height = image->comps[0].h;
            result.components = image->numcomps;
            result.grey = true;
            const std::size_t samples = std::size_t(result.width) * result.height;
            for (OPJ_UINT32 c = 0; c < image->numcomps; ++c)
            {
                const opj_image_comp_t& comp = image->comps[c];
                result.grey = result.grey && comp.data && comp.w == result.width && comp.h == result.height;
                // every 4093rd sample and the last one: the whole buffer without reading 268 M samples
                for (std::size_t i = 0; result.grey && i < samples; i += 4093)
                {
                    result.grey = comp.data[i] == 128;
                }
                result.grey = result.grey && comp.data[samples - 1] == 128;
            }
        }
        if (image)
        {
            opj_image_destroy(image);
        }
        opj_stream_destroy(stream);
        opj_destroy_codec(decoder);
        return result;
    }

    void test_decoded(std::uint32_t side, std::uint32_t components, const char* what)
    {
        const Decoded d = decode(craft(side, side, components), 0);
        check(d.header && d.admitted, what);
        check(d.decoded && d.width == side && d.height == side && d.components == components,
              "and decodes at its full size");
        check(d.grey, "to the image its codestream holds");
    }

    void test_refused(std::uint32_t side, std::uint32_t components, const char* what)
    {
        const Decoded d = decode(craft(side, side, components), 0);
        check(d.header, "its header reads");
        check(!d.admitted && !d.decoded, what);
    }

    // The rule alone, at the bounds (nothing decoded)
    void test_bounds()
    {
        using LLJ2CDecodeGuard::decodeAllowed;
        // the samples: 1 GiB is the most, at the size after the reduce factor
        check(decodeAllowed(8192, 8192, 4, 0, MAX_IMAGE_COMPONENTS), "8192 x 8192 x 4 is 1 GiB: admitted");
        check(!decodeAllowed(8192, 8192, 5, 0, MAX_IMAGE_COMPONENTS), "8192 x 8192 x 5 is 1.25 GiB: refused");
        check(decodeAllowed(8192, 8192, 5, 1, MAX_IMAGE_COMPONENTS), "and admitted one level down");
        check(!decodeAllowed(16384, 16384, 4, 0, MAX_IMAGE_COMPONENTS), "16384 x 16384 x 4 is 4 GiB: refused");
        check(decodeAllowed(16384, 16384, 4, 1, MAX_IMAGE_COMPONENTS), "and admitted one level down (8192 x 8192)");
        check(!decodeAllowed(16384, 16384, 8, 1, MAX_IMAGE_COMPONENTS), "16384 x 16384 x 8 one level down is 2 GiB: refused");
        check(decodeAllowed(4096, 4096, 8, 0, MAX_IMAGE_COMPONENTS), "4096 x 4096 x 8 is 512 MiB: admitted");
        // the sides: 16384 at most, after the reduce factor (rounded up)
        check(decodeAllowed(16384, 1, 1, 0, MAX_IMAGE_COMPONENTS), "a side of 16384 is admitted");
        check(!decodeAllowed(16385, 1, 1, 0, MAX_IMAGE_COMPONENTS), "a side of 16385 is refused");
        check(!decodeAllowed(1, 16385, 1, 0, MAX_IMAGE_COMPONENTS), "either side");
        check(decodeAllowed(32768, 1, 1, 1, MAX_IMAGE_COMPONENTS), "32768 one level down is 16384: admitted");
        check(!decodeAllowed(32769, 1, 1, 1, MAX_IMAGE_COMPONENTS), "32769 one level down is 16385: refused");
        check(!decodeAllowed(0xffffffffull, 0xffffffffull, 1, 0, MAX_IMAGE_COMPONENTS), "the largest grid is refused");
        // the components: 1 to MAX_IMAGE_COMPONENTS
        check(!decodeAllowed(64, 64, 0, 0, MAX_IMAGE_COMPONENTS), "no component: refused");
        check(decodeAllowed(64, 64, 8, 0, MAX_IMAGE_COMPONENTS), "8 components: admitted");
        check(!decodeAllowed(64, 64, 9, 0, MAX_IMAGE_COMPONENTS), "9 components: refused");
        check(!decodeAllowed(64, 64, 260, 0, MAX_IMAGE_COMPONENTS), "260 components: refused");
        check(!decodeAllowed(64, 64, 16384, 0, MAX_IMAGE_COMPONENTS), "16384 components (OpenJPEG's limit): refused");
        // a reduce factor no codestream has
        check(!decodeAllowed(64, 64, 4, 32, MAX_IMAGE_COMPONENTS), "a reduce factor above 31: refused");
    }
}

int main()
{
    std::printf("OpenJPEG %s\n", opj_version());
    test_bounds();
    // the audit's codestreams: refused before OpenJPEG allocates for them
    test_refused(16384, 4, "16384 x 16384 x 4 (4 GiB in OpenJPEG) is refused before opj_decode");
    test_refused(4096, 260, "4096 x 4096 x 260 (16.25 GiB in OpenJPEG) is refused before opj_decode");
    // and what a texture can be still decodes, up to the bound
    test_decoded(1024, 4, "1024 x 1024 x 4 is admitted");
    test_decoded(8192, 4, "8192 x 8192 x 4 (1 GiB in OpenJPEG, the bound) is admitted");
    if (failures == 0)
    {
        std::printf("llj2cdecodeguard_openjpeg_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
