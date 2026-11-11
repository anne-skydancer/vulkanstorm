/**
 * @file llcloudshadowmap.h
 * @brief The cloud transmittance map: cloud shadows on the ground from the flat cloud layer
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#ifndef LL_LLCLOUDSHADOWMAP_H
#define LL_LLCLOUDSHADOWMAP_H

#include "llrendertarget.h"
#include "llcloudshadow.h"

class LLGLSLShader;

// Stage L2 of the light study (doc/light/light-model-study.md section 3). Once a frame the flat
// layer's opacity (cloudsF.glsl's alpha1) is evaluated for a 512x512 patch of the world, anchored
// in the world and snapped (llcloudshadow.h), into an RG8 map of the light the layer lets through
// (stage L2b, llcloudoptics.h): R the direct beam, G the diffuse light under the cloud. Every shader
// that lights with the sun looks R up in calcAtmosphericVarsLinear (atmosphericsFuncs.glsl) and
// multiplies the linear sunlit by it, the moon's light as much as the sun's; probes read the map's
// mean. G waits for the overcast veil (stage L4). RenderCloudShadowStrength 0 turns all of it off: no pass, no tap, and the sky is drawn as it
// was.
class LLCloudShadowMap
{
public:
    // Build the map for this frame if there are clouds and the strength is above 0. Cheap to call
    // more than once a frame: it builds once.
    void update();

    // Bind the map, its matrix for the current view and its parameters to `shader` (a no-op for
    // shaders without the cloud_shadow_map sampler). Safe when the map is off: the shader then
    // sees a strength of 0.
    void bind(LLGLSLShader& shader);

    void cleanup();

    bool isActive() const { return mActive; }

private:
    LLRenderTarget mTarget;
    bool mAllocated = false;
    bool mActive = false;
    U64 mBuiltFrame = U64(-1);
    LLCloudShadow::Layout mLayout;
    F32 mLod = 0.f;
    F32 mStrength = 0.f;
    // the map's origin (south-west corner) in agent coordinates (east, north), metres
    double mOriginAgent[2] = { 0.0, 0.0 };
};

#endif // LL_LLCLOUDSHADOWMAP_H
