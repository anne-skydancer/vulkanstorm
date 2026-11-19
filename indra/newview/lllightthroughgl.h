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
// and the nearest see-through caster's depth (R32F, NEAREST, 1 where none is), and a framebuffer with them as its two colour targets
// and the pool's static depth atlas (the tiles' opaque casters) as its depth attachment, read only: a see-through caster is recorded
// only where it lies in front of the tile's opaque depth, so a pane hidden behind a wall or a floor tints nothing in front of it. Made
// the first time a tile of the pool meets a see-through caster (ensure, from LLPipeline::renderShadow inside the tile's static
// render), freed with the pool's atlases or when no tile has held a caster for a while (freeStorage). Once made it holds every drawn
// tile's rect: the pool clears a tile's rect before each static render and carries it through a repack.
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

    // The pool's atlas edge and its static depth texture (D32F, the tiles' opaque casters: the caster pass's depth test reads it), when
    // its atlases are made. No GL: the layer is made by ensure, at this edge.
    void setAtlas(U32 edge, U32 depth_atlas);
    U32 edge() const { return mEdge; }
    bool allocated() const { return mFbo != 0; }

    // Makes the textures and the framebuffer at the edge, cleared to "no caster"; true when they exist. False when they could not be
    // made (logged once, nothing kept; not tried again until the pool's atlases are made again), on a device below OpenGL 4.4
    // (glClearTexImage) and before setAtlas: the caller then draws the see-through casters into the depth as without the layer.
    bool ensure();

    // Frees the textures and the framebuffer and forgets the atlas (with the pool's atlases, before the depth atlas goes). Context alive,
    // main thread.
    void free();

    // Frees the textures and the framebuffer only: ensure makes them again at the same atlas (the pool's release of an idle layer, once
    // no tile has held a see-through caster for a while). Context alive, main thread.
    void freeStorage();

    // A tile's rect back to "no caster" (glClearTexSubImage: no framebuffer, scissor or colour mask involved). No-op without a layer.
    void clearRect(const llprojectorpool::Rect& rect);

    // The carry through a repack (lllightthrough::planCarry): every copy's source into a transient texture pair, then the clears, then
    // the copies to their new rects. Returns the keys of the tiles whose transient could not be made, or whose move changes the size
    // (planCarry never lists one; refused here too, so no copy runs over a neighbour): their new rect is cleared, and the caller draws
    // them again. The transients of one carry hold every copied tile at once: at most one more layer, for the length of the repack.
    std::vector<U32> carry(const lllightthrough::CarryPlan& plan);

    // The lookup's samplers, when the layer exists: tau and the depth to the pool's own two uniforms (the projector pool's are
    // LLShaderMgr::PROJ_THROUGH_ATLAS and PROJ_THROUGH_DEPTH; the point-light faces name theirs).
    void bind(LLGLSLShader& shader, S32 tau_uniform, S32 depth_uniform) const;

    // No texture unit keeps the layer's textures as its current one (before the layer is written; LLTexUnit's cache stays right).
    void unbindUnits() const;

    // The caster pass's query: a GL_ANY_SAMPLES_PASSED query for the tile `key`, begun by the DrawScope that is given its id and
    // ended with it; 0 when none could be made (the tile keeps its flag). A new one drops the tile's earlier one. The result is read
    // by resolveQueries, never waited for: the keys of the tiles whose pass wrote no sample (their layer rect is "no caster").
    U32 beginQuery(U32 key);
    void cancelQuery(U32 key);
    std::vector<U32> resolveQueries();

    // The caster pass (LLPipeline::renderLightThroughObjects), as a scope over one tile: the layer's framebuffer with both targets drawn,
    // the tau target multiplying (glBlendFunc(GL_ZERO, GL_SRC_COLOR): the product of the casters, in any order) and the depth target
    // keeping the nearest (glBlendEquationi(1, GL_MIN)); the depth test against the pool's static depth (GL_LEQUAL, no depth writes);
    // back faces culled (the side a camera at the lamp would see: a one-sided pane lets light through once, as it shows its alpha once;
    // a double-sided glTF material is drawn from both sides by its own batch, LLRenderPass::pushGLTFBatch), whatever face rule the
    // caller's depth pass uses; colour writes on; the scissor at the tile's rect. The tile's rect must be the caller's viewport (the
    // tile render's: the same texels in the depth atlas and the layer; asserted). The destructor puts back the framebuffer it found,
    // the blend equation (GL_FUNC_ADD) and function (LLRender's, BT_ALPHA), the colour mask the caller names (the shadow pass's) and,
    // through its members, the blend, depth, cull and scissor states LLGLState knew; the scissor box stays at the rect, the box every
    // static tile render sets.
    class DrawScope
    {
    public:
        // query: the id beginQuery returned for this tile (0: none)
        DrawScope(const LLLightThroughLayer& layer, const llprojectorpool::Rect& rect, bool color_writes_after, U32 query = 0);
        ~DrawScope();
        DrawScope(const DrawScope&) = delete;
        DrawScope& operator=(const DrawScope&) = delete;

    private:
        LLGLEnable mBlend;
        LLGLDepthTest mDepth;
        LLGLEnable mCull;
        LLGLEnable mScissor;
        U32 mSavedFbo;
        bool mColorAfter;
        U32 mQuery;
    };

private:
    U32 mEdge = 0;
    U32 mDepthAtlas = 0; // the pool's static depth atlas: the framebuffer's depth attachment (not owned)
    U32 mTau = 0;
    U32 mDepth = 0;
    U32 mFbo = 0;
    lllightthrough::PendingQueries mQueries;
    std::vector<U32> mQueryFree; // query objects not in use (reused; deleted with the layer)
    bool mFailed = false; // ensure() failed at this atlas: not tried again until setAtlas or free
};

#endif // LL_LLLIGHTTHROUGHGL_H
