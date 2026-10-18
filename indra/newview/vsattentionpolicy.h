// Shared attention decisions and byte layout, transposed from LLHUDEffect.
// LGPL-2.1, like the viewer.
#pragma once
#include "stdtypes.h"
#include "lluuid.h"
#include "v3math.h"
#include "v3dmath.h"
#include "v3color.h"
#include <array>
#include <string_view>
struct VSAttentionPolicy {F32 timeout=0,priority=0;std::string_view name;LLColor3 color;};
VSAttentionPolicy vs_look_attention_policy(bool male,U8 type);
VSAttentionPolicy vs_point_attention_policy(U8 type);
inline bool vs_attention_update_due(const LLVector3& position,const LLVector3& last,F32 time,F32 lastSend)
{return dist_vec_squared(position,last)>.05f*.05f && time-lastSend>1.f/4.f;}
inline bool vs_attention_limited(const VSAttentionPolicy& previous,bool enabled)
{
    return enabled && previous.name!="None" && previous.name!="Idle" && previous.name!="Respond" &&
        previous.name!="Conversation" && previous.name!="FreeLook" && previous.name!="AutoListen";
}
std::array<U8,57> vs_attention_wire(const LLUUID& source,const LLUUID& target,const LLVector3d& offset,U8 subtype);

