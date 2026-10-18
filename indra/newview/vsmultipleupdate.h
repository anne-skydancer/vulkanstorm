// Original MultipleObjectUpdate body, shared by real renderer object sources.
// LGPL-2.1, like the viewer.
#pragma once
#include "message.h"
template<class Object>void vs_pack_multiple_update(LLMessageSystem& message,const Object& object,U8 type)
{
    U8 data[256];
    message.nextBlockFast(_PREHASH_ObjectData);
    message.addU32Fast(_PREHASH_ObjectLocalID,object.localID());
    message.addU8Fast(_PREHASH_Type,type);
    S32 offset=0;
    if(type&UPD_POSITION)
    {
        htolememcpy(&data[offset],&(object.position().mV),MVT_LLVector3,12);
        offset+=12;
    }
    if(type&UPD_ROTATION)
    {
        const LLQuaternion quat=object.rotation();const LLVector3 vec=quat.packToVector3();
        htolememcpy(&data[offset],&(vec.mV),MVT_LLQuaternion,12);
        offset+=12;
    }
    if(type&UPD_SCALE)
    {
        htolememcpy(&data[offset],&(object.scale().mV),MVT_LLVector3,12);
        offset+=12;
    }
    message.addBinaryDataFast(_PREHASH_Data,data,offset);
}
