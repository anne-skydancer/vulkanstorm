// Shared ObjectProperties decoding, transposed from LLSelectMgr. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsobjectproperties.h"
#include "message.h"
#include <array>
#include <cstring>

LLPermissions VSObjectProperties::permissions() const
{
    LLPermissions result;
    result.init(creator_id, owner_id, last_owner_id, group_id);
    result.initMasks(base_mask, owner_mask, everyone_mask, group_mask, next_owner_mask);
    return result;
}

std::optional<VSObjectProperties> vs_read_object_properties(LLMessageSystem& message, S32 block)
{
    VSObjectProperties result;
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, result.id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_CreatorID, result.creator_id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, result.owner_id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, result.group_id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_LastOwnerID, result.last_owner_id, block);
    message.getU64Fast(_PREHASH_ObjectData, _PREHASH_CreationDate, result.creation_date, block);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_BaseMask, result.base_mask, block);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_OwnerMask, result.owner_mask, block);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_GroupMask, result.group_mask, block);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_EveryoneMask, result.everyone_mask, block);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_NextOwnerMask, result.next_owner_mask, block);
    message.getS32Fast(_PREHASH_ObjectData, _PREHASH_OwnershipCost, result.ownership_cost, block);
    result.sale_info.unpackMultiMessage(&message, _PREHASH_ObjectData, block);
    result.ag_perms.unpackMessage(&message, _PREHASH_ObjectData, _PREHASH_AggregatePerms, block);
    result.ag_texture_perms.unpackMessage(&message, _PREHASH_ObjectData, _PREHASH_AggregatePermTextures, block);
    result.ag_texture_perms_owner.unpackMessage(&message, _PREHASH_ObjectData, _PREHASH_AggregatePermTexturesOwner, block);
    result.category.unpackMultiMessage(&message, _PREHASH_ObjectData, block);
    message.getS16Fast(_PREHASH_ObjectData, _PREHASH_InventorySerial, result.inv_serial, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_ItemID, result.item_id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_FolderID, result.folder_id, block);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_FromTaskID, result.from_task_id, block);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_Name, result.name, block);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_Description, result.desc, block);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_TouchName, result.touch_name, block);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_SitName, result.sit_name, block);
    const S32 size = message.getSizeFast(_PREHASH_ObjectData, block, _PREHASH_TextureID);
    if (size < 0 || size > VS_OBJECT_PROPERTIES_MAX_TEXTURES * UUID_BYTES || size % UUID_BYTES)
        return {};
    if (size)
    {
        std::array<U8, VS_OBJECT_PROPERTIES_MAX_TEXTURES * UUID_BYTES> bytes{};
        message.getBinaryDataFast(_PREHASH_ObjectData, _PREHASH_TextureID, bytes.data(), 0, block, S32(bytes.size()));
        result.texture_ids.reserve(size / UUID_BYTES);
        for (S32 offset = 0; offset < size; offset += UUID_BYTES)
        {
            LLUUID id;
            std::memcpy(id.mData, bytes.data() + offset, UUID_BYTES);
            result.texture_ids.push_back(id);
        }
    }
    result.complete = true;
    return result;
}

VSObjectProperties vs_read_object_properties_family(LLMessageSystem& message)
{
    VSObjectProperties result;
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_RequestFlags, result.request_flags);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_ObjectID, result.id);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_OwnerID, result.owner_id);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_GroupID, result.group_id);
    message.getUUIDFast(_PREHASH_ObjectData, _PREHASH_LastOwnerID, result.last_owner_id);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_BaseMask, result.base_mask);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_OwnerMask, result.owner_mask);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_GroupMask, result.group_mask);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_EveryoneMask, result.everyone_mask);
    message.getU32Fast(_PREHASH_ObjectData, _PREHASH_NextOwnerMask, result.next_owner_mask);
    message.getS32Fast(_PREHASH_ObjectData, _PREHASH_OwnershipCost, result.ownership_cost);
    result.sale_info.unpackMessage(&message, _PREHASH_ObjectData);
    result.category.unpackMessage(&message, _PREHASH_ObjectData);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_Name, result.name);
    message.getStringFast(_PREHASH_ObjectData, _PREHASH_Description, result.desc);
    return result;
}
