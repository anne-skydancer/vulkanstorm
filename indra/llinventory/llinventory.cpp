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
#include "llxorcipher.h"
#include "llsd.h"
#include "message.h"

#include "llsdutil.h"

#include "llinventory_vks.h"

// The rules are Rust since vkstorm-rust rung seven (sub-rung R1):
// vkstorm-inventory through vkstorm_ffi.h, over a flat view of the fields
// (names and descriptions, CRCs, slam bits, legacy text, message blocks).
// The fields, the accessors, the copies and the message field I/O stay
// here, and so do the LLSD forms (asLLSD, fromLLSD, exportLLSD,
// importLLSD/importLLSDMap and the ll_create_* functions): they take and
// build C++ LLSD trees, and flattening those across the C ABI cost more
// than the rules saved. They retire once the cache load (R2) and the AIS
// replies (R3a) no longer build C++ LLSD.

///----------------------------------------------------------------------------
/// Exported functions
///----------------------------------------------------------------------------
// FIXME D567 - what's the point of these, especially if we don't even use them consistently?
static const std::string INV_ITEM_ID_LABEL("item_id");
static const std::string INV_FOLDER_ID_LABEL("cat_id");
static const std::string INV_PARENT_ID_LABEL("parent_id");
static const std::string INV_THUMBNAIL_LABEL("thumbnail");
static const std::string INV_FAVORITE_LABEL("favorite");
static const std::string INV_THUMBNAIL_ID_LABEL("thumbnail_id");
static const std::string INV_ASSET_TYPE_LABEL("type");
static const std::string INV_PREFERRED_TYPE_LABEL("preferred_type");
static const std::string INV_INVENTORY_TYPE_LABEL("inv_type");
static const std::string INV_NAME_LABEL("name");
static const std::string INV_DESC_LABEL("desc");
static const std::string INV_PERMISSIONS_LABEL("permissions");
static const std::string INV_SHADOW_ID_LABEL("shadow_id");
static const std::string INV_ASSET_ID_LABEL("asset_id");
static const std::string INV_LINKED_ID_LABEL("linked_id");
static const std::string INV_SALE_INFO_LABEL("sale_info");
static const std::string INV_FLAGS_LABEL("flags");
static const std::string INV_CREATION_DATE_LABEL("created_at");
static const std::string INV_TOGGLED_LABEL("toggled");

// key used by agent-inventory-service
static const std::string INV_ASSET_TYPE_LABEL_WS("type_default");
static const std::string INV_FOLDER_ID_LABEL_WS("category_id");

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

const LLUUID MAGIC_ID("3c115e51-04f4-523c-9fa6-98aff1034730");

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
    LLSD sd;
    asLLSD(sd);
    return sd;
}

void LLInventoryItem::asLLSD( LLSD& sd ) const
{
    sd[INV_ITEM_ID_LABEL] = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    ll_fill_sd_from_permissions(sd[INV_PERMISSIONS_LABEL], mPermissions);

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    if (!mRuntime.empty())
    {
        sd["script"] = LLSD().with("runtime", mRuntime);
    }

    U32 mask = mPermissions.getMaskBase();
    if(((mask & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED)
        || (mAssetUUID.isNull()))
    {
        sd[INV_ASSET_ID_LABEL] = mAssetUUID;
    }
    else
    {
        // *TODO: get rid of this. Phoenix 2008-01-30
        LLUUID shadow_id(mAssetUUID);
        LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
        cipher.encrypt(shadow_id.mData, UUID_BYTES);
        sd[INV_SHADOW_ID_LABEL] = shadow_id;
    }
    sd[INV_ASSET_TYPE_LABEL] = std::string(LLAssetType::lookup(mType));
    const std::string inv_type_str = LLInventoryType::lookup(mInventoryType);
    if(!inv_type_str.empty())
    {
        sd[INV_INVENTORY_TYPE_LABEL] = inv_type_str;
    }
    else
    {
        sd[INV_INVENTORY_TYPE_LABEL] = (LLSD::Integer)mInventoryType;
    }
    //sd[INV_FLAGS_LABEL] = (S32)mFlags;
    sd[INV_FLAGS_LABEL] = ll_sd_from_U32(mFlags);
    mSaleInfo.asLLSD(sd[INV_SALE_INFO_LABEL]);
    sd[INV_NAME_LABEL] = mName;
    sd[INV_DESC_LABEL] = mDescription;
    sd[INV_CREATION_DATE_LABEL] = (LLSD::Integer)mCreationDate;
}

bool LLInventoryItem::fromLLSD(const LLSD& sd, bool is_new)
{
    LL_PROFILE_ZONE_SCOPED;
    if (is_new)
    {
        // If we're adding LLSD to an existing object, need avoid
        // clobbering these fields.
        mInventoryType = LLInventoryType::IT_NONE;
        mAssetUUID.setNull();
    }

    // TODO - figure out if this should be moved into the noclobber fields above
    mThumbnailUUID.setNull();
    mFavorite = false;
    // <FS:TJ> [FIRE-36028] Fix OpenSim object permissions
    //mPermissions.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    if (!LLPermissions::getIsInOpenSim())
    {
        mPermissions.init(LLUUID::null, LLUUID::null, LLUUID::null, LLUUID::null);
    }
    // </FS:TJ>

    if (is_new || sd.has("script") || (sd.has("metadata") && sd["metadata"].has("script")))
    {
        mRuntime = sd.has("script") ? sd["script"]["runtime"].asString()
                                   : sd["metadata"]["script"]["runtime"].asString();
    }

    // iterate as map to avoid making unnecessary temp copies of everything
    LLSD::map_const_iterator i, end;
    end = sd.endMap();
    for (i = sd.beginMap(); i != end; ++i)
    {
        if (i->first == INV_ITEM_ID_LABEL)
        {
            mUUID = i->second;
            continue;
        }

        if (i->first == INV_PARENT_ID_LABEL)
        {
            mParentUUID = i->second;
            continue;
        }

        if (i->first == INV_THUMBNAIL_LABEL)
        {
            const LLSD &thumbnail_map = i->second;
            if (thumbnail_map.has(INV_ASSET_ID_LABEL))
            {
                mThumbnailUUID = thumbnail_map[INV_ASSET_ID_LABEL];
            }
            /* Example:
                <key> asset_id </key>
                <uuid> acc0ec86 - 17f2 - 4b92 - ab41 - 6718b1f755f7 </uuid>
                <key> perms </key>
                <integer> 8 </integer>
                <key>service</key>
                <integer> 3 </integer>
                <key>version</key>
                <integer> 1 </key>
            */
          continue;
        }

        if (i->first == INV_THUMBNAIL_ID_LABEL)
        {
            mThumbnailUUID = i->second.asUUID();
            continue;
        }

        if (i->first == INV_FAVORITE_LABEL)
        {
            const LLSD& favorite_map = i->second;
            if (favorite_map.has(INV_TOGGLED_LABEL))
            {
                mFavorite = favorite_map[INV_TOGGLED_LABEL].asBoolean();
            }
            continue;
        }

        if (i->first == INV_PERMISSIONS_LABEL)
        {
            mPermissions.importLLSD(i->second);
            continue;
        }

        if (i->first == INV_SALE_INFO_LABEL)
        {
            // Sale info used to contain next owner perm. It is now in
            // the permissions. Thus, we read that out, and fix legacy
            // objects. It's possible this op would fail, but it
            // should pick up the vast majority of the tasks.
            bool has_perm_mask = false;
            U32  perm_mask     = 0;
            if (!mSaleInfo.fromLLSD(i->second, has_perm_mask, perm_mask))
            {
                return false;
            }
            if (has_perm_mask)
            {
                if (perm_mask == PERM_NONE)
                {
                    perm_mask = mPermissions.getMaskOwner();
                }
                // fair use fix.
                if (!(perm_mask & PERM_COPY))
                {
                    perm_mask |= PERM_TRANSFER;
                }
                mPermissions.setMaskNext(perm_mask);
            }
            continue;
        }

        if (i->first == INV_SHADOW_ID_LABEL)
        {
            mAssetUUID = i->second;
            LLXORCipher cipher(MAGIC_ID.mData, UUID_BYTES);
            cipher.decrypt(mAssetUUID.mData, UUID_BYTES);
            continue;
        }

        if (i->first == INV_ASSET_ID_LABEL)
        {
            mAssetUUID = i->second;
            continue;
        }

        if (i->first == INV_LINKED_ID_LABEL)
        {
            mAssetUUID = i->second;
            continue;
        }

        if (i->first == INV_ASSET_TYPE_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isString())
            {
                mType = LLAssetType::lookup(label.asStringRef().c_str());
            }
            else if (label.isInteger())
            {
                S8 type = (U8) label.asInteger();
                mType   = static_cast<LLAssetType::EType>(type);
            }
            continue;
        }

        if (i->first == INV_INVENTORY_TYPE_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isString())
            {
                mInventoryType = LLInventoryType::lookup(label.asStringRef().c_str());
            }
            else if (label.isInteger())
            {
                S8 type        = (U8) label.asInteger();
                mInventoryType = static_cast<LLInventoryType::EType>(type);
            }
            continue;
        }

        if (i->first == INV_FLAGS_LABEL)
        {
            LLSD const &label = i->second;
            if (label.isBinary())
            {
                mFlags = ll_U32_from_sd(label);
            }
            else if (label.isInteger())
            {
                mFlags = label.asInteger();
            }
            continue;
        }

        if (i->first == INV_NAME_LABEL)
        {
            mName = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(mName, ' ');
            LLStringUtil::replaceChar(mName, '|', ' ');
            continue;
        }

        if (i->first == INV_DESC_LABEL)
        {
            mDescription = i->second.asString();
            LLStringUtil::replaceNonstandardASCII(mDescription, ' ');
            continue;
        }

        if (i->first == INV_CREATION_DATE_LABEL)
        {
            mCreationDate = i->second.asInteger();
            continue;
        }
    }

    // Need to convert 1.0 simstate files to a useful inventory type
    // and potentially deal with bad inventory tyes eg, a landmark
    // marked as a texture.
    if((LLInventoryType::IT_NONE == mInventoryType)
       || !inventory_and_asset_types_match(mInventoryType, mType))
    {
        LL_DEBUGS() << "Resetting inventory type for " << mUUID << LL_ENDL;
        mInventoryType = LLInventoryType::defaultForAssetType(mType);
    }

    mPermissions.initMasks(mInventoryType);

    return true;
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
    LLSD sd = LLSD();
    sd[INV_ITEM_ID_LABEL]   = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    S8 type = static_cast<S8>(mPreferredType);
    sd[INV_ASSET_TYPE_LABEL] = type;
    sd[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    return sd;
}

LLSD LLInventoryCategory::asAISCreateCatLLSD() const
{
    LLSD sd                 = LLSD();
    sd[INV_FOLDER_ID_LABEL_WS]  = mUUID;
    sd[INV_PARENT_ID_LABEL] = mParentUUID;
    S8 type                 = static_cast<S8>(mPreferredType);
    sd[INV_ASSET_TYPE_LABEL_WS] = type;
    sd[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        sd[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }

    if (mFavorite)
    {
        sd[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }

    return sd;
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
    std::string_view w;

    w = INV_FOLDER_ID_LABEL_WS;
    if (sd.has(w))
    {
        mUUID = sd[w];
    }
    w = INV_PARENT_ID_LABEL;
    if (sd.has(w))
    {
        mParentUUID = sd[w];
    }
    mThumbnailUUID.setNull();
    w = INV_THUMBNAIL_LABEL;
    if (sd.has(w))
    {
        const LLSD &thumbnail_map = sd[w];
        w = INV_ASSET_ID_LABEL;
        if (thumbnail_map.has(w))
        {
            mThumbnailUUID = thumbnail_map[w];
        }
    }
    else
    {
        w = INV_THUMBNAIL_ID_LABEL;
        if (sd.has(w))
        {
            mThumbnailUUID = sd[w];
        }
    }
    mFavorite = false;
    w = INV_FAVORITE_LABEL;
    if (sd.has(w))
    {
        const LLSD& favorite_map = sd[w];
        w = INV_TOGGLED_LABEL;
        if (favorite_map.has(w))
        {
            mFavorite = favorite_map[w].asBoolean();
        }
    }
    w = INV_ASSET_TYPE_LABEL;
    if (sd.has(w))
    {
        S8 type = (U8)sd[w].asInteger();
        mPreferredType = static_cast<LLFolderType::EType>(type);
    }
    w = INV_ASSET_TYPE_LABEL_WS;
    if (sd.has(w))
    {
        S8 type = (U8)sd[w].asInteger();
        mPreferredType = static_cast<LLFolderType::EType>(type);
    }

    w = INV_NAME_LABEL;
    if (sd.has(w))
    {
        mName = sd[w].asString();
        LLStringUtil::replaceNonstandardASCII(mName, ' ');
        LLStringUtil::replaceChar(mName, '|', ' ');
    }
    return true;
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
    cat_data[INV_FOLDER_ID_LABEL] = mUUID;
    cat_data[INV_PARENT_ID_LABEL] = mParentUUID;
    cat_data[INV_ASSET_TYPE_LABEL] = std::string(LLAssetType::lookup(mType));
    cat_data[INV_PREFERRED_TYPE_LABEL] = LLFolderType::lookup(mPreferredType);
    cat_data[INV_NAME_LABEL] = mName;

    if (mThumbnailUUID.notNull())
    {
        cat_data[INV_THUMBNAIL_LABEL] = LLSD().with(INV_ASSET_ID_LABEL, mThumbnailUUID);
    }
    if (mFavorite)
    {
        cat_data[INV_FAVORITE_LABEL] = LLSD().with(INV_TOGGLED_LABEL, mFavorite);
    }
}

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
    if (label == INV_FOLDER_ID_LABEL)
    {
        setUUID(value.asUUID());
        return true;
    }
    else if (label == INV_PARENT_ID_LABEL)
    {
        setParent(value.asUUID());
        return true;
    }
    else if (label == INV_ASSET_TYPE_LABEL)
    {
        setType(LLAssetType::lookup(value.asString()));
        return true;
    }
    else if (label == INV_PREFERRED_TYPE_LABEL)
    {
        setPreferredType(LLFolderType::lookup(value.asString()));
        return true;
    }
    else if (label == INV_THUMBNAIL_LABEL)
    {
        LLUUID thumbnail_uuid;
        if (value.has(INV_ASSET_ID_LABEL))
        {
            thumbnail_uuid = value[INV_ASSET_ID_LABEL].asUUID();
        }
        setThumbnailUUID(thumbnail_uuid);
        return true;
    }
    if (label == INV_FAVORITE_LABEL)
    {
        bool favorite = false;
        if (value.has(INV_TOGGLED_LABEL))
        {
            favorite = value[INV_TOGGLED_LABEL].asBoolean();
        }
        setFavorite(favorite);
    }
    else if (label == INV_NAME_LABEL)
    {
        mName = value.asString();
        LLStringUtil::replaceNonstandardASCII(mName, ' ');
        LLStringUtil::replaceChar(mName, '|', ' ');
        return true;
    }
    return false;
}

///----------------------------------------------------------------------------
/// Local function definitions for testing purposes
///----------------------------------------------------------------------------

LLSD ll_create_sd_from_inventory_item(LLPointer<LLInventoryItem> item)
{
    LLSD rv;
    if(item.isNull()) return rv;
    if (item->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_item() for item with AT_NONE"
            << LL_ENDL;
        return rv;
    }
    rv[INV_ITEM_ID_LABEL] =  item->getUUID();
    rv[INV_PARENT_ID_LABEL] = item->getParentUUID();
    rv[INV_NAME_LABEL] = item->getName();
    rv[INV_ASSET_TYPE_LABEL] = LLAssetType::lookup(item->getType());
    rv[INV_ASSET_ID_LABEL] = item->getAssetUUID();
    rv[INV_DESC_LABEL] = item->getDescription();
    rv[INV_SALE_INFO_LABEL] = ll_create_sd_from_sale_info(item->getSaleInfo());
    rv[INV_PERMISSIONS_LABEL] =
        ll_create_sd_from_permissions(item->getPermissions());
    rv[INV_INVENTORY_TYPE_LABEL] =
        LLInventoryType::lookup(item->getInventoryType());
    rv[INV_FLAGS_LABEL] = (S32)item->getFlags();
    rv[INV_CREATION_DATE_LABEL] = (S32)item->getCreationDate();
    return rv;
}

LLSD ll_create_sd_from_inventory_category(LLPointer<LLInventoryCategory> cat)
{
    LLSD rv;
    if(cat.isNull()) return rv;
    if (cat->getType() == LLAssetType::AT_NONE)
    {
        LL_WARNS() << "ll_create_sd_from_inventory_category() for cat with AT_NONE"
            << LL_ENDL;
        return rv;
    }
    rv[INV_FOLDER_ID_LABEL] = cat->getUUID();
    rv[INV_PARENT_ID_LABEL] = cat->getParentUUID();
    rv[INV_NAME_LABEL] = cat->getName();
    rv[INV_ASSET_TYPE_LABEL] = LLAssetType::lookup(cat->getType());
    if(LLFolderType::lookupIsProtectedType(cat->getPreferredType()))
    {
        rv[INV_PREFERRED_TYPE_LABEL] =
            LLFolderType::lookup(cat->getPreferredType()).c_str();
    }
    return rv;
}

LLPointer<LLInventoryCategory> ll_create_category_from_sd(const LLSD& sd_cat)
{
    LLPointer<LLInventoryCategory> rv = new LLInventoryCategory;
    rv->setUUID(sd_cat[INV_FOLDER_ID_LABEL].asUUID());
    rv->setParent(sd_cat[INV_PARENT_ID_LABEL].asUUID());
    rv->rename(sd_cat[INV_NAME_LABEL].asString());
    rv->setType(
        LLAssetType::lookup(sd_cat[INV_ASSET_TYPE_LABEL].asString()));
    rv->setPreferredType(
            LLFolderType::lookup(
                sd_cat[INV_PREFERRED_TYPE_LABEL].asString()));
    return rv;
}
