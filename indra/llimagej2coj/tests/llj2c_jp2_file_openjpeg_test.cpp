// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone check against OpenJPEG (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I "$pkg/include/openjpeg" indra/llimagej2coj/tests/llj2c_jp2_file_openjpeg_test.cpp libopenjp2.a -lm -lpthread -o jp2test
// The viewer decodes textures with OPJ_CODEC_J2K (JPEG2KDecode::decode, llimagej2coj.cpp), the raw codestream the
// Second Life asset server stores. A real .jp2 file is the JP2 box format around a codestream: the J2K decoder
// cannot read it, so the viewer must not map the "jp2" extension to the J2C codec (llimage.cpp; the table is
// checked by scripts/tests/test_jp2_extension.py). A raw codestream of the same image decodes.

#include "openjpeg.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        std::printf("%s %s\n", cond ? "ok  " : "FAIL", what);
        if (!cond)
        {
            ++failures;
        }
    }

    void quiet(const char*, void*) {}

    // Encode a small grey ramp in the given container format to the file.
    bool encode(const std::string& path, OPJ_CODEC_FORMAT format)
    {
        const OPJ_UINT32 size = 32;
        opj_cparameters_t params;
        opj_set_default_encoder_parameters(&params);
        params.tcp_numlayers = 1;
        params.cp_disto_alloc = 1;
        params.tcp_rates[0] = 0; // lossless

        opj_image_cmptparm_t comp;
        std::memset(&comp, 0, sizeof(comp));
        comp.dx = comp.dy = 1;
        comp.w = comp.h = size;
        comp.prec = 8;
        comp.sgnd = 0;
        opj_image_t* image = opj_image_create(1, &comp, OPJ_CLRSPC_GRAY);
        if (!image)
        {
            return false;
        }
        image->x0 = image->y0 = 0;
        image->x1 = image->y1 = size;
        for (OPJ_UINT32 i = 0; i < size * size; ++i)
        {
            image->comps[0].data[i] = (OPJ_INT32)((i * 7) & 0xff);
        }

        opj_codec_t* codec = opj_create_compress(format);
        opj_set_info_handler(codec, quiet, nullptr);
        opj_set_warning_handler(codec, quiet, nullptr);
        opj_set_error_handler(codec, quiet, nullptr);
        bool ok = opj_setup_encoder(codec, &params, image);
        opj_stream_t* stream = ok ? opj_stream_create_default_file_stream(path.c_str(), OPJ_FALSE) : nullptr;
        ok = ok && stream && opj_start_compress(codec, image, stream) && opj_encode(codec, stream) &&
             opj_end_compress(codec, stream);
        if (stream)
        {
            opj_stream_destroy(stream);
        }
        opj_destroy_codec(codec);
        opj_image_destroy(image);
        return ok;
    }

    // Read the header and decode with the J2K decoder, as the viewer does.
    bool decode_as_j2k(const std::string& path)
    {
        opj_dparameters_t params;
        opj_set_default_decoder_parameters(&params);
        opj_codec_t* codec = opj_create_decompress(OPJ_CODEC_J2K);
        opj_set_info_handler(codec, quiet, nullptr);
        opj_set_warning_handler(codec, quiet, nullptr);
        opj_set_error_handler(codec, quiet, nullptr);
        opj_stream_t* stream = opj_stream_create_default_file_stream(path.c_str(), OPJ_TRUE);
        opj_image_t* image = nullptr;
        bool ok = opj_setup_decoder(codec, &params) && stream && opj_read_header(stream, codec, &image) &&
                  opj_decode(codec, stream, image) && opj_end_decompress(codec, stream) &&
                  image && image->comps[0].w == 32 && image->comps[0].h == 32 &&
                  image->comps[0].data[1] == 7;
        if (stream)
        {
            opj_stream_destroy(stream);
        }
        opj_destroy_codec(codec);
        if (image)
        {
            opj_image_destroy(image);
        }
        return ok;
    }
}

int main(int argc, char** argv)
{
    std::string dir = argc > 1 ? argv[1] : ".";
    const std::string j2c = dir + "/jp2test.j2c";
    const std::string jp2 = dir + "/jp2test.jp2";
    check(encode(j2c, OPJ_CODEC_J2K), "encode a raw J2C codestream");
    check(encode(jp2, OPJ_CODEC_JP2), "encode a real JP2 box file");
    check(decode_as_j2k(j2c), "the J2K decoder reads the raw codestream");
    check(!decode_as_j2k(jp2), "the J2K decoder refuses the JP2 file");
    std::remove(j2c.c_str());
    std::remove(jp2.c_str());
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
