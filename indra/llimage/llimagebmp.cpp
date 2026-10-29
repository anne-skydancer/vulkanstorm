/**
 * @file llimagebmp.cpp
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

#include "linden_common.h"

#include "llimagebmp.h"
#include "llerror.h"
#include "vkstorm_ffi.h"

// The BMP codec on the vkstorm-rust track: vkstorm-image is the port of the
// viewer's reader (8, 24 and 32 bits per pixel, bitfield masks, either row
// order; always 3 components out) and writer (8-bit with a colour table or
// 24-bit).

/**
 * LLImageBMP
 */
LLImageBMP::LLImageBMP()
    :
    LLImageFormatted(IMG_CODEC_BMP)
{
}

LLImageBMP::~LLImageBMP()
{
}


bool LLImageBMP::updateData()
{
    resetLastError();

    LLImageDataLock lock(this);

    // Check to make sure that this instance has been initialized with data
    U8* mdata = getData();
    if (!mdata || (0 == getDataSize()))
    {
        setLastError("Uninitialized instance of LLImageBMP");
        return false;
    }

    // Read the bitmap headers in order to get all the useful info
    // about this image
    VksImageHeader header{};
    if (!vks_bmp_header(mdata, (size_t)getDataSize(), &header))
    {
        setLastError(vks_image_last_error());
        return false;
    }

    setSize((S32)header.width, (S32)header.height, (S32)header.components);
    return true;
}

bool LLImageBMP::decode(LLImageRaw* raw_image, F32 decode_time)
{
    llassert_always(raw_image);

    resetLastError();

    LLImageDataLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    // Check to make sure that this instance has been initialized with data
    const U8* mdata = getData();
    if (!mdata || (0 == getDataSize()))
    {
        setLastError("llimagebmp trying to decode an image with no data!");
        return false;
    }

    VksRaster* decoded = vks_bmp_decode(mdata, (size_t)getDataSize());
    if (!decoded)
    {
        setLastError(vks_image_last_error());
        return false;
    }

    const U32 width = vks_raster_width(decoded);
    const U32 height = vks_raster_height(decoded);
    const U32 components = vks_raster_components(decoded);
    if (!raw_image->resize((U16)width, (U16)height, (S8)components) || raw_image->isBufferInvalid())
    {
        vks_raster_free(decoded);
        setLastError("llimagebmp failed to resize image!");
        return false;
    }
    memcpy(raw_image->getData(), vks_raster_data(decoded), vks_raster_len(decoded));
    vks_raster_free(decoded);

    return true;
}

bool LLImageBMP::encode(const LLImageRaw* raw_image, F32 encode_time)
{
    llassert_always(raw_image);

    resetLastError();

    LLImageDataSharedLock lockIn(raw_image);
    LLImageDataLock lockOut(this);

    S32 src_components = raw_image->getComponents();
    S32 dst_components =  ( src_components < 3 ) ? 1 : 3;

    if( (2 == src_components) || (4 == src_components) )
    {
        LL_INFOS() << "Dropping alpha information during BMP encoding" << LL_ENDL;
    }

    if (raw_image->isBufferInvalid())
    {
        setLastError("Invalid input, no buffer");
        return false;
    }

    setSize(raw_image->getWidth(), raw_image->getHeight(), dst_components);

#if LL_DARWIN
    const S32 pels_per_meter = 2834; // 72dpi
#else
    const S32 pels_per_meter = 0;
#endif

    VksBytes* encoded = vks_bmp_encode(raw_image->getData(), (size_t)raw_image->getDataSize(),
                                       raw_image->getWidth(), raw_image->getHeight(), (U32)src_components, pels_per_meter);
    if (!encoded)
    {
        setLastError(vks_image_last_error());
        return false;
    }

    const size_t file_bytes = vks_bytes_len(encoded);
    // Allocate the new buffer for the data.
    U8* mdata = allocateData((S32)file_bytes);
    if (!mdata) //memory allocation failed
    {
        vks_bytes_free(encoded);
        return false ;
    }
    memcpy(mdata, vks_bytes_data(encoded), file_bytes);
    vks_bytes_free(encoded);

    return true;
}
