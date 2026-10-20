/**
 * @file llimagej2crust.h
 * @brief JPEG 2000 implementation backed by the Rust decoder, with OpenJPEG fallback.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLIMAGEJ2CRUST_H
#define LL_LLIMAGEJ2CRUST_H

#include "llimagej2coj.h"
#include <atomic>

// Decodes with vkstorm-j2c (hayro-jpeg2000) when enabled, otherwise with the
// OpenJPEG implementation it derives from. Encoding always uses OpenJPEG.
// A Rust decode failure falls back to OpenJPEG for that image.
class LLImageJ2CRust : public LLImageJ2COJ
{
public:
    LLImageJ2CRust();
    virtual ~LLImageJ2CRust();

    // Runtime switch (default on). Set from the J2CDecoderRust setting.
    static void setRustEnabled(bool enabled);
    static bool isRustEnabled();

    // Counters for tests and diagnostics.
    static U32 getRustDecodeCount();
    static U32 getFallbackDecodeCount();

protected:
    bool getMetadata(LLImageJ2C& base) override;
    bool decodeImpl(LLImageJ2C& base, LLImageRaw& raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count) override;
    std::string getEngineInfo() const override;

private:
    // Returns false when the Rust decoder could not decode; the caller then
    // falls back to OpenJPEG. Returns true when finished, with done set as
    // decodeImpl must report it.
    bool decodeRust(LLImageJ2C& base, LLImageRaw& raw_image, S32 first_channel, S32 max_channel_count, bool& done);

    static std::atomic<bool> sRustEnabled;
    static std::atomic<U32> sRustDecodes;
    static std::atomic<U32> sFallbackDecodes;
};

#endif
