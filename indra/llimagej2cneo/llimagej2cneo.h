// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The viewer's J2C implementation on neo-j2k (USE_NEOJ2K, cmake/NeoJ2K.cmake): the decoder and the encoder of
// llimagej2coj, on neo-j2k's C API. One codec per binary: this class is what fallbackCreateLLImageJ2CImpl()
// makes in a build with USE_NEOJ2K, and llimagej2coj is not built then.

#ifndef LL_LLIMAGEJ2CNEO_H
#define LL_LLIMAGEJ2CNEO_H

#include "llimagej2c.h"

const F32 LAST_TCP_RATE = 1.f/DEFAULT_COMPRESSION_RATE; // should be 8, giving a 1:8 ratio

class LLImageJ2CNeo : public LLImageJ2CImpl
{
public:
    LLImageJ2CNeo();
    virtual ~LLImageJ2CNeo();
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
