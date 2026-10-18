#include "llviewerprecompiledheaders.h"
#include "vsattentionpolicy.h"
#include "llhudeffectpointat.h"
#include "message.h"
#include <cstring>
VSAttentionPolicy vs_point_attention_policy(U8 type)
{
    // The point policy has no gender or XML overrides in the reference viewer.
    constexpr F32 maximum=F32_MAX/4.f;
    static constexpr F32 timeouts[]{maximum,maximum,maximum,0};
    static constexpr F32 priorities[]{0,1,2,3};
    if(type>=POINTAT_NUM_TARGETS)return {};
    return {timeouts[type],priorities[type],{}, {}};
}
std::array<U8,57> vs_attention_wire(const LLUUID& source,const LLUUID& target,const LLVector3d& offset,U8 subtype)
{
    std::array<U8,57> result{};
    htolememcpy(result.data(),source.mData,MVT_LLUUID,16);
    htolememcpy(result.data()+16,target.mData,MVT_LLUUID,16);
    htolememcpy(result.data()+32,offset.mdV,MVT_LLVector3d,24);
    htolememcpy(result.data()+56,&subtype,MVT_U8,1);
    return result;
}
