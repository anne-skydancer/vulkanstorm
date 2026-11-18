/**
 * @file lllocalshadows.h
 * @brief What the local shadow caches (the projector pool, later the point-light face pool) share: one save and restore of the GL
 *        state their tile renders change, and the tile origin of the dithering programs
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
    // (LLShadowCascadeFit::alphaDitherOrigin), and the frame of the pattern (0: fixed; the sun cascades of the main view
    // rotate it while the Stochastic shadows' history accumulates, LLPipeline::shadowCasterFrame). (0, 0, 0) puts it back.
    static void setTileOrigin(F32 x, F32 y, S32 frame = 0);
};

#endif // LL_LLLOCALSHADOWS_H
