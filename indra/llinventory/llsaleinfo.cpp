/**
 * @file llsaleinfo.cpp
 * @brief
 *
 * $LicenseInfo:firstyear=2002&license=viewerlgpl$
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

#include <iostream>
#include "linden_common.h"

#include "llsaleinfo.h"

#include "llerror.h"
#include "message.h"
#include "llsdutil.h"

#include "llinventory_vks.h"

// The rules are Rust since vkstorm-rust rung seven (sub-rung R1):
// vkstorm-inventory through vkstorm_ffi.h, over this object in place. The
// sale type names and the message field I/O stay here.

// use this to avoid temporary object creation
const LLSaleInfo LLSaleInfo::DEFAULT;

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

const char* FOR_SALE_NAMES[] =
{
    "not",
    "orig",
    "copy",
    "cntn"
};

// The writers fall back to "not" for a type without a name, with this
// warning once.
static void warn_unknown_sale_type(LLSaleInfo::EForSale type)
{
    if (!LLSaleInfo::lookup(type))
    {
        LL_WARNS_ONCE() << "Unknown sale type: " << type << LL_ENDL;
    }
}

///----------------------------------------------------------------------------
/// Class llsaleinfo
///----------------------------------------------------------------------------

// Default constructor
LLSaleInfo::LLSaleInfo() :
    mSaleType(LLSaleInfo::FS_NOT),
    mSalePrice(DEFAULT_PRICE)
{
    static_assert(sizeof(LLSaleInfo) == VKS_INV_SALE_SIZE && alignof(LLSaleInfo) == alignof(VksInvSale) &&
                  sizeof(EForSale) == sizeof(int32_t) && offsetof(LLSaleInfo, mSaleType) == 0 &&
                  offsetof(LLSaleInfo, mSalePrice) == 4,
                  "LLSaleInfo is VksInvSale");
}

LLSaleInfo::LLSaleInfo(EForSale sale_type, S32 sale_price) :
    mSaleType(sale_type),
    mSalePrice(sale_price)
{
    mSalePrice = llclamp(mSalePrice, 0, S32_MAX);
}

bool LLSaleInfo::isForSale() const
{
    return (FS_NOT != mSaleType);
}

U32 LLSaleInfo::getCRC32() const
{
    return vks_inv_sale_crc32(vks());
}

bool LLSaleInfo::exportLegacyStream(std::ostream& output_stream) const
{
    warn_unknown_sale_type(mSaleType);
    LLInvRecordsPtr text(vks_inv_sale_export_legacy(vks()));
    return ll_inv_write_text(output_stream, text.get(), "vks_inv_sale_export_legacy");
}

LLSD LLSaleInfo::asLLSD() const
{
    warn_unknown_sale_type(mSaleType);
    return ll_inv_sd(vks_inv_sale_fill_llsd(vks(), nullptr), "vks_inv_sale_fill_llsd");
}

void LLSaleInfo::asLLSD(LLSD& sd) const
{
    warn_unknown_sale_type(mSaleType);
    LLSDFlat into(sd);
    VksLlsdDoc* filled = vks_inv_sale_fill_llsd(vks(), into.get());
    if (!filled)
    {
        ll_inv_vks_warn("vks_inv_sale_fill_llsd");
        return;
    }
    sd = ll_sd_from_vks_doc(filled);
}

bool LLSaleInfo::fromLLSD(const LLSD& sd, bool& has_perm_mask, U32& perm_mask)
{
    LLSDFlat flat(sd);
    if (!vks_inv_sale_from_llsd(vks(), flat.get(), &has_perm_mask, &perm_mask))
    {
        ll_inv_vks_warn("vks_inv_sale_from_llsd");
        return false;
    }
    return true;
}

bool LLSaleInfo::importLegacyStream(std::istream& input_stream, bool& has_perm_mask, U32& perm_mask)
{
    LLSaleInfo read;
    bool read_has = false;
    U32 read_mask = 0;
    if (!ll_inv_read_legacy(input_stream, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            read = *this;
            read_has = has_perm_mask;
            read_mask = perm_mask;
            return vks_inv_sale_import_legacy(read.vks(), data, len, &read_has, &read_mask, &stream);
        }))
    {
        ll_inv_vks_warn("vks_inv_sale_import_legacy");
        return false;
    }
    *this = read;
    has_perm_mask = read_has;
    perm_mask = read_mask;
    return true;
}

void LLSaleInfo::setSalePrice(S32 price)
{
    vks_inv_sale_set_price(vks(), price);
}

LLSD LLSaleInfo::packMessage() const
{
    return ll_inv_sd(vks_inv_sale_pack_message_llsd(vks()), "vks_inv_sale_pack_message_llsd");
}

void LLSaleInfo::packMessage(LLMessageSystem* msg) const
{
    U8 sale_type = static_cast<U8>(mSaleType);
    msg->addU8Fast(_PREHASH_SaleType, sale_type);
    msg->addS32Fast(_PREHASH_SalePrice, mSalePrice);
    //msg->addU32Fast(_PREHASH_NextOwnerMask, mNextOwnerPermMask);
}

void LLSaleInfo::unpackMessage(LLSD sales)
{
    LLSDFlat flat(sales);
    if (!vks_inv_sale_unpack_message_llsd(vks(), flat.get()))
    {
        ll_inv_vks_warn("vks_inv_sale_unpack_message_llsd");
    }
}

void LLSaleInfo::unpackMessage(LLMessageSystem* msg, const char* block)
{
    U8 sale_type = static_cast<U8>(mSaleType);
    S32 sale_price = mSalePrice;
    msg->getU8Fast(block, _PREHASH_SaleType, sale_type);
    msg->getS32Fast(block, _PREHASH_SalePrice, sale_price);
    vks_inv_sale_unpack_wire(vks(), sale_type, sale_price);
}

void LLSaleInfo::unpackMultiMessage(LLMessageSystem* msg, const char* block,
                                    S32 block_num)
{
    U8 sale_type = static_cast<U8>(mSaleType);
    S32 sale_price = mSalePrice;
    msg->getU8Fast(block, _PREHASH_SaleType, sale_type, block_num);
    msg->getS32Fast(block, _PREHASH_SalePrice, sale_price, block_num);
    vks_inv_sale_unpack_wire(vks(), sale_type, sale_price);
}

LLSaleInfo::EForSale LLSaleInfo::lookup(const char* name)
{
    for(S32 i = 0; i < FS_COUNT; i++)
    {
        if(0 == strcmp(name, FOR_SALE_NAMES[i]))
        {
            // match
            return (EForSale)i;
        }
    }
    return FS_NOT;
}

const char* LLSaleInfo::lookup(EForSale type)
{
    if((type >= 0) && (type < FS_COUNT))
    {
        return FOR_SALE_NAMES[S32(type)];
    }
    else
    {
        return NULL;
    }
}

// Allow accumulation of sale info. The price of each is added,
// conflict in sale type results in FS_NOT, and the permissions are
// tightened.
void LLSaleInfo::accumulate(const LLSaleInfo& sale_info)
{
    vks_inv_sale_accumulate(vks(), sale_info.vks());
}

bool LLSaleInfo::operator==(const LLSaleInfo &rhs) const
{
    return (
        (mSaleType == rhs.mSaleType) &&
        (mSalePrice == rhs.mSalePrice)
        );
}

bool LLSaleInfo::operator!=(const LLSaleInfo &rhs) const
{
    return (
        (mSaleType != rhs.mSaleType) ||
        (mSalePrice != rhs.mSalePrice)
        );
}


///----------------------------------------------------------------------------
/// Local function definitions
///----------------------------------------------------------------------------

///----------------------------------------------------------------------------
/// exported functions
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_sale_info(const LLSaleInfo& sale)
{
    return ll_inv_sd(vks_inv_sale_fill_llsd(vks_sale(sale), nullptr), "vks_inv_sale_fill_llsd");
}

LLSaleInfo ll_sale_info_from_sd(const LLSD& sd)
{
    LLSaleInfo rv;
    LLSDFlat flat(sd);
    if (!vks_inv_sale_from_sd(vks_sale(rv), flat.get()))
    {
        ll_inv_vks_warn("vks_inv_sale_from_sd");
    }
    return rv;
}
