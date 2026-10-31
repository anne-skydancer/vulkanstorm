/**
 * @file llpermissions.cpp
 * @author Phoenix
 * @brief Permissions for objects and inventory.
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

#include "linden_common.h"

#include "llpermissions.h"

// library includes
#include "message.h"
#include "llsd.h"

#include "llinventory_vks.h"

// The rules are Rust since vkstorm-rust rung seven (sub-rung R1):
// vkstorm-inventory through vkstorm_ffi.h. LLPermissions and
// LLAggregatePermissions are its records in place; the message field I/O,
// the copy and the debug output stay here.

///----------------------------------------------------------------------------
/// Class LLPermissions
///----------------------------------------------------------------------------

const LLPermissions LLPermissions::DEFAULT;

bool LLPermissions::sIsInOpenSim = false; // <FS:TJ/> [FIRE-36028] Fix OpenSim object permissions

// No creator = created by system
LLPermissions::LLPermissions()
{
    static_assert(sizeof(LLPermissions) == VKS_INV_PERMS_SIZE && alignof(LLPermissions) == alignof(VksInvPerms),
                  "LLPermissions is VksInvPerms");
    static_assert(offsetof(LLPermissions, mCreator) == VKS_INV_PERMS_OFFSET_CREATOR &&
                  offsetof(LLPermissions, mOwner) == VKS_INV_PERMS_OFFSET_OWNER &&
                  offsetof(LLPermissions, mLastOwner) == VKS_INV_PERMS_OFFSET_LAST_OWNER &&
                  offsetof(LLPermissions, mGroup) == VKS_INV_PERMS_OFFSET_GROUP &&
                  offsetof(LLPermissions, mMaskBase) == VKS_INV_PERMS_OFFSET_BASE &&
                  offsetof(LLPermissions, mMaskOwner) == VKS_INV_PERMS_OFFSET_OWNER_MASK &&
                  offsetof(LLPermissions, mMaskEveryone) == VKS_INV_PERMS_OFFSET_EVERYONE &&
                  offsetof(LLPermissions, mMaskGroup) == VKS_INV_PERMS_OFFSET_GROUP_MASK &&
                  offsetof(LLPermissions, mMaskNextOwner) == VKS_INV_PERMS_OFFSET_NEXT_OWNER &&
                  offsetof(LLPermissions, mIsGroupOwned) == VKS_INV_PERMS_OFFSET_GROUP_OWNED,
                  "LLPermissions has the field offsets of VksInvPerms");
    init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
}


// Default to created by system
void LLPermissions::init(const LLUUID& creator, const LLUUID& owner, const LLUUID& last_owner, const LLUUID& group)
{
    vks_inv_perms_init(vks(), creator.mData, owner.mData, last_owner.mData, group.mData);
}


void LLPermissions::initMasks(PermissionMask base, PermissionMask owner,
                              PermissionMask everyone, PermissionMask group,
                              PermissionMask next)
{
    vks_inv_perms_init_masks(vks(), base, owner, everyone, group, next);
}

// ! BACKWARDS COMPATIBILITY ! Override masks for inventory types that
// no longer can have restricted permissions.  This takes care of previous
// version landmarks that could have had no copy/mod/transfer bits set.
void LLPermissions::initMasks(LLInventoryType::EType type)
{
    vks_inv_perms_init_masks_for_type(vks(), (int32_t)type);
}

bool LLPermissions::getOwnership(LLUUID& owner_id, bool& is_group_owned) const
{
    return vks_inv_perms_get_ownership(vks(), owner_id.mData, &is_group_owned);
}

LLUUID LLPermissions::getSafeOwner() const
{
    LLUUID owner;
    if (vks_inv_perms_safe_owner(vks(), owner.mData))
    {
        return owner;
    }
    LL_WARNS() << "LLPermissions::getSafeOwner() called with no valid owner!" << LL_ENDL;
    LLUUID unused_uuid;
    unused_uuid.generate();

    return unused_uuid;
}

U32 LLPermissions::getCRC32() const
{
    return vks_inv_perms_crc32(vks());
}

void LLPermissions::set(const LLPermissions& from)
{
    mCreator    = from.mCreator;
    mOwner      = from.mOwner;
    mLastOwner  = from.mLastOwner;
    mGroup      = from.mGroup;

    mMaskBase       = from.mMaskBase;
    mMaskOwner      = from.mMaskOwner;
    mMaskEveryone   = from.mMaskEveryone;
    mMaskGroup      = from.mMaskGroup;
    mMaskNextOwner = from.mMaskNextOwner;
    mIsGroupOwned = from.mIsGroupOwned;
}

// Fix hierarchy of permissions.
void LLPermissions::fix()
{
    vks_inv_perms_fix(vks());
}

// Allow accumulation of permissions. Results in the tightest
// permissions possible. In the case of clashing UUIDs, it sets the ID
// to LLUUID::null.
void LLPermissions::accumulate(const LLPermissions& perm)
{
    vks_inv_perms_accumulate(vks(), perm.vks());
}

// saves last owner, sets current owner, and sets the group.  note
// that this function has to more cleverly apply the fair use
// permissions.
bool LLPermissions::setOwnerAndGroup(
    const LLUUID& agent,
    const LLUUID& owner,
    const LLUUID& group,
    bool is_atomic)
{
    return vks_inv_perms_set_owner_and_group(vks(), agent.mData, owner.mData, group.mData, is_atomic);
}

//Fix for DEV-33917, last owner isn't used much and has little impact on
//permissions so it's reasonably safe to do this, however, for now,
//limiting the functionality of this routine to objects which are
//group owned.
void LLPermissions::setLastOwner(const LLUUID& last_owner)
{
    vks_inv_perms_set_last_owner(vks(), last_owner.mData);
}


// only call this if you know what you're doing
// there are usually perm-bit consequences when the
// ownerhsip changes
void LLPermissions::yesReallySetOwner(const LLUUID& owner, bool group_owned)
{
    vks_inv_perms_yes_really_set_owner(vks(), owner.mData, group_owned);
}

bool LLPermissions::deedToGroup(const LLUUID& agent, const LLUUID& group)
{
    return vks_inv_perms_deed_to_group(vks(), agent.mData, group.mData);
}

bool LLPermissions::setBaseBits(const LLUUID& agent, bool set, PermissionMask bits)
{
    return vks_inv_perms_set_base_bits(vks(), agent.mData, set, bits);
}


// Note: If you attempt to set bits that the base bits doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setOwnerBits(const LLUUID& agent, bool set, PermissionMask bits)
{
    return vks_inv_perms_set_owner_bits(vks(), agent.mData, set, bits);
}

bool LLPermissions::setGroupBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    return vks_inv_perms_set_group_bits(vks(), agent.mData, group.mData, set, bits);
}


// Note: If you attempt to set bits that the creator or owner doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setEveryoneBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    return vks_inv_perms_set_everyone_bits(vks(), agent.mData, group.mData, set, bits);
}

// Note: If you attempt to set bits that the creator or owner doesn't allow,
// the function will succeed, but those bits will not be set.
bool LLPermissions::setNextOwnerBits(const LLUUID& agent, const LLUUID& group, bool set, PermissionMask bits)
{
    return vks_inv_perms_set_next_owner_bits(vks(), agent.mData, group.mData, set, bits);
}

//
// LLSD support for HTTP messages.
//
LLSD LLPermissions::packMessage() const
{
    return ll_inv_sd(vks_inv_perms_pack_message_llsd(vks()), "vks_inv_perms_pack_message_llsd");
}

//
// Messaging support
//
void LLPermissions::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_CreatorID, mCreator);
    msg->addUUIDFast(_PREHASH_OwnerID, mOwner);
    msg->addUUIDFast(_PREHASH_GroupID, mGroup);

    msg->addU32Fast(_PREHASH_BaseMask,      mMaskBase );
    msg->addU32Fast(_PREHASH_OwnerMask, mMaskOwner );
    msg->addU32Fast(_PREHASH_GroupMask, mMaskGroup );
    msg->addU32Fast(_PREHASH_EveryoneMask,  mMaskEveryone );
    msg->addU32Fast(_PREHASH_NextOwnerMask, mMaskNextOwner );
    msg->addBOOLFast(_PREHASH_GroupOwned, mIsGroupOwned);
}

void LLPermissions::unpackMessage(LLSD perms)
{
    LLSDFlat flat(perms);
    if (!vks_inv_perms_unpack_message_llsd(vks(), flat.get()))
    {
        ll_inv_vks_warn("vks_inv_perms_unpack_message_llsd");
    }
}

void LLPermissions::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num)
{
    msg->getUUIDFast(block, _PREHASH_CreatorID, mCreator, block_num);
    msg->getUUIDFast(block, _PREHASH_OwnerID,   mOwner, block_num);
    msg->getUUIDFast(block, _PREHASH_GroupID,   mGroup, block_num);

    msg->getU32Fast(block, _PREHASH_BaseMask, mMaskBase, block_num );
    msg->getU32Fast(block, _PREHASH_OwnerMask, mMaskOwner, block_num );
    msg->getU32Fast(block, _PREHASH_GroupMask, mMaskGroup, block_num );
    msg->getU32Fast(block, _PREHASH_EveryoneMask, mMaskEveryone, block_num );
    msg->getU32Fast(block, _PREHASH_NextOwnerMask, mMaskNextOwner, block_num );
    bool tmp;
    msg->getBOOLFast(block, _PREHASH_GroupOwned, tmp, block_num);
    mIsGroupOwned = tmp;
}

bool LLPermissions::importLegacyStream(std::istream& input_stream)
{
    LLPermissions read;
    if (!ll_inv_read_legacy(input_stream, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            read = *this;
            return vks_inv_perms_import_legacy(read.vks(), data, len, &stream);
        }))
    {
        ll_inv_vks_warn("vks_inv_perms_import_legacy");
        return false;
    }
    *this = read;
    return true;
}


bool LLPermissions::exportLegacyStream(std::ostream& output_stream) const
{
    LLInvRecordsPtr text(vks_inv_perms_export_legacy(vks()));
    return ll_inv_write_text(output_stream, text.get(), "vks_inv_perms_export_legacy");
}

void LLPermissions::importLLSD(const LLSD& sd_perm)
{
    LLSDFlat flat(sd_perm);
    if (!vks_inv_perms_import_llsd(vks(), flat.get()))
    {
        ll_inv_vks_warn("vks_inv_perms_import_llsd");
    }
}

bool LLPermissions::operator==(const LLPermissions &rhs) const
{
    return vks_inv_perms_equal(vks(), rhs.vks());
}


bool LLPermissions::operator!=(const LLPermissions &rhs) const
{
    return !vks_inv_perms_equal(vks(), rhs.vks());
}

std::ostream& operator<<(std::ostream &s, const LLPermissions &perm)
{
    s << "{Creator="        << perm.getCreator();
    s << ", Owner="         << perm.getOwner();
    s << ", Group="         << perm.getGroup();
    s << std::hex << ", BaseMask=0x" << perm.getMaskBase();
    s << ", OwnerMask=0x" << perm.getMaskOwner();
    s << ", EveryoneMask=0x" << perm.getMaskEveryone();
    s << ", GroupMask=0x" << perm.getMaskGroup();
    s << ", NextOwnerMask=0x" << perm.getMaskNextOwner() << std::dec;
    s << "}";
    return s;
}

///----------------------------------------------------------------------------
/// Class LLAggregatePermissions
///----------------------------------------------------------------------------

const LLAggregatePermissions LLAggregatePermissions::empty;


LLAggregatePermissions::LLAggregatePermissions()
{
    static_assert(sizeof(LLAggregatePermissions) == VKS_INV_AGGREGATE_SIZE &&
                  offsetof(LLAggregatePermissions, mBits) == 0,
                  "LLAggregatePermissions is VksInvAggregate");
    for(S32 i = 0; i < PI_COUNT; ++i)
    {
        mBits[i] = AP_EMPTY;
    }
}

LLAggregatePermissions::EValue LLAggregatePermissions::getValue(PermissionBit bit) const
{
    return (LLAggregatePermissions::EValue)vks_inv_aggregate_value(vks(), bit);
}

// returns the bits compressed into a single byte: 00TTMMCC
// where TT = transfer, MM = modify, and CC = copy
// LSB is to the right
U8 LLAggregatePermissions::getU8() const
{
    return vks_inv_aggregate_to_u8(vks());
}

bool LLAggregatePermissions::isEmpty() const
{
    return vks_inv_aggregate_is_empty(vks());
}

void LLAggregatePermissions::aggregate(PermissionMask mask)
{
    vks_inv_aggregate_mask(vks(), mask);
}

void LLAggregatePermissions::aggregate(const LLAggregatePermissions& ag)
{
    vks_inv_aggregate_merge(vks(), ag.vks());
}


void LLAggregatePermissions::packMessage(LLMessageSystem* msg, const char* field) const
{
    msg->addU8Fast(field, getU8());
}

void LLAggregatePermissions::unpackMessage(LLMessageSystem* msg, const char* block, const char* field, S32 block_num)
{
    U8 bits = 0;
    msg->getU8Fast(block, field, bits, block_num);
    vks_inv_aggregate_from_u8(vks(), bits);
}

const std::string AGGREGATE_VALUES[4] =
    {
        std::string( "Empty" ),
        std::string( "None" ),
        std::string( "Some" ),
        std::string( "All" )
    };

std::ostream& operator<<(std::ostream &s, const LLAggregatePermissions &perm)
{
    s << "{PI_COPY=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_COPY]];
    s << ", PI_MODIFY=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_MODIFY]];
    s << ", PI_TRANSFER=" << AGGREGATE_VALUES[perm.mBits[LLAggregatePermissions::PI_TRANSFER]];
    s << "}";
    return s;
}

// This converts a permissions mask into a string for debugging use. The
// export column (X) is there in an OpenSim build only, for isOpenSim.
void mask_to_string(U32 mask, char* str, bool isOpenSim /*=false*/) // <FS:Beq/> remove misleading X for export when not in OpenSim
{
    // At most five characters and the NUL: no more than the C++ wrote.
    vks_inv_mask_to_string(mask, isOpenSim, str, 6);
}

std::string mask_to_string(U32 mask, bool isOpenSim /*=false*/) // <FS:Beq/> remove misleading X for export when not in OpenSim
{
    char str[16];
    mask_to_string(mask, str, isOpenSim); // <FS:Beq/> remove misleading X for export when not in OpenSim
    return std::string(str);
}

///----------------------------------------------------------------------------
/// exported functions
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_permissions(const LLPermissions& perm)
{
    return ll_inv_sd(vks_inv_perms_fill_llsd(vks_perms(perm), nullptr), "vks_inv_perms_fill_llsd");
}
void ll_fill_sd_from_permissions(LLSD& rv, const LLPermissions& perm)
{
    LLSDFlat into(rv);
    VksLlsdDoc* filled = vks_inv_perms_fill_llsd(vks_perms(perm), into.get());
    if (!filled)
    {
        ll_inv_vks_warn("vks_inv_perms_fill_llsd");
        return;
    }
    rv = ll_sd_from_vks_doc(filled);
}

LLPermissions ll_permissions_from_sd(const LLSD& sd_perm)
{
    LLPermissions rv;
    rv.importLLSD(sd_perm);
    return rv;
}
