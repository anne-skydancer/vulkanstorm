/**
 * @file llimagej2crust.cpp
 * @brief JPEG 2000 implementation backed by the Rust decoder, with OpenJPEG fallback.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llimagej2crust.h"
#include "vkstorm_ffi.h"

// Factory function: see declaration in llimagej2c.cpp
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
    return new LLImageJ2CRust();
}

std::atomic<bool> LLImageJ2CRust::sRustEnabled{true};
std::atomic<U32> LLImageJ2CRust::sRustDecodes{0};
std::atomic<U32> LLImageJ2CRust::sFallbackDecodes{0};

LLImageJ2CRust::LLImageJ2CRust() = default;
LLImageJ2CRust::~LLImageJ2CRust() = default;

void LLImageJ2CRust::setRustEnabled(bool enabled) { sRustEnabled.store(enabled, std::memory_order_relaxed); }
bool LLImageJ2CRust::isRustEnabled() { return sRustEnabled.load(std::memory_order_relaxed); }
U32 LLImageJ2CRust::getRustDecodeCount() { return sRustDecodes.load(std::memory_order_relaxed); }
U32 LLImageJ2CRust::getFallbackDecodeCount() { return sFallbackDecodes.load(std::memory_order_relaxed); }

std::string LLImageJ2CRust::getEngineInfo() const
{
    // LLTextureCache stores this string in a 32-byte header field and
    // LL_ERRS when it does not fit, so keep it short and stable; a change
    // purges the texture cache, which a decoder change should do anyway.
    return std::string(vks_j2c_engine_info());
}

bool LLImageJ2CRust::getMetadata(LLImageJ2C& base)
{
    if (!isRustEnabled())
    {
        return LLImageJ2COJ::getMetadata(base);
    }

    VksJ2cMetadata meta{};
    {
        LLImageDataLock lock(&base);
        if (!vks_j2c_metadata(base.getData(), (size_t)base.getDataSize(), &meta))
        {
            return LLImageJ2COJ::getMetadata(base);
        }
    }
    base.mDiscardLevel = 0;
    base.setSize((S32)meta.width, (S32)meta.height, (S32)meta.components);
    return true;
}

bool LLImageJ2CRust::decodeRust(LLImageJ2C& base, LLImageRaw& raw_image, S32 first_channel, S32 max_channel_count, bool& done)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageDataLock lockIn(&base);
    LLImageDataLock lockOut(&raw_image);

    done = false;
    const S32 data_size = base.getDataSize();
    const S32 max_bytes = base.getMaxBytes() ? base.getMaxBytes() : data_size;
    const U32 discard = (U32)llmax(0, (S32)base.mDiscardLevel);

    VksJ2cDecoded* decoded = vks_j2c_decode(base.getData(), (size_t)data_size, (size_t)max_bytes,
                                            discard, (U32)llmax(0, first_channel), (U32)llmax(0, max_channel_count));
    if (!decoded)
    {
        return false;
    }

    U32 width = 0, height = 0, channels = 0, components = 0;
    vks_j2c_decoded_size(decoded, &width, &height, &channels, &components);

    raw_image.resize((U16)width, (U16)height, (S8)channels);
    U8* rawp = raw_image.getData();
    if (!rawp)
    {
        vks_j2c_decoded_free(decoded);
        base.setLastError("Memory error");
        base.decodeFailed();
        done = true;
        return true;
    }

    const size_t bytes = (size_t)width * height * channels;
    if (!vks_j2c_decoded_copy(decoded, rawp, bytes))
    {
        vks_j2c_decoded_free(decoded);
        return false;
    }

    if (const char* comment = vks_j2c_decoded_comment(decoded))
    {
        raw_image.mComment.assign(comment);
    }
    vks_j2c_decoded_free(decoded);

    base.setDiscardLevel((S8)discard);
    done = true;
    return true;
}

bool LLImageJ2CRust::decodeImpl(LLImageJ2C& base, LLImageRaw& raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count)
{
    if (isRustEnabled())
    {
        bool done = false;
        if (decodeRust(base, raw_image, first_channel, max_channel_count, done))
        {
            sRustDecodes.fetch_add(1, std::memory_order_relaxed);
            return done;
        }
        sFallbackDecodes.fetch_add(1, std::memory_order_relaxed);
        LL_DEBUGS("Texture") << "Rust J2C decode failed; falling back to OpenJPEG" << LL_ENDL;
    }
    return LLImageJ2COJ::decodeImpl(base, raw_image, decode_time, first_channel, max_channel_count);
}
