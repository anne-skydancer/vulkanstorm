/**
 * @file llimagej2coj.h
 * @brief JPEG 2000 codec implementation for the viewer, in Rust.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */
#ifndef LL_LLIMAGEJ2COJ_H
#define LL_LLIMAGEJ2COJ_H

#include "llimagej2c.h"

const F32 LAST_TCP_RATE = 1.f/DEFAULT_COMPRESSION_RATE; // should be 8, giving a 1:8 ratio

// The JPEG 2000 implementation on the vkstorm-rust track: decoding
// (hayro-jpeg2000) and encoding (j2k) through the vkstorm-j2c crate, reached
// over the C ABI in vkstorm_ffi.h. The class keeps its historical name so the
// rest of the tree is unchanged; nothing in it is OpenJPEG any more.
class LLImageJ2COJ : public LLImageJ2CImpl
{
public:
    LLImageJ2COJ();
    virtual ~LLImageJ2COJ();
protected:
    virtual bool getMetadata(LLImageJ2C &base);
    virtual bool decodeImpl(LLImageJ2C &base, LLImageRaw &raw_image, F32 decode_time, S32 first_channel, S32 max_channel_count);
    virtual bool encodeImpl(LLImageJ2C &base, const LLImageRaw &raw_image, const char* comment_text, F32 encode_time=0.0,
                                bool reversible = false);
    virtual bool initDecode(LLImageJ2C &base, LLImageRaw &raw_image, int discard_level = -1, int* region = NULL);
    virtual bool initEncode(LLImageJ2C &base, LLImageRaw &raw_image, int blocks_size = -1, int precincts_size = -1, int levels = 0);
    virtual std::string getEngineInfo() const;
};

#endif
