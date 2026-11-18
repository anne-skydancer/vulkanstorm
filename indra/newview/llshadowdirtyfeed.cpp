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
#include <cstring>

#include "lldrawable.h"
#include "llgltfmaterial.h"
#include "llmaterial.h"
#include "lltextureentry.h"
#include "llspatialpartition.h"
#include "llthread.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewertexture.h"
#include "llvovolume.h"
#include "pipeline.h"

using namespace llprojectorpool;

// the rule's constants are the viewer's own flags (the unit test checks the rule with these values)
static_assert(kRebuildVolume == LLDrawable::REBUILD_VOLUME && kRebuildTcoord == LLDrawable::REBUILD_TCOORD &&
              kRebuildColor == LLDrawable::REBUILD_COLOR && kRebuildPosition == LLDrawable::REBUILD_POSITION &&
              kRebuildRigged == LLDrawable::REBUILD_RIGGED && kRebuildGeometry == LLDrawable::REBUILD_GEOMETRY &&
              kRebuildAll == LLDrawable::REBUILD_ALL, "llprojectorpool's rebuild flags are LLDrawable's");

namespace
{
    // All of this is main-thread state; the one thing another thread may touch is sOffThread (an atomic flag).
    DirtyList sList;
    const std::vector<DirtyBox>* sApplied = nullptr;
    U32 sAppliedFrame = 0;
    bool sHaveFrame = false;
    U32 sShifts = 0; // agent-space shifts seen (main thread)
    std::atomic<bool> sOffThread{ false };
    bool sForced = false;
    Cause sForcedCause = Cause::Other;
    // the roots of the lamps each pool holds tiles for (compared, never dereferenced): one list per owner and their sorted union,
    // which is what the hooks test
    llprojectorpool::OwnerLists<const LLViewerObject*> sLampRoots;
    // the open TextureEntryRebuild (its drawable, compared only; whether a shape rebuild was queued for it before the request; whether the
    // setter's markTextured changed the key) and the drawable whose own rebuild runs (OwnRebuild): main-thread scope markers
    const LLDrawable* sTEDrawable = nullptr;
    bool sTEQueuedShape = false;
    bool sTEKeyChanged = false;
    LLDrawable* sOwnRebuild = nullptr;

    inline U32 owner_index(LLShadowDirtyFeed::Owner owner)
    {
        return owner == LLShadowDirtyFeed::Owner::Projectors ? 0u : 1u;
    }
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

    // a face whose silhouette in a shadow depends on its texture, material or UVs: blended, alpha-masked or carrying an alpha
    // channel. An opaque face's texture never shows in a depth tile, so its changes (and its animated textures) are not counted.
    bool alpha_face(LLVOVolume* volume, U8 face)
    {
        const LLTextureEntry* te = volume->getTE(face);
        if (!te)
        {
            return false;
        }
        if (te->getColor().mV[VALPHA] < 0.999f)
        {
            return true;
        }
        const LLMaterialPtr material = te->getMaterialParams();
        if (material.notNull())
        {
            const U8 mode = material->getDiffuseAlphaMode();
            if (mode == LLMaterial::DIFFUSE_ALPHA_MODE_BLEND || mode == LLMaterial::DIFFUSE_ALPHA_MODE_MASK)
            {
                return true;
            }
            // NONE and EMISSIVE never go to the alpha pool and never mask (LLPipeline::getPoolTypeFromTE: only the face's own
            // alpha counts, tested above), so the shadow pass draws the face opaque whatever its texture carries
            if (mode == LLMaterial::DIFFUSE_ALPHA_MODE_NONE || mode == LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE)
            {
                return false;
            }
        }
        const LLGLTFMaterial* gltf = te->getGLTFRenderMaterial();
        if (gltf && gltf->mAlphaMode != LLGLTFMaterial::ALPHA_MODE_OPAQUE)
        {
            return true;
        }
        const LLViewerTexture* image = volume->getTEImage(face);
        return image && (image->getComponents() == 4 || image->getComponents() == 2); // not yet loaded: unknown, counted when it finishes (LLFace::dirtyTexture)
    }

    // The prim's silhouette key (llprojectorpool::texturedDirties): what its faces cut out of a depth tile. An opaque face counts only as
    // opaque (its texture never shows in a depth tile), and a double-sided glTF one by that flag (the shadow pass draws it from both
    // sides); each blended, alpha-masked or alpha-textured face adds its silhouette inputs (llprojectorpool::SilhouetteFace: its index,
    // alpha, texture and the texture's channel count, mapping, and the legacy or glTF material's alpha mode and cutoff, with the glTF base
    // colour texture and transform). Fullbright, glow, shine, bump, an RGB-only tint, emissive, the PBR colour and the light's own colour
    // are not inputs (lamp shadows S2: blinker scripts toggle them with the light).
    U32 silhouette_key(LLVOVolume* volume)
    {
        static_assert(sizeof(llprojectorpool::SilhouetteFace::texture) == size_t(UUID_BYTES), "a texture id is 16 bytes");
        U32 key = llprojectorpool::kSilhouetteKeySeed;
        const U8 faces = volume->getNumTEs();
        for (U8 i = 0; i < faces; ++i)
        {
            const LLTextureEntry* te = volume->getTE(i);
            if (!te)
            {
                continue;
            }
            const LLGLTFMaterial* render_gltf = te->getGLTFRenderMaterial();
            llprojectorpool::SilhouetteFace f;
            f.index = i;
            f.cutout = alpha_face(volume, i);
            f.double_sided = render_gltf && render_gltf->mDoubleSided;
            if (!f.cutout)
            {
                key = llprojectorpool::silhouetteFaceMix(key, f); // an opaque face: which sides cast, nothing for a single-sided one
                continue;
            }
            f.alpha = te->getColor().mV[VALPHA];
            memcpy(f.texture, te->getID().mData, UUID_BYTES);
            te->getScale(&f.scale[0], &f.scale[1]);
            te->getOffset(&f.offset[0], &f.offset[1]);
            f.rotation = te->getRotation();
            f.texgen = U8(te->getTexGen());
            const LLViewerTexture* image = volume->getTEImage(i);
            f.components = S8(image ? image->getComponents() : -1);
            const LLMaterialPtr material = te->getMaterialParams();
            if (material.notNull())
            {
                f.material = true;
                f.alpha_mode = material->getDiffuseAlphaMode();
                f.alpha_cutoff = material->getAlphaMaskCutoff();
            }
            if (render_gltf)
            {
                const LLGLTFMaterial::TextureTransform& tt = render_gltf->mTextureTransform[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR];
                f.gltf = true;
                f.gltf_alpha_mode = U32(render_gltf->mAlphaMode);
                f.gltf_cutoff = render_gltf->mAlphaCutoff;
                f.gltf_alpha = render_gltf->mBaseColor.mV[VALPHA];
                memcpy(f.gltf_texture, render_gltf->mTextureId[LLGLTFMaterial::GLTF_TEXTURE_INFO_BASE_COLOR].mData, UUID_BYTES);
                f.gltf_offset[0] = tt.mOffset.mV[VX];
                f.gltf_offset[1] = tt.mOffset.mV[VY];
                f.gltf_scale[0] = tt.mScale.mV[VX];
                f.gltf_scale[1] = tt.mScale.mV[VY];
                f.gltf_rotation = tt.mRotation;
            }
            key = llprojectorpool::silhouetteFaceMix(key, f);
        }
        return key;
    }

    // Stores `now` as the object's silhouette key and returns the one it held. `known` says whether the key had been written since
    // tracking last started (POOL_ALPHA_KNOWN; setTracking clears it on every object when tracking switches on): an object that predates
    // tracking has an unknown key, which texturedDirties treats as changed. Main thread only.
    U32 swapAlphaKey(LLVOVolume* volume, U32 now, bool& known)
    {
        known = (volume->mPoolFlags & LLViewerObject::POOL_ALPHA_KNOWN) != 0;
        const U32 was = volume->mPoolAlphaKey;
        volume->mPoolFlags |= LLViewerObject::POOL_ALPHA_KNOWN;
        volume->mPoolAlphaKey = now;
        return was;
    }
}

std::atomic<U32> LLShadowDirtyFeed::sTracking{ 0 };

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
void LLShadowDirtyFeed::setTracking(Owner owner, bool on)
{
    if (!on_main_thread())
    {
        return; // the owners are main-thread caches; nothing changes off it
    }
    const U32 bit = static_cast<U32>(owner);
    const U32 was = sTracking.load(std::memory_order_relaxed);
    const U32 now = llprojectorpool::ownerMaskAfter(was, bit, on);
    sTracking.store(now, std::memory_order_relaxed);
    const llprojectorpool::OwnerEdge edge = llprojectorpool::ownerEdge(was, now);
    if (!on)
    {
        sLampRoots.clear(owner_index(owner));
        if (edge == llprojectorpool::OwnerEdge::Last)
        {
            reset(); // the last owner is gone: nobody reads the pending boxes
        }
    }
    else if (edge == llprojectorpool::OwnerEdge::First)
    {
        // the silhouette keys are only kept while tracking: forget them all (POOL_ALPHA_KNOWN), so the first change of each object
        // after this pushes once (conservatively) and then knows; and so is the texture-entry mark (POOL_TE_REBUILD), which a rebuild
        // queued before the switch never earned. A walk over the object list, once per switch-on of the first owner (a second owner
        // joins keys that were written for the first, which hold for any reader).
        const S32 count = gObjectList.getNumObjects();
        for (S32 i = 0; i < count; ++i)
        {
            if (LLViewerObject* object = gObjectList.getObject(i))
            {
                object->mPoolFlags &= (U8)~(LLViewerObject::POOL_ALPHA_KNOWN | LLViewerObject::POOL_TE_REBUILD);
            }
        }
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
    sLampRoots.clearAll();
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
bool LLShadowDirtyFeed::skipped(LLDrawable* drawable, LLViewerObject* object, bool* edge)
{
    if (edge)
    {
        *edge = false;
    }
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
    const bool lamp = sLampRoots.contains(static_cast<const LLViewerObject*>(root));
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
            if (edge)
            {
                *edge = true;
            }
            return false;
        }
    }
    return hookSkips(of, rf, active, lamp);
}

// static
void LLShadowDirtyFeed::setLampRoots(Owner owner, std::vector<const LLViewerObject*>& roots)
{
    if (!on_main_thread())
    {
        return;
    }
    sLampRoots.set(owner_index(owner), roots); // sorted and de-duplicated; the union is rebuilt only when this owner's list changed
}

// static
U32 LLShadowDirtyFeed::shifts()
{
    return sShifts;
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
    LLVOVolume* volume = drawable->getVOVolume();
    bool edge = false;
    if (skipped(drawable, object, &edge))
    {
        if (volume)
        {
            volume->mPoolFlags &= (U8)~LLViewerObject::POOL_ALPHA_KNOWN; // not kept for objects out of the fixed layer
        }
        return;
    }
    // a push of the drawable's own rebuild (OwnRebuild: its octree move, its completion) keeps the texture-entry mark and follows it; any
    // other push (a move, a put or a remove, a counted rebuild, a mesh arriving) means the queued rebuild is no longer a texture-entry one
    const bool own = drawable == sOwnRebuild && !edge;
    const bool marked = own && object && (object->mPoolFlags & LLViewerObject::POOL_TE_REBUILD) != 0;
    if (object && !own)
    {
        object->mPoolFlags &= (U8)~LLViewerObject::POOL_TE_REBUILD;
    }
    if (volume)
    {
        bool known;
        const U32 now = silhouette_key(volume);
        const U32 was = swapAlphaKey(volume, now, known); // keep the silhouette key current at every rebuild, put and edit
        if (!ownRebuildPushes(marked, was, now, known))
        {
            return; // the rebuild of a tint, an emissive or a material that left the silhouette alone: nothing a tile shows changed
        }
    }
    pushExtents(drawable, cause);
}

// static
void LLShadowDirtyFeed::pushExtents(LLDrawable* drawable, Cause cause)
{
    Aabb box;
    if (extentsOf(drawable, box))
    {
        push(box, cause);
    }
}

// static
void LLShadowDirtyFeed::noteRebuild(LLDrawable* drawable, U32 rebuild_flag)
{
    if (!tracking() || !drawable)
    {
        return;
    }
    if (on_main_thread())
    {
        LLViewerObject* object = drawable->getVObj();
        const bool marked = object && (object->mPoolFlags & LLViewerObject::POOL_TE_REBUILD) != 0;
        bool mark;
        if (drawable == sTEDrawable)
        {
            // a texture-entry setter's own request (TextureEntryRebuild): its markTextured applied the silhouette rule, so the request
            // pushes nothing; the rebuild's own pushes apply the rule again unless the key changed or a shape rebuild was queued
            mark = textureEntryMarkAfterRequest(marked, sTEQueuedShape, sTEKeyChanged);
        }
        else
        {
            mark = textureEntryMarkAfterRebuild(marked, rebuild_flag);
        }
        if (object && mark != marked)
        {
            object->mPoolFlags = mark ? (U8)(object->mPoolFlags | LLViewerObject::POOL_TE_REBUILD)
                                      : (U8)(object->mPoolFlags & ~LLViewerObject::POOL_TE_REBUILD);
        }
        if (drawable == sTEDrawable)
        {
            return;
        }
    }
    if (rebuildDirties(rebuild_flag))
    {
        noteDrawable(drawable, Cause::Edit); // off the main thread it flags the next frame "everything is stale"
    }
}

// static
void LLShadowDirtyFeed::noteMeshLoaded(LLVOVolume* volume)
{
    if (!tracking() || !volume || !on_main_thread())
    {
        return;
    }
    volume->mPoolFlags |= LLViewerObject::POOL_MESH_LOADED;
    if (volume->mDrawable.notNull())
    {
        noteDrawable(volume->mDrawable.get(), Cause::Edit);
    }
}

// static
bool LLShadowDirtyFeed::meshLoaded(const LLDrawable* drawable)
{
    const LLViewerObject* object = drawable ? drawable->getVObj() : nullptr;
    return object && (object->mPoolFlags & LLViewerObject::POOL_MESH_LOADED) != 0;
}

// static
void LLShadowDirtyFeed::clearMeshLoaded(const LLDrawable* drawable)
{
    LLViewerObject* object = drawable ? const_cast<LLDrawable*>(drawable)->getVObj().get() : nullptr;
    if (object)
    {
        object->mPoolFlags &= ~LLViewerObject::POOL_MESH_LOADED;
    }
}

// static
void LLShadowDirtyFeed::noteTextured(LLDrawable* drawable)
{
    if (!tracking() || !drawable || !on_main_thread())
    {
        return;
    }
    LLVOVolume* volume = drawable->getVOVolume();
    if (!volume)
    {
        return;
    }
    // Callers differ: some run before the change is applied (LLVOVolume::setTEColor, which calls again after an alpha change), most
    // after. So the prim's state at this call may be the old one or the new one; the stored key is what the feed last saw. The tile is
    // stale when the key changed (a face turned opaque closes a hole the cached tile still has, an alpha face's texture or mapping moved
    // its holes); a change that leaves the key alone (fullbright, glow, shine, bump, an RGB tint, emissive, the PBR colour, the light's
    // colour) dirties nothing. An object that just became active or static again pushes at that edge, whatever its key.
    bool edge = false;
    if (skipped(drawable, volume, &edge))
    {
        volume->mPoolFlags &= (U8)~LLViewerObject::POOL_ALPHA_KNOWN;
        return;
    }
    const U32 now = silhouette_key(volume);
    bool known;
    const U32 was = swapAlphaKey(volume, now, known);
    const bool dirties = texturedDirties(was, now, known);
    if (dirties)
    {
        // the faces move to their new passes when the queued rebuild completes: that completion pushes again (no texture-entry mark)
        volume->mPoolFlags &= (U8)~LLViewerObject::POOL_TE_REBUILD;
        if (drawable == sTEDrawable)
        {
            sTEKeyChanged = true;
        }
    }
    if (dirties || edge)
    {
        pushExtents(drawable, Cause::Edit);
    }
}

LLShadowDirtyFeed::TextureEntryRebuild::TextureEntryRebuild(LLDrawable* drawable)
{
    if (!drawable || !tracking() || !on_main_thread())
    {
        return; // the request is counted as any other
    }
    mActive = true;
    mPrevDrawable = sTEDrawable;
    mPrevQueuedShape = sTEQueuedShape;
    mPrevKeyChanged = sTEKeyChanged;
    sTEDrawable = drawable;
    // a rebuild with a shape flag already in the queue for this drawable (read before the setter's markRebuild adds its own flags)
    sTEQueuedShape = drawable->isState(LLDrawable::IN_REBUILD_Q) &&
                     drawable->isState(LLDrawable::REBUILD_VOLUME | LLDrawable::REBUILD_POSITION | LLDrawable::REBUILD_RIGGED);
    sTEKeyChanged = false;
}

LLShadowDirtyFeed::TextureEntryRebuild::~TextureEntryRebuild()
{
    if (mActive)
    {
        sTEDrawable = mPrevDrawable;
        sTEQueuedShape = mPrevQueuedShape;
        sTEKeyChanged = mPrevKeyChanged;
    }
}

LLShadowDirtyFeed::OwnRebuild::OwnRebuild(LLDrawable* drawable)
{
    if (!drawable || !tracking() || !on_main_thread())
    {
        return;
    }
    mDrawable = drawable;
    mPrev = sOwnRebuild;
    // a rebuild that waited (a volume or rigged rebuild, or a mesh that loaded) changes the picture when it completes
    mPush = drawable->isState(LLDrawable::REBUILD_VOLUME | LLDrawable::REBUILD_RIGGED) || meshLoaded(drawable);
    sOwnRebuild = drawable;
}

LLShadowDirtyFeed::OwnRebuild::~OwnRebuild()
{
    if (mDrawable)
    {
        sOwnRebuild = mPrev;
    }
}

void LLShadowDirtyFeed::OwnRebuild::completed()
{
    if (!mDrawable)
    {
        return;
    }
    if (mPush)
    {
        noteDrawable(mDrawable, Cause::Edit);
        clearMeshLoaded(mDrawable);
    }
    if (LLViewerObject* object = mDrawable->getVObj())
    {
        object->mPoolFlags &= (U8)~LLViewerObject::POOL_TE_REBUILD; // the rebuild is done; the next one earns its own mark
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
    // Boxes pushed after this call in the same shift would be in the wrong space for the drawables not yet shifted. The only
    // hook LLDrawable::shiftPos fires is the REBUILD_ALL of a static drawable that is not a prim, a tree or terrain (grass),
    // and grass is not a casting type (casting_type), so none follows for the types that count.
    ++sShifts;
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
