/**
 * @file llshadowdirtyfeed.h
 * @brief What changed in the scene, as boxes with a cause, for the shadow caches (projector tiles now, point-light faces later)
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

#ifndef LL_LLSHADOWDIRTYFEED_H
#define LL_LLSHADOWDIRTYFEED_H

#include <atomic>
#include <vector>

#include "llprojectorshadowpool.h"
#include "v3math.h"

class LLDrawable;
class LLViewerObject;
class LLVOVolume;
class LLVector4a;

// The scene's change hooks (spec 4.4, P3), kept apart from any one cache: a hook pushes the extents of what changed, with a
// cause, into one double-buffered list; a shadow cache (the projector pool, later the point-light face atlas) takes the frame's
// list once with beginFrame() and tests the boxes against its own volumes (llprojectorpool::ChangeIndex, keyed by its own
// tile or face keys). Nothing here knows about projectors.
//
// The classifier lives here too: an object the simulator reported moving, or a physical one, is not a fixed caster
// (llprojectorpool::castsFixed / hookSkips); the bits are on the viewer object (mPoolFlags).
//
// Threads: every hook runs on the main thread (the render thread is the main thread in this viewer). A hook that runs elsewhere
// changes nothing and flags the next frame "everything is stale", which is safe and costs one redraw of every tile. The
// hooks are no-ops until a cache switches the feed on (setTracking, per owner); the classifier (noteServerUpdate) is the exception and
// always runs, so the sticky bits are right when a cache starts. No GL, no frees, no shared state beyond this feed's own list;
// nothing here is reached from LLRender::shutdown, LLImageGL or LLVertexBuffer::cleanupClass.
class LLShadowDirtyFeed
{
public:
    using Cause = llprojectorpool::Cause;

    // ---- the consumers' side ----
    // The caches that use the feed. Tracking and the lamp roots are kept per owner (P4), so one cache releasing its tiles
    // neither switches the hooks off nor drops the other's pending boxes or lamp roots.
    enum class Owner : U32
    {
        Projectors = 1u << 0, // the projector pool (llprojectorshadows)
        Points = 1u << 1      // the point-light face pool (a later stage)
    };

    // A cache that holds tiles switches the hooks on for itself; off, they cost one relaxed load. The hooks record while any
    // owner tracks. The last owner to switch off drops every pending box (nobody is left to read them); an owner that switches
    // off while another tracks drops only its own lamp roots. Main thread only (off it, nothing changes).
    static void setTracking(Owner owner, bool on);
    static bool tracking() { return sTracking.load(std::memory_order_relaxed) != 0; }
    static bool tracking(Owner owner) { return (sTracking.load(std::memory_order_relaxed) & static_cast<U32>(owner)) != 0; }

    // Once per frame (idempotent within a frame, so two caches may call it): hands out the boxes pushed since the last frame.
    // The reference stays valid until the next frame's call.
    static const std::vector<llprojectorpool::DirtyBox>& beginFrame(U32 frame);
    static bool everything(); // the list just handed out lost boxes or was told "everything": every tile is stale
    static void reset();      // drops every pending and applied box (a cache was released)

    // The cause a nested hook is given instead of the one it was called with (the LOD pin's forceLOD, which goes through
    // markRebuild, is a "LOD up", not an "edit").
    struct CauseScope
    {
        explicit CauseScope(Cause cause);
        ~CauseScope();
        CauseScope(const CauseScope&) = delete;
        CauseScope& operator=(const CauseScope&) = delete;
    private:
        bool mHad;
        Cause mPrev;
    };

    // ---- the hooks ----
    // A drawable's extents changed hands: added to or removed from a partition, moved, or rebuilt. Skips what is not a fixed
    // caster (avatars, attachments, animesh, HUD, flexi, client-spun, render types that cast no lamp shadow) and, while it is
    // active (bridged: the tile's cull drops it), a mover or physical object whose linkset is not a lamp's own. A mover at rest
    // is static and drawn, so it is a fixed caster: its changes push, and so does the edge where it becomes active or static.
    static void noteDrawable(LLDrawable* drawable, Cause cause);
    static void pushExtents(LLDrawable* drawable, Cause cause); // the drawable's extents, no skip test, no flag refresh

    // LLPipeline::markRebuild: only a rebuild that can change the picture counts (llprojectorpool::rebuildDirties: volume, rigged,
    // position alone; not texcoord, colour or the whole REBUILD_GEOMETRY set).
    static void noteRebuild(LLDrawable* drawable, U32 rebuild_flag);

    // LLVOVolume::notifyMeshLoaded: a mesh arrived (it raises REBUILD_GEOMETRY, which markRebuild does not count): the extents are
    // pushed now and again when the rebuild it waits for completes (LLPipeline::updateGeom asks meshLoaded and clears it).
    static void noteMeshLoaded(LLVOVolume* volume);
    static bool meshLoaded(const LLDrawable* drawable);
    static void clearMeshLoaded(const LLDrawable* drawable);

    // LLPipeline::markTextured: a texture, material, bump or glow change of a prim. Only a prim with a blended, alpha-masked or
    // alpha-textured face counts (the silhouette of its shadow follows the texture); an opaque prim's texture changes and
    // animated textures never push. A texture that finishes loading reaches this hook through LLFace::dirtyTexture's markTextured (any
    // component count; its REBUILD_VOLUME for 4-component textures reaches noteRebuild as well). A prim the feed has not seen since
    // tracking started (POOL_ALPHA_KNOWN unset) pushes once, whatever it looks like now.
    static void noteTextured(LLDrawable* drawable);

    // LLPipeline::shiftObjects moved agent space by `offset`: pending boxes move with it.
    static void noteShift(const LLVector3& offset);

    // LLViewerObject::processUpdateMessage: the classifier. Runs whether or not a cache is tracking (the bits are sticky for
    // the session). Pushes the object's extents when it turns into a mover or its physics flag changes.
    static void noteServerUpdate(LLViewerObject* object, bool pos_changed, bool rot_changed, bool velocity, bool omega);

    // The roots of the linksets that hold a lamp the pool draws a tile for (the lamp's own linkset is the fixture: its changes
    // push whatever its flags). Set once a frame by each pool for its own lamps (the feed tests the union); the pointers are only
    // compared. Main thread.
    static void setLampRoots(Owner owner, std::vector<const LLViewerObject*>& roots);
    // How many agent-space shifts the feed has seen (a cache that keeps volumes in agent space rebuilds them when it changes).
    static U32 shifts();
    // Objects the classifier has flagged as movers since the feed started (stats).
    static U32 moverObjects();

    // ---- the classifier's reads ----
    static llprojectorpool::ObjectFlags flagsOf(const LLViewerObject* object);
    // The rule for the fixed layer: does this bridge (an active root and its linkset) cast into a static render? `lights_drawable`
    // is the lamp whose tile is being drawn (its own linkset is the fixture); null outside a tile render.
    static bool bridgeCasts(const LLDrawable* bridge_root, const LLDrawable* lights_drawable);

    // The boxes' space is agent space: the extents of a drawable, or of its bridge when it lives inside one. False for none.
    static bool extentsOf(LLDrawable* drawable, llprojectorpool::Aabb& out);

private:
    static void push(const llprojectorpool::Aabb& box, Cause cause);
    static bool skipped(LLDrawable* drawable, LLViewerObject* object);

    static std::atomic<U32> sTracking; // the owners tracking, a mask of Owner
};

#endif // LL_LLSHADOWDIRTYFEED_H
