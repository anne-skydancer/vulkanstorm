/**
 * @file llimagej2coj_test.cpp
 * @brief The Rust JPEG 2000 codec through LLImageJ2C on the bundled textures.
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
#include "llimagej2c.h"
#include "llimagej2coj.h"
#include "lldiriterator.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <vector>

#ifndef VKS_J2C_TEST_TEXTURE_DIR
#error VKS_J2C_TEST_TEXTURE_DIR must point at a directory of .j2c textures
#endif

namespace
{
    std::vector<std::string> listTextures()
    {
        std::vector<std::string> files;
        std::string dir(VKS_J2C_TEST_TEXTURE_DIR);
        LLDirIterator iter(dir, "*.j2c");
        std::string name;
        while (iter.next(name))
        {
            files.push_back(dir + "/" + name);
        }
        return files;
    }

    LLPointer<LLImageJ2C> loadJ2C(const std::string& path)
    {
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        if (!j2c->load(path)) // reads the file and parses the header
        {
            return nullptr;
        }
        return j2c;
    }

    bool decodeAt(LLPointer<LLImageJ2C> j2c, S32 discard, S32 max_bytes, LLPointer<LLImageRaw>& out)
    {
        if (j2c.isNull())
        {
            return false;
        }
        if (max_bytes > 0)
        {
            j2c->setMaxBytes(max_bytes);
        }
        out = new LLImageRaw();
        j2c->initDecode(*out, discard, nullptr);
        return j2c->decode(out, 0.0f) && !out->isBufferInvalid() && out->getWidth() > 0;
    }

    F64 psnr(const LLImageRaw& a, const LLImageRaw& b)
    {
        const S32 n = llmin(a.getDataSize(), b.getDataSize());
        const U8* pa = a.getData();
        const U8* pb = b.getData();
        F64 sq = 0.0;
        for (S32 i = 0; i < n; ++i)
        {
            const F64 d = (F64)pa[i] - (F64)pb[i];
            sq += d * d;
        }
        const F64 mse = sq / llmax(1, n);
        return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    }
}

namespace tut
{
    struct j2coj_data {};
    typedef test_group<j2coj_data> j2coj_group;
    typedef j2coj_group::object j2coj_object;
    tut::j2coj_group j2coj("LLImageJ2COJ");

    // Every bundled texture parses and decodes at full resolution with the
    // dimensions and component count the header declares.
    template<> template<>
    void j2coj_object::test<1>()
    {
        std::vector<std::string> files = listTextures();
        ensure("found bundled .j2c textures", !files.empty());
        for (const std::string& path : files)
        {
            LLPointer<LLImageJ2C> j2c = loadJ2C(path);
            ensure(path + ": header parsed", j2c.notNull());
            LLPointer<LLImageRaw> raw;
            ensure(path + ": decoded", decodeAt(j2c, 0, 0, raw));
            ensure_equals(path + ": width", (S32)raw->getWidth(), j2c->getWidth());
            ensure_equals(path + ": height", (S32)raw->getHeight(), j2c->getHeight());
            ensure_equals(path + ": components", (S32)raw->getComponents(), j2c->getComponents());
        }
    }

    // Discard levels halve the image each step.
    template<> template<>
    void j2coj_object::test<2>()
    {
        for (const std::string& path : listTextures())
        {
            LLPointer<LLImageJ2C> probe = loadJ2C(path);
            if (probe.isNull() || probe->getWidth() < 64 || probe->getHeight() < 64) continue;
            for (S32 discard = 1; discard <= 2; ++discard)
            {
                LLPointer<LLImageRaw> raw;
                ensure(path + ": decoded at discard " + std::to_string(discard), decodeAt(loadJ2C(path), discard, 0, raw));
                ensure_equals(path + ": reduced width", (S32)raw->getWidth(), (probe->getWidth() + (1 << discard) - 1) >> discard);
                ensure_equals(path + ": reduced height", (S32)raw->getHeight(), (probe->getHeight() + (1 << discard) - 1) >> discard);
            }
        }
    }

    // Truncated input (a progressive fetch) still decodes to the full size
    // and resembles the complete decode.
    template<> template<>
    void j2coj_object::test<3>()
    {
        for (const std::string& path : listTextures())
        {
            LLPointer<LLImageJ2C> probe = loadJ2C(path);
            if (probe.isNull()) continue;
            const S32 max_bytes = probe->getDataSize() / 4;
            if (max_bytes < 512) continue; // tiny UI textures: the header alone exceeds a quarter

            LLPointer<LLImageRaw> full, part;
            ensure(path + ": complete decode", decodeAt(loadJ2C(path), 0, 0, full));
            ensure(path + ": truncated decode", decodeAt(loadJ2C(path), 0, max_bytes, part));
            ensure_equals(path + ": truncated width", (S32)part->getWidth(), (S32)full->getWidth());
            ensure_equals(path + ": truncated height", (S32)part->getHeight(), (S32)full->getHeight());
            ensure(path + ": truncated decode resembles the complete one", psnr(*full, *part) > 20.0);
        }
    }

    // Encoding round-trips: lossy at the upload ratio, lossless exactly, with
    // the comment carried in the codestream.
    template<> template<>
    void j2coj_object::test<4>()
    {
        std::vector<std::string> files = listTextures();
        S32 tested = 0;
        for (const std::string& path : files)
        {
            LLPointer<LLImageJ2C> probe = loadJ2C(path);
            if (probe.isNull() || probe->getWidth() < 128 || probe->getHeight() < 128) continue;
            LLPointer<LLImageRaw> src;
            ensure(path + ": source decode", decodeAt(probe, 0, 0, src));

            LLPointer<LLImageJ2C> lossy = new LLImageJ2C();
            ensure(path + ": lossy encode", lossy->encode(src, "vkstorm test", 0.0f));
            ensure(path + ": lossy size is a fraction of raw", lossy->getDataSize() < src->getDataSize() / 3);
            LLPointer<LLImageRaw> back;
            ensure(path + ": lossy decode", decodeAt(lossy, 0, 0, back));
            ensure_equals(path + ": lossy width", (S32)back->getWidth(), (S32)src->getWidth());
            ensure(path + ": lossy round trip quality", psnr(*src, *back) > 30.0);
            ensure_equals(path + ": comment carried", back->mComment, std::string("vkstorm test"));

            LLPointer<LLImageJ2C> lossless = new LLImageJ2C();
            lossless->setReversible(true);
            ensure(path + ": lossless encode", lossless->encode(src, 0.0f));
            LLPointer<LLImageRaw> exact;
            ensure(path + ": lossless decode", decodeAt(lossless, 0, 0, exact));
            ensure_equals(path + ": lossless size", exact->getDataSize(), src->getDataSize());
            ensure(path + ": lossless round trip exact", memcmp(exact->getData(), src->getData(), src->getDataSize()) == 0);
            ++tested;
        }
        ensure("encoded at least one texture", tested > 0);
    }

    // The engine string fits LLTextureCache's 32-byte header field, and junk
    // input is rejected.
    template<> template<>
    void j2coj_object::test<5>()
    {
        const std::string info = LLImageJ2C::getEngineInfo();
        ensure("engine info names the Rust codec", info.find("Rust") != std::string::npos);
        ensure("engine info fits the texture cache header", info.size() + 1 <= 32);

        const std::string junk_path = "llimagej2coj_junk.j2c";
        {
            std::ofstream out(junk_path, std::ios::binary);
            std::vector<char> junk(256, 0x5a);
            out.write(junk.data(), (std::streamsize)junk.size());
        }
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        ensure("junk has no metadata", !j2c->load(junk_path));
        std::remove(junk_path.c_str());
    }
}
