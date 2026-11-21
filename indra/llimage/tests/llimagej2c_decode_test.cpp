// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file llimagej2c_decode_test.cpp
 * @brief Characterisation tests for LLImageJ2COJ::decodeImpl on malformed codestreams (and, built with
 *        LL_J2C_NEOJ2K, for LLImageJ2CNeo::decodeImpl: the same tests, fixtures patched where neo-j2k's encoder
 *        takes no such image)
 *        (short comment blocks, oversized images).
 */

#include "linden_common.h"
#include "../test/lltut.h"
#include "../llimage.h"
#include "../llimagej2c.h"
#include "../../llimagej2coj/llj2ccommon.h" // LL_J2C_CACHE_KEY

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

        // A codestream whose header declares a w x 64 image with 3 components. OpenJPEG's encoder makes one; the
        // viewer's own limits (a side of 4096 at most) are those of neo-j2k's encoder, so a build with USE_NEOJ2K
        // takes a small image's codestream and patches the size in SIZ: the decode is refused from the header,
        // before any sample is read, as it is for a real codestream of that size.
        static std::vector<U8> encode_wide(U32 w)
        {
#ifdef LL_J2C_NEOJ2K
            std::vector<U8> bytes = encode(64, 64, 3);
            if (bytes.size() < 40 || bytes[2] != 0xff || bytes[3] != 0x51)
            {
                return std::vector<U8>();
            }
            // SOC, SIZ marker, Lsiz, Rsiz, then Xsiz (offset 8), Ysiz, XOsiz, YOsiz, XTsiz (offset 24), YTsiz
            for (int off : { 8, 24 })
            {
                bytes[off] = static_cast<U8>(w >> 24);
                bytes[off + 1] = static_cast<U8>(w >> 16);
                bytes[off + 2] = static_cast<U8>(w >> 8);
                bytes[off + 3] = static_cast<U8>(w);
            }
            return bytes;
#else
            return encode(static_cast<U16>(w), 64, 3);
#endif
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
        std::vector<U8> wide = encode_wide(5000);
        ensure("encoded wide", !wide.empty());
        LLPointer<LLImageRaw> refused = decode(wide);
        ensure_equals("nothing decoded", refused->getWidth(), 0);
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<4>()
    {
        set_test_name("a header declaring more than 16384 is refused");
        std::vector<U8> bytes = encode_wide(17000);
        ensure("encoded", !bytes.empty());
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("nothing decoded", raw->getWidth(), 0);
    }

    // Encodes a 32x32 image whose components 1 and 2 are subsampled by 2 (so their buffers are 16x16).
    // factor_mismatch is not constructible through the encoder; component size mismatch is.
    static std::vector<U8> encode_subsampled()
    {
        std::vector<U8> out;
#ifdef LL_J2C_NEOJ2K
        // neo-j2k's encoder takes no subsampled component: a 32 x 32 codestream, patched so that its SIZ gives
        // components 1 and 2 a separation of 2 and its COD no colour transform. The header is then the one the
        // OpenJPEG encoder writes for this image, and the decode is refused at the components' sizes.
        out = llimagej2c_decode_test::encode(32, 32, 3);
        if (out.size() < 60 || out[2] != 0xff || out[3] != 0x51)
        {
            return std::vector<U8>();
        }
        // SIZ: Csiz at offset 40, then Ssiz, XRsiz, YRsiz of each component
        for (int c = 1; c < 3; ++c)
        {
            out[42 + 3 * c + 1] = 2;
            out[42 + 3 * c + 2] = 2;
        }
        size_t cod = 0;
        for (size_t i = 4; i + 10 < out.size(); ++i)
        {
            if (out[i] == 0xff && out[i + 1] == 0x52)
            {
                cod = i;
                break;
            }
        }
        if (cod == 0)
        {
            return std::vector<U8>();
        }
        out[cod + 9] = 0; // SGcod: the multiple component transform
        return out;
#else
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
#endif
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

    // The viewer's own J2C interface through whichever codec the build has (OpenJPEG, or neo-j2k with USE_NEOJ2K):
    // the same expectations in both, which is what makes them a parity check.

    // A smooth gradient with some detail, so that a lossy encode is close to it and a lossless one is exact
    static LLPointer<LLImageRaw> pattern(U16 w, U16 h, S8 comps)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(w, h, comps);
        U8* p = raw->getData();
        for (S32 y = 0; p && y < h; ++y)
        {
            for (S32 x = 0; x < w; ++x)
            {
                for (S32 c = 0; c < comps; ++c)
                {
                    p[(y * w + x) * comps + c] = static_cast<U8>(x * 255 / (w > 1 ? w - 1 : 1) / 2 + y * 255 / (h > 1 ? h - 1 : 1) / 3 + c * 17 + ((x * 7 + y * 13) & 7));
                }
            }
        }
        return raw;
    }

    static LLPointer<LLImageJ2C> encodeImage(LLImageRaw* raw, const char* comment, bool reversible)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        j2c->setReversible(reversible);
        if (!j2c->encode(raw, comment, 0.0f) || j2c->getDataSize() <= 0)
        {
            return nullptr;
        }
        return j2c;
    }

    static LLPointer<LLImageJ2C> copyOf(const U8* data, S32 size)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        U8* dst = j2c->allocateData(size);
        memcpy(dst, data, size);
        return j2c;
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<6>()
    {
        set_test_name("a lossy encode is smaller than its source, reads back its size and is close to it");
        LLPointer<LLImageRaw> raw = pattern(128, 96, 3);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, "lossy", false);
        ensure("encoded", j2c.notNull());
        ensure("smaller than the source", j2c->getDataSize() < raw->getDataSize() / 2);
        LLPointer<LLImageJ2C> read = copyOf(j2c->getData(), j2c->getDataSize());
        ensure("the header reads", read->updateData());
        ensure_equals("width", read->getWidth(), 128);
        ensure_equals("height", read->getHeight(), 96);
        ensure_equals("components", read->getComponents(), 3);
        read->setDiscardLevel(0);
        LLPointer<LLImageRaw> out = new LLImageRaw();
        ensure("decodes", read->decode(out, 0.f));
        ensure_equals("decoded width", out->getWidth(), 128);
        ensure_equals("decoded components", out->getComponents(), 3);
        S64 error = 0;
        for (S32 i = 0; i < raw->getDataSize(); ++i)
        {
            error += abs((S32)raw->getData()[i] - (S32)out->getData()[i]);
        }
        ensure("a mean error under 6 levels", error / raw->getDataSize() < 6);
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<7>()
    {
        set_test_name("a lossless encode of any small size and one to four components decodes exactly");
        for (U16 side : { 4, 5, 8, 16, 31, 32, 33, 63, 64, 100 })
        {
            for (S8 comps = 1; comps <= 4; ++comps)
            {
                LLPointer<LLImageRaw> raw = pattern(side, side + 3, comps);
                LLPointer<LLImageJ2C> j2c = encodeImage(raw, nullptr, true);
                ensure("encoded " + std::to_string(side) + "x" + std::to_string(comps), j2c.notNull());
                std::vector<U8> bytes(j2c->getData(), j2c->getData() + j2c->getDataSize());
                LLPointer<LLImageRaw> out = decode(bytes);
                ensure_equals("width " + std::to_string(side), out->getWidth(), side);
                ensure_equals("height", out->getHeight(), side + 3);
                ensure_equals("components", out->getComponents(), comps);
                ensure("exact " + std::to_string(side) + "x" + std::to_string(comps), memcmp(raw->getData(), out->getData(), raw->getDataSize()) == 0);
            }
        }
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<8>()
    {
        set_test_name("a comment survives the round trip, and no comment is the default one");
        LLPointer<LLImageRaw> raw = pattern(32, 32, 3);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, "round trip comment", true);
        ensure("encoded", j2c.notNull());
        LLPointer<LLImageJ2C> read = copyOf(j2c->getData(), j2c->getDataSize());
        read->setDiscardLevel(0);
        LLPointer<LLImageRaw> out = new LLImageRaw();
        ensure("decodes", read->decode(out, 0.f));
        ensure_equals("comment", out->mComment, std::string("round trip comment"));
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<9>()
    {
        set_test_name("the header gives the size and the components of a codestream");
        LLPointer<LLImageRaw> raw = pattern(256, 128, 4);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, nullptr, true);
        ensure("encoded", j2c.notNull());
        LLPointer<LLImageJ2C> read = copyOf(j2c->getData(), j2c->getDataSize());
        ensure("reads", read->updateData());
        ensure_equals("width", read->getWidth(), 256);
        ensure_equals("height", read->getHeight(), 128);
        ensure_equals("components", read->getComponents(), 4);
        LLPointer<LLImageJ2C> few = copyOf(j2c->getData(), 10);
        ensure("a header cut short does not read", !few->updateData());
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<10>()
    {
        set_test_name("each discard level decodes at half the size, down to the 5th");
        LLPointer<LLImageRaw> raw = pattern(256, 256, 3);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, nullptr, true);
        ensure("encoded", j2c.notNull());
        for (S32 discard = 0; discard <= 5; ++discard)
        {
            LLPointer<LLImageJ2C> read = copyOf(j2c->getData(), j2c->getDataSize());
            ensure("reads", read->updateData());
            read->setDiscardLevel(discard);
            LLPointer<LLImageRaw> out = new LLImageRaw();
            ensure("decodes " + std::to_string(discard), read->decode(out, 0.f));
            ensure_equals("width at discard " + std::to_string(discard), out->getWidth(), 256 >> discard);
            ensure_equals("height", out->getHeight(), 256 >> discard);
            ensure_equals("discard level", read->getDiscardLevel(), discard);
        }
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<11>()
    {
        set_test_name("a cut download decodes what it holds");
        LLPointer<LLImageRaw> raw = pattern(128, 128, 3);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, nullptr, false);
        ensure("encoded", j2c.notNull());
        for (S32 percent : { 90, 60, 30 })
        {
            const S32 cut = j2c->getDataSize() * percent / 100;
            std::vector<U8> bytes(j2c->getData(), j2c->getData() + cut);
            LLPointer<LLImageRaw> out = decode(bytes);
            ensure_equals("width of the cut at " + std::to_string(percent), out->getWidth(), 128);
            ensure_equals("components", out->getComponents(), 3);
        }
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<12>()
    {
        set_test_name("the channel after the colour (the bake mask) decodes alone");
        LLPointer<LLImageRaw> raw = pattern(64, 64, 5);
        LLPointer<LLImageJ2C> j2c = encodeImage(raw, nullptr, true);
        ensure("encoded", j2c.notNull());
        LLPointer<LLImageJ2C> read = copyOf(j2c->getData(), j2c->getDataSize());
        ensure("reads", read->updateData());
        ensure_equals("components", read->getComponents(), 5);
        read->setDiscardLevel(0);
        LLPointer<LLImageRaw> mask = new LLImageRaw();
        ensure("decodes the channel", read->decodeChannels(mask, 0.f, 4, 4));
        ensure_equals("one channel", mask->getComponents(), 1);
        ensure_equals("width", mask->getWidth(), 64);
        bool same = true;
        for (S32 i = 0; i < 64 * 64; ++i)
        {
            same = same && mask->getData()[i] == raw->getData()[i * 5 + 4];
        }
        ensure("the channel is the fifth", same);
        LLPointer<LLImageRaw> colour = new LLImageRaw();
        read->setDiscardLevel(0);
        ensure("decodes the colour", read->decodeChannels(colour, 0.f, 0, 4));
        ensure_equals("four channels", colour->getComponents(), 4);
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<14>()
    {
        set_test_name("both codecs give the texture cache the same key, so that switching never purges it");
        // The OpenJPEG build computes it from the library, so the constant is checked against the real value there;
        // the neo-j2k build returns the constant.
        ensure_equals("texture cache key", LLImageJ2C::getEngineInfo(), std::string(LL_J2C_CACHE_KEY));
        ensure("the codec's name is given apart", !LLImageJ2C::getEngineName().empty());
    }

    template<> template<>
    void llimagej2c_decode_test_object_t::test<13>()
    {
        set_test_name("a JP2 box file is not a codestream: refused, not decoded");
        // the 12-byte JP2 signature box and a file type box, then padding: what a .jp2 file starts with
        const U8 jp2[] = { 0x00, 0x00, 0x00, 0x0c, 0x6a, 0x50, 0x20, 0x20, 0x0d, 0x0a, 0x87, 0x0a,
                           0x00, 0x00, 0x00, 0x14, 'f', 't', 'y', 'p', 'j', 'p', '2', ' ', 0, 0, 0, 0, 'j', 'p', '2', ' ',
                           0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
        LLPointer<LLImageJ2C> j2c = copyOf(jp2, sizeof(jp2));
        ensure("the header is refused", !j2c->updateData());
        std::vector<U8> bytes(jp2, jp2 + sizeof(jp2));
        LLPointer<LLImageRaw> raw = decode(bytes);
        ensure_equals("nothing decoded", raw->getWidth(), 0);
    }
}
