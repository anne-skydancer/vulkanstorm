// Shared CPU simulator object properties, used by both renderer peers. LGPL-2.1.
#pragma once
#include "llcategory.h"
#include "llpermissions.h"
#include "llsaleinfo.h"
#include "lluuid.h"
#include <optional>
#include <string>

class LLMessageSystem;
struct VSObjectProperties
{
    LLUUID id, creator_id, owner_id, group_id, last_owner_id;
    U64 creation_date = 0;
    U32 base_mask = 0, owner_mask = 0, group_mask = 0, everyone_mask = 0, next_owner_mask = 0;
    U32 request_flags = 0;
    S32 ownership_cost = 0;
    LLSaleInfo sale_info;
    LLCategory category;
    LLAggregatePermissions ag_perms, ag_texture_perms, ag_texture_perms_owner;
    S16 inv_serial = 0;
    LLUUID item_id, folder_id, from_task_id;
    std::string name, desc, touch_name, sit_name;
    uuid_vec_t texture_ids;
    bool complete = false;

    LLPermissions permissions() const;
};

// The shared selection controller supports this many saved texture entries.
constexpr S32 VS_OBJECT_PROPERTIES_MAX_TEXTURES = 32;
std::optional<VSObjectProperties> vs_read_object_properties(LLMessageSystem&, S32 block);
VSObjectProperties vs_read_object_properties_family(LLMessageSystem&);
