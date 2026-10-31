/**
 * @file llinventory_vks.h
 * @brief What the inventory data model shares to call vkstorm-inventory
 *        (Rust) through vkstorm_ffi.h: records, strings and the legacy text
 *        streams. Private to llinventory.
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

#ifndef LL_LLINVENTORY_VKS_H
#define LL_LLINVENTORY_VKS_H

#include "llpermissions.h"
#include "llsaleinfo.h"
#include "lluuid.h"
#include "vkstorm_ffi.h"

#include <istream>
#include <memory>
#include <ostream>
#include <string>

// LLPermissions, LLSaleInfo and LLAggregatePermissions are the Rust records
// in place: llpermissions.cpp and llsaleinfo.cpp assert the layouts.
inline const VksInvPerms* vks_perms(const LLPermissions& p) { return reinterpret_cast<const VksInvPerms*>(&p); }
inline VksInvPerms* vks_perms(LLPermissions& p) { return reinterpret_cast<VksInvPerms*>(&p); }
inline const VksInvSale* vks_sale(const LLSaleInfo& s) { return reinterpret_cast<const VksInvSale*>(&s); }
inline VksInvSale* vks_sale(LLSaleInfo& s) { return reinterpret_cast<VksInvSale*>(&s); }

inline const uint8_t* vks_ptr(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }
inline VksInvBytes vks_bytes(const std::string& s) { return VksInvBytes{ vks_ptr(s), s.size() }; }
inline std::string vks_string(const VksInvBytes& b)
{
    return b.len ? std::string(reinterpret_cast<const char*>(b.ptr), b.len) : std::string();
}
inline void vks_assign(std::string& out, const VksInvBytes& b)
{
    if (b.len)
    {
        out.assign(reinterpret_cast<const char*>(b.ptr), b.len);
    }
    else
    {
        out.clear();
    }
}
inline void vks_set_uuid(uint8_t* out, const LLUUID& id) { memcpy(out, id.mData, UUID_BYTES); }
inline LLUUID vks_uuid(const uint8_t* in)
{
    LLUUID id;
    memcpy(id.mData, in, UUID_BYTES);
    return id;
}

struct LLInvRecordsFree
{
    void operator()(VksInvRecords* r) const { vks_inv_records_free(r); }
};
typedef std::unique_ptr<VksInvRecords, LLInvRecordsFree> LLInvRecordsPtr;

// Logs a vks_inv_* call that failed (null, false or no change): a caught
// panic, with vks_inv_last_error().
void ll_inv_vks_warn(const char* call);

// Writes the text of an export's result block; false, logged, for a failure.
bool ll_inv_write_text(std::ostream& output, VksInvRecords* r, const char* call);

// The rest of a legacy text stream as the vks_inv_* readers take it (see
// VksInvStream in vkstorm_ffi.h), read without seeking: Windows text-mode
// file streams do not return to a tellg() position exactly.
//
// The window starts as what the stream buffer holds. A reader that runs into
// its end sets the eof bit; while the stream has more, the window grows by
// what the buffer holds next and the reader runs again from the start. A
// reader never stops before where it ran out the last time, so what it did
// not consume lies in the last part read, still in the buffer, and goes
// back with sungetc. The stream ends where the reader stopped, in the state
// it left.
class LLInvLegacyWindow
{
public:
    explicit LLInvLegacyWindow(std::istream& input);

    const uint8_t* data() const { return reinterpret_cast<const uint8_t*>(mData.data()); }
    size_t size() const { return mData.size(); }
    // The stream's state bits on entry (VKS_INV_STREAM_*).
    uint32_t entryState() const { return mEntryState; }
    // Reads the next part; false at the end of the stream or on a read error.
    bool grow();
    // A read or put-back threw (as istream's own reads would, it sets badbit).
    bool bad() const { return mBad; }
    // Leaves the stream after what the reader consumed, in its state.
    void finish(const VksInvStream& stream);
    // A failed call: what can go back goes back, and the stream fails.
    void fail();

private:
    void putBack(size_t count);

    std::istream& mInput;
    std::string mData;
    size_t mLastPart = 0; // where the last part read starts in mData
    size_t mPartCap = PART_MIN;
    uint32_t mEntryState = 0;
    bool mBad = false;
    // Parts double from PART_MIN up to PART_MAX: few re-reads of a long
    // record, and the cap keeps the size in streamsize range however many
    // short refills a file buffer gives.
    static constexpr size_t PART_MIN = 4096;
    static constexpr size_t PART_MAX = 1 << 20;
};

// Runs a vks_inv_* legacy reader over the rest of `input`. `read(data, len,
// stream)` makes the call, taking stream.state and setting stream to the
// reader's result, and returns false when the call failed. It may run more
// than once, each time from the same starting object. False when the call
// failed (the stream then fails).
template <typename Read>
bool ll_inv_read_legacy(std::istream& input, Read&& read)
{
    LLInvLegacyWindow window(input);
    for (;;)
    {
        VksInvStream stream{ 0, window.entryState() };
        if (!read(window.data(), window.size(), stream))
        {
            window.fail();
            return false;
        }
        if (!(stream.state & VKS_INV_STREAM_EOF) || !window.grow())
        {
            if (window.bad())
            {
                // The stream failed under the reader: an import failure, as
                // when istream's own reads catch the error.
                window.fail();
                return false;
            }
            window.finish(stream);
            // Putting back what the reader did not consume can fail too; the
            // stream then has badbit and is not where the reader left it.
            return !window.bad();
        }
    }
}

#endif // LL_LLINVENTORY_VKS_H
