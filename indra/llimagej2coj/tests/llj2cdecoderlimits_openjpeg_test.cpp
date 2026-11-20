// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llj2cdecoderlimits.h against OpenJPEG 2.5.4.44499299587 or later (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/llimagej2coj -I <package>/include/openjpeg indra/llimagej2coj/tests/llj2cdecoderlimits_openjpeg_test.cpp -L <package>/lib -lopenjp2
// The viewer's decoders (llimagej2coj.cpp: the header pass and the decode pass) call LLJ2CDecoderLimits::applySLTexture
// after opj_setup_decoder and before opj_read_header. Codestreams over the SL profile (1,024 tiles, 8 components,
// 64 layers, 2,048 px per decoded side, 256 MiB per decode) must be refused before any buffer is allocated: the
// tiles and components at opj_read_header, the decoded side and the memory when opj_decode starts. An ordinary
// texture must decode bit for bit as it does without the limits. With an OpenJPEG that lacks the API (a system libopenjp2) the test reports a skip.

#include "openjpeg.h"

#ifdef OPJ_HAVE_DECODER_LIMITS

#include "llj2cdecoderlimits.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
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

    // A width x height codestream with `components` 8-bit unsigned components, one tile, 5 decomposition levels, all
    // code-blocks empty (the same crafted stream as llj2cdecodeguard_openjpeg_test.cpp)
    std::vector<unsigned char> craft(std::uint32_t width, std::uint32_t height, std::uint32_t components,
                                     std::uint32_t tileWidth = 0, std::uint32_t tileHeight = 0,
                                     std::uint32_t layers = 1)
    {
        const std::uint32_t levels = 5;
        std::vector<unsigned char> out;
        put16(out, 0xff4f);

        std::vector<unsigned char> siz;
        put16(siz, 0);
        put32(siz, width);
        put32(siz, height);
        put32(siz, 0);
        put32(siz, 0);
        put32(siz, tileWidth ? tileWidth : width);
        put32(siz, tileHeight ? tileHeight : height);
        put32(siz, 0);
        put32(siz, 0);
        put16(siz, components);
        for (std::uint32_t c = 0; c < components; ++c)
        {
            siz.push_back(7);
            siz.push_back(1);
            siz.push_back(1);
        }
        marker(out, 0xff51, siz);

        marker(out, 0xff52, { 0, 0, static_cast<unsigned char>(layers >> 8), static_cast<unsigned char>(layers), 0, static_cast<unsigned char>(levels), 4, 4, 0, 1 });

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

    // A codestream in memory, read with the semantics of the wrapper's opj_read, opj_skip and opj_seek
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

    // collects the error messages in the order OpenJPEG reports them
    void collect(const char* msg, void* user)
    {
        if (user && msg)
        {
            static_cast<std::vector<std::string>*>(user)->push_back(msg);
        }
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

    // The decoder as the wrapper builds it, with the limits on or off
    opj_codec_t* openDecoder(bool limits, bool* limitsSet = nullptr, std::vector<std::string>* errors = nullptr)
    {
        opj_dparameters_t parameters;
        std::memset(&parameters, 0, sizeof(parameters));
        opj_set_default_decoder_parameters(&parameters);
        opj_codec_t* decoder = opj_create_decompress(OPJ_CODEC_J2K);
        opj_setup_decoder(decoder, &parameters);
        opj_set_error_handler(decoder, errors ? collect : quiet, errors);
        opj_set_warning_handler(decoder, quiet, nullptr);
        opj_set_info_handler(decoder, quiet, nullptr);
        if (limits)
        {
            const bool set = LLJ2CDecoderLimits::applySLTexture(decoder);
            if (limitsSet)
            {
                *limitsSet = set;
            }
        }
        opj_decoder_set_strict_mode(decoder, OPJ_FALSE);
        return decoder;
    }

    // true if opj_read_header accepts the codestream; with `decode`, also opj_decode at the reduce factor
    bool headerAccepted(const std::vector<unsigned char>& codestream, bool limits, bool decode = false,
                        OPJ_UINT32 reduce = 0, std::vector<std::string>* errors = nullptr)
    {
        Stream in{ codestream, 0 };
        opj_codec_t* decoder = openDecoder(limits, nullptr, errors);
        opj_stream_t* stream = openStream(in);
        opj_image_t* image = nullptr;
        opj_set_decoded_resolution_factor(decoder, reduce);
        bool ok = opj_read_header(stream, decoder, &image);
        if (ok && decode)
        {
            ok = opj_decode(decoder, stream, image);
        }
        if (image)
        {
            opj_image_destroy(image);
        }
        opj_stream_destroy(stream);
        opj_destroy_codec(decoder);
        return ok;
    }

    struct Pixels
    {
        bool ok = false;
        std::vector<std::vector<std::int32_t>> comps;
    };

    Pixels decodeAll(const std::vector<unsigned char>& codestream, bool limits)
    {
        Pixels result;
        Stream in{ codestream, 0 };
        opj_codec_t* decoder = openDecoder(limits);
        opj_stream_t* stream = openStream(in);
        opj_image_t* image = nullptr;
        if (opj_read_header(stream, decoder, &image) && opj_decode(decoder, stream, image))
        {
            result.ok = true;
            for (OPJ_UINT32 c = 0; c < image->numcomps; ++c)
            {
                const opj_image_comp_t& comp = image->comps[c];
                result.comps.emplace_back(comp.data, comp.data + static_cast<std::size_t>(comp.w) * comp.h);
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

    OPJ_SIZE_T sinkWrite(void* buffer, OPJ_SIZE_T n, void* user)
    {
        auto* v = static_cast<std::vector<unsigned char>*>(user);
        v->insert(v->end(), static_cast<unsigned char*>(buffer), static_cast<unsigned char*>(buffer) + n);
        return n;
    }

    OPJ_OFF_T sinkSkip(OPJ_OFF_T n, void*)
    {
        return n;
    }

    OPJ_BOOL sinkSeek(OPJ_OFF_T, void*)
    {
        return OPJ_FALSE;
    }

    // A lossless 5/3 encoding of a w x h x 3 image of gradient plus pseudo-random detail; the source is returned
    std::vector<unsigned char> encodeTexture(std::uint32_t w, std::uint32_t h, std::vector<std::vector<std::int32_t>>& src)
    {
        std::vector<unsigned char> out;
        opj_cparameters_t params;
        opj_set_default_encoder_parameters(&params);
        params.prog_order = OPJ_RLCP; // the viewer's setting (neo-j2k's encoder takes no other)
        params.tcp_numlayers = 1;
        params.cp_disto_alloc = 1;
        params.tcp_rates[0] = 0; // lossless
        params.numresolution = 5;
        opj_image_cmptparm_t cmpt[3];
        std::memset(cmpt, 0, sizeof(cmpt));
        for (auto& c : cmpt)
        {
            c.dx = c.dy = 1;
            c.w = w;
            c.h = h;
            c.prec = 8;
            c.sgnd = 0;
        }
        opj_image_t* image = opj_image_create(3, cmpt, OPJ_CLRSPC_SRGB);
        image->x0 = image->y0 = 0;
        image->x1 = w;
        image->y1 = h;
        std::uint32_t rng = 12345;
        src.assign(3, std::vector<std::int32_t>(static_cast<std::size_t>(w) * h));
        for (std::uint32_t c = 0; c < 3; ++c)
        {
            for (std::size_t i = 0; i < src[c].size(); ++i)
            {
                rng = rng * 1664525u + 1013904223u;
                const std::uint32_t x = static_cast<std::uint32_t>(i % w);
                const std::uint32_t y = static_cast<std::uint32_t>(i / w);
                const std::int32_t v = static_cast<std::int32_t>((x * 255 / w + y * 127 / h * (c + 1) + ((rng >> 24) & 15)) & 255);
                src[c][i] = image->comps[c].data[i] = v;
            }
        }
        opj_codec_t* encoder = opj_create_compress(OPJ_CODEC_J2K);
        opj_set_error_handler(encoder, quiet, nullptr);
        opj_set_warning_handler(encoder, quiet, nullptr);
        opj_set_info_handler(encoder, quiet, nullptr);
        if (opj_setup_encoder(encoder, &params, image))
        {
            opj_stream_t* stream = opj_stream_create(1 << 16, OPJ_FALSE);
            opj_stream_set_user_data(stream, &out, nullptr);
            opj_stream_set_write_function(stream, sinkWrite);
            opj_stream_set_skip_function(stream, sinkSkip);
            opj_stream_set_seek_function(stream, sinkSeek);
            if (!(opj_start_compress(encoder, image, stream) && opj_encode(encoder, stream) &&
                  opj_end_compress(encoder, stream)))
            {
                out.clear();
            }
            opj_stream_destroy(stream);
        }
        opj_destroy_codec(encoder);
        opj_image_destroy(image);
        return out;
    }

    void testRefusals()
    {
        // the decoded side: checked when opj_decode starts, before the buffers are allocated
        check(headerAccepted(craft(2048, 64, 3), true, true), "2048 x 64 x 3 decodes within the profile");
        check(!headerAccepted(craft(2049, 64, 3), true, true), "a width of 2049 is refused by opj_decode");
        check(!headerAccepted(craft(64, 2049, 3), true, true), "a height of 2049 is refused by opj_decode");
        check(!headerAccepted(craft(4096, 4096, 4), true, true), "4096 x 4096 x 4 is refused by opj_decode");
        check(headerAccepted(craft(4096, 4096, 4), true, true, 1), "and decodes one level down (2048 x 2048)");
        check(!headerAccepted(craft(16384, 16384, 4), true, true), "16384 x 16384 x 4 (4 GiB in OpenJPEG) is refused by opj_decode");
        // the components: checked when the main header is read
        check(headerAccepted(craft(64, 64, 8), true), "8 components are within the profile");
        check(!headerAccepted(craft(64, 64, 9), true), "9 components are refused at the header");
        check(!headerAccepted(craft(64, 64, 260), true), "260 components are refused at the header");
        // the tiles and the layers: checked when the main header is read
        check(headerAccepted(craft(1024, 1, 3, 1, 1), true), "1,024 tiles are within the profile");
        check(!headerAccepted(craft(1025, 1, 3, 1, 1), true), "1,025 tiles are refused at the header");
        check(headerAccepted(craft(1025, 1, 3, 1, 1), false), "1,025 tiles read without the limits");
        check(headerAccepted(craft(64, 64, 3, 0, 0, 64), true), "64 layers are within the profile");
        check(!headerAccepted(craft(64, 64, 3, 0, 0, 65), true), "65 layers are refused at the header");
        check(headerAccepted(craft(64, 64, 3, 0, 0, 65), false), "65 layers read without the limits");
        // the memory: 2048 x 2048 x 8 components is 128 MiB, the most the side and component limits allow, and
        // below the 256 MiB budget, so the budget itself is never the first to refuse
        check(headerAccepted(craft(2048, 2048, 8), true, true), "2048 x 2048 x 8 (128 MiB) decodes within the profile");
        // the first message names the cause: the generic failure OpenJPEG reports after it must not hide it
        {
            std::vector<std::string> errors;
            check(!headerAccepted(craft(64, 64, 9), true, false, 0, &errors) && !errors.empty() &&
                      errors.front().find("limit") != std::string::npos,
                  "the first error for 9 components names the limit");
            errors.clear();
            check(!headerAccepted(craft(1025, 1, 3, 1, 1), true, false, 0, &errors) && !errors.empty() &&
                      errors.front().find("limit") != std::string::npos,
                  "the first error for 1,025 tiles names the limit");
            errors.clear();
            check(!headerAccepted(craft(64, 64, 3, 0, 0, 65), true, false, 0, &errors) && !errors.empty() &&
                      errors.front().find("limit") != std::string::npos,
                  "the first error for 65 layers names the limit");
            errors.clear();
            check(!headerAccepted(craft(2049, 64, 3), true, true, 0, &errors) && !errors.empty() &&
                      errors.front().find("limit") != std::string::npos,
                  "the first error for a decoded width of 2049 names the limit");
        }
        // the same headers without the limits: the refusals come from the profile alone
        check(headerAccepted(craft(4096, 4096, 4), false, true, 1), "4096 x 4096 x 4 decodes one level down without the limits");
        check(headerAccepted(craft(2049, 64, 3), false, true), "a width of 2049 decodes without the limits");
        check(headerAccepted(craft(64, 64, 9), false), "9 components read without the limits");
        bool set = false;
        opj_codec_t* d = openDecoder(true, &set);
        opj_destroy_codec(d);
        check(set, "the limits can be set on a fresh decoder");
        check(!LLJ2CDecoderLimits::applySLTexture(nullptr), "a missing decoder is a failure, not a pass");
    }

    void testIdentical()
    {
        for (const std::uint32_t side : { 64u, 256u, 1024u })
        {
            std::vector<std::vector<std::int32_t>> src;
            const std::vector<unsigned char> j2c = encodeTexture(side, side, src);
            check(!j2c.empty(), "a test texture encodes");
            const Pixels with = decodeAll(j2c, true);
            const Pixels without = decodeAll(j2c, false);
            check(with.ok && without.ok, "the texture decodes with and without the limits");
            check(with.comps == without.comps, "bit for bit the same pixels with and without the limits");
            check(with.comps == src, "and the lossless pixels are the source's");
            // a cut download (two thirds of the codestream), decoded in the partial mode of the viewer
            const std::vector<unsigned char> cut(j2c.begin(), j2c.begin() + static_cast<std::ptrdiff_t>(j2c.size() * 2 / 3));
            const Pixels cutWith = decodeAll(cut, true);
            const Pixels cutWithout = decodeAll(cut, false);
            check(cutWith.ok, "the cut download decodes with the limits");
            check(cutWith.ok == cutWithout.ok && cutWith.comps == cutWithout.comps,
                  "a cut download decodes the same with and without the limits");
        }
    }
}

int main()
{
    std::printf("OpenJPEG %s\n", opj_version());
    testRefusals();
    testIdentical();
    if (failures == 0)
    {
        std::printf("llj2cdecoderlimits_openjpeg_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}

#else

#include <cstdio>

int main()
{
    std::printf("llj2cdecoderlimits_openjpeg_test: skipped, this OpenJPEG has no opj_decoder_set_limits\n");
    return 0;
}

#endif
