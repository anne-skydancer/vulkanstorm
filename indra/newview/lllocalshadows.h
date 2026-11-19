/**
 * @file lllocalshadows.h
 * @brief What the local shadow caches (the projector pool, later the point-light face pool) share: one save and restore of the GL
 *        state their tile renders change, the tile origin of the Stochastic caster rule's programs, and the light-through target
 *        of a static render
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

#ifndef LL_LLLOCALSHADOWS_H
#define LL_LLLOCALSHADOWS_H

#include "llprojectorshadowpool.h"

class LLLightThroughLayer;

// Lamp shadows L3: the light-through layer a static tile render draws its see-through casters into, and the tile's rect in it (the
// same texels as the tile's rect in its depth atlas). Set by the pool around the render (LLLocalShadowGL::setThroughTarget), read by
// LLPipeline::renderShadow in the static pass (LLPipeline::LOCAL_SHADOW_STATIC); every other pass has none.
struct LLLightThroughTarget
{
    LLLightThroughLayer* layer = nullptr;
    llprojectorpool::Rect rect;
    bool drawn = false; // the render drew a see-through caster into the rect (set by renderShadow)
};

// The tile renders of a frame change the framebuffer, the viewport, the matrices and the camera id, and leave the shadow programs
// bound. A scope saves that state when the first scope of the frame opens and puts it back when the last one closes (a counter,
// llprojectorpool::ScopeCounter): a second cache's update inside an outer scope does not save and restore again, so a frame has
// one save and one restore however many caches render tiles. Main render thread only; every call returns at once off it.
class LLLocalShadowGL
{
public:
    class Scope
    {
    public:
        Scope();
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;
    private:
        bool mActive;
    };

    // The programs of the semi-transparent casters' rule (Stochastic, shadowStochasticCasterF.glsl): the texel origin of its
    // blocks, a projector tile's (not of the atlas) or a sun cascade's wrapped world-grid index
    // (LLShadowCascadeFit::alphaDitherOrigin). (0, 0) puts it back.
    static void setTileOrigin(F32 x, F32 y);

    // Lamp shadows L3, the seam every lamp pool uses: around the static render of a tile (or of a point light's face), the pool names its
    // light-through layer and the tile's rect; LLPipeline::renderShadow then draws the see-through casters of that render into the rect
    // (making the layer the first time one is met) instead of dithering them into the depth. throughTarget() is null when none is set;
    // endThroughTarget() clears it and returns whether the render drew into it. Main render thread only.
    static void setThroughTarget(LLLightThroughLayer* layer, const llprojectorpool::Rect& rect);
    static LLLightThroughTarget* throughTarget();
    static bool endThroughTarget();
};

#endif // LL_LLLOCALSHADOWS_H
