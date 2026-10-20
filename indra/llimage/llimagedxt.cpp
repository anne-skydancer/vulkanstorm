/**
 * @file llimagedxt.cpp
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

#include "llimagedxt.h"
#include "llmemory.h"
#include "vkstorm_ffi.h"

// The DXT container on the vkstorm-rust track: vkstorm-image is the port of
// the viewer's header parsing, mip layout, decode of the uncompressed formats,
// encode with the generated mip chain, and DXT to DXR conversion. Where the
// C++ aborted (LL_ERRS) on malformed input the port reports an error.

//static
void LLImageDXT::checkMinWidthHeight(EFileFormat format, S32& width, S32& height)
{
    vks_dxt_check_min_width_height((S32)format, &width, &height);
}

//static
S32 LLImageDXT::formatBits(EFileFormat format)
{
    S32 bits = vks_dxt_format_bits((S32)format);
    if (!bits)
    {
        LL_ERRS() << "LLImageDXT::Unknown format: " << format << LL_ENDL;
    }
    return bits;
};

//static
S32 LLImageDXT::formatBytes(EFileFormat format, S32 width, S32 height)
{
    S32 bytes = vks_dxt_format_bytes((S32)format, width, height);
    if (!bytes)
    {
        LL_ERRS() << "LLImageDXT::Unknown format: " << format << LL_ENDL;
    }
    return bytes;
}

//static
S32 LLImageDXT::formatComponents(EFileFormat format)
{
    S32 components = vks_dxt_format_components((S32)format);
    if (!components)
    {
        LL_ERRS() << "LLImageDXT::Unknown format: " << format << LL_ENDL;
    }
    return components;
};

// static
LLImageDXT::EFileFormat LLImageDXT::getFormat(S32 fourcc)
{
    return EFileFormat(vks_dxt_format_from_fourcc(fourcc));
}

//static
S32 LLImageDXT::getFourCC(EFileFormat format)
{
    return vks_dxt_fourcc((S32)format);
}

//static
void LLImageDXT::calcDiscardWidthHeight(S32 discard_level, EFileFormat format, S32& width, S32& height)
{
    vks_dxt_calc_discard_width_height(discard_level, (S32)format, &width, &height);
}

//static
S32 LLImageDXT::calcNumMips(S32 width, S32 height)
{
    return vks_dxt_calc_num_mips(width, height);
}

//============================================================================

LLImageDXT::LLImageDXT()
    : LLImageFormatted(IMG_CODEC_DXT),
      mFileFormat(FORMAT_UNKNOWN),
      mHeaderSize(0)
{
}

LLImageDXT::~LLImageDXT()
{
}

namespace
{
    VksDxtHeader headerOf(LLImageDXT& image, LLImageDXT::EFileFormat format, S32 header_size)
    {
        VksDxtHeader header{};
        header.width = image.getWidth();
        header.height = image.getHeight();
        header.components = (U32)image.getComponents();
        header.format = (S32)format;
        header.header_size = header_size;
        header.discard_level = image.getDiscardLevel();
        return header;
    }
}

// virtual
bool LLImageDXT::updateData()
{
    resetLastError();

    LLImageDataLock lock(this);

    U8* data = getData();
    S32 data_size = getDataSize();

    if (!data || !data_size)
    {
        setLastError("LLImageDXT uninitialized");
        return false;
    }

    VksDxtHeader header{};
    if (!vks_dxt_header(data, (size_t)data_size, &header))
    {
        const std::string err(vks_image_last_error());
        setLastError(err);
        LL_WARNS() << "LLImageDXT: " << err << LL_ENDL;
        return false;
    }

    mHeaderSize = header.header_size;
    mFileFormat = EFileFormat(header.format);
    setSize((S32)header.width, (S32)header.height, (S32)header.components);
    setDiscardLevel((S8)header.discard_level);

    return true;
}

// discard: 0 = largest (last) mip
S32 LLImageDXT::getMipOffset(S32 discard)
{
    S32 offset = vks_dxt_mip_offset((S32)mFileFormat, mHeaderSize, getWidth(), getHeight(), discard);
    if (offset < 0)
    {
        LL_ERRS() << "getMipOffset called with old (unsupported) format" << LL_ENDL;
    }
    return offset;
}

void LLImageDXT::setFormat()
{
    S32 ncomponents = getComponents();
    switch (ncomponents)
    {
      case 3: mFileFormat = FORMAT_DXR1; break;
      case 4: mFileFormat = FORMAT_DXR3; break;
      default: LL_ERRS() << "LLImageDXT::setFormat called with ncomponents = " << ncomponents << LL_ENDL;
    }
    mHeaderSize = calcHeaderSize();
}

// virtual
bool LLImageDXT::decode(LLImageRaw* raw_image, F32 time)
{
    llassert_always(raw_image);

    if (mFileFormat >= FORMAT_DXT1 && mFileFormat <= FORMAT_DXR5)
    {
        LL_WARNS() << "Attempt to decode compressed LLImageDXT to Raw (unsupported)" << LL_ENDL;
        return false;
    }

    LLImageDataSharedLock lockIn(this);
    LLImageDataLock lockOut(raw_image);

    if (!getData())
    {
        setLastError("LLImageDXT trying to decode an image with not enough data!");
        return false;
    }

    const VksDxtHeader header = headerOf(*this, mFileFormat, mHeaderSize);
    VksRaster* decoded = vks_dxt_decode(getData(), (size_t)getDataSize(), &header);
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
        setLastError("llImageDXT failed to resize image!");
        return false;
    }
    memcpy(raw_image->getData(), vks_raster_data(decoded), vks_raster_len(decoded)); /* Flawfinder: ignore */
    vks_raster_free(decoded);

    return true;
}

bool LLImageDXT::getMipData(LLPointer<LLImageRaw>& raw, S32 discard)
{
    if (discard < 0)
    {
        discard = mDiscardLevel;
    }
    else if (discard < mDiscardLevel)
    {
        LL_ERRS() << "Request for invalid discard level" << LL_ENDL;
    }

    LLImageDataSharedLock lock(this);

    const VksDxtHeader header = headerOf(*this, mFileFormat, mHeaderSize);
    VksRaster* mip = vks_dxt_mip_data(getData(), (size_t)getDataSize(), &header, discard);
    if (!mip)
    {
        setLastError(vks_image_last_error());
        return false;
    }
    raw = new LLImageRaw(vks_raster_data(mip), (U16)vks_raster_width(mip), (U16)vks_raster_height(mip), (S8)vks_raster_components(mip));
    vks_raster_free(mip);
    return true;
}

// virtual
bool LLImageDXT::encode(const LLImageRaw* raw_image, F32 time)
{
    llassert_always(raw_image);

    if (raw_image->isBufferInvalid())
    {
        setLastError("Invalid input, no buffer");
        return false;
    }

    S32 ncomponents = raw_image->getComponents();
    EFileFormat format;
    switch (ncomponents)
    {
      case 1:
        format = FORMAT_A8;
        break;
      case 3:
        format = FORMAT_RGB8;
        break;
      case 4:
        format = FORMAT_RGBA8;
        break;
      default:
        LL_ERRS() << "LLImageDXT::encode: Unhandled channel number: " << ncomponents << LL_ENDL;
        return 0;
    }

    LLImageDataLock lock(this);

    S32 width = raw_image->getWidth();
    S32 height = raw_image->getHeight();

    setSize(width, height, ncomponents);
    mHeaderSize = sizeof(dxtfile_header_t);
    mFileFormat = format;

    VksBytes* encoded = vks_dxt_encode(raw_image->getData(), (size_t)raw_image->getDataSize(), width, height, (U32)ncomponents);
    if (!encoded)
    {
        setLastError(vks_image_last_error());
        return false;
    }

    const size_t totbytes = vks_bytes_len(encoded);
    U8* data = allocateData((S32)totbytes);
    if (data)
    {
        memcpy(data, vks_bytes_data(encoded), totbytes); /* Flawfinder: ignore */
    }
    vks_bytes_free(encoded);

    return data != NULL;
}

// virtual
bool LLImageDXT::convertToDXR()
{
    switch (mFileFormat)
    {
      case FORMAT_DXR1:
      case FORMAT_DXR2:
      case FORMAT_DXR3:
      case FORMAT_DXR4:
      case FORMAT_DXR5:
        return false; // nothing to do
      case FORMAT_DXT1:
      case FORMAT_DXT2:
      case FORMAT_DXT3:
      case FORMAT_DXT4:
      case FORMAT_DXT5:
        break;
      default:
        LL_WARNS() << "convertToDXR: can not convert format: " << llformat("0x%08x",getFourCC(mFileFormat)) << LL_ENDL;
        return false;
    }

    LLImageDataLock lock(this);

    const VksDxtHeader header = headerOf(*this, mFileFormat, mHeaderSize);
    VksBytes* converted = vks_dxt_convert_to_dxr(getData(), (size_t)getDataSize(), &header);
    if (!converted)
    {
        LL_WARNS() << "convertToDXR failed: " << vks_image_last_error() << LL_ENDL;
        return false;
    }

    const S32 total_bytes = (S32)vks_bytes_len(converted);
    U8* newdata = (U8*)ll_aligned_malloc_16(total_bytes);
    if (!newdata)
    {
        vks_bytes_free(converted);
        LLError::LLUserWarningMsg::showOutOfMemory();
        LL_WARNS() << "Out of memory in LLImageDXT::convertToDXR()" << LL_ENDL;
        return false;
    }
    memcpy(newdata, vks_bytes_data(converted), total_bytes); /* Flawfinder: ignore */
    vks_bytes_free(converted);
    setData(newdata, total_bytes);
    updateData();
    return true;
}

// virtual
S32 LLImageDXT::calcHeaderSize()
{
    return static_cast<S32>(llmax(sizeof(dxtfile_header_old_t), sizeof(dxtfile_header_t)));
}

// virtual
S32 LLImageDXT::calcDataSize(S32 discard_level)
{
    if (mFileFormat == FORMAT_UNKNOWN)
    {
        LL_ERRS() << "calcDataSize called with unloaded LLImageDXT" << LL_ENDL;
        return 0;
    }
    const VksDxtHeader header = headerOf(*this, mFileFormat, mHeaderSize);
    S32 bytes = vks_dxt_data_size(&header, discard_level);
    if (bytes < 0)
    {
        LL_ERRS() << "calcDataSize failed: " << vks_image_last_error() << LL_ENDL;
        return 0;
    }
    return bytes;
}

//============================================================================
