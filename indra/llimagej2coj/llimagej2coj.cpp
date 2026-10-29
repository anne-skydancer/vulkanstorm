/**
 * @file llimagej2coj.cpp
 * @brief JPEG 2000 codec implementation for the viewer, in Rust.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llimagej2coj.h"
#include "vkstorm_ffi.h"

// Factory function: see declaration in llimagej2c.cpp
LLImageJ2CImpl* fallbackCreateLLImageJ2CImpl()
{
    return new LLImageJ2COJ();
}

LLImageJ2COJ::LLImageJ2COJ() = default;
LLImageJ2COJ::~LLImageJ2COJ() = default;

std::string LLImageJ2COJ::getEngineInfo() const
{
    // LLTextureCache stores this string in a 32-byte header field and
    // LL_ERRS when it does not fit, so keep it short and stable; a change
    // purges the texture cache, which a codec change should do anyway.
    return std::string(vks_j2c_engine_info());
}

bool LLImageJ2COJ::initDecode(LLImageJ2C &base, LLImageRaw &raw_image, int discard_level, int* region)
{
    base.mDiscardLevel = discard_level;
    return false;
}

bool LLImageJ2COJ::initEncode(LLImageJ2C &base, LLImageRaw &raw_image, int blocks_size, int precincts_size, int levels)
{
    // The encoder derives block, precinct and level settings from the image
    // size the way the viewer always has; these hints are not used.
    return false;
}

bool LLImageJ2COJ::getMetadata(LLImageJ2C &base)
{
    LLImageDataLock lock(&base);

    VksJ2cMetadata meta{};
    if (!vks_j2c_metadata(base.getData(), (size_t)base.getDataSize(), &meta))
    {
        return false;
    }
    base.mDiscardLevel = 0;
    base.setSize((S32)meta.width, (S32)meta.height, (S32)meta.components);
    return true;
}

bool LLImageJ2COJ::decodeImpl(LLImageJ2C &base, LLImageRaw &raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLImageDataLock lockIn(&base);
    LLImageDataLock lockOut(&raw_image);

    const S32 data_size = base.getDataSize();
    const S32 max_bytes = base.getMaxBytes() ? base.getMaxBytes() : data_size;
    const U32 discard = (U32)llmax(0, (S32)base.mDiscardLevel);

    VksJ2cDecoded* decoded = vks_j2c_decode(base.getData(), (size_t)data_size, (size_t)max_bytes,
                                            discard, (U32)llmax(0, first_channel), (U32)llmax(0, max_channel_count));
    if (!decoded)
    {
        LL_DEBUGS("Texture") << "ERROR -> decodeImpl: failed to decode image!" << LL_ENDL;
        base.decodeFailed();
        return true; // done
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
        return true; // done
    }

    const size_t bytes = (size_t)width * height * channels;
    if (!vks_j2c_decoded_copy(decoded, rawp, bytes))
    {
        vks_j2c_decoded_free(decoded);
        base.setLastError("Decoded size mismatch");
        base.decodeFailed();
        return true; // done
    }

    if (const char* comment = vks_j2c_decoded_comment(decoded))
    {
        raw_image.mComment.assign(comment);
    }
    vks_j2c_decoded_free(decoded);

    base.setDiscardLevel((S8)discard);
    return true; // done
}

bool LLImageJ2COJ::encodeImpl(LLImageJ2C &base, const LLImageRaw &raw_image, const char* comment_text, F32 encode_time, bool reversible)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (raw_image.isBufferInvalid())
    {
        base.setLastError("Invalid input, no buffer");
        return false;
    }

    LLImageDataSharedLock lockIn(&raw_image);
    LLImageDataLock lockOut(&base);

    VksJ2cEncoded* encoded = vks_j2c_encode(raw_image.getData(), (size_t)raw_image.getDataSize(),
                                            raw_image.getWidth(), raw_image.getHeight(), (U32)raw_image.getComponents(),
                                            comment_text, reversible ? 1 : 0);
    if (!encoded)
    {
        base.setLastError("JPEG 2000 encoding failed");
        LL_WARNS() << "JPEG 2000 encoding was unsuccessful, returning false" << LL_ENDL;
        return false;
    }

    const size_t len = vks_j2c_encoded_len(encoded);
    bool ok = base.allocateData((S32)len) != nullptr;
    if (ok)
    {
        memcpy(base.getData(), vks_j2c_encoded_data(encoded), len);
        base.updateData();
    }
    else
    {
        base.setLastError("Memory error");
    }
    vks_j2c_encoded_free(encoded);
    return ok;
}
