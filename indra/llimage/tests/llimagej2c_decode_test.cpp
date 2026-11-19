// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file llimagej2c_decode_test.cpp
 * @brief Characterisation tests for LLImageJ2COJ::decodeImpl on malformed codestreams
 *        (short comment blocks, oversized images).
 */

#include "linden_common.h"
#include "../test/lltut.h"
#include "../llimage.h"
#include "../llimagej2c.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "openjpeg.h"
#include <vector>

namespace tut
{
    struct llimagej2c_decode_test
    {
        llimagej2c_decode_test() { LLImage::initClass(); }
        ~llimagej2c_decode_test() { LLImage::cleanupClass(); }

        // Encode a w x h image of the given component count. Returns the codestream bytes (empty on failure).
        static std::vector<U8> encode(U16 w, U16 h, S8 comps)
        {
            LLPointer<LLImageRaw> raw = new LLImageRaw(w, h, comps);
            U8* p = raw->getData();
            for (S32 i = 0; p && i < raw->getDataSize(); ++i)
            {
                p[i] = static_cast<U8>(i * 7);
            }
            LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
            j2c->setReversible(true);
            if (!j2c->encode(raw, 0.0f) || j2c->getDataSize() <= 0)
            {
                return std::vector<U8>();
            }
            return std::vector<U8>(j2c->getData(), j2c->getData() + j2c->getDataSize());
        }

        // Runs a full decode; returns the raw image (width 0 when the decode was refused).
        static LLPointer<LLImageRaw> decode(const std::vector<U8>& bytes)
        {
            LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
            U8* data = j2c->allocateData(static_cast<S32>(bytes.size()));
            memcpy(data, bytes.data(), bytes.size());
            j2c->setDiscardLevel(0); // updateData() would pick a coarser level from the tiny byte count
            LLPointer<LLImageRaw> raw = new LLImageRaw();
            j2c->decode(raw, 0.0f);
            return raw;
        }
    };

    typedef test_group<llimagej2c_decode_test> llimagej2c_decode_test_t;
    typedef llimagej2c_decode_test_t::object llimagej2c_decode_test_object_t;
    tut::llimagej2c_decode_test_t tut_llimagej2c_decode_test("LLImageJ2CDecode");

    template<> template<>
    void llimagej2c_decode_test_object_t::test<1>()
    {
        set_test_name("a well-formed image round-trips");
        std::vector<U8> bytes = encode(32, 32, 3);
        ensure("encoded", !bytes.empty());
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("width", raw->getWidth(), 32);
        ensure_equals("height", raw->getHeight(), 32);
        ensure_equals("components", raw->getComponents(), 3);
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<2>()
    {
        set_test_name("comment blocks with c_length 0..3 do not throw");
        std::vector<U8> bytes = encode(16, 16, 3);
        ensure("encoded", bytes.size() > 4);
        for (U8 c_length = 0; c_length <= 3; ++c_length)
        {
            // FF 64 <length hi> <length lo> then padding that makes the byte at position+2+c_length an 0xFF
            std::vector<U8> patched(bytes.begin(), bytes.begin() + 2); // SOC
            const U8 block[] = { 0xff, 0x64, 0x00, c_length, 0xff, 0xff, 0xff, 0xff };
            patched.insert(patched.end(), block, block + sizeof(block));
            patched.insert(patched.end(), bytes.begin() + 2, bytes.end());
            try
            {
                decode(patched);
            }
            catch (const std::exception& e)
            {
                fail(std::string("c_length ") + std::to_string(c_length) + " threw " + e.what());
            }
        }
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<3>()
    {
        set_test_name("a 2048 px wide image decodes, a wider one is refused (SL decoder profile)");
        std::vector<U8> bytes = encode(2048, 64, 3);
        ensure("encoded", !bytes.empty());
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("width", raw->getWidth(), 2048);
        ensure_equals("height", raw->getHeight(), 64);
        std::vector<U8> wide = encode(5000, 64, 3);
        ensure("encoded wide", !wide.empty());
        LLPointer<LLImageRaw> refused = decode(wide);
        ensure_equals("nothing decoded", refused->getWidth(), 0);
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<4>()
    {
        set_test_name("a header declaring more than 16384 is refused");
        std::vector<U8> bytes = encode(17000, 64, 3);
        ensure("encoded", !bytes.empty());
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("nothing decoded", raw->getWidth(), 0);
    }

    // Encodes a 32x32 image whose components 1 and 2 are subsampled by 2 (so their buffers are 16x16).
    // factor_mismatch is not constructible through the encoder; component size mismatch is.
    static std::vector<U8> encode_subsampled()
    {
        std::vector<U8> out;
        opj_image_cmptparm_t cp[3];
        memset(cp, 0, sizeof(cp));
        for (int c = 0; c < 3; ++c)
        {
            cp[c].prec = 8;
            cp[c].sgnd = 0;
            cp[c].dx = c == 0 ? 1 : 2;
            cp[c].dy = c == 0 ? 1 : 2;
            cp[c].w = c == 0 ? 32 : 16;
            cp[c].h = c == 0 ? 32 : 16;
        }
        opj_image_t* image = opj_image_create(3, cp, OPJ_CLRSPC_SYCC);
        if (!image) return out;
        image->x0 = 0; image->y0 = 0; image->x1 = 32; image->y1 = 32;
        for (int c = 0; c < 3; ++c)
            for (OPJ_UINT32 i = 0; i < cp[c].w * cp[c].h; ++i)
                image->comps[c].data[i] = (OPJ_INT32)((i * 5 + c * 11) & 0xff);
        opj_cparameters_t params;
        opj_set_default_encoder_parameters(&params);
        params.tcp_mct = 0;
        params.tcp_numlayers = 1;
        params.cp_disto_alloc = 1;
        params.tcp_rates[0] = 0;
        params.numresolution = 3;
        std::string path = std::string(getenv("TEMP") ? getenv("TEMP") : ".") + "/llimagej2c_subsampled.j2k";
        opj_codec_t* codec = opj_create_compress(OPJ_CODEC_J2K);
        bool ok = codec && opj_setup_encoder(codec, &params, image);
        opj_stream_t* stream = ok ? opj_stream_create_default_file_stream(path.c_str(), OPJ_FALSE) : nullptr;
        ok = ok && stream && opj_start_compress(codec, image, stream) && opj_encode(codec, stream) && opj_end_compress(codec, stream);
        if (stream) opj_stream_destroy(stream);
        if (codec) opj_destroy_codec(codec);
        opj_image_destroy(image);
        if (ok)
        {
            FILE* f = fopen(path.c_str(), "rb");
            if (f)
            {
                U8 buf[4096];
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.insert(out.end(), buf, buf + n);
                fclose(f);
            }
            remove(path.c_str());
        }
        return out;
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<5>()
    {
        set_test_name("components of different size are refused, not read out of bounds");
        std::vector<U8> bytes = encode_subsampled();
        ensure("encoded", !bytes.empty());
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("nothing decoded", raw->getWidth(), 0);
    }
}
