// Read-only texture-console projection of each backend's real fetch/cache owners.
// LGPL-2.1.
#pragma once
#include "lluuid.h"
#include <limits>
struct VSTextureDebugInfo
{
    LLUUID id;
    F32 area=0,progress=0,fetchDelta=F32_MAX,requestDelta=F32_MAX;
    F32 lastPacket=F32_MAX,lastBound=F32_MAX;
    S32 desired=-1,requested=-1,discard=-1,width=0,height=0,minDiscard=0,fetchState=0;
    U32 fetchPriority=0;
    S32 residentBytes=0;
    U64 snapshotBytes=0,auxiliaryBytes=0;
    bool needsCreate=false,fullyLoaded=false,missing=false,fetching=false,dontDiscard=false,missed=false,error=false;
};
