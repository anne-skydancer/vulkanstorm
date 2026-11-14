/**
 * @file llprojectorshadows.h
 * @brief The projector shadow pool (Stochastic shadow method): one depth tile per projector, in two shared atlases
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

#ifndef LL_LLPROJECTORSHADOWS_H
#define LL_LLPROJECTORSHADOWS_H

#include <map>
#include <unordered_map>
#include <vector>

#include "llpointer.h"
#include "llquaternion.h"
#include "v3math.h"
#include "v3dmath.h"
#include "lldrawable.h"
#include "llprojectorshadowpool.h"
#include "llglheaders.h"
#include "llrender.h"

class LLCamera;
class LLCullResult;
class LLGLSLShader;
class LLVOVolume;

// Replaces the two re-rendered shadow slots of the Legacy method for the Stochastic method (RenderShadowMethod 2,
// RenderShadowDetail 2): every projector (spotlight) within the shadows' reach of the camera's position has a tile of its
// own in a shared depth atlas S (the tiles as drawn), copied into the live atlas L, the only texture the lighting programs
// read (spotLightF.glsl, sampleProjectorShadow). The tile's size and its view come from the light's pose, never from the
// camera's orientation; the caster LOD and the avatars' visibility still follow the camera until P3/P4. The rules (sizes, level, placement, order of work) are llprojectorshadowpool.h's; this class is the GL
// half. Spec: reports/projector-pool-2026-11-13/design-final.md; doc/shadows/projector-shadow-pool.md.
//
// Stage P3 (cache and camera-proof): a tile is drawn again only when something that shows in it changed (the scene's change
// hooks, llshadowdirtyfeed.h, tested against the tile's volume through the 8 m grid), when its lamp's pose changed, or while an
// avatar is in its light (the avatars are still drawn into S until P4). A static scene costs no tile render. The casters a tile
// draws are held at full detail (the LOD pin), so a camera that moves changes nothing in a tile. Movers and physical objects are
// not in the fixed layer. The tiles still run under the fixed CPU caps of llprojectorpool (static 0.35 ms, dynamic 0.30 ms).
//
// Interfaces a second cache (the point lights' faces) reuses, all keyed by a tile or face key and never by "the projector":
// llshadowdirtyfeed.h (the hooks and the per-frame boxes), llprojectorpool::ChangeIndex (boxes against volumes),
// llprojectorpool::classifyTile (the verdict of one tile), selectStatic/selectDynamic (the order of work) and PinBook (the LOD
// pin's books). What is projector-specific stays in this class: the light set, the pose, the lookup matrix.
//
// Main render thread only. Every entry point asserts it and returns at once off it, or while the viewer exits; nothing
// here is reached from LLRender::shutdown, LLImageGL or LLVertexBuffer::cleanupClass, which worker threads with shared
// contexts run (the sVBCache lesson, #403). The destructor makes no GL call: release() runs with the context alive
// (LLPipeline::cleanup, destroyGL, a method or detail change).
class LLProjectorShadows
{
public:
    LLProjectorShadows();
    ~LLProjectorShadows();

    // The pool runs for the Stochastic method with shadow detail 2 (sun and projectors); the same test the shader manager
    // makes for the PROJECTOR_SHADOW_POOL permutation of the spot light programs.
    static bool wanted();

    // The atlases and their framebuffers exist.
    bool active() const { return mAtlasS != 0 && mAtlasL != 0; }

    // Idempotent: makes the atlases when they are wanted and not there, at the size the video memory allows, one tier
    // smaller if an allocation fails. Never changes the method: if the smallest tier fails, the lights have no tile (their
    // projector is lit without a shadow), the failure is logged and the allocation is tried again later (update()).
    // Returns active().
    bool ensureAllocated();

    // Frees the atlases, their framebuffers, every tile and every record (they hold drawables).
    // Context alive, main thread.
    void release();

    // Drops every record (and the tiles they hold). Does not touch the atlases.
    void clear();

    // Once a frame, from LLPipeline::generateSunShadow's Stochastic branch (not for probe renders): the light set, the
    // sizes and the placement, then the tile renders the budget allows, nearest first. The viewer's render state (the
    // framebuffer, viewport, matrices, camera id) is put back.
    void update(LLCamera& camera);

    // From LLPipeline::setupSpotLight for each drawn projector: this light's lookup uniforms. A light without a drawn tile
    // gets proj_shadow_idx -1 (no lookup).
    void setupLight(LLGLSLShader& shader, LLDrawable* light);

    // From LLPipeline::bindShadowMaps: the live atlas on the program's sampler (if it has one).
    void bindAtlas(LLGLSLShader& shader);

    // From LLPipeline::unlinkDrawable (a drawable is going away): its record and tile are dropped.
    void forget(const LLDrawable* drawable);

    // From LLPipeline::renderShadowImpl, after the tile's cull and sort (LLPipeline::LOCAL_SHADOW_STATIC): the LOD pin. Every volume the
    // tile is about to draw, inside the light's volume, is held at full detail (a counter on the volume, one per tile).
    void pinCasters(LLCullResult& result);

    // The longest a changed tile waited for its render (the time from the frame that applied the change to the end of its render, plus
    // the frame before it: a change reaches the pool at most one frame ahead), since the last reset. Development stats only.
    void resetAgeStats() { mDirtyAgeMsMax = 0.f; }

    // Counters for the development stats API and the log.
    struct Stats
    {
        bool present = false;
        S32 level = 0;
        U32 atlasSize = 0;
        U32 lights = 0;            // records: the lights in range
        U32 lightsWithoutTile = 0; // of them, with no drawn tile (the O-4 gate: 0 after the first fill)
        U64 tileRenders = 0;       // tile renders since the atlases were made
        U64 repacks = 0;
        F32 costEmaMs = 0.f;       // CPU ms of one tile render (running average)
        F32 lastFrameRenderMs = 0.f;
        U32 lastFrameRenders = 0;
        U32 maxStaleFrames = 0;    // the oldest placed, drawn tile: frames since it was drawn
        // P3
        U64 staticRenders = 0;     // since the atlases were made: renders of the static schedule (a tile's first fill and every change)
        U64 dynamicRenders = 0;    // ... of the dynamic schedule (a moving lamp, an avatar in the light)
        U32 lastFrameStatic = 0;
        U32 lastFrameDynamic = 0;
        U32 pinnedObjects = 0;     // volumes held at full detail by at least one tile
        U32 pendingTiles = 0;      // drawn tiles a change touched and the scheduler has not drawn again yet
        U32 avatarTiles = 0;       // tiles with an avatar in their light this frame
        U32 dirtyBoxesLastFrame = 0;
        U32 frame = 0;             // the frame the stats were read in
        F32 dirtyAgeMsMax = 0.f;   // the longest a changed tile waited for its render since resetAgeStats
        U64 dirtyRenders = 0;      // renders a change caused (all but the first fills, lamp moves, avatars and resizes)
        llprojectorpool::CauseCounts causes; // why the renders happened (the first cause that made the tile stale)
    };
    Stats getStats() const;

    // per tile (the stats API; development builds add the CRC of its texels in the atlas S, read through atlasS())
    struct TileInfo
    {
        U32 key = 0, x = 0, y = 0, size = 0, drawnFrame = 0;
        F32 camDist = 0.f;
        bool pending = false;      // stale: waiting to be drawn again
    };
    std::vector<TileInfo> getTiles() const;
    U32 atlasS() const { return mAtlasS; }

private:
    // the pose a tile was drawn with: what its lookup matrix is built from. The origin is kept in global coordinates, so a
    // region crossing (the viewer's agent-space shift) does not move it.
    struct DrawnPose
    {
        LLVector3d originGlobal;
        LLQuaternion rot;
        LLVector3 scale;
        F32 fov = 0.f;
        F32 radius = 0.f;
        F32 dist = 0.f; // origin to the near plane
        bool valid = false;
    };

    struct Record
    {
        U32 key = 0;
        LLPointer<LLDrawable> drawable;
        U32 base = 128;            // the light's own tile size (llprojectorpool::baseSize)
        llprojectorpool::Rect rect;
        bool placed = false;
        DrawnPose drawn;
        U32 drawnFrame = 0;        // 0: never drawn, the lookup is off
        F32 camDist = 0.f;         // distance of the lamp's edge to the camera's position
        F32 lastCpuMs = 0.f;       // the cost of its last render (the scheduler's prediction)
        bool seen = false;         // in this frame's light set
        // P3: what the lamp and the scene did since the tile was drawn
        llprojectorpool::Pose drawnPose;   // the pose the tile holds (the comparison of the lamp's changes)
        llprojectorpool::Pose curPose;     // this frame's
        llprojectorpool::Pose prevPose;    // last frame's
        bool prevPoseValid = false;
        bool lampMoving = false;           // the pose changed since last frame (a dynamic item)
        bool poseStale = false;            // the pose differs from the one the tile holds
        llprojectorpool::Pyramid volume;   // the light's volume, agent space, from this frame's pose (camera independent)
        bool pending = false;              // a dirty box touched it since it was drawn
        llprojectorpool::Cause pendingCause = llprojectorpool::Cause::Other;
        U32 dirtyFrame = 0;                // the frame the first such box was applied
        F64 dirtyTime = 0.0;               // ... and its time (seconds)
        bool lowres = false;               // down-sized by a layout change: drawn again at the new size
        bool holdsAvatar = false;          // an avatar is in its light this frame
        U32 avatarCount = 0;
        bool drawnWithAvatar = false;      // its picture was drawn with an avatar in the light
    };

    bool allocateAtlases(U32 size);
    void freeAtlases();
    void updateLightSet(LLCamera& camera);
    bool layoutChanged();
    bool relayout();
    void applyPlan(const std::vector<llprojectorpool::LayoutRequest>& requests, const llprojectorpool::RepackPlan& plan);
    void copyTile(U32 from_tex, const llprojectorpool::Rect& src, U32 to_tex, const llprojectorpool::Rect& dst);
    void copyAtlas(U32 from_tex, U32 to_tex);
    void blitTile(U32 from_fbo, const llprojectorpool::Rect& src, U32 to_fbo, const llprojectorpool::Rect& dst);
    void renderTile(Record& rec, llprojectorpool::Cause cause, bool dynamic);
    void unpinTile(U32 key);
    void unpinObject(const llprojectorpool::Id128& id);
    void pinDrawable(LLDrawable* drawable);
    void setTileOrigin(F32 x, F32 y);
    void dropRecord(std::map<U32, Record>::iterator it);
    U32 desiredSize(const Record& rec) const;

    U32 mAtlasS = 0;   // the tiles as drawn (static layer; in P2 everything is drawn into it)
    U32 mAtlasL = 0;   // the live atlas: S plus whatever is drawn over it (P4); the only texture the lookups read
    U32 mFboS = 0;
    U32 mFboL = 0;
    U32 mAtlasSize = 0;

    llprojectorpool::BuddyAtlas mBuddy;
    llprojectorpool::LevelController mLevel;
    std::map<U32, Record> mRecords;
    std::unordered_map<const LLDrawable*, U32> mKeyOf;
    U32 mNextKey = 1;
    U32 mFrame = 0;                // the frame count update() last ran in
    F32 mCostEmaMs = 0.5f;         // placeholder until the first tile render is timed (a new record's prediction is capped, coldItemMs)
    F64 mNextAllocTry = 0.0;       // seconds: when a failed allocation is tried again
    bool mAllocFailed = false;
    F64 mAllocBackoff = 5.0;       // seconds between allocation retries

    U64 mTileRenders = 0;
    U64 mRepacks = 0;
    F32 mLastFrameRenderMs = 0.f;
    U32 mLastFrameRenders = 0;
    U32 mLightsWithoutTile = 0;

    // P3
    llprojectorpool::PinBook mPins;            // the LOD pin's books, per tile key and object id
    llprojectorpool::ChangeIndex mIndex;       // this frame's tile volumes, for the dirty boxes
    llprojectorpool::CauseCounts mCauses;
    Record* mRenderingRec = nullptr;           // the tile being drawn (pinCasters reads it)
    llprojectorpool::Pyramid mRenderVolume;    // ... and its volume
    U64 mStaticRenders = 0;
    U64 mDynamicRenders = 0;
    U32 mLastFrameStatic = 0;
    U32 mLastFrameDynamic = 0;
    U32 mPendingTiles = 0;
    U32 mAvatarTiles = 0;
    U32 mDirtyBoxes = 0;
    F32 mDirtyAgeMsMax = 0.f;
    U64 mDirtyRenders = 0;
};

#endif // LL_LLPROJECTORSHADOWS_H
