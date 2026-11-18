// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llj2creduce.h against OpenJPEG (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/llimagej2coj $(pkg-config --cflags libopenjp2) indra/llimagej2coj/tests/llj2creduce_test.cpp $(pkg-config --libs libopenjp2)
// A texture's first request asks for discard level 5. The viewer's encoder gives a side below 32 px fewer than
// 6 resolutions (1 + floor(log2(side)): 16 px, 5), and OpenJPEG refuses a reduce factor at or above the
// resolutions while it reads the header, so such a texture failed its first decode and was marked missing.
// The codestreams here are made with the viewer's encoder settings for a small upload (lossless, one layer,
// RLCP, its resolution rule), and read and decoded with the calls of JPEG2KDecode (llimagej2coj.cpp):
// readHeader takes the fewest resolutions of any component from opj_get_cstr_info, decode sets the reduce
// factor before the header and decodes. LLImageJ2COJ::decodeImpl passes LLJ2CReduce::reduceFactor on.

#include "llj2creduce.h"

#include "openjpeg.h"

#include <algorithm>
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

    // A codestream in memory, read and written with the semantics of the wrapper's opj_read, opj_skip and
    // opj_seek (EOF as -1)
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

    OPJ_SIZE_T streamWrite(void* buffer, OPJ_SIZE_T count, void* user)
    {
        Stream* s = static_cast<Stream*>(user);
        const OPJ_SIZE_T end = static_cast<OPJ_SIZE_T>(s->offset) + count;
        if (end > s->bytes.size())
        {
            s->bytes.resize(end);
        }
        std::memcpy(s->bytes.data() + s->offset, buffer, count);
        s->offset = static_cast<OPJ_OFF_T>(end);
        return count;
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

    std::string lastError;

    void onError(const char* msg, void*)
    {
        lastError += msg;
    }

    void quiet(const char*, void*)
    {
    }

    opj_stream_t* openStream(Stream& s, bool input)
    {
        opj_stream_t* stream = opj_stream_create(input ? s.bytes.size() : 1u << 16, input ? OPJ_TRUE : OPJ_FALSE);
        opj_stream_set_user_data(stream, &s, nullptr);
        opj_stream_set_user_data_length(stream, s.bytes.size());
        opj_stream_set_read_function(stream, streamRead);
        opj_stream_set_write_function(stream, streamWrite);
        opj_stream_set_skip_function(stream, streamSkip);
        opj_stream_set_seek_function(stream, streamSeek);
        return stream;
    }

    void setHandlers(opj_codec_t* codec)
    {
        opj_set_error_handler(codec, onError, nullptr);
        opj_set_warning_handler(codec, quiet, nullptr);
        opj_set_info_handler(codec, quiet, nullptr);
    }

    unsigned char pixel(int x, int y, int c)
    {
        return static_cast<unsigned char>((x * 13 + y * 7 + c * 50) % 256);
    }

    // JPEG2KEncode for a small upload: lossless (reversible, one layer, rate 0), RLCP, rate allocation by
    // distortion, colour transform from 3 components, and fewer resolutions for a side below 64 px:
    // 1 + floor(log2(the smaller side)), the rule of llimagej2coj.cpp (`resolutions` here)
    std::vector<unsigned char> encode(int width, int height, int resolutions)
    {
        constexpr int COMPONENTS = 3;
        opj_cparameters_t parameters;
        opj_set_default_encoder_parameters(&parameters);
        parameters.prog_order = OPJ_RLCP;
        parameters.cp_disto_alloc = 1;
        parameters.irreversible = 0;
        parameters.tcp_numlayers = 1;
        parameters.tcp_rates[0] = 0.0f;
        parameters.tcp_mct = 1;
        parameters.numresolution = resolutions;

        std::vector<opj_image_cmptparm_t> components(COMPONENTS);
        for (opj_image_cmptparm_t& c : components)
        {
            c.prec = 8;
            c.sgnd = 0;
            c.dx = static_cast<OPJ_UINT32>(parameters.subsampling_dx);
            c.dy = static_cast<OPJ_UINT32>(parameters.subsampling_dy);
            c.w = static_cast<OPJ_UINT32>(width);
            c.h = static_cast<OPJ_UINT32>(height);
        }
        opj_image_t* image = opj_image_create(COMPONENTS, components.data(), OPJ_CLRSPC_SRGB);
        image->x1 = static_cast<OPJ_UINT32>(width);
        image->y1 = static_cast<OPJ_UINT32>(height);
        for (int c = 0; c < COMPONENTS; ++c)
        {
            for (int y = 0; y < height; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    image->comps[c].data[y * width + x] = pixel(x, y, c);
                }
            }
        }

        Stream out;
        opj_codec_t* encoder = opj_create_compress(OPJ_CODEC_J2K);
        setHandlers(encoder);
        opj_stream_t* stream = openStream(out, false);
        const bool ok = opj_setup_encoder(encoder, &parameters, image)
                        && opj_start_compress(encoder, image, stream)
                        && opj_encode(encoder, stream)
                        && opj_end_compress(encoder, stream);
        opj_stream_destroy(stream);
        opj_destroy_codec(encoder);
        opj_image_destroy(image);
        if (!ok)
        {
            out.bytes.clear();
        }
        return out.bytes;
    }

    // JPEG2KDecode::readHeader: the size and the fewest resolutions of any component
    struct Header
    {
        bool ok = false;
        int width = 0;
        int height = 0;
        int resolutions = 0;
    };

    Header readHeader(const std::vector<unsigned char>& codestream)
    {
        Header header;
        opj_dparameters_t parameters;
        std::memset(&parameters, 0, sizeof(parameters));
        opj_set_default_decoder_parameters(&parameters);
        parameters.cp_reduce = 0;
        parameters.flags |= OPJ_DPARAMETERS_DUMP_FLAG;

        Stream in{ codestream, 0 };
        opj_codec_t* decoder = opj_create_decompress(OPJ_CODEC_J2K);
        setHandlers(decoder);
        opj_image_t* image = nullptr;
        opj_stream_t* stream = openStream(in, true);
        if (opj_setup_decoder(decoder, &parameters))
        {
#if OPJ_VERSION_MAJOR > 2 || (OPJ_VERSION_MAJOR == 2 && OPJ_VERSION_MINOR >= 5)
            opj_decoder_set_strict_mode(decoder, OPJ_FALSE);
#endif
            if (opj_read_header(stream, decoder, &image))
            {
                opj_codestream_info_v2_t* info = opj_get_cstr_info(decoder);
                if (info)
                {
                    header.ok = true;
                    header.width = static_cast<int>(info->tw * info->tdx);
                    header.height = static_cast<int>(info->th * info->tdy);
                    for (OPJ_UINT32 c = 0; info->m_default_tile_info.tccp_info && c < info->nbcomps; ++c)
                    {
                        const int n = static_cast<int>(info->m_default_tile_info.tccp_info[c].numresolutions);
                        header.resolutions = (c == 0) ? n : std::min(header.resolutions, n);
                    }
                    opj_destroy_cstr_info(&info);
                }
            }
        }
        if (image)
        {
            opj_image_destroy(image);
        }
        opj_stream_destroy(stream);
        opj_destroy_codec(decoder);
        return header;
    }

    // JPEG2KDecode::decode at reduce factor `reduce`: set before the header, as the wrapper does
    struct Decoded
    {
        bool header = false;    // opj_read_header succeeded
        bool decoded = false;   // opj_decode succeeded
        unsigned factor = 0;    // comps[0].factor: the level decodeImpl reports (setDiscardLevel)
        unsigned width = 0;
        unsigned height = 0;
        bool pixelsExact = false; // at factor 0: the encoded pixels (lossless)
    };

    Decoded decodeAt(const std::vector<unsigned char>& codestream, int reduce, int full_width, int full_height)
    {
        Decoded result;
        lastError.clear();
        opj_dparameters_t parameters;
        std::memset(&parameters, 0, sizeof(parameters));
        opj_set_default_decoder_parameters(&parameters);
        parameters.cp_reduce = 0;

        Stream in{ codestream, 0 };
        opj_codec_t* decoder = opj_create_decompress(OPJ_CODEC_J2K);
        opj_setup_decoder(decoder, &parameters);
        setHandlers(decoder);
        opj_stream_t* stream = openStream(in, true);
        opj_image_t* image = nullptr;

        opj_set_decoded_resolution_factor(decoder, static_cast<OPJ_UINT32>(reduce)); // before the header, as the wrapper
#if OPJ_VERSION_MAJOR > 2 || (OPJ_VERSION_MAJOR == 2 && OPJ_VERSION_MINOR >= 5)
        opj_decoder_set_strict_mode(decoder, OPJ_FALSE);
#endif
        result.header = opj_read_header(stream, decoder, &image);
        if (result.header)
        {
            result.decoded = opj_decode(decoder, stream, image) && image && image->numcomps;
            opj_end_decompress(decoder, stream);
        }
        if (result.decoded)
        {
            result.factor = image->comps[0].factor;
            result.width = image->comps[0].w;
            result.height = image->comps[0].h;
            if (result.factor == 0 && result.width == unsigned(full_width) && result.height == unsigned(full_height))
            {
                result.pixelsExact = true;
                for (OPJ_UINT32 c = 0; c < image->numcomps; ++c)
                {
                    for (int y = 0; y < full_height; ++y)
                    {
                        for (int x = 0; x < full_width; ++x)
                        {
                            result.pixelsExact = result.pixelsExact
                                && image->comps[c].data[y * full_width + x] == pixel(x, y, int(c));
                        }
                    }
                }
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

    void test_rule()
    {
        static_assert(LLJ2CReduce::reduceFactor(5, 5) == 4, "a request below the coarsest level gets the coarsest");
        check(LLJ2CReduce::reduceFactor(5, 6) == 5, "6 resolutions: discard 5 unchanged (every texture in the cache)");
        check(LLJ2CReduce::reduceFactor(5, 5) == 4, "5 resolutions (16 px): discard 5 becomes 4");
        check(LLJ2CReduce::reduceFactor(5, 4) == 3, "4 resolutions (8 px): discard 5 becomes 3");
        check(LLJ2CReduce::reduceFactor(5, 1) == 0, "1 resolution: discard 0");
        check(LLJ2CReduce::reduceFactor(4, 5) == 4 && LLJ2CReduce::reduceFactor(0, 5) == 0,
              "levels the codestream has are unchanged");
        check(LLJ2CReduce::reduceFactor(5, 0) == 5, "resolutions not known: unchanged");
        check(LLJ2CReduce::reduceFactor(-1, 5) == -1, "a negative request is passed on unchanged, as before");
    }

    // A codestream with fewer resolutions than the first request's level: fails today, decodes at its coarsest now
    void test_fewer_resolutions(int width, int height, int resolutions, const char* what)
    {
        const std::vector<unsigned char> codestream = encode(width, height, resolutions);
        check(!codestream.empty(), what);
        if (codestream.empty())
        {
            return;
        }

        const Header header = readHeader(codestream);
        check(header.ok && header.width == width && header.height == height, "the header gives the full size");
        check(header.resolutions == resolutions, "the header gives the codestream's resolutions");

        // before the fix: the first request (discard 5) as it was passed on
        const Decoded old_way = decodeAt(codestream, 5, width, height);
        check(!old_way.header && !old_way.decoded, "a reduce factor at or above the resolutions fails the header read");
        check(lastError.find("resolutions") != std::string::npos, "OpenJPEG names the resolutions as the reason");

        // the fix: decoded at the coarsest level the codestream has, and that level is reported
        const int reduce = LLJ2CReduce::reduceFactor(5, header.resolutions);
        check(reduce == resolutions - 1, "the first request decodes at the coarsest level");
        const Decoded fixed = decodeAt(codestream, reduce, width, height);
        check(fixed.header && fixed.decoded, "the first request decodes");
        check(fixed.factor == unsigned(resolutions - 1), "the decoded level is the coarsest, as reported to the fetcher");
        // processFetchResults: the full size is the decoded size shifted by the reported level
        check((fixed.width << fixed.factor) == unsigned(width) && (fixed.height << fixed.factor) == unsigned(height),
              "the decoded size and level give back the full size");

        // the whole image still decodes exactly (the refinement the texture asks for next)
        const Decoded full = decodeAt(codestream, LLJ2CReduce::reduceFactor(0, header.resolutions), width, height);
        check(full.decoded && full.pixelsExact, "discard 0 decodes the whole image losslessly");
    }

    // A codestream with at least 6 resolutions: discard 5 is passed on unchanged and decodes as before
    void test_enough_resolutions(int width, int height, int resolutions)
    {
        const std::vector<unsigned char> codestream = encode(width, height, resolutions);
        check(!codestream.empty(), "a codestream with 6 or more resolutions encodes");
        if (codestream.empty())
        {
            return;
        }
        const Header header = readHeader(codestream);
        check(header.ok && header.resolutions == resolutions, "the header gives its resolutions");
        check(LLJ2CReduce::reduceFactor(5, header.resolutions) == 5, "discard 5 is passed on unchanged");
        const Decoded at5 = decodeAt(codestream, 5, width, height);
        check(at5.decoded && at5.factor == 5, "discard 5 decodes at level 5, as before");
        check(at5.width == unsigned((width + 31) >> 5) && at5.height == unsigned((height + 31) >> 5),
              "at the size of level 5");
    }
}

int main()
{
    std::printf("OpenJPEG %s\n", opj_version());
    test_rule();
    // the audit's case: 16 x 16, 5 resolutions (what the encoder picks for a 16 px side)
    test_fewer_resolutions(16, 16, 5, "16 x 16 with 5 resolutions encodes");
    // a strip with a 16 px side: 512 x 16, 5 resolutions
    test_fewer_resolutions(512, 16, 5, "512 x 16 with 5 resolutions encodes");
    // 8 x 8: 4 resolutions
    test_fewer_resolutions(8, 8, 4, "8 x 8 with 4 resolutions encodes");
    // 32 x 32 (6 resolutions) and 64 x 64 (7, the encoder's rule for one 64 px tile): unchanged
    test_enough_resolutions(32, 32, 6);
    test_enough_resolutions(64, 64, 7);
    if (failures == 0)
    {
        std::printf("llj2creduce_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
