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
// hooks are no-ops until a cache switches the feed on (setTracking). No GL, no frees, no shared state beyond this feed's
// own list; nothing here is reached from LLRender::shutdown, LLImageGL or LLVertexBuffer::cleanupClass.
class LLShadowDirtyFeed
{
public:
    using Cause = llprojectorpool::Cause;

    // ---- the consumers' side ----
    // A cache that holds tiles switches the hooks on; off, they cost one relaxed load.
    static void setTracking(bool on);
    static bool tracking() { return sTracking.load(std::memory_order_relaxed); }

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
    // caster (avatars, attachments, animesh, HUD, flexi, client-spun, movers, physical objects, render types that cast no
    // lamp shadow).
    static void noteDrawable(LLDrawable* drawable, Cause cause);

    // LLPipeline::markRebuild: only a rebuild that can change the picture (volume, position, rigged) counts.
    static void noteRebuild(LLDrawable* drawable, U32 rebuild_flag);

    // LLPipeline::shiftObjects moved agent space by `offset`: pending boxes move with it.
    static void noteShift(const LLVector3& offset);

    // LLViewerObject::processUpdateMessage: the classifier. Runs whether or not a cache is tracking (the bits are sticky for
    // the session). Pushes the object's extents when it turns into a mover or its physics flag changes.
    static void noteServerUpdate(LLViewerObject* object, bool pos_changed, bool rot_changed, bool velocity, bool omega);

    // ---- the classifier's reads ----
    static llprojectorpool::ObjectFlags flagsOf(const LLViewerObject* object);
    // The rule for the fixed layer: does this bridge (an active root and its linkset) cast into a static render? `lights_root`
    // is the lamp whose tile is being drawn (its own linkset is the fixture); null outside a tile render.
    static bool bridgeCasts(const LLDrawable* bridge_root, const LLDrawable* lights_drawable);

    // The boxes' space is agent space: the extents of a drawable, or of its bridge when it lives inside one. False for none.
    static bool extentsOf(LLDrawable* drawable, llprojectorpool::Aabb& out);

private:
    static void push(const llprojectorpool::Aabb& box, Cause cause);
    static bool skipped(LLDrawable* drawable, LLViewerObject* object);

    static std::atomic<bool> sTracking;
};

#endif // LL_LLSHADOWDIRTYFEED_H
