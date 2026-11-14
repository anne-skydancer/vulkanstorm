/**
 * @file llshadowdirtyfeed.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llshadowdirtyfeed.h"

#include <algorithm>

#include "lldrawable.h"
#include "llspatialpartition.h"
#include "llthread.h"
#include "llviewerobject.h"
#include "llvovolume.h"
#include "pipeline.h"

using namespace llprojectorpool;

namespace
{
    // All of this is main-thread state; the one thing another thread may touch is sOffThread (an atomic flag).
    DirtyList sList;
    const std::vector<DirtyBox>* sApplied = nullptr;
    U32 sAppliedFrame = 0;
    bool sHaveFrame = false;
    std::atomic<bool> sOffThread{ false };
    bool sForced = false;
    Cause sForcedCause = Cause::Other;
    std::vector<const LLViewerObject*> sLampRoots; // the roots of the lamps the pool holds tiles for (compared, never dereferenced)
    std::atomic<U32> sMoverObjects{ 0 };           // objects flagged as movers since the feed started (the bit is sticky)

    Aabb to_aabb(const LLVector4a* ext)
    {
        Aabb b;
        for (int i = 0; i < 3; ++i)
        {
            b.lo[i] = ext[0].getF32ptr()[i];
            b.hi[i] = ext[1].getF32ptr()[i];
        }
        return b;
    }

    // the drawable types whose changes dirty a lamp's tile: prims and trees (the shadow render-type mask also lists avatars, the
    // dynamic layer, and water, which casts nothing). Grass and terrain are left out on purpose: their geometry follows the
    // camera's distance (the grass blade count in LLVOGrass::updateGeometry, the terrain patch's render stride in
    // LLSurfacePatch::updateVisibility), so counting them would redraw tiles as the camera moves. A tile keeps the grass and
    // the terrain it was drawn with (a known limit: a terraform or a grass patch edit shows in a tile when something else
    // redraws it; doc/shadows/projector-shadow-pool.md).
    bool casting_type(LLDrawable* drawable)
    {
        const S32 type = drawable->getRenderType();
        return type == LLPipeline::RENDER_TYPE_VOLUME || type == LLPipeline::RENDER_TYPE_TREE;
    }
}

std::atomic<bool> LLShadowDirtyFeed::sTracking{ false };

LLShadowDirtyFeed::CauseScope::CauseScope(Cause cause) : mHad(sForced), mPrev(sForcedCause)
{
    sForced = true;
    sForcedCause = cause;
}

LLShadowDirtyFeed::CauseScope::~CauseScope()
{
    sForced = mHad;
    sForcedCause = mPrev;
}

// static
void LLShadowDirtyFeed::setTracking(bool on)
{
    sTracking.store(on, std::memory_order_relaxed);
    if (!on && on_main_thread())
    {
        reset();
    }
}

// static
const std::vector<DirtyBox>& LLShadowDirtyFeed::beginFrame(U32 frame)
{
    static const std::vector<DirtyBox> none;
    if (!on_main_thread())
    {
        return none;
    }
    if (!sHaveFrame || sAppliedFrame != frame)
    {
        if (sOffThread.exchange(false))
        {
            sList.pushEverything(); // a hook ran off the main thread: whatever it changed is unknown
        }
        sApplied = &sList.swapForApply();
        sAppliedFrame = frame;
        sHaveFrame = true;
    }
    return sApplied ? *sApplied : none;
}

// static
bool LLShadowDirtyFeed::everything()
{
    return sHaveFrame && sList.everything();
}

// static
void LLShadowDirtyFeed::reset()
{
    sList.clear();
    sApplied = nullptr;
    sHaveFrame = false;
    sOffThread.store(false);
    sLampRoots.clear();
}

// static
void LLShadowDirtyFeed::push(const Aabb& box, Cause cause)
{
    sList.push(box, sForced ? sForcedCause : cause);
}

// static
ObjectFlags LLShadowDirtyFeed::flagsOf(const LLViewerObject* object)
{
    ObjectFlags f;
    if (object)
    {
        f.physical = object->flagUsePhysics();
        f.mover = (object->mPoolFlags & LLViewerObject::POOL_MOVER) != 0;
    }
    return f;
}

// static
bool LLShadowDirtyFeed::extentsOf(LLDrawable* drawable, Aabb& out)
{
    if (!drawable)
    {
        return false;
    }
    // inside a bridge (an active linkset) the drawable's own extents are in the bridge's space: the bridge's are agent space
    if (!drawable->isSpatialBridge())
    {
        LLSpatialGroup* group = drawable->getSpatialGroup();
        LLSpatialPartition* part = group ? group->getSpatialPartition() : nullptr;
        LLSpatialBridge* bridge = part ? part->asBridge() : nullptr;
        if (bridge)
        {
            drawable = bridge;
        }
    }
    if (!drawable->getEntry())
    {
        return false; // never put into an octree: no extents
    }
    const LLVector4a* ext = drawable->getSpatialExtents();
    if (!ext)
    {
        return false;
    }
    out = to_aabb(ext);
    // a drawable whose extents were never computed has the empty box at the origin: it says nothing about where it is
    bool empty = true;
    for (int i = 0; i < 3; ++i)
    {
        empty = empty && out.lo[i] == 0.f && out.hi[i] == 0.f;
    }
    return !empty;
}

// static
bool LLShadowDirtyFeed::skipped(LLDrawable* drawable, LLViewerObject* object)
{
    if (!object || !casting_type(drawable))
    {
        return true;
    }
    // the dynamic layer, worn things, HUD, animated objects: not fixed casters
    if (object->isAvatar() || object->isAttachment() || object->isHUDAttachment() || object->isAnimatedObject() || object->getAvatar())
    {
        return true;
    }
    // flexible prims and client-spun parts change every frame without a message: they cast with the pose of the last render
    if (object->isFlexible() || !object->getAngularVelocity().isExactlyZero())
    {
        return true;
    }
    LLViewerObject* root = object->getRootEdit();
    if (!root)
    {
        root = object;
    }
    const ObjectFlags of = flagsOf(object), rf = flagsOf(root);
    // The same rule as the tile's cull (bridgeCasts): a mover or physical linkset is out of the fixed layer only while it is
    // active (bridged); at rest it is drawn like any fixed caster, and its changes push.
    const bool active = drawable->isActive();
    const bool lamp = std::binary_search(sLampRoots.begin(), sLampRoots.end(), static_cast<const LLViewerObject*>(root));
    if (of.physical || of.mover || rf.physical || rf.mover)
    {
        // The object just became active (the tile that holds it at its old pose must drop it: push the extents it leaves) or
        // static again (it is drawn from now on: push where it is). One push at each edge, whatever order the hooks run in.
        const bool was_active = (object->mPoolFlags & LLViewerObject::POOL_ACTIVE) != 0;
        if (was_active != active)
        {
            if (active)
            {
                object->mPoolFlags |= LLViewerObject::POOL_ACTIVE;
            }
            else
            {
                object->mPoolFlags &= ~LLViewerObject::POOL_ACTIVE;
            }
            return false;
        }
    }
    return hookSkips(of, rf, active, lamp);
}

// static
void LLShadowDirtyFeed::setLampRoots(std::vector<const LLViewerObject*>& roots)
{
    if (!on_main_thread())
    {
        return;
    }
    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    sLampRoots.swap(roots);
}

// static
U32 LLShadowDirtyFeed::moverObjects()
{
    return sMoverObjects.load(std::memory_order_relaxed);
}

// static
void LLShadowDirtyFeed::noteDrawable(LLDrawable* drawable, Cause cause)
{
    if (!tracking() || !drawable)
    {
        return;
    }
    if (!on_main_thread())
    {
        sOffThread.store(true);
        return;
    }
    LLViewerObject* object = drawable->getVObj();
    if (skipped(drawable, object))
    {
        return;
    }
    Aabb box;
    if (extentsOf(drawable, box))
    {
        push(box, cause);
    }
}

// static
void LLShadowDirtyFeed::noteRebuild(LLDrawable* drawable, U32 rebuild_flag)
{
    if (!tracking())
    {
        return;
    }
    const U32 mask = LLDrawable::REBUILD_VOLUME | LLDrawable::REBUILD_POSITION | LLDrawable::REBUILD_RIGGED;
    if (rebuildDirties(rebuild_flag, mask))
    {
        noteDrawable(drawable, Cause::Edit);
    }
}

// static
void LLShadowDirtyFeed::noteShift(const LLVector3& offset)
{
    if (!tracking())
    {
        return;
    }
    if (!on_main_thread())
    {
        sOffThread.store(true);
        return;
    }
    sList.translatePending(offset.mV[VX], offset.mV[VY], offset.mV[VZ]);
}

// static
bool LLShadowDirtyFeed::bridgeCasts(const LLDrawable* bridge_root, const LLDrawable* lights_drawable)
{
    const LLViewerObject* root = bridge_root ? bridge_root->getVObj() : nullptr;
    if (!root)
    {
        return true;
    }
    // only the active linksets of fixed things: an avatar's attachments and animesh are the dynamic layer's
    if (root->isAttachment() || root->isHUDAttachment() || root->isAnimatedObject() || root->isAvatar())
    {
        return true;
    }
    bool lights_own = false;
    if (lights_drawable)
    {
        const LLViewerObject* lamp = lights_drawable->getVObj();
        const LLViewerObject* lamp_root = lamp ? lamp->getRootEdit() : nullptr;
        lights_own = lamp_root && lamp_root == root;
    }
    return castsFixed(flagsOf(root), lights_own);
}

// static
void LLShadowDirtyFeed::noteServerUpdate(LLViewerObject* object, bool pos_changed, bool rot_changed, bool velocity, bool omega)
{
    if (!object || object->isAvatar() || !on_main_thread())
    {
        return;
    }
    const bool created_seen = (object->mPoolFlags & LLViewerObject::POOL_CREATED_SEEN) != 0;
    object->mPoolFlags |= LLViewerObject::POOL_CREATED_SEEN;

    ObjectFlags before;
    before.physical = (object->mPoolFlags & LLViewerObject::POOL_PHYSICAL) != 0;
    before.mover = (object->mPoolFlags & LLViewerObject::POOL_MOVER) != 0;

    ServerUpdate u;
    u.created_seen = created_seen;
    u.selected = object->isSelected();
    u.pos_changed = pos_changed;
    u.rot_changed = rot_changed;
    u.velocity = velocity;
    u.omega = omega;
    u.is_root = object->isRootEdit();

    ObjectFlags after = before;
    after.physical = object->flagUsePhysics();
    after.mover = before.mover || becomesMover(u);
    if (after.physical)
    {
        object->mPoolFlags |= LLViewerObject::POOL_PHYSICAL;
    }
    else
    {
        object->mPoolFlags &= ~LLViewerObject::POOL_PHYSICAL;
    }
    if (after.mover)
    {
        if (!before.mover)
        {
            sMoverObjects.fetch_add(1, std::memory_order_relaxed);
        }
        object->mPoolFlags |= LLViewerObject::POOL_MOVER;
    }

    // the transition: the object leaves (or enters) the fixed layer, so the tiles that hold it are drawn once more. The extents
    // pushed are where it was drawn: this runs before the drawable's own move is applied.
    if (layerTransition(before, after) && tracking())
    {
        LLDrawable* drawable = object->mDrawable.get();
        if (drawable && casting_type(drawable))
        {
            // a root with a bridge is drawn from the bridge: its extents are the linkset's
            LLDrawable* source = drawable->getSpatialBridge() ? static_cast<LLDrawable*>(drawable->getSpatialBridge()) : drawable;
            Aabb box;
            if (extentsOf(source, box))
            {
                push(box, Cause::Mover);
            }
        }
    }
}
