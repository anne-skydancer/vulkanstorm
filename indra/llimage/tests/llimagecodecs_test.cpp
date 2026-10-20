/**
 * @file llimagecodecs_test.cpp
 * @brief The Rust raster codecs through LLImagePNG, LLImageTGA, LLImageBMP
 *        and LLImageDXT on the bundled textures.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "lltut.h"

#include "llimage.h"
#include "llimagebmp.h"
#include "llimagedxt.h"
#include "llimagepng.h"
#include "llimagetga.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <vector>

#ifndef VKS_IMAGE_TEST_SKIN_DIR
#error VKS_IMAGE_TEST_SKIN_DIR must point at the default skin directory
#endif
#ifndef VKS_IMAGE_TEST_MANIFEST
#error VKS_IMAGE_TEST_MANIFEST must point at the CRC manifest of the default skin
#endif
#ifndef VKS_IMAGE_TEST_CHARACTER_DIR
#error VKS_IMAGE_TEST_CHARACTER_DIR must point at the character directory
#endif

namespace
{
    // CRC-32 (IEEE) of a buffer, as zlib's crc32 computes it.
    U32 crc32Of(const U8* data, size_t len)
    {
        static U32 table[256];
        static bool init = false;
        if (!init)
        {
            for (U32 i = 0; i < 256; ++i)
            {
                U32 c = i;
                for (int k = 0; k < 8; ++k) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            init = true;
        }
        U32 crc = 0xFFFFFFFFu;
        for (size_t i = 0; i < len; ++i) crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
        return ~crc;
    }

    struct Expected
    {
        bool fail = false;
        U32 crc = 0;
        S32 width = 0, height = 0, components = 0;
        S32 libpng_bytes = 0; // size of libpng's re-encode of the decoded raster (PNG only)
    };

    // The manifest: path relative to the skin directory (forward slashes) -> expectation.
    std::map<std::string, Expected> loadManifest()
    {
        std::map<std::string, Expected> m;
        std::ifstream in(VKS_IMAGE_TEST_MANIFEST);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#') continue;
            Expected e;
            if (line.rfind("FAIL ", 0) == 0)
            {
                e.fail = true;
                m[line.substr(5)] = e;
                continue;
            }
            std::istringstream s(line);
            std::string crc;
            s >> crc >> e.width >> e.height >> e.components >> e.libpng_bytes;
            std::string path;
            std::getline(s, path);
            if (!path.empty() && path[0] == ' ') path.erase(0, 1);
            e.crc = (U32)std::stoul(crc, nullptr, 16);
            m[path] = e;
        }
        return m;
    }

    std::string relativePath(const std::filesystem::path& p, const std::filesystem::path& root)
    {
        std::string rel = std::filesystem::relative(p, root).generic_string();
        return rel;
    }

    std::vector<std::filesystem::path> listFiles(const std::string& dir, const char* ext)
    {
        std::vector<std::filesystem::path> files;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir))
        {
            if (!entry.is_regular_file()) continue;
            std::string e = entry.path().extension().string();
            for (char& c : e) c = (char)tolower((unsigned char)c);
            if (e == ext) files.push_back(entry.path());
        }
        return files;
    }

    LLPointer<LLImageRaw> synthetic(U16 w, U16 h, S8 c, U32 seed)
    {
        LLPointer<LLImageRaw> raw = new LLImageRaw(w, h, c);
        U8* d = raw->getData();
        U32 state = seed;
        for (S32 i = 0; i < raw->getDataSize(); ++i)
        {
            state = 1103515245u * state + 12345u;
            d[i] = (U8)(((i / c) * 7 + (i % c) * 50 + (state >> 20)) & 0xff);
        }
        return raw;
    }

    bool sameRaster(const LLImageRaw* a, const LLImageRaw* b)
    {
        return a->getWidth() == b->getWidth() && a->getHeight() == b->getHeight()
            && a->getComponents() == b->getComponents() && a->getDataSize() == b->getDataSize()
            && memcmp(a->getData(), b->getData(), a->getDataSize()) == 0;
    }

    // Hand encoded bytes to another formatted image (parses the header).
    void copyEncoded(LLImageFormatted* dst, const U8* data, S32 size)
    {
        U8* buf = dst->allocateData(size);
        memcpy(buf, data, (size_t)size);
        dst->updateData();
    }

    void writeFile(const std::string& path, const std::vector<char>& bytes)
    {
        std::ofstream out(path, std::ios::binary);
        out.write(bytes.data(), (std::streamsize)bytes.size());
    }
}

namespace tut
{
    struct codecs_data {};
    typedef test_group<codecs_data> codecs_group;
    typedef codecs_group::object codecs_object;
    tut::codecs_group codecs("LLImageCodecs");

    // Every PNG and TGA in the default skin decodes through LLImagePNG /
    // LLImageTGA to exactly the raster the C++ codecs produced (CRC manifest).
    template<> template<>
    void codecs_object::test<1>()
    {
        std::map<std::string, Expected> manifest = loadManifest();
        ensure("manifest loaded", manifest.size() > 1000);
        std::filesystem::path root(VKS_IMAGE_TEST_SKIN_DIR);
        S32 checked = 0, failures_expected = 0;
        for (const char* ext : {".png", ".tga"})
        {
            for (const std::filesystem::path& p : listFiles(VKS_IMAGE_TEST_SKIN_DIR, ext))
            {
                const std::string rel = relativePath(p, root);
                auto it = manifest.find(rel);
                ensure(rel + ": in manifest", it != manifest.end());
                LLPointer<LLImageFormatted> image;
                if (ext[1] == 'p') image = new LLImagePNG();
                else image = new LLImageTGA();
                LLPointer<LLImageRaw> raw = new LLImageRaw();
                const bool ok = image->load(p.string()) && image->decode(raw, 0.0f);
                if (it->second.fail)
                {
                    ensure(rel + ": refused like the C++", !ok);
                    ++failures_expected;
                    continue;
                }
                ensure(rel + ": decoded (" + LLImage::getLastThreadError() + ")", ok);
                ensure_equals(rel + ": width", (S32)raw->getWidth(), it->second.width);
                ensure_equals(rel + ": height", (S32)raw->getHeight(), it->second.height);
                ensure_equals(rel + ": components", (S32)raw->getComponents(), it->second.components);
                ensure_equals(rel + ": raster CRC", crc32Of(raw->getData(), (size_t)raw->getDataSize()), it->second.crc);
                ++checked;
            }
        }
        ensure("checked the manifest", checked > 1000);
        ensure_equals("files the C++ refused", failures_expected, 1);
    }

    // The PNG encode gate over every PNG of the default skin: the re-encode
    // decodes to the same raster, each file is within 20% (or 128 bytes) of
    // libpng's size for the same raster, and the corpus does not grow in total.
    template<> template<>
    void codecs_object::test<5>()
    {
        std::map<std::string, Expected> manifest = loadManifest();
        std::filesystem::path root(VKS_IMAGE_TEST_SKIN_DIR);
        S64 total_ours = 0, total_libpng = 0;
        S32 checked = 0;
        for (const std::filesystem::path& p : listFiles(VKS_IMAGE_TEST_SKIN_DIR, ".png"))
        {
            const std::string rel = relativePath(p, root);
            auto it = manifest.find(rel);
            ensure(rel + ": in manifest", it != manifest.end());
            if (it->second.fail || it->second.libpng_bytes <= 0) continue;
            LLPointer<LLImagePNG> png = new LLImagePNG();
            LLPointer<LLImageRaw> raw = new LLImageRaw();
            ensure(rel + ": decoded", png->load(p.string()) && png->decode(raw, 0.0f));
            LLPointer<LLImagePNG> again = new LLImagePNG();
            ensure(rel + ": re-encoded", again->encode(raw, 0.0f));
            LLPointer<LLImagePNG> parsed = new LLImagePNG();
            copyEncoded(parsed, again->getData(), again->getDataSize());
            LLPointer<LLImageRaw> back = new LLImageRaw();
            ensure(rel + ": re-decoded", parsed->decode(back, 0.0f));
            ensure(rel + ": re-encode decodes identically", sameRaster(raw, back));
            // 20%, or 128 bytes for tiny files where a deflate block boundary
            // is a large fraction of the file.
            const F64 ratio = (F64)again->getDataSize() / (F64)it->second.libpng_bytes;
            ensure(rel + ": within 20% or 128 bytes of libpng's size (" + std::to_string(again->getDataSize()) + " vs " + std::to_string(it->second.libpng_bytes) + ")",
                   ratio <= 1.20 || again->getDataSize() <= it->second.libpng_bytes + 128);
            total_ours += again->getDataSize();
            total_libpng += it->second.libpng_bytes;
            ++checked;
        }
        ensure("checked the PNG corpus", checked > 900);
        ensure("PNG corpus does not grow (" + std::to_string(total_ours) + " vs libpng " + std::to_string(total_libpng) + ")", total_ours <= total_libpng);
    }

    // PNG, TGA, BMP and DXT encoders round-trip through their decoders.
    template<> template<>
    void codecs_object::test<2>()
    {
        for (S8 c : {1, 2, 3, 4})
        {
            LLPointer<LLImageRaw> src = synthetic(37, 23, c, 7 + c);

            // PNG: every component count, exact; 1 and 2 decode as grey -> RGB(A).
            LLPointer<LLImagePNG> png = new LLImagePNG();
            ensure("png encode", png->encode(src, 0.0f));
            LLPointer<LLImagePNG> png2 = new LLImagePNG();
            copyEncoded(png2, png->getData(), png->getDataSize());
            LLPointer<LLImageRaw> back = new LLImageRaw();
            ensure("png decode", png2->decode(back, 0.0f));
            ensure_equals("png components", (S32)back->getComponents(), c < 3 ? c + 2 : (S32)c);
            for (S32 i = 0; i < src->getWidth() * src->getHeight(); ++i)
            {
                const U8* s = src->getData() + i * c;
                const U8* d = back->getData() + i * back->getComponents();
                if (c == 1) ensure("png grey", d[0] == s[0] && d[1] == s[0] && d[2] == s[0]);
                else if (c == 2) ensure("png grey alpha", d[0] == s[0] && d[1] == s[0] && d[2] == s[0] && d[3] == s[1]);
                else ensure("png rgb", memcmp(s, d, c) == 0);
            }

            // TGA: 1, 3 and 4 exact; 2 is written as intensity plus alpha.
            LLPointer<LLImageTGA> tga = new LLImageTGA();
            ensure("tga encode", tga->encode(src, 0.0f));
            LLPointer<LLImageTGA> tga2 = new LLImageTGA();
            copyEncoded(tga2, tga->getData(), tga->getDataSize());
            back = new LLImageRaw();
            ensure("tga decode", tga2->decode(back, 0.0f));
            if (c == 2)
            {
                ensure_equals("tga 2 -> 4 components", (S32)back->getComponents(), 4);
            }
            else if (c == 4)
            {
                // alpha is not all opaque in the synthetic image, so 4 stays 4
                ensure("tga rgba round trip", sameRaster(src, back));
            }
            else
            {
                ensure("tga round trip", sameRaster(src, back));
            }

            // BMP: 3 exact; 4 drops alpha; 1 and 2 become grey RGB.
            LLPointer<LLImageBMP> bmp = new LLImageBMP();
            ensure("bmp encode", bmp->encode(src, 0.0f));
            LLPointer<LLImageBMP> bmp2 = new LLImageBMP();
            copyEncoded(bmp2, bmp->getData(), bmp->getDataSize());
            back = new LLImageRaw();
            ensure("bmp decode", bmp2->decode(back, 0.0f));
            ensure_equals("bmp components", (S32)back->getComponents(), 3);
            if (c >= 3)
            {
                for (S32 i = 0; i < src->getWidth() * src->getHeight(); ++i)
                {
                    ensure("bmp rgb", memcmp(src->getData() + i * c, back->getData() + i * 3, 3) == 0);
                }
            }

            // DXT: 1, 3 and 4 exact at discard 0.
            if (c != 2)
            {
                LLPointer<LLImageDXT> dxt = new LLImageDXT();
                ensure("dxt encode", dxt->encode(src, 0.0f));
                LLPointer<LLImageDXT> dxt2 = new LLImageDXT();
                copyEncoded(dxt2, dxt->getData(), dxt->getDataSize());
                ensure_equals("dxt discard", (S32)dxt2->getDiscardLevel(), 0);
                back = new LLImageRaw();
                ensure("dxt decode", dxt2->decode(back, 0.0f));
                ensure("dxt round trip", sameRaster(src, back));

                // Without the finest mip the file decodes at discard 1 to the
                // 2x2 box-filtered image.
                LLPointer<LLImageDXT> part = new LLImageDXT();
                copyEncoded(part, dxt->getData(), dxt2->calcDataSize(1));
                ensure_equals("dxt truncated discard", (S32)part->getDiscardLevel(), 1);
                LLPointer<LLImageRaw> mip = new LLImageRaw();
                ensure("dxt truncated decode", part->decode(mip, 0.0f));
                ensure_equals("dxt mip width", (S32)mip->getWidth(), 18);
                ensure_equals("dxt mip height", (S32)mip->getHeight(), 11);
                std::vector<U8> expected(18 * 11 * c);
                LLImageBase::generateMip(src->getData(), expected.data(), 18, 11, c);
                ensure("dxt mip is the box filter", memcmp(mip->getData(), expected.data(), expected.size()) == 0);
            }
        }
    }

    // The avatar mask processing on a bundled monochrome run-length TGA.
    template<> template<>
    void codecs_object::test<3>()
    {
        S32 processed = 0;
        for (const std::filesystem::path& p : listFiles(VKS_IMAGE_TEST_CHARACTER_DIR, ".tga"))
        {
            LLPointer<LLImageTGA> tga = new LLImageTGA(p.string());
            if (tga->getDataSize() <= 0 || tga->getComponents() != 1) continue;
            LLPointer<LLImageRaw> plain = new LLImageRaw();
            if (!tga->decode(plain)) continue;
            LLPointer<LLImageRaw> step = new LLImageRaw();
            if (!tga->decodeAndProcess(step, 0.0f, 1.0f)) continue; // not run-length: skipped by the viewer too
            ensure_equals(p.string() + ": processed width", (S32)step->getWidth(), (S32)plain->getWidth());
            // weight 1 -> threshold 0: every pixel passes
            for (S32 i = 0; i < step->getDataSize(); ++i) ensure("step function saturates", step->getData()[i] == 0xFF);
            LLPointer<LLImageRaw> ramp = new LLImageRaw();
            ensure(p.string() + ": ramp", tga->decodeAndProcess(ramp, 1.0f, 0.0f));
            // domain 1, weight 0 -> identity ramp (within float rounding of the table)
            for (S32 i = 0; i < plain->getDataSize(); ++i)
            {
                ensure(p.string() + ": identity ramp", abs((int)ramp->getData()[i] - (int)plain->getData()[i]) <= 1);
            }
            ++processed;
        }
        ensure("processed a mask", processed > 0);
    }

    // Error reporting keeps the viewer's messages.
    template<> template<>
    void codecs_object::test<4>()
    {
        const std::string junk_png = "llimagecodecs_junk.png";
        writeFile(junk_png, std::vector<char>(64, 0x5a));
        LLPointer<LLImagePNG> png = new LLImagePNG();
        ensure("junk png refused", !png->load(junk_png));
        ensure_equals("junk png message", LLImage::getLastThreadError(), std::string("LLImagePNG data does not have a valid PNG header!"));
        std::remove(junk_png.c_str());

        // A 4-bit BMP: parses the headers, refuses the depth.
        std::vector<char> bmp(14 + 40 + 64 + 8, 0);
        bmp[0] = 'B'; bmp[1] = 'M'; bmp[10] = (char)(14 + 40 + 64);
        bmp[14] = 40; bmp[18] = 2; bmp[22] = 2; bmp[26] = 1; bmp[28] = 4;
        const std::string bmp_path = "llimagecodecs_4bit.bmp";
        writeFile(bmp_path, bmp);
        LLPointer<LLImageBMP> image = new LLImageBMP();
        ensure("4-bit bmp refused", !image->load(bmp_path));
        ensure_equals("4-bit bmp message", LLImage::getLastThreadError(), std::string("Unsupported bit depth"));
        std::remove(bmp_path.c_str());

        // A TGA with no image data.
        std::vector<char> tga(18 + 4, 0);
        tga[12] = 1; tga[14] = 1; tga[16] = 24;
        const std::string tga_path = "llimagecodecs_type0.tga";
        writeFile(tga_path, tga);
        LLPointer<LLImageTGA> t = new LLImageTGA();
        ensure("type 0 tga refused", !t->load(tga_path));
        ensure_equals("type 0 tga message", LLImage::getLastThreadError(), std::string("Unable to load file.  TGA file contains no image data."));
        std::remove(tga_path.c_str());

        // A DXT file with an old DXT1 four-cc is refused instead of aborting.
        LLPointer<LLImageRaw> src = synthetic(8, 8, 3, 3);
        LLPointer<LLImageDXT> dxt = new LLImageDXT();
        ensure("dxt encode", dxt->encode(src, 0.0f));
        std::vector<char> bad(dxt->getData(), dxt->getData() + dxt->getDataSize());
        const S32 fourcc = 0x31545844; // DXT1
        memcpy(&bad[84], &fourcc, 4);
        const std::string dxt_path = "llimagecodecs_dxt1.dxt";
        writeFile(dxt_path, bad);
        LLPointer<LLImageDXT> d = new LLImageDXT();
        ensure("dxt1 refused", !d->load(dxt_path));
        ensure_equals("dxt1 message", LLImage::getLastThreadError(), std::string("getMipOffset called with old (unsupported) format"));
        std::remove(dxt_path.c_str());
    }
}
