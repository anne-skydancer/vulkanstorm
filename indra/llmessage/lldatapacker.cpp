/**
 * @file lldatapacker.cpp
 * @brief Data packer implementation.
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

// The binary and text forms are written and read by the Rust crate
// vkstorm-messages (vkstorm-rust rung three, see doc/rust_port.md). These
// classes keep their interface and their buffer state (pointer, cursor,
// size, the write and name flags) and hand it to the crate per call; the
// file form keeps its file and stream I/O and uses the crate for the text
// of each value. The warnings the C++ gave are given here.

#include "linden_common.h"

#include "lldatapacker.h"
#include "llerror.h"

#include "message.h"

#include "v4color.h"
#include "v4coloru.h"
#include "v2math.h"
#include "v3math.h"
#include "v4math.h"
#include "lluuid.h"
#include "vkstorm_ffi.h"

// *NOTE: there are functions below which use sscanf and rely on this
// particular value of DP_BUFSIZE. Search for '511' (DP_BUFSIZE - 1)
// to find them if you change this number.
const S32 DP_BUFSIZE = 512;

namespace
{
    // The text of a value line (with its newline, without the name) for
    // the file form.
    std::string value_text(int32_t kind, const int64_t* ints, size_t nints, const float* floats, size_t nfloats, const U8* data, size_t ndata)
    {
        std::string out;
        size_t n = vks_dp_ascii_format(kind, ints, nints, floats, nfloats, data, ndata, NULL, 0);
        out.resize(n);
        if (n)
        {
            vks_dp_ascii_format(kind, ints, nints, floats, nfloats, data, ndata, (uint8_t*)&out[0], n);
        }
        return out;
    }
}

LLDataPacker::LLDataPacker() : mPassFlags(0), mWriteEnabled(false)
{
}

//virtual
void LLDataPacker::reset()
{
    LL_ERRS() << "Using unimplemented datapacker reset!" << LL_ENDL;
}

//virtual
void LLDataPacker::dumpBufferToLog()
{
    LL_ERRS() << "dumpBufferToLog not implemented for this type!" << LL_ENDL;
}

bool LLDataPacker::packFixed(const F32 value, const char *name,
                             const bool is_signed, const U32 int_bits, const U32 frac_bits)
{
    bool success = true;
    int32_t total_bits = 0;
    uint32_t fixed = vks_dp_fixed_pack(value, is_signed, int_bits, frac_bits, &total_bits);

    if (total_bits <= 8)
    {
        packU8((U8)fixed, name);
    }
    else if (total_bits <= 16)
    {
        packU16((U16)fixed, name);
    }
    else if (total_bits <= 31)
    {
        packU32((U32)fixed, name);
    }
    else
    {
        LL_ERRS() << "Using fixed-point packing of " << total_bits << " bits, why?!" << LL_ENDL;
    }
    return success;
}

bool LLDataPacker::unpackFixed(F32 &value, const char *name,
                               const bool is_signed, const U32 int_bits, const U32 frac_bits)
{
    S32 total_bits = vks_dp_fixed_total_bits(is_signed, int_bits, frac_bits);
    if (total_bits > 31)
    {
        LL_ERRS() << "Bad bit count: " << total_bits << LL_ENDL;
        return false;
    }

    U32 fixed = 0;
    bool assigned = false;
    bool success = unpackFixedBits(fixed, total_bits, assigned, name);

    // a value that could not be read, or a text value without a number,
    // leaves the caller's as it was
    if (success && assigned)
    {
        value = vks_dp_fixed_unpack(fixed, is_signed, int_bits, frac_bits);
    }
    return success;
}

// The binary form: a read succeeds only when the bytes are there.
bool LLDataPacker::unpackFixedBits(U32 &fixed, S32 total_bits, bool &assigned, const char *name)
{
    bool success = false;
    if (total_bits <= 8)
    {
        U8 fixed_8 = 0;
        success = unpackU8(fixed_8, name);
        fixed = fixed_8;
    }
    else if (total_bits <= 16)
    {
        U16 fixed_16 = 0;
        success = unpackU16(fixed_16, name);
        fixed = fixed_16;
    }
    else
    {
        U32 fixed_32 = 0;
        success = unpackU32(fixed_32, name);
        fixed = fixed_32;
    }
    assigned = success;
    return success;
}

bool LLDataPacker::unpackU16s(U16 *values, S32 count, const char *name)
{
    for (S32 idx = 0; idx < count; ++idx)
    {
        if (!unpackU16(values[idx], name))
        {
            LL_WARNS("DATAPACKER") << "Buffer overflow reading Unsigned 16s \"" << name << "\" at index " << idx << "!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

bool LLDataPacker::unpackS16s(S16 *values, S32 count, const char *name)
{
    for (S32 idx = 0; idx < count; ++idx)
    {
        if (!unpackS16(values[idx], name))
        {
            LL_WARNS("DATAPACKER") << "Buffer overflow reading Signed 16s \"" << name << "\" at index " << idx << "!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

bool LLDataPacker::unpackF32s(F32 *values, S32 count, const char *name)
{
    for (S32 idx = 0; idx < count; ++idx)
    {
        if (!unpackF32(values[idx], name))
        {
            LL_WARNS("DATAPACKER") << "Buffer overflow reading Float 32s \"" << name << "\" at index " << idx << "!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

bool LLDataPacker::unpackColor4Us(LLColor4U *values, S32 count, const char *name)
{
    for (S32 idx = 0; idx < count; ++idx)
    {
        if (!unpackColor4U(values[idx], name))
        {
            LL_WARNS("DATAPACKER") << "Buffer overflow reading Float 32s \"" << name << "\" at index " << idx << "!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

bool LLDataPacker::unpackUUIDs(LLUUID *values, S32 count, const char *name)
{
    for (S32 idx = 0; idx < count; ++idx)
    {
        if (!unpackUUID(values[idx], name))
        {
            LL_WARNS("DATAPACKER") << "Buffer overflow reading UUIDs \"" << name << "\" at index " << idx << "!" << LL_ENDL;
            return false;
        }
    }
    return true;
}

//---------------------------------------------------------------------------
// LLDataPackerBinaryBuffer implementation
//---------------------------------------------------------------------------

// The buffer state for one call to the crate, written back afterwards.
#define VKS_BINARY_STATE VksDpBuffer s = { mBufferp, mBufferSize, (S32)(mCurBufferp - mBufferp), mWriteEnabled, 0 }
#define VKS_BINARY_DONE mCurBufferp = mBufferp + s.cur

bool LLDataPackerBinaryBuffer::packString(const std::string& value, const char *name)
{
    VKS_BINARY_STATE;
    bool ok = vks_dp_bin_pack_string(&s, (const uint8_t*)value.data(), value.size()) != 0;
    if (!ok) verifyLength((S32)value.length() + 1, name);
    VKS_BINARY_DONE;
    return ok;
}


bool LLDataPackerBinaryBuffer::unpackString(std::string& value, const char *name)
{
    VKS_BINARY_STATE;
    const uint8_t* p = NULL;
    size_t n = 0;
    bool ok = vks_dp_bin_unpack_string(&s, &p, &n) != 0;
    if (ok)
    {
        value.assign((const char*)p, n);
    }
    else
    {
        LL_WARNS() << "Buffer overflow in BinaryBuffer length verify, field name " << name << "!" << LL_ENDL;
    }
    VKS_BINARY_DONE;
    return ok;
}

bool LLDataPackerBinaryBuffer::packBinaryData(const U8 *value, S32 size, const char *name)
{
    VKS_BINARY_STATE;
    bool ok = vks_dp_bin_pack_binary(&s, value, size) != 0;
    if (!ok) verifyLength(size + 4, name);
    VKS_BINARY_DONE;
    return ok;
}


bool LLDataPackerBinaryBuffer::unpackBinaryData(U8 *value, S32 value_size, S32 &out_size, const char *name)
{
    // The size checks and the bounded copy follow upstream #5486
    // (ce139f26e0): value_size bounds what is written to value.
    if (!mBufferp || mCurBufferp < mBufferp || mCurBufferp + 4 > mBufferp + mBufferSize)
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData would unpack invalid data, aborting!" << LL_ENDL;
        out_size = 0;
        return false;
    }
    if (value_size < 0)
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData passed negative buffer size, aborting!" << LL_ENDL;
        out_size = 0;
        return false;
    }
    // the size field is read into the caller's size even when the data
    // behind it cannot be, as it always was
    memcpy(&out_size, mCurBufferp, 4);
    if (out_size < 0)
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData unpacked invalid size, aborting!" << LL_ENDL;
        out_size = 0;
        return false;
    }
    VKS_BINARY_STATE;
    const uint8_t* p = NULL;
    size_t n = 0;
    bool ok = vks_dp_bin_unpack_binary(&s, &p, &n) != 0;
    if (ok)
    {
        out_size = (S32)n;
        S32 copy_size = llmin(out_size, value_size);
        if (copy_size > 0) memcpy(value, p, copy_size);
        if (value_size < out_size)
        {
            LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData buffer too small for data, truncating!" << LL_ENDL;
        }
    }
    else
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData would unpack invalid data, aborting!" << LL_ENDL;
    }
    VKS_BINARY_DONE;
    return ok;
}


bool LLDataPackerBinaryBuffer::packBinaryDataFixed(const U8 *value, S32 size, const char *name)
{
    return packBytes(value, size, name);
}


bool LLDataPackerBinaryBuffer::unpackBinaryDataFixed(U8 *value, S32 size, const char *name)
{
    return unpackBytes(value, size, name);
}


bool LLDataPackerBinaryBuffer::packU8(const U8 value, const char *name)
{
    return packBytes(&value, 1, name);
}


bool LLDataPackerBinaryBuffer::unpackU8(U8 &value, const char *name)
{
    return unpackBytes(&value, 1, name);
}


bool LLDataPackerBinaryBuffer::packU16(const U16 value, const char *name)
{
    return packBytes(&value, 2, name);
}


bool LLDataPackerBinaryBuffer::unpackU16(U16 &value, const char *name)
{
    return unpackBytes(&value, 2, name);
}

bool LLDataPackerBinaryBuffer::packS16(const S16 value, const char *name)
{
    // advances even when refused, as it always did
    bool success = verifyLength(sizeof(S16), name);

    if (mWriteEnabled && success)
    {
        memcpy(mCurBufferp, &value, 2);
    }
    mCurBufferp += 2;
    return success;
}

bool LLDataPackerBinaryBuffer::unpackS16(S16 &value, const char *name)
{
    bool success = verifyLength(sizeof(S16), name) && mBufferp && mCurBufferp >= mBufferp && (mCurBufferp - mBufferp) <= mBufferSize - 2;

    if (success)
    {
        memcpy(&value, mCurBufferp, 2);
    }
    mCurBufferp += 2;
    return success;
}

bool LLDataPackerBinaryBuffer::packU32(const U32 value, const char *name)
{
    return packBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::unpackU32(U32 &value, const char *name)
{
    return unpackBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::packS32(const S32 value, const char *name)
{
    return packBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::unpackS32(S32 &value, const char *name)
{
    return unpackBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::packF32(const F32 value, const char *name)
{
    return packBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::unpackF32(F32 &value, const char *name)
{
    return unpackBytes(&value, 4, name);
}


bool LLDataPackerBinaryBuffer::packColor4(const LLColor4 &value, const char *name)
{
    return packBytes(value.mV, 16, name);
}


bool LLDataPackerBinaryBuffer::unpackColor4(LLColor4 &value, const char *name)
{
    return unpackBytes(value.mV, 16, name);
}


bool LLDataPackerBinaryBuffer::packColor4U(const LLColor4U &value, const char *name)
{
    return packBytes(value.mV, 4, name);
}


bool LLDataPackerBinaryBuffer::unpackColor4U(LLColor4U &value, const char *name)
{
    return unpackBytes(value.mV, 4, name);
}



bool LLDataPackerBinaryBuffer::packVector2(const LLVector2 &value, const char *name)
{
    return packBytes(value.mV, 8, name);
}


bool LLDataPackerBinaryBuffer::unpackVector2(LLVector2 &value, const char *name)
{
    return unpackBytes(value.mV, 8, name);
}


bool LLDataPackerBinaryBuffer::packVector3(const LLVector3 &value, const char *name)
{
    return packBytes(value.mV, 12, name);
}


bool LLDataPackerBinaryBuffer::unpackVector3(LLVector3 &value, const char *name)
{
    return unpackBytes(value.mV, 12, name);
}

bool LLDataPackerBinaryBuffer::packVector4(const LLVector4 &value, const char *name)
{
    return packBytes(value.mV, 16, name);
}


bool LLDataPackerBinaryBuffer::unpackVector4(LLVector4 &value, const char *name)
{
    return unpackBytes(value.mV, 16, name);
}

bool LLDataPackerBinaryBuffer::packUUID(const LLUUID &value, const char *name)
{
    return packBytes(value.mData, 16, name);
}


bool LLDataPackerBinaryBuffer::unpackUUID(LLUUID &value, const char *name)
{
    return unpackBytes(value.mData, 16, name);
}

const LLDataPackerBinaryBuffer& LLDataPackerBinaryBuffer::operator=(const LLDataPackerBinaryBuffer &a)
{
    if (a.getBufferSize() > getBufferSize())
    {
        // We've got problems, ack!
        LL_ERRS() << "Trying to do an assignment with not enough room in the target." << LL_ENDL;
    }
    memcpy(mBufferp, a.mBufferp, a.getBufferSize());    /*Flawfinder: ignore*/
    return *this;
}

void LLDataPackerBinaryBuffer::dumpBufferToLog()
{
    LL_WARNS() << "Binary Buffer Dump, size: " << mBufferSize << LL_ENDL;
    char line_buffer[256]; /*Flawfinder: ignore*/
    S32 i;
    S32 cur_line_pos = 0;

    S32 cur_line = 0;
    for (i = 0; i < mBufferSize; i++)
    {
        snprintf(line_buffer + cur_line_pos*3, sizeof(line_buffer) - cur_line_pos*3, "%02x ", mBufferp[i]);     /* Flawfinder: ignore */
        cur_line_pos++;
        if (cur_line_pos >= 16)
        {
            cur_line_pos = 0;
            LL_WARNS() << "Offset:" << std::hex << cur_line*16 << std::dec << " Data:" << line_buffer << LL_ENDL;
            cur_line++;
        }
    }
    if (cur_line_pos)
    {
        LL_WARNS() << "Offset:" << std::hex << cur_line*16 << std::dec << " Data:" << line_buffer << LL_ENDL;
    }
}

//---------------------------------------------------------------------------
// LLDataPackerAsciiBuffer implementation
//---------------------------------------------------------------------------

#define VKS_ASCII_STATE VksDpBuffer s = { (uint8_t*)mBufferp, mBufferSize, (S32)(mCurBufferp - mBufferp), mWriteEnabled, mIncludeNames }
#define VKS_ASCII_DONE mCurBufferp = mBufferp + s.cur

bool LLDataPackerAsciiBuffer::packValue(int32_t kind, const char* name, const int64_t* ints, size_t nints, const float* floats, size_t nfloats, const U8* data, size_t ndata, const char* what)
{
    VKS_ASCII_STATE;
    int32_t truncated = 0;
    bool ok = vks_dp_ascii_pack(&s, kind, (const uint8_t*)name, strlen(name), ints, nints, floats, nfloats, data, ndata, &truncated) != 0;
    VKS_ASCII_DONE;
    if (truncated)
    {
        LL_WARNS() << "LLDataPackerAsciiBuffer::" << what << ": truncated: " << LL_ENDL;
    }
    return ok;
}

// getValueStr: the next value line into out_value (at most value_len - 1
// bytes), after the name when names are included
bool LLDataPackerAsciiBuffer::getValueStr(const char *name, char *out_value, S32 value_len)
{
    VKS_ASCII_STATE;
    size_t n = 0;
    bool ok = vks_dp_ascii_next_value(&s, (const uint8_t*)name, strlen(name), (uint8_t*)out_value, value_len - 1, &n) != 0;
    VKS_ASCII_DONE;
    if (!ok)
    {
        LL_WARNS() << "Data packer expecting keyword of type " << name << ", got another keyword instead!" << LL_ENDL;
        out_value[0] = 0;
        return false;
    }
    out_value[n] = 0;
    return true;
}

bool LLDataPackerAsciiBuffer::packString(const std::string& value, const char *name)
{
    return packValue(VKS_DP_STRING, name, NULL, 0, NULL, 0, (const U8*)value.data(), value.size(), "packString");
}

bool LLDataPackerAsciiBuffer::unpackString(std::string& value, const char *name)
{
    char valuestr[DP_BUFSIZE]; /*Flawfinder: ignore*/
    if (!getValueStr(name, valuestr, DP_BUFSIZE))  // NULL terminated
    {
        return false;
    }
    value = valuestr;
    return true;
}


bool LLDataPackerAsciiBuffer::packBinaryData(const U8 *value, S32 size, const char *name)
{
    return packValue(VKS_DP_BINARY, name, NULL, 0, NULL, 0, value, size > 0 ? (size_t)size : 0, "packBinaryData");
}


bool LLDataPackerAsciiBuffer::unpackBinaryData(U8 *value, S32 value_size, S32 &out_size, const char *name)
{
    char valuestr[DP_BUFSIZE];      /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        out_size = 0;
        return false;
    }
    // The size checks and the bounded copy follow upstream #5486
    // (ce139f26e0): value_size bounds what is written to value.
    if (value_size < 0)
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData passed negative buffer size, aborting!" << LL_ENDL;
        out_size = 0;
        return false;
    }
    // the line holds at most (DP_BUFSIZE - 12) / 3 bytes of data; only
    // what is there is written
    U8 data[DP_BUFSIZE];
    int32_t parsed_size = 0;
    size_t ndata = 0;
    if (vks_dp_ascii_parse(VKS_DP_BINARY, (const uint8_t*)valuestr, strlen(valuestr), 0, NULL, NULL, data, sizeof(data), &parsed_size, &ndata) > 0)
    {
        out_size = parsed_size;
    }
    if (llmin(out_size, value_size) != out_size)
    {
        LL_WARNS() << "LLDataPackerAsciiBuffer::unpackBinaryData: buffer too small for data, truncating!" << LL_ENDL;
    }
    S32 copy_size = llmin((S32)ndata, value_size);
    if (copy_size > 0) memcpy(value, data, copy_size);
    return true;
}


bool LLDataPackerAsciiBuffer::packBinaryDataFixed(const U8 *value, S32 size, const char *name)
{
    return packValue(VKS_DP_BINARY_FIXED, name, NULL, 0, NULL, 0, value, size > 0 ? (size_t)size : 0, "packBinaryDataFixed");
}


bool LLDataPackerAsciiBuffer::unpackBinaryDataFixed(U8 *value, S32 size, const char *name)
{
    char valuestr[DP_BUFSIZE];      /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    U8 data[DP_BUFSIZE];
    size_t ndata = 0;
    vks_dp_ascii_parse(VKS_DP_BINARY_FIXED, (const uint8_t*)valuestr, strlen(valuestr), size > 0 ? (size_t)size : 0, NULL, NULL, data, sizeof(data), NULL, &ndata);
    if (ndata) memcpy(value, data, llmin((size_t)llmax(size, 0), ndata));
    return true;
}

bool LLDataPackerAsciiBuffer::unpackInt(int32_t kind, int64_t& out, bool& assigned, const char* name)
{
    assigned = false;
    char valuestr[DP_BUFSIZE];      /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    int64_t v = 0;
    if (vks_dp_ascii_parse(kind, (const uint8_t*)valuestr, strlen(valuestr), 1, &v, NULL, NULL, 0, NULL, NULL) > 0)
    {
        out = v;
        assigned = true;
    }
    // otherwise nothing to read: the value is left as it was
    return true;
}

// The text forms: the number as the C++ scanned it (%d up to 16 bits, %u
// for 32), or nothing.
bool LLDataPackerAsciiBuffer::unpackFixedBits(U32 &fixed, S32 total_bits, bool &assigned, const char *name)
{
    int64_t v = 0;
    if (!unpackInt(total_bits <= 16 ? VKS_DP_INT : VKS_DP_UINT, v, assigned, name)) return false;
    if (assigned)
    {
        fixed = total_bits <= 8 ? (U32)(U8)v : (total_bits <= 16 ? (U32)(U16)v : (U32)v);
    }
    return true;
}

bool LLDataPackerAsciiBuffer::unpackFloats(float* out, size_t count, const char* name)
{
    char valuestr[DP_BUFSIZE];      /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    float v[4] = { 0.f, 0.f, 0.f, 0.f };
    int32_t n = vks_dp_ascii_parse(VKS_DP_FLOATS, (const uint8_t*)valuestr, strlen(valuestr), count, NULL, v, NULL, 0, NULL, NULL);
    for (int32_t i = 0; i < n && i < (int32_t)count; ++i)
    {
        out[i] = v[i];
    }
    return true;
}

bool LLDataPackerAsciiBuffer::packU8(const U8 value, const char *name)
{
    int64_t v = value;
    return packValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0, "packU8");
}


bool LLDataPackerAsciiBuffer::unpackU8(U8 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (U8)v;
    return true;
}

bool LLDataPackerAsciiBuffer::packU16(const U16 value, const char *name)
{
    int64_t v = value;
    return packValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0, "packU16");
}


bool LLDataPackerAsciiBuffer::unpackU16(U16 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (U16)v;
    return true;
}

bool LLDataPackerAsciiBuffer::packS16(const S16 value, const char *name)
{
    int64_t v = value;
    return packValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0, "packS16");
}


bool LLDataPackerAsciiBuffer::unpackS16(S16 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (S16)v;
    return true;
}

bool LLDataPackerAsciiBuffer::packU32(const U32 value, const char *name)
{
    int64_t v = value;
    return packValue(VKS_DP_UINT, name, &v, 1, NULL, 0, NULL, 0, "packU32");
}


bool LLDataPackerAsciiBuffer::unpackU32(U32 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_UINT, v, assigned, name)) return false;
    if (assigned) value = (U32)v;
    return true;
}


bool LLDataPackerAsciiBuffer::packS32(const S32 value, const char *name)
{
    int64_t v = value;
    return packValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0, "packS32");
}


bool LLDataPackerAsciiBuffer::unpackS32(S32 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (S32)v;
    return true;
}


bool LLDataPackerAsciiBuffer::packF32(const F32 value, const char *name)
{
    return packValue(VKS_DP_FLOATS, name, NULL, 0, &value, 1, NULL, 0, "packF32");
}


bool LLDataPackerAsciiBuffer::unpackF32(F32 &value, const char *name)
{
    return unpackFloats(&value, 1, name);
}


bool LLDataPackerAsciiBuffer::packColor4(const LLColor4 &value, const char *name)
{
    return packValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 4, NULL, 0, "packColor4");
}


bool LLDataPackerAsciiBuffer::unpackColor4(LLColor4 &value, const char *name)
{
    return unpackFloats(value.mV, 4, name);
}

bool LLDataPackerAsciiBuffer::packColor4U(const LLColor4U &value, const char *name)
{
    return packValue(VKS_DP_BYTES, name, NULL, 0, NULL, 0, value.mV, 4, "packColor4U");
}


bool LLDataPackerAsciiBuffer::unpackColor4U(LLColor4U &value, const char *name)
{
    char valuestr[DP_BUFSIZE];   /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    int64_t v[4] = { 0, 0, 0, 0 };
    int32_t n = vks_dp_ascii_parse(VKS_DP_BYTES, (const uint8_t*)valuestr, strlen(valuestr), 4, v, NULL, NULL, 0, NULL, NULL);
    for (int32_t i = 0; i < n && i < 4; ++i)
    {
        value.mV[i] = (U8)v[i];
    }
    return true;
}


bool LLDataPackerAsciiBuffer::packVector2(const LLVector2 &value, const char *name)
{
    return packValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 2, NULL, 0, "packVector2");
}


bool LLDataPackerAsciiBuffer::unpackVector2(LLVector2 &value, const char *name)
{
    return unpackFloats(value.mV, 2, name);
}


bool LLDataPackerAsciiBuffer::packVector3(const LLVector3 &value, const char *name)
{
    return packValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 3, NULL, 0, "packVector3");
}


bool LLDataPackerAsciiBuffer::unpackVector3(LLVector3 &value, const char *name)
{
    return unpackFloats(value.mV, 3, name);
}

bool LLDataPackerAsciiBuffer::packVector4(const LLVector4 &value, const char *name)
{
    return packValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 4, NULL, 0, "packVector4");
}


bool LLDataPackerAsciiBuffer::unpackVector4(LLVector4 &value, const char *name)
{
    return unpackFloats(value.mV, 4, name);
}


bool LLDataPackerAsciiBuffer::packUUID(const LLUUID &value, const char *name)
{
    std::string tmp_str;
    value.toString(tmp_str);
    return packValue(VKS_DP_UUID, name, NULL, 0, NULL, 0, (const U8*)tmp_str.data(), tmp_str.size(), "packUUID");
}


bool LLDataPackerAsciiBuffer::unpackUUID(LLUUID &value, const char *name)
{
    char valuestr[DP_BUFSIZE];  /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }

    U8 tmp_str[64];   /* Flawfinder: ignore */
    size_t n = 0;
    if (vks_dp_ascii_parse(VKS_DP_UUID, (const uint8_t*)valuestr, strlen(valuestr), 0, NULL, NULL, tmp_str, 63, NULL, &n) > 0)
    {
        value.set(std::string((const char*)tmp_str, n));
    }
    return true;
}

void LLDataPackerAsciiBuffer::dump()
{
    LL_INFOS() << "Buffer: " << mBufferp << LL_ENDL;
}

//---------------------------------------------------------------------------
// LLDataPackerAsciiFile implementation
//---------------------------------------------------------------------------
void LLDataPackerAsciiFile::writeValue(int32_t kind, const char* name, const int64_t* ints, size_t nints, const float* floats, size_t nfloats, const U8* data, size_t ndata)
{
    writeIndentedName(name);
    // the stream form wrote each float through a 20-byte buffer
    // (convertF32ToString), cutting long values to 19 characters
    if (kind == VKS_DP_FLOATS && !mFP)
    {
        kind = VKS_DP_FLOATS_SHORT;
    }
    std::string text = value_text(kind, ints, nints, floats, nfloats, data, ndata);
    if (mFP)
    {
        fwrite(text.data(), 1, text.size(), mFP);
    }
    else if (mOutputStream)
    {
        *mOutputStream << text;
    }
}

bool LLDataPackerAsciiFile::packString(const std::string& value, const char *name)
{
    writeValue(VKS_DP_STRING, name, NULL, 0, NULL, 0, (const U8*)value.data(), value.size());
    return true;
}

bool LLDataPackerAsciiFile::unpackString(std::string& value, const char *name)
{
    bool success = true;
    char valuestr[DP_BUFSIZE];  /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    value = valuestr;
    return success;
}


bool LLDataPackerAsciiFile::packBinaryData(const U8 *value, S32 size, const char *name)
{
    writeValue(VKS_DP_BINARY, name, NULL, 0, NULL, 0, value, size > 0 ? (size_t)size : 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackBinaryData(U8 *value, S32 value_size, S32 &out_size, const char *name)
{
    char valuestr[DP_BUFSIZE];      /* Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        out_size = 0;
        return false;
    }
    // The size checks and the bounded copy follow upstream #5486
    // (ce139f26e0): value_size bounds what is written to value.
    if (value_size < 0)
    {
        LL_WARNS() << "LLDataPackerBinaryBuffer::unpackBinaryData passed negative buffer size, aborting!" << LL_ENDL;
        out_size = 0;
        return false;
    }
    // the line holds at most (DP_BUFSIZE - 12) / 3 bytes of data; only
    // what is there is written
    U8 data[DP_BUFSIZE];
    int32_t parsed_size = 0;
    size_t ndata = 0;
    if (vks_dp_ascii_parse(VKS_DP_BINARY, (const uint8_t*)valuestr, strlen(valuestr), 0, NULL, NULL, data, sizeof(data), &parsed_size, &ndata) > 0)
    {
        out_size = parsed_size;
    }
    if (llmin(out_size, value_size) != out_size)
    {
        LL_WARNS() << "LLDataPackerAsciiBuffer::unpackBinaryData: buffer too small for data, truncating!" << LL_ENDL;
    }
    S32 copy_size = llmin((S32)ndata, value_size);
    if (copy_size > 0) memcpy(value, data, copy_size);
    return true;
}


bool LLDataPackerAsciiFile::packBinaryDataFixed(const U8 *value, S32 size, const char *name)
{
    writeValue(VKS_DP_BINARY_FIXED, name, NULL, 0, NULL, 0, value, size > 0 ? (size_t)size : 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackBinaryDataFixed(U8 *value, S32 size, const char *name)
{
    char valuestr[DP_BUFSIZE]; /*Flawfinder: ignore*/
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    U8 data[DP_BUFSIZE];
    size_t ndata = 0;
    vks_dp_ascii_parse(VKS_DP_BINARY_FIXED, (const uint8_t*)valuestr, strlen(valuestr), size > 0 ? (size_t)size : 0, NULL, NULL, data, sizeof(data), NULL, &ndata);
    if (ndata) memcpy(value, data, llmin((size_t)llmax(size, 0), ndata));
    return true;
}

bool LLDataPackerAsciiFile::unpackInt(int32_t kind, int64_t& out, bool& assigned, const char* name)
{
    assigned = false;
    char valuestr[DP_BUFSIZE];      /*Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    int64_t v = 0;
    if (vks_dp_ascii_parse(kind, (const uint8_t*)valuestr, strlen(valuestr), 1, &v, NULL, NULL, 0, NULL, NULL) > 0)
    {
        out = v;
        assigned = true;
    }
    // otherwise nothing to read: the value is left as it was
    return true;
}

// The text forms: the number as the C++ scanned it (%d up to 16 bits, %u
// for 32), or nothing.
bool LLDataPackerAsciiFile::unpackFixedBits(U32 &fixed, S32 total_bits, bool &assigned, const char *name)
{
    int64_t v = 0;
    if (!unpackInt(total_bits <= 16 ? VKS_DP_INT : VKS_DP_UINT, v, assigned, name)) return false;
    if (assigned)
    {
        fixed = total_bits <= 8 ? (U32)(U8)v : (total_bits <= 16 ? (U32)(U16)v : (U32)v);
    }
    return true;
}

bool LLDataPackerAsciiFile::unpackFloats(float* out, size_t count, const char* name)
{
    char valuestr[DP_BUFSIZE]; /*Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    float v[4] = { 0.f, 0.f, 0.f, 0.f };
    int32_t n = vks_dp_ascii_parse(VKS_DP_FLOATS, (const uint8_t*)valuestr, strlen(valuestr), count, NULL, v, NULL, 0, NULL, NULL);
    for (int32_t i = 0; i < n && i < (int32_t)count; ++i)
    {
        out[i] = v[i];
    }
    return true;
}

bool LLDataPackerAsciiFile::packU8(const U8 value, const char *name)
{
    int64_t v = value;
    writeValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackU8(U8 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (U8)v;
    return true;
}

bool LLDataPackerAsciiFile::packU16(const U16 value, const char *name)
{
    int64_t v = value;
    writeValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackU16(U16 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (U16)v;
    return true;
}

bool LLDataPackerAsciiFile::packS16(const S16 value, const char *name)
{
    int64_t v = value;
    writeValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackS16(S16 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (S16)v;
    return true;
}

bool LLDataPackerAsciiFile::packU32(const U32 value, const char *name)
{
    int64_t v = value;
    writeValue(VKS_DP_UINT, name, &v, 1, NULL, 0, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackU32(U32 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_UINT, v, assigned, name)) return false;
    if (assigned) value = (U32)v;
    return true;
}


bool LLDataPackerAsciiFile::packS32(const S32 value, const char *name)
{
    int64_t v = value;
    writeValue(VKS_DP_INT, name, &v, 1, NULL, 0, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackS32(S32 &value, const char *name)
{
    int64_t v = 0;
    bool assigned = false;
    if (!unpackInt(VKS_DP_INT, v, assigned, name)) return false;
    if (assigned) value = (S32)v;
    return true;
}


bool LLDataPackerAsciiFile::packF32(const F32 value, const char *name)
{
    writeValue(VKS_DP_FLOATS, name, NULL, 0, &value, 1, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackF32(F32 &value, const char *name)
{
    return unpackFloats(&value, 1, name);
}


bool LLDataPackerAsciiFile::packColor4(const LLColor4 &value, const char *name)
{
    writeValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 4, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackColor4(LLColor4 &value, const char *name)
{
    return unpackFloats(value.mV, 4, name);
}

bool LLDataPackerAsciiFile::packColor4U(const LLColor4U &value, const char *name)
{
    writeValue(VKS_DP_BYTES, name, NULL, 0, NULL, 0, value.mV, 4);
    return true;
}


bool LLDataPackerAsciiFile::unpackColor4U(LLColor4U &value, const char *name)
{
    char valuestr[DP_BUFSIZE]; /*Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }
    int64_t v[4] = { 0, 0, 0, 0 };
    int32_t n = vks_dp_ascii_parse(VKS_DP_BYTES, (const uint8_t*)valuestr, strlen(valuestr), 4, v, NULL, NULL, 0, NULL, NULL);
    for (int32_t i = 0; i < n && i < 4; ++i)
    {
        value.mV[i] = (U8)v[i];
    }
    return true;
}


bool LLDataPackerAsciiFile::packVector2(const LLVector2 &value, const char *name)
{
    writeValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 2, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackVector2(LLVector2 &value, const char *name)
{
    return unpackFloats(value.mV, 2, name);
}


bool LLDataPackerAsciiFile::packVector3(const LLVector3 &value, const char *name)
{
    writeValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 3, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackVector3(LLVector3 &value, const char *name)
{
    return unpackFloats(value.mV, 3, name);
}

bool LLDataPackerAsciiFile::packVector4(const LLVector4 &value, const char *name)
{
    writeValue(VKS_DP_FLOATS, name, NULL, 0, value.mV, 4, NULL, 0);
    return true;
}


bool LLDataPackerAsciiFile::unpackVector4(LLVector4 &value, const char *name)
{
    return unpackFloats(value.mV, 4, name);
}


bool LLDataPackerAsciiFile::packUUID(const LLUUID &value, const char *name)
{
    std::string tmp_str;
    value.toString(tmp_str);
    writeValue(VKS_DP_UUID, name, NULL, 0, NULL, 0, (const U8*)tmp_str.data(), tmp_str.size());
    return true;
}


bool LLDataPackerAsciiFile::unpackUUID(LLUUID &value, const char *name)
{
    char valuestr[DP_BUFSIZE]; /*Flawfinder: ignore */
    if (!getValueStr(name, valuestr, DP_BUFSIZE))
    {
        return false;
    }

    U8 tmp_str[64]; /*Flawfinder: ignore */
    size_t n = 0;
    if (vks_dp_ascii_parse(VKS_DP_UUID, (const uint8_t*)valuestr, strlen(valuestr), 0, NULL, NULL, tmp_str, 63, NULL, &n) > 0)
    {
        value.set(std::string((const char*)tmp_str, n));
    }
    return true;
}


void LLDataPackerAsciiFile::writeIndentedName(const char *name)
{
    std::string indent_buf(mIndent > 0 ? (size_t)mIndent : 0, '\t');
    if (mFP)
    {
        fprintf(mFP,"%s%s\t",indent_buf.c_str(), name);
    }
    else if (mOutputStream)
    {
        *mOutputStream << indent_buf << name << "\t";
    }
}

bool LLDataPackerAsciiFile::getValueStr(const char *name, char *out_value, S32 value_len)
{
    bool success = false;
    char buffer[DP_BUFSIZE]; /*Flawfinder: ignore*/
    buffer[0] = '\0';

    if (mFP)
    {
        fpos_t last_pos;
        if (0 != fgetpos(mFP, &last_pos)) // 0==success for fgetpos
        {
            LL_WARNS() << "Data packer failed to fgetpos" << LL_ENDL;
            return false;
        }

        if (fgets(buffer, DP_BUFSIZE, mFP) == NULL)
        {
            buffer[0] = '\0';
        }

        const uint8_t* key = NULL; size_t key_len = 0;
        const uint8_t* value = NULL; size_t value_n = 0;
        vks_dp_ascii_split((const uint8_t*)buffer, strlen(buffer), &key, &key_len, &value, &value_n);

        if (!key_len)
        {
            LL_WARNS() << "Data packer could not get the keyword!" << LL_ENDL;
            fsetpos(mFP, &last_pos);
            return false;
        }
        if (key_len != strlen(name) || memcmp(key, name, key_len) != 0)
        {
            LL_WARNS() << "Data packer expecting keyword of type " << name << ", got " << std::string((const char*)key, key_len) << " instead!" << LL_ENDL;
            fsetpos(mFP, &last_pos);
            return false;
        }

        S32 min_len = llmin((S32)value_n + 1, value_len);
        if (min_len > 1) memcpy(out_value, value, min_len - 1); /*Flawfinder: ignore*/
        out_value[min_len-1] = 0;
        success = true;
    }
    else if (mInputStream)
    {
        mInputStream->getline(buffer, DP_BUFSIZE);

        const uint8_t* key = NULL; size_t key_len = 0;
        const uint8_t* value = NULL; size_t value_n = 0;
        vks_dp_ascii_split((const uint8_t*)buffer, strlen(buffer), &key, &key_len, &value, &value_n);
        if (!key_len)
        {
            LL_WARNS() << "Data packer could not get the keyword!" << LL_ENDL;
            return false;
        }
        if (key_len != strlen(name) || memcmp(key, name, key_len) != 0)
        {
            LL_WARNS() << "Data packer expecting keyword of type " << name << ", got " << std::string((const char*)key, key_len) << " instead!" << LL_ENDL;
            return false;
        }

        S32 min_len = llmin((S32)value_n + 1, value_len);
        if (min_len > 1) memcpy(out_value, value, min_len - 1); /*Flawfinder: ignore*/
        out_value[min_len-1] = 0;
        success = true;
    }

    return success;
}
