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

class LLVOAvatar;

class LLCamera;
class LLCullResult;
class LLGLSLShader;
class LLVOVolume;

// Replaces the two re-rendered shadow slots of the Legacy method for the Stochastic method (RenderShadowMethod 2,
// RenderShadowDetail 2): every projector (spotlight) within the shadows' reach of the camera's position has a tile of its
// own in a shared depth atlas S (the tiles as drawn), copied into the live atlas L, the only texture the lighting programs
// read (spotLightF.glsl, sampleProjectorShadow). The tile's size and its view come from the light alone (its pose, cone and reach;
// the casters are held at full detail by the LOD pin); the camera's position only picks which tiles shrink first when the atlas is
// over its budget (the pressure valve, L2), and a size change is drawn before it is shown (draw-then-swap). Since P4 the avatars are a dynamic layer: S holds the
// fixed layer, L is S plus the avatars in the light, paired by position alone (no visibility, no impostor test). The rules (sizes, level, placement, order of work) are llprojectorshadowpool.h's; this class is the GL half. Spec: reports/projector-pool-2026-11-13/design-final.md; doc/shadows/projector-shadow-pool.md.
//
// Stage P3 (cache and camera-proof): a tile is drawn again only when something that shows in it changed (the scene's change
// hooks, llshadowdirtyfeed.h, tested against the tile's volume by a flat scan of the tile volumes), when its lamp's pose changed, or while an
// avatar is in its light (P4: only the avatar layer of that tile, over a fresh copy of S). A static scene costs no tile render. The
// casters a tile draws are held at full detail (the LOD pin), so a camera that moves changes no tile's content (its position changes a
// tile's size only while the atlas is over its budget: a redraw). Movers and physical objects are not in the fixed layer. The tile work
// of a frame runs under one shared CPU cap (llprojectorpool::kFrameCapMs: the dynamic layer first, the static tiles in the rest).
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
        U32 lights = 0;            // the lamps in range
        U32 cells = 0;             // their tiles (records): one per lamp, or the cells of a wide lamp
        U32 lightsWithoutTile = 0; // of the tiles, with no drawn tile (the O-4 gate: 0 after the first fill)
        U64 resizes = 0;           // tile size changes since the atlases were made (drawn at the new size into a new rect, then swapped in)
        U64 resizeRepacks = 0;     // compactions (the name is the stats API's): an arriving tile, or a growth after its hold, found no block although the free texels held it (a whole repack at the tiles' sizes)
        U64 resizeDeferred = 0;    // tile-frames a growth waited: no block of its new size, even with its own rect freed (counted once a frame per waiting tile)
        U64 tileRenders = 0;       // tile renders since the atlases were made
        U64 repacks = 0;
        F32 costEmaMs = 0.f;       // CPU ms of one tile render (running average)
        F32 lastFrameRenderMs = 0.f;
        U32 lastFrameRenders = 0;
        U32 maxStaleFrames = 0;    // the oldest PENDING tile: frames since the first change that touched it (0 when none waits)
        // P3
        U64 staticRenders = 0;     // since the atlases were made: renders of the static schedule (a tile's first fill and every change)
        U64 dynamicRenders = 0;    // ... of the dynamic schedule (a moving lamp redrawn whole)
        U32 lastFrameStatic = 0;
        U32 lastFrameDynamic = 0;
        U64 layerRenders = 0;      // P4: avatar layers alone (S -> L for the tile, then the avatars over it), since the atlases were made
        U32 lastFrameLayer = 0;
        U32 lastFrameAvatarDraws = 0; // avatar draws of the last frame (the A_max share)
        U32 pinnedObjects = 0;     // prims and trees held at full detail by at least one tile (the LOD pin has no ceiling)
        U64 pinnedTriangles = 0;   // their triangles at the held detail (read when the stats are asked for: the cost of the pin)
        U32 moverObjects = 0;      // objects the classifier flagged as movers since the feed started (they are out of the fixed layer while active)
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
        U32 lamp = 0, cell = 0;    // the lamp's id and the cell's index (llprojectorpool::cellIndex; 0 for a lamp of one tile)
        F32 camDist = 0.f;
        bool pending = false;      // stale: waiting to be drawn again
        bool avatar = false;       // an avatar is in its light, or L still holds one over the tile
        bool poseStale = false;    // the lamp's pose differs from the pose the tile was drawn with
        bool lampMoving = false;   // the lamp's pose changed since the last frame
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

    // One tile: a lamp of one tile has one record, a wide lamp one per cell (key = lamp << 4 | cellIndex); the lamp's fields repeat in each.
    struct Record
    {
        U32 key = 0;
        U32 lamp = 0;              // the lamp's id (mKeyOf)
        U32 cell = 0;              // llprojectorpool::cellIndex(ci, cj)
        int ci = 0, cj = 0;        // the cell's column and row on the lamp's image plane
        llprojectorpool::CellGrid grid; // the lamp's cells (1 x 1: one tile, no guard)
        LLPointer<LLDrawable> drawable;
        U32 base = 128;            // this tile's size from its lamp alone (llprojectorpool::tileBaseSize: cone, aspect, reach and the cell's guard)
        int band = llprojectorpool::kMaxBand; // the valve's distance band (nextBand): which tiles shrink first when the atlas is over its budget
        U32 want = 0;              // this frame's size: base, shifted down by the valve's pressure for the band (a render that finds it differs resizes)
        F32 cellDist = 0.f;        // the camera position's distance to the tile's own volume (m): the valve's band, and a cell's order of service
        llprojectorpool::Pyramid core;   // the cell's interior volume, agent space (sizing, the own avatar's cadence); the lamp's for one tile
        U32 volumeNc = 0;                // the tile size `volume` (with its guard) was built for
        llprojectorpool::Rect rect;
        bool placed = false;
        DrawnPose drawn;
        U32 drawnFrame = 0;        // 0: never drawn, the lookup is off
        F32 camDist = 0.f;         // distance of the lamp's edge to the camera's position
        F32 lastCpuMs = 0.f;       // the cost of its last render (the scheduler's prediction)
        bool seen = false;         // in this frame's light set
        bool off = false;          // S2: the lamp is switched off, black or at radius 0: its tiles are held (not drawn, not resized) ...
        F64 offSince = 0.0;        // ... since this time (seconds), for llprojectorpool::kOffHoldSeconds
        bool retiring = false;     // N3: a cell of the lamp's old grid, serving the lookup until every cell of its new grid is drawn
        // P3: what the lamp and the scene did since the tile was drawn
        llprojectorpool::Pose drawnPose;   // the pose the tile holds (the comparison of the lamp's changes)
        llprojectorpool::Pose curPose;     // this frame's
        llprojectorpool::Pose prevPose;    // last frame's
        bool prevPoseValid = false;
        bool lampMoving = false;           // the pose changed since last frame (a dynamic item)
        bool poseStale = false;            // the pose differs from the one the tile holds
        llprojectorpool::Pyramid volume;   // the light's volume, agent space, from the pose below (camera independent)
        llprojectorpool::Pose volumePose;  // the pose `volume` was built for (rebuilt when the lamp's pose changes or agent space shifts)
        bool volumeValid = false;
        bool pending = false;              // a dirty box touched it since it was drawn
        llprojectorpool::Cause pendingCause = llprojectorpool::Cause::Other;
        llprojectorpool::Cause verdictCause = llprojectorpool::Cause::Other; // this frame's classifyTile cause, for a tile in the work list
        U32 dirtyFrame = 0;                // the frame the first such box was applied
        F64 dirtyTime = 0.0;               // ... and its time (seconds)
        bool holdsAvatar = false;          // an avatar is in its light this frame
        U32 avatarCount = 0;
        bool avatarInLayer = false;        // L holds an avatar of this tile over S (the dynamic layer, P4): redrawn while served, restored from S after
        F32 lastAvatarMs = 0.f;            // the CPU cost of its last avatar layer (the scheduler's prediction, an EMA seeded by the first sample; 0: itemCost floors it)
        F32 avatarDist = 0.f;              // the camera position's distance to the nearest avatar in its light (the face's order of service)
        // C3: L's tile equals S's tile outside layerRect (tile texels) when layerRectValid; else L's tile is unknown and the next layer pass copies it whole
        llprojectorpool::TexelRect layerRect;
        bool layerRectValid = false;
        U32 layerFrame = 0;                // the frame its avatar layer was last drawn or restored (the lag of the dynamic layer)
    };

    bool allocateAtlases(U32 size);
    void freeAtlases();
    void updateLightSet(LLCamera& camera);
    void layout(F32 dt_seconds);
    bool compact();
    bool resizeDue(const Record& rec) const;
    void applyPlan(const std::vector<llprojectorpool::LayoutRequest>& requests, const llprojectorpool::RepackPlan& plan);
    void copyTile(U32 from_tex, const llprojectorpool::Rect& src, U32 to_tex, const llprojectorpool::Rect& dst);
    // S -> L over a rectangle of the tile (tile texels), the same texels of both atlases
    void copyTileTexels(const llprojectorpool::Rect& tile, const llprojectorpool::TexelRect& texels);
    void copyAtlas(U32 from_tex, U32 to_tex);
    void renderTile(Record& rec, llprojectorpool::Cause cause, bool dynamic);
    // The avatars that cast this frame (collectCasters, once per update): the pairing with the lamps and every face's draw list read it.
    // The pointers are valid only inside update(), which empties the list before it returns.
    struct Caster
    {
        LLVOAvatar* avatar = nullptr;
        llprojectorpool::Aabb box;
        bool own = false; // the user's own avatar (gAgentAvatarp): its faces are drawn every frame
    };
    void collectCasters();
    // The avatars a face draws, and the tile rectangle they cover (C3: the copy and the draw are limited to it).
    struct FaceAvatars
    {
        std::vector<const LLVOAvatar*> avatars;
        llprojectorpool::TexelRect rect;
    };
    FaceAvatars faceAvatars(const Record& rec, const glm::mat4& view, const glm::mat4& proj, const LLVector3& origin) const;
    // The dynamic layer (P4): L's tile is S's tile again, and the avatars in the light are drawn over it (LOCAL_SHADOW_DYNAMIC: the avatar
    // partitions and the worn objects only, from the pose the tile holds), scissored to face.rect. Returns the CPU ms of the avatar draws.
    F32 drawAvatars(Record& rec, const FaceAvatars& face, const glm::mat4& view, const glm::mat4& proj, const LLVector3& origin, F32 far_clip);
    void renderLayer(Record& rec);
    void unpinTile(U32 key);
    void forgetPinned(const LLDrawable* drawable);
    void unpinObject(const llprojectorpool::Id128& id);
    void pinDrawable(LLDrawable* drawable);
    void dropRecord(std::map<U32, Record>::iterator it);
    void dropLamp(U32 lamp, const LLDrawable* drawable);
    void retireLamp(U32 lamp, LLDrawable* drawable);
    bool lampDrawn(U32 lamp) const;
    void holdOff(U32 lamp, LLDrawable* drawable, const LLVector3& cam_pos, F32 reach, F64 now);

    U32 mAtlasS = 0;   // the tiles as drawn: the fixed layer only (the avatars are not drawn into it since P4)
    U32 mAtlasL = 0;   // the live atlas: S plus the avatars drawn over it (P4); the only texture the lookups read
    U32 mFboS = 0;
    U32 mFboL = 0;
    U32 mAtlasSize = 0;

    llprojectorpool::BuddyAtlas mBuddy;
    llprojectorpool::LevelController mLevel;  // the pressure valve (L2)
    llprojectorpool::GrowthHold mGrowthHold;  // the hold before a compaction for a growth that fragmentation blocks
    std::vector<llprojectorpool::ValveTile> mValveTiles; // layout()'s scratch, filled and read within one call (the capacity is kept)
    std::vector<Record*> mValveRecs;                     // ... the record of each valve tile; cleared before layout() returns
    std::map<U32, Record> mRecords;
    std::unordered_map<const LLDrawable*, U32> mKeyOf;
    std::unordered_map<const LLDrawable*, U32> mRetiringOf; // N3: a lamp's old id whose cells serve until its new grid is drawn
    U32 mNextKey = 1;
    U32 mFrame = 0;                // the frame count update() last ran in
    U32 mShiftsSeen = 0;           // LLShadowDirtyFeed::shifts() at the last update: agent space moved since, every volume is rebuilt
    F32 mCostEmaMs = 0.5f;         // placeholder until the first tile render is timed (a new record's prediction is capped, coldItemMs)
    F64 mNextAllocTry = 0.0;       // seconds: when a failed allocation is tried again
    bool mAllocFailed = false;
    F64 mAllocBackoff = 5.0;       // seconds between allocation retries

    U64 mTileRenders = 0;
    U64 mRepacks = 0;
    U64 mResizes = 0;
    U64 mResizeRepacks = 0;
    U64 mResizeDeferred = 0;
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
    U64 mLayerRenders = 0;         // avatar layers drawn (P4)
    U32 mLastFrameLayer = 0;
    U32 mLastFrameAvatarDraws = 0;
    U32 mPendingTiles = 0;
    U32 mAvatarTiles = 0;
    U32 mDirtyBoxes = 0;
    F32 mDirtyAgeMsMax = 0.f;
    U64 mDirtyRenders = 0;
    std::vector<Caster> mCasters;  // this frame's casting avatars (update() only; empty between frames, the capacity is kept)
};

#endif // LL_LLPROJECTORSHADOWS_H
