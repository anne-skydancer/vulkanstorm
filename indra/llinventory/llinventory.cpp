/**
 * @file llinventory.cpp
 * @brief Implementation of the inventory system.
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
#include "llinventory.h"

#include "llfasttimer.h"
#include "llinventorydefines.h"
#include "llsd.h"
#include "message.h"

#include "llinventory_vks.h"

// The rules are Rust since vkstorm-rust rung seven (sub-rung R1):
// vkstorm-inventory through vkstorm_ffi.h, over a flat view of the fields
// (names and descriptions, CRCs, slam bits, legacy text, LLSD, message
// blocks). The fields, the accessors, the copies and the message field I/O
// stay here.

///----------------------------------------------------------------------------
/// Class LLInventoryObject
///----------------------------------------------------------------------------

LLInventoryObject::LLInventoryObject(const LLUUID& uuid,
                                     const LLUUID& parent_uuid,
                                     LLAssetType::EType type,
                                     const std::string& name)
:   mUUID(uuid),
    mParentUUID(parent_uuid),
    mType(type),
    mName(name),
    mCreationDate(0),
    mFavorite(false)
{
    correctInventoryName(mName);
}

LLInventoryObject::LLInventoryObject()
:   mType(LLAssetType::AT_NONE),
    mCreationDate(0),
    mFavorite(false)
{
}

LLInventoryObject::~LLInventoryObject()
{
}

void LLInventoryObject::copyObject(const LLInventoryObject* other)
{
    mUUID = other->mUUID;
    mParentUUID = other->mParentUUID;
    mType = other->mType;
    mName = other->mName;
    mThumbnailUUID = other->mThumbnailUUID;
    mFavorite = other->mFavorite;
    mRuntime = other->mRuntime;
}

const LLUUID& LLInventoryObject::getUUID() const
{
    return mUUID;
}

const LLUUID& LLInventoryObject::getParentUUID() const
{
    return mParentUUID;
}

const LLUUID& LLInventoryObject::getThumbnailUUID() const
{
    return mThumbnailUUID;
}

bool LLInventoryObject::getIsFavorite() const
{
    return mFavorite;
}

const std::string& LLInventoryObject::getRuntime() const
{
    return mRuntime;
}

void LLInventoryObject::setRuntime(std::string_view runtime)
{
    mRuntime = runtime;
}

const std::string& LLInventoryObject::getName() const
{
    return mName;
}

// To bypass linked items, since llviewerinventory's getType
// will return the linked-to item's type instead of this object's type.
LLAssetType::EType LLInventoryObject::getActualType() const
{
    return mType;
}

bool LLInventoryObject::getIsLinkType() const
{
    return LLAssetType::lookupIsLinkType(mType);
}

// See LLInventoryItem override.
// virtual
const LLUUID& LLInventoryObject::getLinkedUUID() const
{
    return mUUID;
}

LLAssetType::EType LLInventoryObject::getType() const
{
    return mType;
}

void LLInventoryObject::setUUID(const LLUUID& new_uuid)
{
    mUUID = new_uuid;
}

void LLInventoryObject::rename(const std::string& n)
{
    char name[VKS_INV_NAME_BUF];
    size_t len = 0;
    if (vks_inv_rename(vks_ptr(mName), mName.size(), vks_ptr(n), n.size(), name, sizeof(name), &len))
    {
        mName.assign(name, len);
    }
}

void LLInventoryObject::setParent(const LLUUID& new_parent)
{
    mParentUUID = new_parent;
}

void LLInventoryObject::setThumbnailUUID(const LLUUID& thumbnail_uuid)
{
    mThumbnailUUID = thumbnail_uuid;
}

void LLInventoryObject::setFavorite(bool favorite)
{
    mFavorite = favorite;
}

void LLInventoryObject::setType(LLAssetType::EType type)
{
    mType = type;
    if (type != LLAssetType::AT_LSL_TEXT) mRuntime.clear();
}

void LLInventoryObject::toVks(VksInvObject& view) const
{
    vks_set_uuid(view.id, mUUID);
    vks_set_uuid(view.parent, mParentUUID);
    vks_set_uuid(view.thumbnail, mThumbnailUUID);
    view.asset_type = (int32_t)mType;
    view.favorite = mFavorite;
    view.creation_date = (int64_t)mCreationDate;
    view.name = vks_bytes(mName);
    view.runtime = vks_bytes(mRuntime);
}

void LLInventoryObject::fromVks(const VksInvObject& view)
{
    mUUID = vks_uuid(view.id);
    mParentUUID = vks_uuid(view.parent);
    mThumbnailUUID = vks_uuid(view.thumbnail);
    mType = (LLAssetType::EType)view.asset_type;
    mFavorite = view.favorite;
    mCreationDate = (time_t)view.creation_date;
    vks_assign(mName, view.name);
    vks_assign(mRuntime, view.runtime);
}

// virtual
bool LLInventoryObject::importLegacyStream(std::istream& input_stream)
{
    VksInvObject view;
    toVks(view);
    LLInvRecordsPtr result;
    if (!ll_inv_read_legacy(input_stream, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            result.reset(vks_inv_object_import_legacy(&view, data, len, &stream));
            if (!result)
            {
                return false;
            }
            stream = vks_inv_records_view(result.get())->stream;
            return true;
        }))
    {
        ll_inv_vks_warn("vks_inv_object_import_legacy");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->objects[0]);
    return r->ok;
}

bool LLInventoryObject::exportLegacyStream(std::ostream& output_stream, bool) const
{
    VksInvObject view;
    toVks(view);
    LLInvRecordsPtr text(vks_inv_object_export_legacy(&view));
    return ll_inv_write_text(output_stream, text.get(), "vks_inv_object_export_legacy");
}

void LLInventoryObject::updateParentOnServer(bool) const
{
    // don't do nothin'
    LL_WARNS() << "LLInventoryObject::updateParentOnServer() called.  Doesn't do anything." << LL_ENDL;
}

void LLInventoryObject::updateServer(bool) const
{
    // don't do nothin'
    LL_WARNS() << "LLInventoryObject::updateServer() called.  Doesn't do anything." << LL_ENDL;
}

// static
void LLInventoryObject::correctInventoryName(std::string& name)
{
    char corrected[VKS_INV_NAME_BUF];
    const size_t len = vks_inv_correct_name(vks_ptr(name), name.size(), corrected, sizeof(corrected));
    name.assign(corrected, std::min(len, sizeof(corrected) - 1));
}

time_t LLInventoryObject::getCreationDate() const
{
    return mCreationDate;
}

void LLInventoryObject::setCreationDate(time_t creation_date_utc)
{
    mCreationDate = creation_date_utc;
}


const std::string& LLInventoryItem::getDescription() const
{
    return mDescription;
}

const std::string& LLInventoryItem::getActualDescription() const
{
    return mDescription;
}

///----------------------------------------------------------------------------
/// Class LLInventoryItem
///----------------------------------------------------------------------------

LLInventoryItem::LLInventoryItem(const LLUUID& uuid,
                                 const LLUUID& parent_uuid,
                                 const LLPermissions& permissions,
                                 const LLUUID& asset_uuid,
                                 LLAssetType::EType type,
                                 LLInventoryType::EType inv_type,
                                 const std::string& name,
                                 const std::string& desc,
                                 const LLSaleInfo& sale_info,
                                 U32 flags,
                                 S32 creation_date_utc) :
    LLInventoryObject(uuid, parent_uuid, type, name),
    mPermissions(permissions),
    mAssetUUID(asset_uuid),
    mDescription(desc),
    mSaleInfo(sale_info),
    mInventoryType(inv_type),
    mFlags(flags)
{
    mCreationDate = creation_date_utc;

    LLStringUtil::replaceNonstandardASCII(mDescription, ' ');
    LLStringUtil::replaceChar(mDescription, '|', ' ');

    mPermissions.initMasks(inv_type);
}

LLInventoryItem::LLInventoryItem() :
    LLInventoryObject(),
    mPermissions(),
    mAssetUUID(),
    mDescription(),
    mSaleInfo(),
    mInventoryType(LLInventoryType::IT_NONE),
    mFlags(0)
{
    mCreationDate = 0;
}

LLInventoryItem::LLInventoryItem(const LLInventoryItem* other) :
    LLInventoryObject()
{
    copyItem(other);
}

LLInventoryItem::~LLInventoryItem()
{
}

// virtual
void LLInventoryItem::copyItem(const LLInventoryItem* other)
{
    copyObject(other);
    mPermissions = other->mPermissions;
    mAssetUUID = other->mAssetUUID;
    mThumbnailUUID = other->mThumbnailUUID;
    mDescription = other->mDescription;
    mSaleInfo = other->mSaleInfo;
    mInventoryType = other->mInventoryType;
    mFlags = other->mFlags;
    mCreationDate = other->mCreationDate;
    mRuntime = other->mRuntime;
}

// If this is a linked item, then the UUID of the base object is
// this item's assetID.
// virtual
const LLUUID& LLInventoryItem::getLinkedUUID() const
{
    if (LLAssetType::lookupIsLinkType(getActualType()))
    {
        return mAssetUUID;
    }

    return LLInventoryObject::getLinkedUUID();
}

const LLPermissions& LLInventoryItem::getPermissions() const
{
    return mPermissions;
}

const LLUUID& LLInventoryItem::getCreatorUUID() const
{
    return mPermissions.getCreator();
}

const LLUUID& LLInventoryItem::getAssetUUID() const
{
    return mAssetUUID;
}

void LLInventoryItem::setAssetUUID(const LLUUID& asset_id)
{
    mAssetUUID = asset_id;
}

void LLInventoryItem::toVks(VksInvItem& view) const
{
    LLInventoryObject::toVks(view.object);
    view.perms = *vks_perms(mPermissions);
    vks_set_uuid(view.asset, mAssetUUID);
    view.sale = *vks_sale(mSaleInfo);
    view.inv_type = (int32_t)mInventoryType;
    view.flags = mFlags;
    view.description = vks_bytes(mDescription);
}

void LLInventoryItem::fromVks(const VksInvItem& view)
{
    LLInventoryObject::fromVks(view.object);
    *vks_perms(mPermissions) = view.perms;
    mAssetUUID = vks_uuid(view.asset);
    *vks_sale(mSaleInfo) = view.sale;
    mInventoryType = (LLInventoryType::EType)view.inv_type;
    mFlags = view.flags;
    vks_assign(mDescription, view.description);
}

// The item as its virtual accessors give it, for the functions whose C++
// read the accessors rather than the fields (a viewer link item answers
// with its target's).
static void accessor_view(const LLInventoryItem& item, VksInvItem& view)
{
    item.toVks(view);
    vks_set_uuid(view.object.id, item.getUUID());
    vks_set_uuid(view.object.thumbnail, item.getThumbnailUUID());
    view.object.asset_type = (int32_t)item.getType();
    view.object.favorite = item.getIsFavorite();
    view.object.creation_date = (int64_t)item.getCreationDate();
    view.object.name = vks_bytes(item.getName());
    view.object.runtime = vks_bytes(item.getRuntime());
    view.perms = *vks_perms(item.getPermissions());
    vks_set_uuid(view.asset, item.getAssetUUID());
    view.sale = *vks_sale(item.getSaleInfo());
    view.inv_type = (int32_t)item.getInventoryType();
    view.flags = item.getFlags();
    view.description = vks_bytes(item.getDescription());
}

U32 LLInventoryItem::getCRC32() const
{
    VksInvItem view;
    toVks(view);
    return vks_inv_item_crc32(&view);
}

// static
void LLInventoryItem::correctInventoryDescription(std::string& desc)
{
    std::string corrected = vks_ffi_string([&](char* buf, size_t cap) {
        return vks_inv_correct_description(vks_ptr(desc), desc.size(), buf, cap);
    });
    desc.swap(corrected);
}

void LLInventoryItem::setDescription(const std::string& d)
{
    std::string desc(d.size() + 1, '\0');
    size_t len = 0;
    if (vks_inv_set_description(vks_ptr(mDescription), mDescription.size(), vks_ptr(d), d.size(), desc.data(),
                                desc.size(), &len))
    {
        desc.resize(std::min(len, d.size()));
        mDescription.swap(desc);
    }
}

void LLInventoryItem::setPermissions(const LLPermissions& perm)
{
    mPermissions = perm;

    // Override permissions to unrestricted if this is a landmark
    mPermissions.initMasks(mInventoryType);
}

void LLInventoryItem::setInventoryType(LLInventoryType::EType inv_type)
{
    mInventoryType = inv_type;
}

void LLInventoryItem::setFlags(U32 flags)
{
    mFlags = flags;
}

// Currently only used in the Viewer to handle calling cards
// where the creator is actually used to store the target.
void LLInventoryItem::setCreator(const LLUUID& creator)
{
    mPermissions.setCreator(creator);
}

void LLInventoryItem::accumulatePermissionSlamBits(const LLInventoryItem& old_item)
{
    VksInvItem item;
    VksInvItem old;
    accessor_view(*this, item);
    accessor_view(old_item, old);
    setFlags(vks_inv_item_slam_bits(&item, &old));
}

const LLSaleInfo& LLInventoryItem::getSaleInfo() const
{
    return mSaleInfo;
}

void LLInventoryItem::setSaleInfo(const LLSaleInfo& sale_info)
{
    mSaleInfo = sale_info;
}

LLInventoryType::EType LLInventoryItem::getInventoryType() const
{
    return mInventoryType;
}

U32 LLInventoryItem::getFlags() const
{
    return mFlags;
}

time_t LLInventoryItem::getCreationDate() const
{
    return mCreationDate;
}


// virtual
void LLInventoryItem::packMessage(LLMessageSystem* msg) const
{
    VksInvItem view;
    toVks(view);
    VksInvItemBlock block;
    if (!vks_inv_item_to_block(&view, nullptr, &block))
    {
        ll_inv_vks_warn("vks_inv_item_to_block");
        return;
    }
    msg->addUUIDFast(_PREHASH_ItemID, vks_uuid(block.item_id));
    msg->addUUIDFast(_PREHASH_FolderID, vks_uuid(block.folder_id));
    msg->addUUIDFast(_PREHASH_CreatorID, vks_uuid(block.creator_id));
    msg->addUUIDFast(_PREHASH_OwnerID, vks_uuid(block.owner_id));
    msg->addUUIDFast(_PREHASH_GroupID, vks_uuid(block.group_id));
    msg->addU32Fast(_PREHASH_BaseMask, block.base_mask);
    msg->addU32Fast(_PREHASH_OwnerMask, block.owner_mask);
    msg->addU32Fast(_PREHASH_GroupMask, block.group_mask);
    msg->addU32Fast(_PREHASH_EveryoneMask, block.everyone_mask);
    msg->addU32Fast(_PREHASH_NextOwnerMask, block.next_owner_mask);
    msg->addBOOLFast(_PREHASH_GroupOwned, block.group_owned);
    msg->addUUIDFast(_PREHASH_AssetID, vks_uuid(block.asset));
    msg->addS8Fast(_PREHASH_Type, block.asset_type);
    msg->addS8Fast(_PREHASH_InvType, block.inv_type);
    msg->addU32Fast(_PREHASH_Flags, block.flags);
    msg->addU8Fast(_PREHASH_SaleType, block.sale_type);
    msg->addS32Fast(_PREHASH_SalePrice, block.sale_price);
    msg->addStringFast(_PREHASH_Name, mName);
    msg->addStringFast(_PREHASH_Description, mDescription);
    msg->addS32Fast(_PREHASH_CreationDate, block.creation_date);
    // getCRC32 is virtual: the CRC of the class that packs.
    msg->addU32Fast(_PREHASH_CRC, getCRC32());
}

// virtual
bool LLInventoryItem::unpackMessage(LLMessageSystem* msg, const char* block, S32 block_num)
{
    VksInvItem view;
    toVks(view);
    // The block starts as the item's own fields, as the reads went into them.
    VksInvItemBlock b;
    if (!vks_inv_item_to_block(&view, nullptr, &b))
    {
        ll_inv_vks_warn("vks_inv_item_to_block");
        return false;
    }
    LLUUID id;
    auto get_uuid = [&](const char* var, uint8_t* field) {
        id = vks_uuid(field);
        msg->getUUIDFast(block, var, id, block_num);
        vks_set_uuid(field, id);
    };
    get_uuid(_PREHASH_ItemID, b.item_id);
    get_uuid(_PREHASH_FolderID, b.folder_id);
    get_uuid(_PREHASH_CreatorID, b.creator_id);
    get_uuid(_PREHASH_OwnerID, b.owner_id);
    get_uuid(_PREHASH_GroupID, b.group_id);
    msg->getU32Fast(block, _PREHASH_BaseMask, b.base_mask, block_num);
    msg->getU32Fast(block, _PREHASH_OwnerMask, b.owner_mask, block_num);
    msg->getU32Fast(block, _PREHASH_GroupMask, b.group_mask, block_num);
    msg->getU32Fast(block, _PREHASH_EveryoneMask, b.everyone_mask, block_num);
    msg->getU32Fast(block, _PREHASH_NextOwnerMask, b.next_owner_mask, block_num);
    msg->getBOOLFast(block, _PREHASH_GroupOwned, b.group_owned, block_num);
    get_uuid(_PREHASH_AssetID, b.asset);
    msg->getS8Fast(block, _PREHASH_Type, b.asset_type, block_num);
    msg->getS8(block, "InvType", b.inv_type, block_num);
    msg->getU32Fast(block, _PREHASH_Flags, b.flags, block_num);
    msg->getU8Fast(block, _PREHASH_SaleType, b.sale_type, block_num);
    msg->getS32Fast(block, _PREHASH_SalePrice, b.sale_price, block_num);
    std::string name = mName;
    std::string desc = mDescription;
    msg->getStringFast(block, _PREHASH_Name, name, block_num);
    msg->getStringFast(block, _PREHASH_Description, desc, block_num);
    b.name = vks_bytes(name);
    b.description = vks_bytes(desc);
    msg->getS32(block, "CreationDate", b.creation_date, block_num);
    b.crc = 0;
    msg->getU32(block, "CRC", b.crc, block_num);

    LLInvRecordsPtr result(vks_inv_item_apply_block(&view, &b));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_item_apply_block");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->items[0]);
    return r->ok;
}

// virtual
bool LLInventoryItem::importLegacyStream(std::istream& input_stream)
{
    VksInvItem view;
    toVks(view);
    LLInvRecordsPtr result;
    if (!ll_inv_read_legacy(input_stream, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            result.reset(vks_inv_item_import_legacy(&view, data, len, &stream));
            if (!result)
            {
                return false;
            }
            stream = vks_inv_records_view(result.get())->stream;
            return true;
        }))
    {
        ll_inv_vks_warn("vks_inv_item_import_legacy");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->items[0]);
    return r->ok;
}

bool LLInventoryItem::exportLegacyStream(std::ostream& output_stream, bool include_asset_key) const
{
    VksInvItem view;
    toVks(view);
    LLInvRecordsPtr text(vks_inv_item_export_legacy(&view, include_asset_key));
    return ll_inv_write_text(output_stream, text.get(), "vks_inv_item_export_legacy");
}

LLSD LLInventoryItem::asLLSD() const
{
    VksInvItem view;
    toVks(view);
    return ll_inv_sd(vks_inv_item_fill_llsd(&view, nullptr), "vks_inv_item_fill_llsd");
}

void LLInventoryItem::asLLSD( LLSD& sd ) const
{
    VksInvItem view;
    toVks(view);
    LLSDFlat into(sd);
    VksLlsdDoc* filled = vks_inv_item_fill_llsd(&view, into.get());
    if (!filled)
    {
        ll_inv_vks_warn("vks_inv_item_fill_llsd");
        return;
    }
    sd = ll_sd_from_vks_doc(filled);
}

bool LLInventoryItem::fromLLSD(const LLSD& sd, bool is_new)
{
    LL_PROFILE_ZONE_SCOPED;
    VksInvItem view;
    toVks(view);
    LLSDFlat flat(sd);
    // In OpenSim the permissions are not reset first (FIRE-36028).
    LLInvRecordsPtr result(vks_inv_item_from_llsd(&view, flat.get(), is_new, LLPermissions::getIsInOpenSim()));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_item_from_llsd");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->items[0]);
    return r->ok;
}

///----------------------------------------------------------------------------
/// Class LLInventoryCategory
///----------------------------------------------------------------------------

LLInventoryCategory::LLInventoryCategory(const LLUUID& uuid,
                                         const LLUUID& parent_uuid,
                                         LLFolderType::EType preferred_type,
                                         const std::string& name) :
    LLInventoryObject(uuid, parent_uuid, LLAssetType::AT_CATEGORY, name),
    mPreferredType(preferred_type)
{
}

LLInventoryCategory::LLInventoryCategory() :
    mPreferredType(LLFolderType::FT_NONE)
{
    mType = LLAssetType::AT_CATEGORY;
}

LLInventoryCategory::LLInventoryCategory(const LLInventoryCategory* other) :
    LLInventoryObject()
{
    copyCategory(other);
}

LLInventoryCategory::~LLInventoryCategory()
{
}

// virtual
void LLInventoryCategory::copyCategory(const LLInventoryCategory* other)
{
    copyObject(other);
    mPreferredType = other->mPreferredType;
}

LLFolderType::EType LLInventoryCategory::getPreferredType() const
{
    return mPreferredType;
}

void LLInventoryCategory::setPreferredType(LLFolderType::EType type)
{
    mPreferredType = type;
}

void LLInventoryCategory::toVks(VksInvCategory& view) const
{
    LLInventoryObject::toVks(view.object);
    view.preferred_type = (int32_t)mPreferredType;
    memset(view.owner, 0, sizeof(view.owner));
    view.version = -1;
}

void LLInventoryCategory::fromVks(const VksInvCategory& view)
{
    LLInventoryObject::fromVks(view.object);
    mPreferredType = (LLFolderType::EType)view.preferred_type;
}

LLSD LLInventoryCategory::asLLSD() const
{
    VksInvCategory view;
    toVks(view);
    return ll_inv_sd(vks_inv_category_to_llsd(&view, VKS_INV_CAT_AS_LLSD), "vks_inv_category_to_llsd");
}

LLSD LLInventoryCategory::asAISCreateCatLLSD() const
{
    VksInvCategory view;
    toVks(view);
    return ll_inv_sd(vks_inv_category_to_llsd(&view, VKS_INV_CAT_AIS_CREATE), "vks_inv_category_to_llsd");
}


// virtual
void LLInventoryCategory::packMessage(LLMessageSystem* msg) const
{
    msg->addUUIDFast(_PREHASH_FolderID, mUUID);
    msg->addUUIDFast(_PREHASH_ParentID, mParentUUID);
    S8 type = static_cast<S8>(mPreferredType);
    msg->addS8Fast(_PREHASH_Type, type);
    msg->addStringFast(_PREHASH_Name, mName);
}

bool LLInventoryCategory::fromLLSD(const LLSD& sd)
{
    VksInvCategory view;
    toVks(view);
    LLSDFlat flat(sd);
    LLInvRecordsPtr result(vks_inv_category_from_llsd(&view, flat.get()));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_category_from_llsd");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->categories[0]);
    return r->ok;
}

// virtual
void LLInventoryCategory::unpackMessage(LLMessageSystem* msg,
                                        const char* block,
                                        S32 block_num)
{
    VksInvCategory view;
    toVks(view);
    // The block starts as the category's own fields, as the reads went into them.
    VksInvFolderBlock b;
    LLUUID id = mUUID;
    msg->getUUIDFast(block, _PREHASH_FolderID, id, block_num);
    vks_set_uuid(b.folder_id, id);
    id = mParentUUID;
    msg->getUUIDFast(block, _PREHASH_ParentID, id, block_num);
    vks_set_uuid(b.parent_id, id);
    b.preferred_type = static_cast<S8>(mPreferredType);
    msg->getS8Fast(block, _PREHASH_Type, b.preferred_type, block_num);
    std::string name = mName;
    msg->getStringFast(block, _PREHASH_Name, name, block_num);
    b.name = vks_bytes(name);

    LLInvRecordsPtr result(vks_inv_category_apply_block(&view, &b));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_category_apply_block");
        return;
    }
    fromVks(vks_inv_records_view(result.get())->categories[0]);
}

// virtual
bool LLInventoryCategory::importLegacyStream(std::istream& input_stream)
{
    VksInvCategory view;
    toVks(view);
    LLInvRecordsPtr result;
    if (!ll_inv_read_legacy(input_stream, [&](const uint8_t* data, size_t len, VksInvStream& stream) {
            result.reset(vks_inv_category_import_legacy(&view, data, len, &stream));
            if (!result)
            {
                return false;
            }
            stream = vks_inv_records_view(result.get())->stream;
            return true;
        }))
    {
        ll_inv_vks_warn("vks_inv_category_import_legacy");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->categories[0]);
    return r->ok;
}

bool LLInventoryCategory::exportLegacyStream(std::ostream& output_stream, bool) const
{
    VksInvCategory view;
    toVks(view);
    LLInvRecordsPtr text(vks_inv_category_export_legacy(&view));
    return ll_inv_write_text(output_stream, text.get(), "vks_inv_category_export_legacy");
}

void LLInventoryCategory::exportLLSD(LLSD& cat_data) const
{
    VksInvCategory view;
    toVks(view);
    LLSDFlat into(cat_data);
    VksLlsdDoc* filled = vks_inv_category_export_llsd(&view, false, into.get());
    if (!filled)
    {
        ll_inv_vks_warn("vks_inv_category_export_llsd");
        return;
    }
    cat_data = ll_sd_from_vks_doc(filled);
}

// importLLSD is virtual (the viewer subclass reads its own keys), so the map
// goes to it key by key.
bool LLInventoryCategory::importLLSDMap(const LLSD& cat_data)
{
    LLSD::map_const_iterator i, end;
    end = cat_data.endMap();
    for ( i = cat_data.beginMap(); i != end; ++i)
    {
        importLLSD(i->first, i->second);
    }
    return true;
}

bool LLInventoryCategory::importLLSD(const std::string& label, const LLSD& value)
{
    VksInvCategory view;
    toVks(view);
    LLSDFlat flat(value);
    LLInvRecordsPtr result(vks_inv_category_import_llsd(&view, vks_ptr(label), label.size(), flat.get(), false));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_category_import_llsd");
        return false;
    }
    const VksInvRecordsView* r = vks_inv_records_view(result.get());
    fromVks(r->categories[0]);
    return r->ok;
}

///----------------------------------------------------------------------------
/// Local function definitions for testing purposes
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_inventory_item(LLPointer<LLInventoryItem> item)
{
    LLSD rv;
    if(item.isNull()) return rv;
    VksInvItem view;
    accessor_view(*item, view);
    rv = ll_inv_sd(vks_inv_item_create_sd(&view), "vks_inv_item_create_sd");
    if (rv.isUndefined() && item->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_item() for item with AT_NONE"
            << LL_ENDL;
    }
    return rv;
}

LLSD ll_create_sd_from_inventory_category(LLPointer<LLInventoryCategory> cat)
{
    LLSD rv;
    if(cat.isNull()) return rv;
    // The C++ read the id, name and type through the accessors.
    VksInvCategory view;
    cat->toVks(view);
    vks_set_uuid(view.object.id, cat->getUUID());
    view.object.name = vks_bytes(cat->getName());
    view.object.asset_type = (int32_t)cat->getType();
    rv = ll_inv_sd(vks_inv_category_to_llsd(&view, VKS_INV_CAT_CREATE_SD), "vks_inv_category_to_llsd");
    if (rv.isUndefined() && cat->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_category() for cat with AT_NONE"
            << LL_ENDL;
    }
    return rv;
}

LLPointer<LLInventoryCategory> ll_create_category_from_sd(const LLSD& sd_cat)
{
    LLPointer<LLInventoryCategory> rv = new LLInventoryCategory;
    LLSDFlat flat(sd_cat);
    LLInvRecordsPtr result(vks_inv_category_from_create_sd(flat.get()));
    if (!result)
    {
        ll_inv_vks_warn("vks_inv_category_from_create_sd");
        return rv;
    }
    rv->fromVks(vks_inv_records_view(result.get())->categories[0]);
    return rv;
}
