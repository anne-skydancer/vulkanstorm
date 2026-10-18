// CPU parameter blocks used by the original object editor and wire protocols.
// LGPL-2.1, like the viewer.
#pragma once
#include "llprimitive.h"
#include <vector>

struct VSObjectPhysicsProperties
{
    U8   shape   = 0;
    F32  gravity = 0, friction = 0, density = 0, restitution = 0;
    bool unknown = true, requested = false, pending = false;
};

struct VSObjectCostValues
{
    F32  object = 0, linkset = 0, physics = 0, linksetPhysics = 0;
    bool stale = true, pending = false;
};

class VSObjectEditParameters
{
public:
    LLFlexibleObjectData      flexible;
    LLLightParams             light;
    LLLightImageParams        lightImage;
    LLSculptParams            sculpt;
    LLExtendedMeshParams      mesh;
    LLReflectionProbeParams   reflection;
    VSObjectPhysicsProperties physics;
    VSObjectCostValues        costs;
    LLNetworkData*            get(U16 type);
    const LLNetworkData*      get(U16 type) const;
    bool                      inUse(U16 type) const;
    bool                      setInUse(U16 type, bool);
    bool                      unpack(U16 type, const U8* data, U32 size);
    bool                      unpack(const std::vector<U8>&);

private:
    U16 mUsed = 0;
};
