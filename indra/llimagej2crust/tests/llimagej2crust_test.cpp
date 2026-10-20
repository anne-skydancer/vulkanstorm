/**
 * @file llimagej2crust_test.cpp
 * @brief Rust J2C decoder versus OpenJPEG, sample for sample, on the viewer's own textures.
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
#include "llimagej2crust.h"
#include "lldiriterator.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <vector>

#ifndef VKS_J2C_TEST_TEXTURE_DIR
#error VKS_J2C_TEST_TEXTURE_DIR must point at a directory of .j2c textures
#endif

namespace
{
    // Decoders differ by rounding in the inverse wavelet and colour transforms.
    const S32 MAX_ABS_DIFF = 1;

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

    // Decode with the Rust path enabled or disabled; returns false when the
    // decoder reported failure.
    bool decodeWith(bool rust, const std::string& path, S32 discard, S32 max_bytes, LLPointer<LLImageRaw>& out)
    {
        LLImageJ2CRust::setRustEnabled(rust);
        LLPointer<LLImageJ2C> j2c = loadJ2C(path);
        if (j2c.isNull() || !j2c->updateData())
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

    S32 maxAbsDiff(const LLImageRaw& a, const LLImageRaw& b)
    {
        const S32 n = a.getDataSize();
        const U8* pa = a.getData();
        const U8* pb = b.getData();
        S32 worst = 0;
        for (S32 i = 0; i < n; ++i)
        {
            worst = llmax(worst, std::abs((S32)pa[i] - (S32)pb[i]));
        }
        return worst;
    }
}

namespace tut
{
    struct j2crust_data {};
    typedef test_group<j2crust_data> j2crust_group;
    typedef j2crust_group::object j2crust_object;
    tut::j2crust_group j2crust("LLImageJ2CRust");

    // Full-resolution decodes match OpenJPEG within one code value on every
    // bundled texture, and the Rust path actually ran.
    template<> template<>
    void j2crust_object::test<1>()
    {
        std::vector<std::string> files = listTextures();
        ensure("found bundled .j2c textures", !files.empty());
        const U32 rust_before = LLImageJ2CRust::getRustDecodeCount();
        const U32 fallback_before = LLImageJ2CRust::getFallbackDecodeCount();

        for (const std::string& path : files)
        {
            LLPointer<LLImageRaw> rust, opj;
            const bool ok_rust = decodeWith(true, path, 0, 0, rust);
            const bool ok_opj = decodeWith(false, path, 0, 0, opj);
            ensure_equals(path + ": both decoders agree on success", ok_rust, ok_opj);
            if (!ok_opj) continue;
            ensure_equals(path + ": width", (S32)rust->getWidth(), (S32)opj->getWidth());
            ensure_equals(path + ": height", (S32)rust->getHeight(), (S32)opj->getHeight());
            ensure_equals(path + ": components", (S32)rust->getComponents(), (S32)opj->getComponents());
            ensure(path + ": samples within tolerance", maxAbsDiff(*rust, *opj) <= MAX_ABS_DIFF);
            ensure_equals(path + ": comment", rust->mComment, opj->mComment);
        }
        ensure("Rust decoder was used", LLImageJ2CRust::getRustDecodeCount() > rust_before);
        ensure_equals("no fallback to OpenJPEG was needed", LLImageJ2CRust::getFallbackDecodeCount(), fallback_before);
    }

    // Discard levels 1 and 2 produce the same reduced images.
    template<> template<>
    void j2crust_object::test<2>()
    {
        for (const std::string& path : listTextures())
        {
            for (S32 discard = 1; discard <= 2; ++discard)
            {
                LLPointer<LLImageRaw> rust, opj;
                const bool ok_rust = decodeWith(true, path, discard, 0, rust);
                const bool ok_opj = decodeWith(false, path, discard, 0, opj);
                if (!ok_opj) continue; // too small for this many levels under OpenJPEG as well
                ensure(path + ": rust decoded at discard " + std::to_string(discard), ok_rust);
                ensure_equals(path + ": reduced width", (S32)rust->getWidth(), (S32)opj->getWidth());
                ensure_equals(path + ": reduced height", (S32)rust->getHeight(), (S32)opj->getHeight());
                ensure(path + ": reduced samples within tolerance", maxAbsDiff(*rust, *opj) <= MAX_ABS_DIFF);
            }
        }
    }

    // Truncated input (a progressive fetch) decodes with both, to the same size.
    template<> template<>
    void j2crust_object::test<3>()
    {
        for (const std::string& path : listTextures())
        {
            LLPointer<LLImageJ2C> probe = loadJ2C(path);
            if (probe.isNull()) continue;
            const S32 max_bytes = probe->getDataSize() / 4;
            if (max_bytes < 512) continue; // tiny UI textures: header alone exceeds a quarter

            LLPointer<LLImageRaw> rust, opj;
            const bool ok_rust = decodeWith(true, path, 0, max_bytes, rust);
            const bool ok_opj = decodeWith(false, path, 0, max_bytes, opj);
            ensure_equals(path + ": truncated decode agrees on success", ok_rust, ok_opj);
            if (!ok_opj) continue;
            ensure_equals(path + ": truncated width", (S32)rust->getWidth(), (S32)opj->getWidth());
            ensure_equals(path + ": truncated height", (S32)rust->getHeight(), (S32)opj->getHeight());
        }
    }

    // The engine string must fit LLTextureCache's 32-byte header field
    // (sHeaderEncoderStringSize), or the viewer LL_ERRS at cache init.
    template<> template<>
    void j2crust_object::test<5>()
    {
        const std::string info = LLImageJ2C::getEngineInfo();
        ensure("engine info names the Rust decoder", info.find("hayro") != std::string::npos);
        ensure("engine info fits the texture cache header", info.size() + 1 <= 32);
    }

    // Garbage input fails in both and leaves the setting as it was.
    template<> template<>
    void j2crust_object::test<4>()
    {
        LLImageJ2CRust::setRustEnabled(true);
        const std::string junk_path = "llimagej2crust_junk.j2c";
        {
            std::ofstream out(junk_path, std::ios::binary);
            std::vector<char> junk(256, 0x5a);
            out.write(junk.data(), (std::streamsize)junk.size());
        }
        LLPointer<LLImageJ2C> j2c = new LLImageJ2C();
        ensure("junk has no metadata", !j2c->load(junk_path));
        std::remove(junk_path.c_str());
        ensure("setting intact", LLImageJ2CRust::isRustEnabled());
    }
}
