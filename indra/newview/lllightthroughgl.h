/**
 * @file lllightthroughgl.h
 * @brief Lamp shadows L3: a lamp pool's light-through layer, the GL half of lllightthrough.h (its two textures and framebuffer, the
 *        state of the caster pass, the clears, the carry through a repack and the lookup's samplers)
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

#ifndef LL_LLLIGHTTHROUGHGL_H
#define LL_LLLIGHTTHROUGHGL_H

#include "lllightthrough.h"
#include "llgl.h"
#include "llglstates.h"

#include <vector>

class LLGLSLShader;

// One per lamp pool, at its atlas' edge and with the atlas' texels (a tile's rect is the same in both): the projector pool owns one,
// and so will the point-light faces' pool, which calls the same members from its own allocate, free, release, repack and face-render
// paths. Two textures: tau (RGBA8, LINEAR, the share of the light let through per colour channel, 1 where no see-through caster is)
// and the nearest see-through caster's depth (R16, NEAREST, 1 where none is), and a framebuffer with them as its two colour targets
// (no depth attachment: the casters are drawn whatever opaque caster is in front of them, so a pane running on under its frame keeps
// its tint there and the lookup's product does not brighten the inside of the frame's penumbra). Made the first time a tile of the
// pool meets a see-through caster (ensure, from LLPipeline::renderShadow inside the tile's static render), freed with the pool's
// atlases. Once made it holds every drawn tile's rect: the pool clears a tile's rect before each static render and carries it through
// a repack.
//
// Main render thread only, like the pools: no call comes from LLRender::shutdown, LLImageGL or LLVertexBuffer::cleanupClass, which
// worker threads with shared contexts run (the sVBCache lesson, #403). The destructor makes no GL call: the owner frees the layer with
// its atlases while the context lives.
class LLLightThroughLayer
{
public:
    LLLightThroughLayer() = default;
    ~LLLightThroughLayer();
    LLLightThroughLayer(const LLLightThroughLayer&) = delete;
    LLLightThroughLayer& operator=(const LLLightThroughLayer&) = delete;

    // the pool's atlas edge, when its atlases are made (no GL: the layer is made by ensure, at this edge)
    void setEdge(U32 edge);
    U32 edge() const { return mEdge; }
    bool allocated() const { return mFbo != 0; }

    // Makes the textures and the framebuffer at the edge, cleared to "no caster"; true when they exist. False when they could not be
    // made (logged once, nothing kept; not tried again until the pool's atlases are made again), on a device below OpenGL 4.4
    // (glClearTexImage) and before setEdge: the caller then draws the see-through casters into the depth as without the layer.
    bool ensure();

    // Frees the textures and the framebuffer and forgets the edge. Context alive, main thread.
    void free();

    // A tile's rect back to "no caster" (glClearTexSubImage: no framebuffer, scissor or colour mask involved). No-op without a layer.
    void clearRect(const llprojectorpool::Rect& rect);

    // The carry through a repack (lllightthrough::planCarry): every copy's source into a transient texture pair, then the clears, then
    // the copies to their new rects. Returns the keys of the tiles whose transient could not be made: their new rect is cleared, and the
    // caller draws them again.
    std::vector<U32> carry(const lllightthrough::CarryPlan& plan);

    // The lookup's samplers (proj_through_atlas, proj_through_depth), when the layer exists.
    void bind(LLGLSLShader& shader) const;

    // No texture unit keeps the layer's textures as its current one (before the layer is written; LLTexUnit's cache stays right).
    void unbindUnits() const;

    // The caster pass (LLPipeline::renderLightThroughObjects), as a scope: the layer's framebuffer with both targets drawn, the tau
    // target multiplying (glBlendFunc(GL_ZERO, GL_SRC_COLOR): the product of the casters, in any order) and the depth target keeping the
    // nearest (glBlendEquationi(1, GL_MIN)), no depth test or depth writes, colour writes on. The destructor puts back the framebuffer it
    // found, the blend equation (GL_FUNC_ADD) and function (LLRender's, BT_ALPHA), the colour mask the caller names (the shadow pass's)
    // and, through its members, the blend and depth states LLGLState knew.
    class DrawScope
    {
    public:
        DrawScope(const LLLightThroughLayer& layer, bool color_writes_after);
        ~DrawScope();
        DrawScope(const DrawScope&) = delete;
        DrawScope& operator=(const DrawScope&) = delete;

    private:
        LLGLEnable mBlend;
        LLGLDepthTest mDepth;
        U32 mSavedFbo;
        bool mColorAfter;
    };

private:
    U32 mEdge = 0;
    U32 mTau = 0;
    U32 mDepth = 0;
    U32 mFbo = 0;
    bool mFailed = false; // ensure() failed at this edge: not tried again until setEdge or free
};

#endif // LL_LLLIGHTTHROUGHGL_H
