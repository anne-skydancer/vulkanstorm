/**
 * @file llnotecard.cpp
 * @brief LLNotecard class definition
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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
#include "llnotecard.h"

#include "llinventory_vks.h"

// The notecard text format (version 1 and 2, with the embedded items) is
// read and written in Rust since vkstorm-rust rung seven (sub-rung R1):
// vkstorm-inventory through vkstorm_ffi.h.

LLNotecard::LLNotecard(S32 max_text)
    : mMaxText(max_text),
      mVersion(0),
      mEmbeddedVersion(0)
{
}

LLNotecard::~LLNotecard()
{
}

bool LLNotecard::importStream(std::istream& str)
{
    LLInvRecordsPtr result;
    if (!ll_inv_read_legacy(str, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            result.reset(vks_inv_notecard_import(data, len, mMaxText, mVersion, mEmbeddedVersion, &stream));
            if (!result)
            {
                return false;
            }
            stream = vks_inv_records_view(result.get())->stream;
            return true;
        }))
    {
        ll_inv_vks_warn("vks_inv_notecard_import");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    // The items read are kept, also from a notecard that fails later.
    for (size_t i = 0; i < r->item_count; ++i)
    {
        LLPointer<LLInventoryItem> item = new LLInventoryItem;
        item->fromVks(r->items[i]);
        mItems.push_back(item);
    }
    mVersion = r->version;
    mEmbeddedVersion = r->embedded_version;
    if (!r->ok)
    {
        LL_WARNS() << "Invalid Linden text file (version " << mVersion << ", embedded items version "
                   << mEmbeddedVersion << ", " << r->item_count << " items read)" << LL_ENDL;
        return false;
    }
    // Actually set the text
    mText = vks_string(r->text);
    return true;
}

////////////////////////////////////////////////////////////////////////////

bool LLNotecard::exportStream( std::ostream& out_stream )
{
    std::vector<VksInvItem> items;
    items.reserve(mItems.size());
    for (const LLPointer<LLInventoryItem>& item : mItems)
    {
        if (item)
        {
            items.emplace_back();
            item->toVks(items.back());
        }
        else
        {
            // No caller sets one; the C++ counted it and left its index unused.
            LL_WARNS() << "Notecard export skips a null embedded item" << LL_ENDL;
        }
    }
    LLInvRecordsPtr text(vks_inv_notecard_export(items.data(), items.size(), vks_ptr(mText), mText.size()));
    return ll_inv_write_text(out_stream, text.get(), "vks_inv_notecard_export");
}

////////////////////////////////////////////////////////////////////////////

void LLNotecard::setItems(const std::vector<LLPointer<LLInventoryItem> >& items)
{
    mItems = items;
}

void LLNotecard::setText(const std::string& text)
{
    mText = text;
}
