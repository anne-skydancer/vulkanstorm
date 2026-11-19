/**
 * @file lllocalshadows.cpp
 * @brief What the local shadow caches share: the frame's one selection, the casting avatars, the LOD pin, the avatars of a tile, one
 *        save and restore of the GL state their tile renders change
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

#include "lllocalshadows.h"

#include "llapp.h"
#include "llcharacter.h"
#include "llframetimer.h"
#include "llglslshader.h"
#include "llpointshadows.h"
#include "llprojectorshadows.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llshadowdirtyfeed.h"
#include "llspatialpartition.h"
#include "llthread.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerobjectlist.h"
#include "llviewershadermgr.h"
#include "llvoavatar.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "pipeline.h"

#include <algorithm>
#include <set>

extern bool gCubeSnapshot; // a reflection probe render (pipeline.cpp)

namespace
{
    // the viewer's GL state the tile renders change, put back by the last scope of the frame
    struct SavedState
    {
        U32 fbo = 0;
        U32 resX = 0, resY = 0;
        S32 viewport[4] = {};
        glm::mat4 view, proj, lastView, lastProj;
        LLViewerCamera::eCameraID cameraId = LLViewerCamera::CAMERA_WORLD;
    };

    // main-thread state: the scopes only open and close on the render thread (a scope off it is inactive and does nothing)
    llprojectorpool::ScopeCounter sScopes;
    SavedState sSaved;
    LLLightThroughTarget sThrough; // L3: the light-through target of the static render in progress (layer null: none)
}

LLLocalShadowGL::Scope::Scope() : mActive(on_main_thread() && !LLApp::isExiting())
{
    if (!mActive || !sScopes.enter())
    {
        return;
    }
    sSaved.fbo = LLRenderTarget::sCurFBO;
    sSaved.resX = LLRenderTarget::sCurResX;
    sSaved.resY = LLRenderTarget::sCurResY;
    for (int i = 0; i < 4; ++i)
    {
        sSaved.viewport[i] = gGLViewport[i];
    }
    sSaved.view = get_current_modelview();
    sSaved.proj = get_current_projection();
    sSaved.lastView = get_last_modelview();
    sSaved.lastProj = get_last_projection();
    sSaved.cameraId = LLViewerCamera::sCurCameraID;
}

LLLocalShadowGL::Scope::~Scope()
{
    if (!mActive || !sScopes.leave())
    {
        return;
    }
    // put back what the tile renders changed
    setTileOrigin(0.f, 0.f);
    gDeferredShadowCubeProgram.bind(); // the program renderShadow leaves bound
    glBindFramebuffer(GL_FRAMEBUFFER, sSaved.fbo);
    LLRenderTarget::sCurFBO = sSaved.fbo;
    for (int i = 0; i < 4; ++i)
    {
        gGLViewport[i] = sSaved.viewport[i];
    }
    glViewport(sSaved.viewport[0], sSaved.viewport[1], sSaved.viewport[2], sSaved.viewport[3]);
    glScissor(sSaved.viewport[0], sSaved.viewport[1], sSaved.viewport[2], sSaved.viewport[3]); // not the last tile's box
    LLRenderTarget::sCurResX = sSaved.resX;
    LLRenderTarget::sCurResY = sSaved.resY;
    set_current_modelview(sSaved.view);
    set_current_projection(sSaved.proj);
    set_last_modelview(sSaved.lastView);
    set_last_projection(sSaved.lastProj);
    LLViewerCamera::sCurCameraID = sSaved.cameraId;
}

// static
void LLLocalShadowGL::setTileOrigin(F32 x, F32 y)
{
    // the programs of the semi-transparent casters' rule (Stochastic): the texel origin of its blocks, the tile's (not the
    // atlas') or the sun cascade's wrapped world-grid index (generateSunCascadesStable). (The avatar's alpha mask program has
    // no rule: the Stochastic method skips its pass.)
    LLGLSLShader* programs[] = {
        &gDeferredShadowAlphaMaskProgram, &gDeferredShadowFullbrightAlphaMaskProgram, &gDeferredShadowGLTFAlphaBlendProgram,
        &gDeferredAvatarAlphaShadowProgram
    };
    for (LLGLSLShader* program : programs)
    {
        for (LLGLSLShader* variant : { program, program->mRiggedVariant })
        {
            if (variant && variant->mProgramObject)
            {
                variant->bind();
                variant->uniform2f(LLShaderMgr::SHADOW_TILE_ORIGIN, x, y);
            }
        }
    }
}

// static
void LLLocalShadowGL::copyTexels(U32 from_tex, S32 from_x, S32 from_y, U32 to_tex, S32 to_x, S32 to_y, S32 w, S32 h)
{
    if (w <= 0 || h <= 0)
    {
        return;
    }
    glCopyImageSubData(from_tex, GL_TEXTURE_2D, 0, from_x, from_y, 0, to_tex, GL_TEXTURE_2D, 0, to_x, to_y, 0, w, h, 1);
}

// static
void LLLocalShadows::collectCasters(std::vector<LLLocalShadowCaster>& out)
{
    static LLCachedControl<bool> friends_only(gSavedSettings, "RenderAvatarFriendsOnly", false);
    out.clear();
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* avatar = static_cast<LLVOAvatar*>(character);
        if (!avatar || avatar->isDead() || avatar->mDrawable.isNull() || avatar->isUIAvatar() || avatar->isTooSlow() ||
            avatar->getOverallAppearance() != LLVOAvatar::AOA_NORMAL ||
            (friends_only() && !avatar->isControlAvatar() && !avatar->isSelf() && !avatar->isBuddy()))
        {
            continue;
        }
        const LLVector4a* ext = avatar->mDrawable->getSpatialExtents();
        if (!ext)
        {
            continue;
        }
        LLLocalShadowCaster c;
        c.avatar = avatar;
        c.own = avatar->isSelf();
        for (int i = 0; i < 3; ++i)
        {
            c.box.lo[i] = ext[0].getF32ptr()[i];
            c.box.hi[i] = ext[1].getF32ptr()[i];
        }
        out.push_back(c);
    }
}

// static
llprojectorpool::Id128 LLLocalShadows::idOf(const LLUUID& uuid)
{
    llprojectorpool::Id128 id;
    static_assert(sizeof(id) == UUID_BYTES, "an object id is 16 bytes");
    memcpy(&id, uuid.mData, sizeof(id));
    return id;
}

// static
void LLLocalShadows::lowerPin(LLViewerObject* object)
{
    if (LLVOVolume* volume = object ? dynamic_cast<LLVOVolume*>(object) : nullptr)
    {
        volume->unpinLampLOD();
    }
    else if (LLVOTree* tree = object ? dynamic_cast<LLVOTree*>(object) : nullptr)
    {
        tree->unpinLampLOD();
    }
}

// static
void LLLocalShadows::pinCasters(LLCullResult& result, llprojectorpool::PinBook& book, U32 key, const llprojectorpool::Pyramid& volume,
                                const LLDrawable* skip)
{
    if (!on_main_thread())
    {
        return;
    }
    auto pin_drawable = [&](LLDrawable* drawable) {
        LLViewerObject* object = drawable && !drawable->isDead() ? drawable->getVObj().get() : nullptr;
        if (!object || object->isDead() || drawable == skip)
        {
            return;
        }
        LLVOVolume* vol = drawable->getVOVolume();
        LLVOTree* tree = vol ? nullptr : dynamic_cast<LLVOTree*>(object);
        if (!vol && !tree)
        {
            return;
        }
        if (vol && (vol->isRiggedMesh() || vol->getAvatar() || vol->isAttachment() || vol->isHUDAttachment() || !volumeCasts(vol)))
        {
            return;
        }
        llprojectorpool::Aabb box;
        if (!LLShadowDirtyFeed::extentsOf(drawable, box) || !llprojectorpool::intersects(volume, box))
        {
            return; // not in this light's volume: the object's detail is the camera's business
        }
        if (book.touch(key, idOf(object->getID())))
        {
            // raises it to the full detail and rebuilds once; the tile is drawn again with it, then holds
            if (vol)
            {
                vol->pinLampLOD();
            }
            else
            {
                tree->pinLampLOD();
            }
        }
    };
    auto pin_group = [&](LLSpatialGroup* group) {
        if (!group || group->isDead())
        {
            return;
        }
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            if ((*i)->hasDrawable())
            {
                pin_drawable((LLDrawable*)(*i)->getDrawable());
            }
        }
    };
    for (LLCullResult::sg_iterator i = result.beginVisibleGroups(); i != result.endVisibleGroups(); ++i)
    {
        pin_group(*i);
    }
    for (LLCullResult::sg_iterator i = result.beginDrawableGroups(); i != result.endDrawableGroups(); ++i)
    {
        pin_group(*i);
    }
}

// static
void LLLocalShadows::unpinObjects(const std::vector<llprojectorpool::Id128>& ids)
{
    if (LLApp::isExiting())
    {
        return; // the objects are going with the world: nothing to give back
    }
    for (const llprojectorpool::Id128& id : ids)
    {
        LLUUID uuid;
        memcpy(uuid.mData, &id, sizeof(id));
        lowerPin(gObjectList.findObject(uuid));
    }
}

// static
void LLLocalShadows::forgetPinned(llprojectorpool::PinBook& book, const LLDrawable* drawable)
{
    const LLViewerObject* object = drawable ? drawable->getVObj() : nullptr;
    if (!object || LLApp::isExiting())
    {
        return;
    }
    const uint32_t n = book.forget(idOf(object->getID()));
    if (n == 0)
    {
        return;
    }
    LLViewerObject* live = gObjectList.findObject(object->getID());
    for (uint32_t i = 0; live && i < n; ++i)
    {
        lowerPin(live);
    }
}

// static
void LLLocalShadows::tileAvatars(const std::vector<LLLocalShadowCaster>& casters, const llprojectorpool::Pyramid& volume, const float clip[16],
                                 const LLVector3& origin, U32 n, std::vector<const LLVOAvatar*>& avatars, llprojectorpool::TexelRect& rect)
{
    avatars.clear();
    std::vector<std::pair<F32, const LLLocalShadowCaster*>> in_light;
    for (const LLLocalShadowCaster& c : casters)
    {
        if (llprojectorpool::intersects(volume, c.box))
        {
            const LLVector3 centre((c.box.lo[0] + c.box.hi[0]) * 0.5f, (c.box.lo[1] + c.box.hi[1]) * 0.5f, (c.box.lo[2] + c.box.hi[2]) * 0.5f);
            in_light.emplace_back((centre - origin).magVecSquared(), &c);
        }
    }
    std::sort(in_light.begin(), in_light.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<llprojectorpool::Aabb> boxes;
    const S32 period = LLVOAvatar::getExtentUpdatePeriod(); // how stale the extents can be
    bool whole = false;
    for (size_t i = 0; i < in_light.size() && i < llprojectorpool::kAvatarDrawsMax; ++i)
    {
        const LLLocalShadowCaster& c = *in_light[i].second;
        avatars.push_back(c.avatar);
        whole = whole || c.avatar->isImpostor();
        boxes.push_back(llprojectorpool::inflateBox(c.box, llprojectorpool::footprintMarginM(c.avatar->getVelocity().magVec(), period, c.box)));
    }
    rect = whole ? llprojectorpool::wholeTileRect(n) : llprojectorpool::faceFootprint(clip, boxes, n);
}

// static
bool LLLocalShadows::volumeCasts(const LLVOVolume* volume)
{
    const S32 faces = volume->getNumTEs();
    for (S32 i = 0; i < faces; ++i)
    {
        const LLTextureEntry* te = volume->getTE((U8)i);
        if (!te || te->getColor().mV[VALPHA] > 0.004f || te->getGLTFRenderMaterial())
        {
            return true;
        }
    }
    return faces == 0;
}

// static
void LLLocalShadows::updateFrame(LLCamera& camera)
{
    using namespace llprojectorpool;
    if (!on_main_thread() || LLApp::isExiting() || gCubeSnapshot)
    {
        return;
    }
    LLProjectorShadows& projectors = gPipeline.mProjectorShadows;
    LLPointShadows& points = gPipeline.mPointShadows;

    // the frame's casting avatars, once for both pools (their pairing and every face's draw list read the same list); no pointer outlives
    // this call: the pools let go of it in endFrame
    static std::vector<LLLocalShadowCaster> casters;
    struct EndOfFrame
    {
        LLProjectorShadows& projectors;
        LLPointShadows& points;
        ~EndOfFrame()
        {
            projectors.endFrame();
            points.endFrame();
            casters.clear();
        }
    } end_of_frame{ projectors, points };
    const bool want_projectors = LLProjectorShadows::wanted();
    const bool want_points = LLPointShadows::wanted();
    if (!want_projectors && !want_points)
    {
        return;
    }
    frameLedger().begin(LLFrameTimer::getFrameCount()); // the frame's spending starts at zero (idempotent within a frame)
    collectCasters(casters);

    // each pool's work, packed with its pool id (kPoolProjectors, kPoolPoints)
    std::vector<StaticItem> static_items;
    std::vector<DynamicItem> dynamic_items;
    projectors.prepare(camera, casters, static_items, dynamic_items);
    points.prepare(camera, casters, static_items, dynamic_items);

    // The budget: one CPU cap for the frame's tile work of every cache together (llprojectorpool::kFrameCapMs, split by selectFrame: the
    // dynamic layer first, up to kDynamicCapMs, the static tiles in what is left). The measured cost of a tile only predicts how many fit;
    // it does not set the budget. What earlier work of the frame spent is already off the cap.
    const FrameLedger& ledger = frameLedger();
    FramePicks picks;
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("local shadows select"); // <Vulkanstorm> hotfix
        picks = selectFrame(static_items, dynamic_items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched());
    }
    if (picks.fixed.empty() && picks.dynamic.empty())
    {
        return;
    }

    // one save and one restore of the GL state for the frame's tile renders (the last scope of the frame restores it)
    LLLocalShadowGL::Scope gl_scope;
    // the live atlases are lighting units' textures until the next lookup binds them again: not while they are written
    projectors.beginRenders();
    points.beginRenders();

    // the in-frame caps: a pick runs only while the measured time of the frame is inside the shared cap and, for the dynamic list,
    // inside its own share. The one exemption: the first pick of the frame when nothing else rendered this frame (no earlier cache,
    // no dynamic pick): the selection kept it inside the cap, or it is the progress pick (mayRunPick)
    std::set<U32> own_keys; // the own avatar's every-frame faces (packed): exempt from the caps, drawn every frame
    for (const DynamicItem& it : dynamic_items)
    {
        if (it.own)
        {
            own_keys.insert(packKey(it.pool, it.key));
        }
    }
    bool only_own = true; // the dynamic picks that ran before a static pick, if all are the own avatar's: the progress head runs beside them
    for (U32 pk : picks.dynamic)
    {
        only_own = only_own && own_keys.count(pk) > 0;
    }
    const bool alone = ledger.untouched() && only_own;
    const F32 dynamic_share = dynamicShareMs(!static_items.empty(), kFrameCapMs); // the whole cap when no static tile waits
    auto run = [&](const std::vector<U32>& packed, bool dynamic) {
        F32 spent_ms = 0.f;
        bool first_pick = true;
        for (U32 pk : packed)
        {
            const bool own = dynamic && own_keys.count(pk) > 0;
            if (!mayRunPick(ledger.left(kFrameCapMs), dynamic, first_pick, alone, own) || (!first_pick && dynamic && !own && spent_ms >= dynamic_share))
            {
                break;
            }
            F32 ms = 0.f;
            const U32 key = unpackKey(pk);
            const bool rendered = unpackPool(pk) == kPoolPoints ? points.renderPick(key, dynamic, ms) : projectors.renderPick(key, dynamic, ms);
            if (rendered)
            {
                spent_ms += ms;
                first_pick = false;
            }
        }
    };
    run(picks.dynamic, true);
    run(picks.fixed, false);
}

// static
void LLLocalShadowGL::setThroughTarget(LLLightThroughLayer* layer, const llprojectorpool::Rect& rect, unsigned key)
{
    if (!on_main_thread())
    {
        llassert(false);
        return;
    }
    sThrough = LLLightThroughTarget();
    sThrough.layer = layer;
    sThrough.rect = rect;
    sThrough.key = key;
}

// static
LLLightThroughTarget* LLLocalShadowGL::throughTarget()
{
    return sThrough.layer && on_main_thread() ? &sThrough : nullptr;
}

// static
bool LLLocalShadowGL::endThroughTarget()
{
    if (!on_main_thread())
    {
        return false;
    }
    const bool drawn = sThrough.layer && sThrough.drawn;
    sThrough = LLLightThroughTarget();
    return drawn;
}
