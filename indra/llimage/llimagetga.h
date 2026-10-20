/**
 * @file llimagetga.h
 * @brief Image implementation to compresses and decompressed TGA files.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLIMAGETGA_H
#define LL_LLIMAGETGA_H

#include "llimage.h"

// This class compresses and decompressed TGA (targa) files. The codec is the
// vkstorm-image port of the viewer's reader and writer (vkstorm-rust track).

class LLImageTGA : public LLImageFormatted
{
protected:
    virtual ~LLImageTGA();

public:
    LLImageTGA();
    LLImageTGA(const std::string& file_name);

    /*virtual*/ std::string getExtension() { return std::string("tga"); }
    /*virtual*/ bool updateData();
    /*virtual*/ bool decode(LLImageRaw* raw_image, F32 decode_time=0.0);
    /*virtual*/ bool encode(const LLImageRaw* raw_image, F32 encode_time=0.0);

    bool             decodeAndProcess(LLImageRaw* raw_image, F32 domain, F32 weight);

private:
    bool             loadFile(const std::string& file_name);

private:
    // Data from header, kept for diagnostics
    U8 mImageType;      // Supported: 2 = Uncompressed true color, 3 = uncompressed monochrome without colormap
    U8 mOriginRightBit; // 1 bit: origin, 0 = left, 1 = right
    U8 mOriginTopBit;   // 1 bit: origin, 0 = bottom, 1 = top
};

#endif
