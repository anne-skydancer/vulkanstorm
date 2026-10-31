/**
 * @file llinventory_vks.cpp
 * @brief What the inventory data model shares to call vkstorm-inventory
 *        (Rust): failure logs, LLSD results, export text and the legacy
 *        stream window.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
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
 * $/LicenseInfo$
 */

#include "linden_common.h"
#include "llinventory_vks.h"

#include <algorithm>

void ll_inv_vks_warn(const char* call)
{
    const char* error = vks_inv_last_error();
    LL_WARNS("Inventory") << call << " failed: " << (error ? error : "") << LL_ENDL;
}

LLSD ll_inv_sd(VksLlsdDoc* doc, const char* call)
{
    if (!doc)
    {
        ll_inv_vks_warn(call);
        return LLSD();
    }
    return ll_sd_from_vks_doc(doc);
}

bool ll_inv_write_text(std::ostream& output, VksInvRecords* r, const char* call)
{
    if (!r)
    {
        ll_inv_vks_warn(call);
        return false;
    }
    const VksInvBytes& text = vks_inv_records_view(r)->text;
    if (text.len)
    {
        output.write(reinterpret_cast<const char*>(text.ptr), (std::streamsize)text.len);
    }
    return true;
}

LLInvLegacyWindow::LLInvLegacyWindow(std::istream& input)
:   mInput(input)
{
    const std::ios::iostate state = input.rdstate();
    if (state & std::ios::eofbit)
    {
        mEntryState |= VKS_INV_STREAM_EOF;
    }
    if (state & std::ios::failbit)
    {
        mEntryState |= VKS_INV_STREAM_FAIL;
    }
    if (state & std::ios::badbit)
    {
        mEntryState |= VKS_INV_STREAM_BAD;
    }
    // A stream that is not good is read by nothing: the window stays empty.
    grow();
}

bool LLInvLegacyWindow::grow()
{
    std::streambuf* sb = mInput.rdbuf();
    if (mEntryState || !sb)
    {
        return false;
    }
    // sgetc fills an empty buffer; in_avail then counts what the buffer holds
    // (not what a file has left), so no part is read past a refill.
    if (sb->sgetc() == std::char_traits<char>::eof())
    {
        return false;
    }
    std::streamsize n = std::min<std::streamsize>(sb->in_avail(), (std::streamsize)mPartCap);
    if (n <= 0)
    {
        n = 1; // an unbuffered stream
    }
    mPartCap *= 2;
    mLastPart = mData.size();
    mData.resize(mLastPart + (size_t)n);
    const std::streamsize got = sb->sgetn(&mData[mLastPart], n);
    mData.resize(mLastPart + (size_t)std::max<std::streamsize>(got, 0));
    return got > 0;
}

void LLInvLegacyWindow::putBack(size_t count)
{
    if (count > mData.size() - mLastPart)
    {
        LL_WARNS("Inventory") << "legacy reader stopped before the last part read: " << count << " bytes to put back, "
                              << mData.size() - mLastPart << " in the buffer" << LL_ENDL;
    }
    std::streambuf* sb = mInput.rdbuf();
    for (size_t i = 0; i < count; ++i)
    {
        if (sb->sungetc() == std::char_traits<char>::eof())
        {
            LL_WARNS("Inventory") << "cannot put back " << count - i << " bytes of a legacy text stream" << LL_ENDL;
            break;
        }
    }
}

void LLInvLegacyWindow::finish(const VksInvStream& stream)
{
    putBack(mData.size() - std::min(stream.consumed, mData.size()));
    std::ios::iostate state = std::ios::goodbit;
    if (stream.state & VKS_INV_STREAM_EOF)
    {
        state |= std::ios::eofbit;
    }
    if (stream.state & VKS_INV_STREAM_FAIL)
    {
        state |= std::ios::failbit;
    }
    if (stream.state & VKS_INV_STREAM_BAD)
    {
        state |= std::ios::badbit;
    }
    mInput.clear(state);
}

void LLInvLegacyWindow::fail()
{
    putBack(mData.size() - mLastPart);
    mInput.setstate(std::ios::failbit);
}
