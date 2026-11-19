/**
 * @file lllocalshadows.h
 * @brief What the local shadow caches (the projector pool and the point lights' face pool) share: the frame's one selection of tile
 *        work over both, the avatars that cast, the LOD pin's helpers, one save and restore of the GL state their tile renders change,
 *        the tile origin of the Stochastic caster rule's programs, and the light-through target of a static render
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

#include <vector>

class LLCamera;
class LLCullResult;
class LLDrawable;
class LLUUID;
class LLVector3;
class LLViewerObject;
class LLVOAvatar;
class LLVOVolume;

// An avatar that casts into the local shadow tiles this frame (LLLocalShadows::collectCasters): its extents (agent space) and whether it
// is the user's own. Valid inside one LLLocalShadows::updateFrame only.
struct LLLocalShadowCaster
{
    LLVOAvatar* avatar = nullptr;
    llprojectorpool::Aabb box;
    bool own = false;
};

class LLLocalShadows
{
public:
    // Once a frame, from LLPipeline::generateSunShadow's Stochastic branch (not for probe renders): each pool prepares its frame (the
    // light set, the sizes and the placement, the changes, the avatar pairing) and lists its work; one selection over both lists under
    // the shared cap (llprojectorpool::selectFrame, the pools' keys packed with their pool ids); then each pick is rendered by its pool,
    // in the selection's order, inside one save and restore of the GL state. With the point pool off (or idle) the projectors' lists, and
    // so their picks, are those of the projector pool alone.
    static void updateFrame(LLCamera& camera);

    // The avatars that cast into a light (as the avatar pool's renderShadow draws them): not dead, not a UI avatar, not too slow, a normal
    // appearance (no jelly doll, RLV silhouette, derendered or too complex avatar), and with RenderAvatarFriendsOnly only a friend, the agent
    // or a control avatar. Nothing here reads the camera. Built once a frame by updateFrame, before its first use (the pairing of avatars
    // with the lamps, then every face's draw list); nothing moves or deletes an avatar during the frame's tile work, and the list is
    // emptied before updateFrame returns, so no pointer outlives the frame.
    static void collectCasters(std::vector<LLLocalShadowCaster>& out);

    // The LOD pin's helpers, shared by the pools: an object's id in the books, a tile letting go of an object (a prim or a tree), and
    // whether a volume casts into a tile at all (a volume whose every face is fully transparent, and none has a glTF material, casts
    // nothing: holding it at full detail would only cost triangles).
    static llprojectorpool::Id128 idOf(const LLUUID& uuid);
    static void lowerPin(LLViewerObject* object);
    static bool volumeCasts(const LLVOVolume* volume);

    // The LOD pin of one tile render (both pools, from LLPipeline::renderShadow after the render's cull and sort): every caster the render
    // is about to draw (its cull's visible and drawable groups) inside `volume` is held at full detail under `key` in `book`, so a camera
    // that moves changes no tile's content. Prims and trees only: not rigged, worn, on an avatar or invisible (volumeCasts), and never
    // `skip` (the prim a point light's faces leave out, null for a projector). The counter on the object is shared by the pools.
    static void pinCasters(LLCullResult& result, llprojectorpool::PinBook& book, U32 key, const llprojectorpool::Pyramid& volume,
                           const LLDrawable* skip);
    // the objects a tile no longer holds (PinBook::endRender, PinBook::releaseTile) are given back, unless the viewer exits (they go with
    // the world)
    static void unpinObjects(const std::vector<llprojectorpool::Id128>& ids);
    // a drawable is going away: the book forgets its object (an object created later with the same id is new and pinned afresh), and the
    // counters the book raised on it are lowered if it lives on (its drawable was rebuilt)
    static void forgetPinned(llprojectorpool::PinBook& book, const LLDrawable* drawable);

    // The avatars a tile or face draws (both pools, P4): the frame's casters whose extents meet `volume`, the nearest to the light's
    // centre `origin` first, at most A_max; and the tile rectangle they cover (C3), their extents grown by the footprint margin through
    // the tile's `clip` matrix, or the whole tile when one is an impostor (its extents are stale). `n`: the tile's size.
    static void tileAvatars(const std::vector<LLLocalShadowCaster>& casters, const llprojectorpool::Pyramid& volume, const float clip[16],
                            const LLVector3& origin, U32 n, std::vector<const LLVOAvatar*>& avatars, llprojectorpool::TexelRect& rect);
};

class LLLightThroughLayer;

// Lamp shadows L3: the light-through layer a static tile render draws its see-through casters into, and the tile's rect in it (the
// same texels as the tile's rect in its depth atlas, and the render's viewport: the caster pass asserts it and scissors to the rect).
// Set by the pool around the render (LLLocalShadowGL::setThroughTarget), read by LLPipeline::renderShadow in the static pass
// (LLPipeline::LOCAL_SHADOW_STATIC); every other pass has none.
struct LLLightThroughTarget
{
    LLLightThroughLayer* layer = nullptr;
    llprojectorpool::Rect rect;
    unsigned key = 0;   // the tile's key (the layer's query of its caster pass)
    bool drawn = false; // the render ran the caster pass into the rect (set by renderShadow)
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
    // blocks, a projector tile's or a point light face's (not of the atlas) or a sun cascade's wrapped world-grid index
    // (LLShadowCascadeFit::alphaDitherOrigin). (0, 0) puts it back.
    static void setTileOrigin(F32 x, F32 y);

    // Lamp shadows L3, the seam every lamp pool uses: around the static render of a tile (or of a point light's face), the pool names its
    // light-through layer and the tile's rect, the render's viewport; LLPipeline::renderShadow then draws the see-through casters of
    // that render into the rect (making the layer the first time one is met) instead of dithering them into the depth, depth-tested
    // against the pool's static depth atlas (LLLightThroughLayer::setAtlas) and with its own face rule (LLLightThroughLayer::DrawScope),
    // whatever the render's own. throughTarget() is null when none is set; endThroughTarget() clears it and returns whether the render
    // ran the caster pass into it. Main render thread only.
    static void setThroughTarget(LLLightThroughLayer* layer, const llprojectorpool::Rect& rect, unsigned key);
    static LLLightThroughTarget* throughTarget();
    static bool endThroughTarget();

    // A bit-exact copy of a w x h texel rectangle between two depth atlases of one size (glCopyImageSubData, GL 4.3 core: the point
    // lights' faces run on GL 4.6 only, LLPointShadows::wanted). Not clipped by the scissor test.
    static void copyTexels(U32 from_tex, S32 from_x, S32 from_y, U32 to_tex, S32 to_x, S32 to_y, S32 w, S32 h);
};

#endif // LL_LLLOCALSHADOWS_H
