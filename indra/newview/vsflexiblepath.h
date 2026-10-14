// Shared CPU flexible-object solver, used by both peer renderers. LGPL-2.1.
#pragma once
#include "llprimitive.h"
#include "llvolume.h"
#include <functional>
struct VSFlexibleSection
{
    LLVector2    mScale;
    LLQuaternion mAxisRotation;
    LLVector3    mPosition, mVelocity, mDirection;
    LLQuaternion mRotation;
    LLVector3    mdPosition;
};
struct VSFlexibleResult
{
    bool topologyChanged = false, positionsChanged = false;
};
using VSFlexibleWind = std::function<LLVector3(const LLVector3&)>;
void             vs_flexible_remap(VSFlexibleSection* source, S32 sourceResolution, VSFlexibleSection* dest, S32 destResolution,
                                   const LLVector3& scale);
void             vs_flexible_parameters(VSFlexibleSection*, S32& initializedResolution, S32 simulationResolution, const LLPathParams&,
                                        const LLVector3& scale, const LLVector3& position, const LLQuaternion&);
VSFlexibleResult vs_flexible_step(VSFlexibleSection*, S32 simulationResolution, S32 renderResolution, LLVolume&,
                                  const LLFlexibleObjectData&, const LLVector3& position, const LLQuaternion&, const LLVector3& scale,
                                  F32 seconds, F32 distance, const VSFlexibleWind&, bool previouslyUpdated,
                                  LLQuaternion& lastSegmentRotation);
