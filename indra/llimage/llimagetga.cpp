/**
 * @file llimagetga.cpp
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

#include "llimagetga.h"

#include "lldir.h"
#include "llerror.h"
#include "llmath.h"
#include "llpointer.h"
#include "vkstorm_ffi.h"

// The TGA codec on the vkstorm-rust track: vkstorm-image is the port of the
// viewer's reader (true colour, monochrome and colour-mapped, raw and
// run-length, either origin; an opaque 32-bit image decodes to 3 components)
// and writer, and of the alpha-gradient processing used for avatar masks.

namespace
{
    // Copy a decoded raster into raw_image and release it.
    bool takeRaster(VksRaster* decoded, LLImageRaw* raw_image, std::string& error)
    {
        const U32 width = vks_raster_width(decoded);
        const U32 height = vks_raster_height(decoded);
        const U32 components = vks_raster_components(decoded);
        if (!raw_image->resize((U16)width, (U16)height, (S8)components) || raw_image->isBufferInvalid())
        {
            vks_raster_free(decoded);
            error = "LLImageTGA::out of memory";
            return false;
        }
        memcpy(raw_image->getData(), vks_raster_data(decoded), vks_raster_len(decoded));
        vks_raster_free(decoded);
        return true;
    }
}

LLImageTGA::LLImageTGA()
    : LLImageFormatted(IMG_CODEC_TGA),
      mImageType(0),
      mOriginRightBit(0),
      mOriginTopBit(0)
{
}

LLImageTGA::LLImageTGA(const std::string& file_name)
    : LLImageFormatted(IMG_CODEC_TGA),
      mImageType(0),
      mOriginRightBit(0),
      mOriginTopBit(0)
{
    loadFile(file_name);
}

LLImageTGA::~LLImageTGA()
{
}

bool LLImageTGA::updateData()
{
    resetLastError();

    LLImageDataLock lock(this);

    // Check to make sure that this instance has been initialized with data
    if (!getData() || (0 == getDataSize()))
    {
        setLastError("LLImageTGA uninitialized");
        return false;
    }

    VksTgaHeader header{};
    if (!vks_tga_header(getData(), (size_t)getDataSize(), &header))
    {
        setLastError(vks_image_last_error());
        return false;
    }

    mImageType = (U8)header.image_type;
    mOriginRightBit = (U8)header.origin_right;
    mOriginTopBit = (U8)header.origin_top;
    setSize((S32)header.width, (S32)header.height, (S32)header.components);

    return true;
}

bool LLImageTGA::decode(LLImageRaw* raw_image, F32 decode_time)
{
    llassert_always(raw_image);

    LLImageDataSharedLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    // Check to make sure that this instance has been initialized with data
    if (!getData() || (0 == getDataSize()))
    {
        setLastError("LLImageTGA trying to decode an image with no data!");
        return false;
    }

    VksRaster* decoded = vks_tga_decode(getData(), (size_t)getDataSize());
    if (!decoded)
    {
        setLastError(vks_image_last_error());
        return false;
    }

    std::string error;
    if (!takeRaster(decoded, raw_image, error))
    {
        setLastError(error);
        return false;
    }
    return true;
}

bool LLImageTGA::encode(const LLImageRaw* raw_image, F32 encode_time)
{
    llassert_always(raw_image);

    LLImageDataSharedLock lockIn(raw_image);
    LLImageDataLock lockOut(this);

    deleteData();

    setSize(raw_image->getWidth(), raw_image->getHeight(), raw_image->getComponents());

    VksBytes* encoded = vks_tga_encode(raw_image->getData(), (size_t)raw_image->getDataSize(),
                                       raw_image->getWidth(), raw_image->getHeight(), (U32)raw_image->getComponents());
    if (!encoded)
    {
        return false;
    }

    // Uncompressed true colour or monochrome, bottom-left origin.
    mImageType = (raw_image->getComponents() == 1) ? 3 : 2;
    mOriginRightBit = 0;
    mOriginTopBit = 0;

    const size_t size = vks_bytes_len(encoded);
    U8* dst = allocateData((S32)size);
    if (dst)
    {
        memcpy(dst, vks_bytes_data(encoded), size);
    }
    vks_bytes_free(encoded);
    return dst != NULL;
}

// Decoded and process the image for use in avatar gradient masks.
// Processing happens during the decode for speed.
bool LLImageTGA::decodeAndProcess( LLImageRaw* raw_image, F32 domain, F32 weight )
{
    llassert_always(raw_image);

    // "Domain" isn't really the right word.  It refers to the width of the
    // ramp portion of the function that relates input and output pixel values.
    // A domain of 0 gives a step function.
    //
    //   |                      /----------------
    //  O|                     / |
    //  u|                    /  |
    //  t|                   /   |
    //  p|------------------/    |
    //  u|                  |    |
    //  t|<---------------->|<-->|
    //   |  "offset"         "domain"
    //   |
    // --+---Input--------------------------------
    //   |

    LLImageDataSharedLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    if (!getData() || (0 == getDataSize()))
    {
        setLastError("LLImageTGA trying to decode an image with no data!");
        return false;
    }

    // Only works for unflipped monochrome RLE images
    if( (getComponents() != 1) || (mImageType != 11) || mOriginTopBit || mOriginRightBit )
    {
        LL_WARNS() << "LLImageTGA trying to alpha-gradient process an image that's not a standard RLE, one component image" << LL_ENDL;
        LL_WARNS() << "getComponents(): " << getComponents() << " mImageType: " << mImageType << " mOriginTopBit:" << mOriginTopBit << " mOriginRightBit: " << mOriginRightBit << LL_ENDL;
        return false;
    }

    VksRaster* decoded = vks_tga_decode_and_process(getData(), (size_t)getDataSize(), domain, weight);
    if (!decoded)
    {
        const std::string err(vks_image_last_error());
        setLastError(err);
        LL_WARNS() << "LLImageTGA alpha-gradient processing failed: " << err << LL_ENDL;
        return false;
    }

    std::string error;
    if (!takeRaster(decoded, raw_image, error))
    {
        LL_ERRS() << "LLImageTGA: Failed to resize image" << LL_ENDL;
        return false;
    }
    return true;
}

// Reads a .tga file and creates an LLImageTGA with its data.
bool LLImageTGA::loadFile( const std::string& path )
{
    auto len = path.size();
    if( len < 5 )
    {
        return false;
    }

    std::string extension = gDirUtilp->getExtension(path);
    if( "tga" != extension )
    {
        return false;
    }

    // <FS:ND> FIRE-16342 make sure no one overwrites this file while we load it
    // LLFILE* file = LLFile::fopen(path, "rb");    /* Flawfinder: ignore */
#ifndef LL_WINDOWS
    LLFILE* file = LLFile::fopen(path, "rb");   /* Flawfinder: ignore */
#else
    LLFILE* file = LLFile::fsopen(path, "rb", _SH_DENYWR);/* Flawfinder: ignore */
#endif
    // </FS:ND>

    if( !file )
    {
        LL_WARNS() << "Couldn't open file " << path << LL_ENDL;
        return false;
    }

    S32 file_size = 0;
    if (!fseek(file, 0, SEEK_END))
    {
        file_size = ftell(file);
        fseek(file, 0, SEEK_SET);
    }

    U8* buffer = allocateData(file_size);
    if(!buffer)
    {
        LL_WARNS() << "could not allocate memory for image loading, size: " << file_size << LL_ENDL;
        fclose(file); // <FS:ND/> Do not leak the file handle.
        return false;
    }

    S32 bytes_read = static_cast<S32>(fread(buffer, 1, file_size, file));
    if( bytes_read != file_size )
    {
        deleteData();
        LL_WARNS() << "Couldn't read file " << path << LL_ENDL;
        fclose(file); // <FS:ND/> Do not leak the file handle.
        return false;
    }

    fclose( file );

    if( !updateData() )
    {
        LL_WARNS() << "Couldn't decode file " << path << LL_ENDL;
        deleteData();
        return false;
    }
    return true;
}
