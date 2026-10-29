/*
 * @file llimagepng.cpp
 * @brief LLImageFormatted glue to encode / decode PNG files, in Rust.
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
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

#include "linden_common.h"
#include "stdtypes.h"
#include "llerror.h"

#include "llimage.h"
#include "llimagepng.h"
#include "vkstorm_ffi.h"

// The PNG codec on the vkstorm-rust track: vkstorm-image reproduces what the
// viewer's libpng produced (palettes and low-depth grey expanded, grey
// converted to RGB, tRNS to alpha, 16-bit samples stripped, gamma to a 2.2
// screen when the file gamma makes it significant) and writes what libpng
// wrote (8-bit, libpng's filter choice, deflate at libpng's settings).

namespace
{
    std::string pngError()
    {
        const std::string err(vks_image_last_error());
        return err == "Invalid or corrupt PNG file" ? "LLImagePNG data does not have a valid PNG header!" : err;
    }
}

// ---------------------------------------------------------------------------
// LLImagePNG
// ---------------------------------------------------------------------------
LLImagePNG::LLImagePNG()
    : LLImageFormatted(IMG_CODEC_PNG)
{
}

LLImagePNG::~LLImagePNG()
{
}

// Virtual
// Parse PNG image information and set the appropriate
// width, height and component (channel) information.
bool LLImagePNG::updateData()
{
    resetLastError();

    LLImageDataLock lock(this);

    // Check to make sure that this instance has been initialized with data
    if (!getData() || (0 == getDataSize()))
    {
        setLastError("Uninitialized instance of LLImagePNG");
        return false;
    }

    VksImageHeader info{};
    if (!vks_png_header(getData(), (size_t)getDataSize(), &info))
    {
        setLastError(pngError());
        return false;
    }

    setSize((S32)info.width, (S32)info.height, (S32)info.components);
    return true;
}

// Virtual
// Decode an in-memory PNG image into the raw RGB or RGBA format
// used within SecondLife.
bool LLImagePNG::decode(LLImageRaw* raw_image, F32 decode_time)
{
    llassert_always(raw_image);

    resetLastError();

    LLImageDataSharedLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    // Check to make sure that this instance has been initialized with data
    if (!getData() || (0 == getDataSize()))
    {
        setLastError("LLImagePNG trying to decode an image with no data!");
        return false;
    }

    VksRaster* decoded = vks_png_decode(getData(), (size_t)getDataSize());
    if (!decoded)
    {
        setLastError(pngError());
        return false;
    }

    const U32 width = vks_raster_width(decoded);
    const U32 height = vks_raster_height(decoded);
    const U32 components = vks_raster_components(decoded);
    if (!raw_image->resize((U16)width, (U16)height, (S8)components) || raw_image->isBufferInvalid())
    {
        vks_raster_free(decoded);
        setLastError("Failed to resize image");
        return false;
    }
    memcpy(raw_image->getData(), vks_raster_data(decoded), vks_raster_len(decoded));
    vks_raster_free(decoded);

    return true;
}

// Virtual
// Encode the in memory RGB image into PNG format.
bool LLImagePNG::encode(const LLImageRaw* raw_image, F32 encode_time)
{
    llassert_always(raw_image);

    resetLastError();

    LLImageDataSharedLock lockIn(raw_image);
    LLImageDataLock lockOut(this);

    // Image logical size
    setSize(raw_image->getWidth(), raw_image->getHeight(), raw_image->getComponents());

    VksBytes* encoded = vks_png_encode(raw_image->getData(), (size_t)raw_image->getDataSize(),
                                       raw_image->getWidth(), raw_image->getHeight(), (U32)raw_image->getComponents());
    if (!encoded)
    {
        setLastError(vks_image_last_error());
        return false;
    }

    const size_t encodedSize = vks_bytes_len(encoded);
    if (allocateData((S32)encodedSize))
        memcpy(getData(), vks_bytes_data(encoded), encodedSize);
    else
        LL_WARNS() << "allocateData() failed." << LL_ENDL;

    vks_bytes_free(encoded);

    return (getData() != NULL);
}
