/**
 * @file llprojectorshadows.cpp
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

#include "llviewerprecompiledheaders.h"

#include "llprojectorshadows.h"

#include "llshadowdirtyfeed.h"
#include "lllocalshadows.h"

#include "llagent.h"
#include "llapp.h"
#include "llspatialpartition.h"
#include "llframetimer.h"
#include "lldrawable.h"
#include "llglslshader.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llshadowmethod.h"
#include "llstochastichistory.h"
#include "llthread.h"
#include "lltimer.h"
#include "llviewercamera.h"
#include "llviewershadermgr.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llvoavatar.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "pipeline.h"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

extern bool gCubeSnapshot; // a reflection probe render (pipeline.cpp)

using namespace llprojectorpool;

namespace
{
    constexpr U32 kTierSmall = kAtlasSmall; // the smallest atlas: the floor of the step-down on a failed allocation

    // The emitter's extent across the beam, in metres, for the penumbra: the prim's face, clamped (emitterExtent: 2 cm .. 15 cm). A
    // bulb of a few centimetres still blurs a little; the upper limit is the constant of P2, so that no lamp is softer than
    // before and only small emitters get sharper.
    F32 emitter_extent(F32 face)
    {
        return emitterExtent(face);
    }

    // What the pool uses of a spotlight, read once per use from the drawable (the geometry setupSpotLight and the Legacy
    // slot renders also use): the origin of the light's frustum, which has the prim's face as its near plane.
    struct SpotGeom
    {
        LLVector3 origin;   // agent space
        LLQuaternion rot;
        LLVector3 scale;
        F32 fov = 0.f;
        F32 radius = 0.f;
        F32 dist = 0.f;     // origin to the near plane
    };

    bool read_spot(LLDrawable* drawable, SpotGeom& g)
    {
        LLVOVolume* volume = drawable ? drawable->getVOVolume() : nullptr;
        if (!volume)
        {
            return false;
        }
        g.fov = volume->getSpotLightParams().mV[0];
        g.radius = volume->getLightRadius();
        g.rot = volume->getRenderRotation();
        g.scale = volume->getScale();
        if (!(g.fov > 0.f && g.fov < 3.1f) || !(g.scale.mV[VY] > 0.f) || !(g.scale.mV[VX] > 0.f) || !(g.radius > 0.f) || !std::isfinite(g.radius) ||
            !std::isfinite(g.scale.mV[VX]) || !std::isfinite(g.scale.mV[VY]) || !std::isfinite(g.scale.mV[VZ]))
        {
            return false;
        }
        LLVector3 at_axis(0.f, 0.f, -g.scale.mV[VZ] * 0.5f);
        at_axis *= g.rot;
        const LLVector3 np = drawable->getPositionAgent() + at_axis;
        at_axis.normVec();
        g.dist = (g.scale.mV[VY] * 0.5f) / tanf(g.fov * 0.5f);
        g.origin = np - at_axis * g.dist;
        return true;
    }

    // The light's view and projection for the shadow render and its lookup (the Legacy slot code of generateSunShadow,
    // factored): the same matrices serve the tile's render and the lookup matrix built from them.
    void spot_matrices(const SpotGeom& g, glm::mat4& view, glm::mat4& proj, F32& far_clip)
    {
        LLMatrix4 mat(g.rot, LLVector4(g.origin, 1.f));
        view = glm::inverse(glm::make_mat4((F32*)mat.mMatrix));
        const F32 near_clip = g.dist + 0.01f;
        far_clip = g.dist + g.radius * 1.5f;
        proj = glm::perspective(g.fov, g.scale.mV[VX] / g.scale.mV[VY], near_clip, far_clip);
    }

    // a projector the pool shadows: a spotlight with a radius and a colour, on a prim that is not worn (D11)
    bool eligible(LLDrawable* drawable)
    {
        if (!drawable || drawable->isDead())
        {
            return false;
        }
        LLVOVolume* volume = drawable->getVOVolume();
        if (!volume || volume->isDead() || !volume->isLightSpotlight())
        {
            return false;
        }
        if (!(volume->getLightRadius() > 0.f) || volume->getLightLinearColor().magVecSquared() < 0.001f)
        {
            return false; // the draw loop skips a black light
        }
        if (volume->isAttachment() || volume->isHUDAttachment() || volume->getAvatar() != nullptr)
        {
            return false;
        }
        // the geometry renderTile needs: a light read_spot rejects would stay never drawn for good
        SpotGeom g;
        return read_spot(drawable, g);
    }

    F32 camera_distance(LLDrawable* drawable, const LLVector3& cam_pos, F32 radius)
    {
        return llmax(0.f, (drawable->getPositionAgent() - cam_pos).length() - radius);
    }
}

LLProjectorShadows::LLProjectorShadows()
{
}

LLProjectorShadows::~LLProjectorShadows()
{
    // no GL here (the pipeline is a global that outlives the context): release() runs first
    llassert(mAtlasS == 0 && mAtlasL == 0 && mFboS == 0 && mFboL == 0);
}

// static
bool LLProjectorShadows::wanted()
{
    return LLPipeline::RenderShadowMethod == LLShadowMethod::STOCHASTIC && LLPipeline::RenderShadowDetail > 1;
}

bool LLProjectorShadows::allocateAtlases(U32 size)
{
    while (glGetError() != GL_NO_ERROR) {}
    // D32F, counted in the viewer's texture memory at the real size (4 bytes a texel), NEAREST, no mips (newTexture2D)
    mAtlasS = LLStochasticEngine::newTexture2D(GL_DEPTH_COMPONENT32F, size, size, GL_R32F, nullptr, GL_DEPTH_COMPONENT, GL_FLOAT);
    mAtlasL = mAtlasS ? LLStochasticEngine::newTexture2D(GL_DEPTH_COMPONENT32F, size, size, GL_R32F, nullptr, GL_DEPTH_COMPONENT, GL_FLOAT) : 0;
    if (!mAtlasS || !mAtlasL)
    {
        freeAtlases();
        return false;
    }
    // depth as it is: the lookup compares each gathered texel against its own reference (no hardware compare), clamped at the edge
    for (U32 tex : { mAtlasS, mAtlasL })
    {
        gGL.getTexUnit(0)->bindManual(LLTexUnit::TT_TEXTURE, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_NONE);
    }
    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);

    // two depth-only framebuffers: S is drawn into (and read by blits), L is written by copies and blits
    const U32 saved_fbo = LLRenderTarget::sCurFBO;
    U32* fbos[2] = { &mFboS, &mFboL };
    const U32 texs[2] = { mAtlasS, mAtlasL };
    bool ok = true;
    for (int i = 0; i < 2; ++i)
    {
        glGenFramebuffers(1, fbos[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, *fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, texs[i], 0);
        glDrawBuffer(GL_NONE);
        glReadBuffer(GL_NONE);
        ok = ok && glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);
    const GLenum error = glGetError();
    if (!ok || error != GL_NO_ERROR)
    {
        LL_WARNS("ProjectorShadows") << "projector shadow atlas " << size << "x" << size << " could not be set up (framebuffer "
                                     << (ok ? "complete" : "incomplete") << ", GL error " << error << ")" << LL_ENDL;
        while (glGetError() != GL_NO_ERROR) {}
        freeAtlases();
        return false;
    }
    mAtlasSize = size;
    mBuddy.reset(size);
    mLevel.reset();
    return true;
}

void LLProjectorShadows::freeAtlases()
{
    // a framebuffer that is the current one must not stay so (LLRenderTarget::release does the same)
    for (U32* fbo : { &mFboS, &mFboL })
    {
        if (*fbo)
        {
            if (LLRenderTarget::sCurFBO == *fbo)
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                LLRenderTarget::sCurFBO = 0;
            }
            glDeleteFramebuffers(1, fbo);
            *fbo = 0;
        }
    }
    // deleteTexture unbinds the name from every texture unit first
    LLStochasticEngine::deleteTexture(mAtlasS);
    LLStochasticEngine::deleteTexture(mAtlasL);
    mAtlasSize = 0;
}

bool LLProjectorShadows::ensureAllocated()
{
    if (!on_main_thread() || LLApp::isExiting())
    {
        return active();
    }
    if (!wanted())
    {
        return false;
    }
    if (active())
    {
        return true;
    }
    // one tier by video memory (2048 below 3 GB, 4096, 8192 from about 12 GB of dedicated memory, within GL_MAX_TEXTURE_SIZE); a
    // failed allocation tries the one below, and the smallest failing leaves the lights without a tile (never another method).
    // The driver reports its dedicated memory (NVX_gpu_memory_info, WGL_AMD_gpu_association) or the viewer falls back to the DXGI
    // budget, which on shared-memory graphics is system RAM: only the first counts for 8192 (llwindowwin32.cpp skips the DXGI
    // raise in the same case).
    const bool dedicated_vram = gGLManager.mHasNVXGpuMemoryInfo || gGLManager.mHasAMDAssociations;
    U32 size = atlasTier(gGLManager.mVRAM, (U32)llmax(gGLManager.mGLMaxTextureSize, 0), dedicated_vram);
    const U32 chosen = size;
    for (; size >= kTierSmall; size >>= 1)
    {
        if (allocateAtlases(size))
        {
            mAllocFailed = false;
            LLShadowDirtyFeed::setTracking(LLShadowDirtyFeed::Owner::Projectors, true); // the scene's change hooks record from now on
            // every record starts again: a tile has to be drawn into the new atlas
            for (auto& entry : mRecords)
            {
                Record& rec = entry.second;
                rec.placed = false;
                rec.drawnFrame = 0;
                rec.drawn.valid = false;
                rec.pending = false;
                rec.lowres = false;
                rec.avatarInLayer = false;
            }
            LL_INFOS("ProjectorShadows") << "projector shadow pool: two " << size << "x" << size << " depth atlases ("
                                         << (atlasBytes(size) >> 20) << " MiB), tier " << chosen << " chosen for " << gGLManager.mVRAM
                                         << " MB of video memory (" << (dedicated_vram ? "dedicated" : "not known to be dedicated")
                                         << "), GL_MAX_TEXTURE_SIZE " << gGLManager.mGLMaxTextureSize << LL_ENDL;
            return true;
        }
        if (!mAllocFailed)
        {
            LL_WARNS("ProjectorShadows") << "projector shadow atlases of " << size << "x" << size << " could not be allocated" << LL_ENDL;
        }
    }
    if (!mAllocFailed)
    {
        LL_WARNS("ProjectorShadows") << "projector shadow pool unavailable: the projectors are lit without shadows; the method stays selected and "
                                        "the allocation is tried again" << LL_ENDL;
    }
    // the retry backs off: 5 s, 10 s, ... up to 60 s
    mAllocBackoff = mAllocFailed ? llmin(mAllocBackoff * 2.0, 60.0) : 5.0;
    mAllocFailed = true;
    mNextAllocTry = LLTimer::getTotalSeconds() + mAllocBackoff;
    return false;
}

void LLProjectorShadows::release()
{
    if (!on_main_thread())
    {
        llassert(false);
        return;
    }
    // order: the framebuffers, the textures, then every tile and record (the LOD pins of later stages are released with them)
    LLShadowDirtyFeed::setTracking(LLShadowDirtyFeed::Owner::Projectors, false); // the hooks stop recording (and the pending boxes go)
    freeAtlases();
    clear(); // the records hold drawables: dropped with the tiles (a later allocation starts from the light set)
    mBuddy.reset(kTierSmall);
    mLevel.reset();
    mAllocFailed = false;
    mTileRenders = 0;
    mRepacks = 0;
    mStaticRenders = 0;
    mDynamicRenders = 0;
    mLayerRenders = 0;
    mDirtyRenders = 0;
    mDirtyAgeMsMax = 0.f;
    mCauses = CauseCounts();
}

void LLProjectorShadows::clear()
{
    // every tile gives its LOD pins back (the books and the counters on the volumes)
    std::vector<U32> keys;
    for (const auto& entry : mRecords)
    {
        keys.push_back(entry.first);
    }
    for (U32 key : keys)
    {
        unpinTile(key);
    }
    mRecords.clear();
    mKeyOf.clear();
    mLightsWithoutTile = 0;
}

void LLProjectorShadows::forget(const LLDrawable* drawable)
{
    if (!drawable || !on_main_thread())
    {
        return;
    }
    forgetPinned(drawable);
    auto key = mKeyOf.find(drawable);
    if (key == mKeyOf.end())
    {
        return;
    }
    auto it = mRecords.find(key->second);
    if (it != mRecords.end())
    {
        dropRecord(it);
    }
    else
    {
        mKeyOf.erase(key);
    }
}

void LLProjectorShadows::dropRecord(std::map<U32, Record>::iterator it)
{
    Record& rec = it->second;
    if (rec.placed && active())
    {
        mBuddy.free(rec.rect);
    }
    unpinTile(rec.key);
    mKeyOf.erase(rec.drawable.get());
    mRecords.erase(it);
}

U32 LLProjectorShadows::desiredSize(const Record& rec) const
{
    return levelSize(rec.base, mLevel.level());
}

// The light set (spec 4.1): the position-based nearby lights plus every light that already has a record, in range of the
// camera's position (add at the reach, drop beyond reach + max(2 m, 10 %)). No view test, no screen area, no count cap
// (calcNearbyLights uses RenderLocalLightCount only as an on/off, >= 1; its six-light cap is the non-deferred path's).
void LLProjectorShadows::updateLightSet(LLCamera& camera)
{
    const LLVector3 cam_pos = camera.getOrigin();
    const F32 reach = spotReach(LLPipeline::RenderFarClip);

    for (auto& entry : mRecords)
    {
        entry.second.seen = false;
    }

    auto consider = [&](LLDrawable* drawable) {
        if (!eligible(drawable))
        {
            return;
        }
        LLVOVolume* volume = drawable->getVOVolume();
        const F32 dist = camera_distance(drawable, cam_pos, volume->getLightRadius());
        auto known = mKeyOf.find(drawable);
        const bool was_in = known != mKeyOf.end();
        if (!inRange(was_in, dist, reach))
        {
            return;
        }
        Record* rec = nullptr;
        if (was_in)
        {
            rec = &mRecords[known->second];
        }
        else
        {
            Record fresh;
            fresh.key = mNextKey++;
            fresh.drawable = drawable;
            fresh.lastCpuMs = coldItemMs(mCostEmaMs); // a never-drawn record's prediction leaves room for several per frame
            rec = &(mRecords[fresh.key] = fresh);
            mKeyOf[drawable] = fresh.key;
        }
        rec->seen = true;
        rec->camDist = dist;
        rec->base = baseSize(volume->getLightRadius(), volume->getSpotLightParams().mV[0]);
    };

    for (const LLPipeline::Light& light : gPipeline.mNearbyLights)
    {
        consider(light.drawable);
    }
    // lights that left mNearbyLights keep their record until their own drop distance (P14)
    std::vector<LLPointer<LLDrawable>> held;
    for (auto& entry : mRecords)
    {
        if (!entry.second.seen)
        {
            held.push_back(entry.second.drawable);
        }
    }
    for (LLDrawable* drawable : held)
    {
        consider(drawable);
    }
    // everything not seen is out: free its tile (the buddy blocks merge back)
    for (auto it = mRecords.begin(); it != mRecords.end();)
    {
        if (!it->second.seen)
        {
            auto gone = it++;
            dropRecord(gone);
        }
        else
        {
            ++it;
        }
    }
}

void LLProjectorShadows::copyTile(U32 from_tex, const Rect& src, U32 to_tex, const Rect& dst)
{
    llassert(src.size == dst.size);
    // GL 4.3 core, fetched by LLGLManager only at 4.3 and above (llgl.cpp). Test the version, not the
    // pointer: on Linux the function is linked directly and its address is never null (-Werror=address).
    if (gGLManager.mGLVersion >= 4.29f)
    {
        glCopyImageSubData(from_tex, GL_TEXTURE_2D, 0, (GLint)src.x, (GLint)src.y, 0, to_tex, GL_TEXTURE_2D, 0, (GLint)dst.x,
                           (GLint)dst.y, 0, (GLsizei)src.size, (GLsizei)src.size, 1);
    }
    else
    {
        // below GL 4.3: a blit between same-size rectangles with NEAREST gives the same bits
        blitTile(from_tex == mAtlasS ? mFboS : mFboL, src, to_tex == mAtlasS ? mFboS : mFboL, dst);
    }
}

void LLProjectorShadows::copyAtlas(U32 from_tex, U32 to_tex)
{
    Rect all;
    all.size = mAtlasSize;
    copyTile(from_tex, all, to_tex, all);
}

void LLProjectorShadows::blitTile(U32 from_fbo, const Rect& src, U32 to_fbo, const Rect& dst)
{
    LLGLDisable scissor(GL_SCISSOR_TEST); // a blit is clipped by the scissor test
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to_fbo);
    glBlitFramebuffer((GLint)src.x, (GLint)src.y, (GLint)(src.x + src.size), (GLint)(src.y + src.size), (GLint)dst.x, (GLint)dst.y,
                      (GLint)(dst.x + dst.size), (GLint)(dst.y + dst.size), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
}

// Applies a repack (spec 4.3), in one frame and with no name swap: every drawn tile S[old] -> L[new] (a copy at the same
// size, bit exact; a blit at another, the tile is then low resolution until it is drawn again), then L -> S in full, so both
// atlases hold the new layout with the old content.
void LLProjectorShadows::applyPlan(const std::vector<LayoutRequest>& requests, const RepackPlan& plan)
{
    const U32 saved_fbo = LLRenderTarget::sCurFBO;
    // L may still be a lighting unit's texture from the last frame
    for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
    {
        LLTexUnit* tex_unit = gGL.getTexUnit(unit);
        if (tex_unit && tex_unit->getCurrTexture() == mAtlasL)
        {
            tex_unit->unbind(tex_unit->getCurrType());
        }
    }
    // L's tiles that hold avatars are S's again before the moves: the whole-atlas copy L -> S below must not carry an avatar into S
    // (the fixed layer). The avatar layers come back with the dynamic schedule of the frame (the tiles keep avatarInLayer).
    // (only when the whole-atlas copy runs: a repack with no moves leaves L's avatar layers as they are)
    if (!plan.moves.empty())
    {
        for (auto& entry : mRecords)
        {
            const Record& r = entry.second;
            if (r.placed && r.drawnFrame != 0 && r.avatarInLayer)
            {
                copyTile(mAtlasS, r.rect, mAtlasL, r.rect);
            }
        }
    }
    for (const Move& move : plan.moves)
    {
        auto it = mRecords.find(move.key);
        if (it == mRecords.end())
        {
            continue;
        }
        if (move.copy)
        {
            copyTile(mAtlasS, move.src, mAtlasL, move.dst);
        }
        else
        {
            blitTile(mFboS, move.src, mFboL, move.dst);
            it->second.lowres = true; // drawn again at its new size
        }
    }
    if (!plan.moves.empty())
    {
        copyAtlas(mAtlasL, mAtlasS);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);

    // adopt the layout: the allocator the plan ended with
    mBuddy = plan.buddy;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        Record& rec = mRecords[requests[i].key];
        rec.rect = plan.placed[i];
        rec.placed = true;
    }
    ++mRepacks;
}

// Places every record at the current level's sizes in a fresh layout (a level change, a tile that changed size, or a
// compaction when an arrival found no room). A layout that does not fit steps the level down once more.
bool LLProjectorShadows::relayout()
{
    std::vector<U32> bases;
    for (const auto& entry : mRecords)
    {
        bases.push_back(entry.second.base);
    }
    for (int attempt = 0; attempt <= kMaxLevel + 1; ++attempt)
    {
        std::vector<LayoutRequest> requests;
        requests.reserve(mRecords.size());
        for (const auto& entry : mRecords)
        {
            const Record& rec = entry.second;
            LayoutRequest q;
            q.key = rec.key;
            q.size = desiredSize(rec);
            q.has_old = rec.placed;
            q.old_rect = rec.rect;
            q.drawn = rec.drawnFrame != 0;
            requests.push_back(q);
        }
        const RepackPlan plan = planRepack(requests, mAtlasSize);
        if (plan.ok)
        {
            applyPlan(requests, plan);
            return true;
        }
        if (!mLevel.update(bases, mAtlasSize, 0.f, true))
        {
            break; // already at the smallest tiles
        }
    }
    LL_WARNS_ONCE("ProjectorShadows") << "projector shadow tiles do not fit the atlas even at the smallest size; some projectors have no shadow" << LL_ENDL;
    return false;
}

// the level and the placement of this frame's light set (spec 4.2); true when a layout was applied
bool LLProjectorShadows::layoutChanged()
{
    std::vector<U32> bases;
    bases.reserve(mRecords.size());
    for (const auto& entry : mRecords)
    {
        bases.push_back(entry.second.base);
    }
    bool need_relayout = mLevel.update(bases, mAtlasSize, gFrameIntervalSeconds.value(), false);
    for (auto& entry : mRecords)
    {
        Record& rec = entry.second;
        const U32 want = desiredSize(rec);
        if (rec.placed && rec.rect.size != want)
        {
            need_relayout = true; // the light's own size changed (its radius or its angle was edited)
        }
        else if (!rec.placed && !need_relayout)
        {
            Rect r;
            if (mBuddy.alloc(want, r))
            {
                rec.rect = r;
                rec.placed = true;
            }
            else
            {
                need_relayout = true; // no room: compact, and step the level down if that does not make room
            }
        }
    }
    if (!need_relayout)
    {
        return false;
    }
    // a level change or a resize moves tiles: the unplaced records are placed by the repack too
    return relayout();
}

// One tile: the light's view (its pose now), the fixed objects into S (the static layer), then S -> L for the tile, then the avatars
// in the light over it in L (the dynamic layer, drawAvatars). The cull frustum comes from the light's matrices. The tile's size and
// view come from the light and the casters are held at full detail by the LOD pin (pinCasters, called from
// LLPipeline::renderShadowImpl), so nothing the camera does reaches the picture; the avatars are chosen by position alone.
void LLProjectorShadows::renderTile(Record& rec, Cause cause, bool dynamic)
{
    SpotGeom g;
    if (!read_spot(rec.drawable, g))
    {
        return;
    }
    glm::mat4 view, proj;
    F32 far_clip = 0.f;
    spot_matrices(g, view, proj, far_clip);

    const F64 cpu_start = LLTimer::getTotalSeconds();

    glBindFramebuffer(GL_FRAMEBUFFER, mFboS);
    LLRenderTarget::sCurFBO = mFboS;
    const S32 n = (S32)rec.rect.size;
    gGLViewport[0] = (S32)rec.rect.x;
    gGLViewport[1] = (S32)rec.rect.y;
    gGLViewport[2] = n;
    gGLViewport[3] = n;
    glViewport(gGLViewport[0], gGLViewport[1], n, n);
    LLRenderTarget::sCurResX = (U32)n;
    LLRenderTarget::sCurResY = (U32)n;
    LLLocalShadowGL::setTileOrigin((F32)rec.rect.x, (F32)rec.rect.y);

    // the LOD pin's books for this render: what the tile meets from here on is what it holds afterwards
    mPins.beginRender(rec.key);
    mRenderingRec = &rec;
    mRenderVolume = rec.volume;
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_STATIC;

    {
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(gGLViewport[0], gGLViewport[1], n, n);
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LESS);
            glClear(GL_DEPTH_BUFFER_BIT); // only the tile: the scissor
        }

        set_current_modelview(view);
        set_current_projection(proj);
        set_last_modelview(view);
        set_last_projection(proj);

        LLCamera shadow_cam = *LLViewerCamera::getInstance();
        shadow_cam.setFar(far_clip);
        shadow_cam.setOrigin(g.origin);
        LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

        static LLCullResult result;
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_SPOT_SHADOW0;
        LLPipeline::RenderSpotLight = rec.drawable;
        gPipeline.renderShadow(view, proj, shadow_cam, result, false);
        LLPipeline::RenderSpotLight = nullptr;
        gGL.flush(); // what the immediate-mode batch holds goes to the tile before the framebuffer changes
    }
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_NONE;
    mRenderingRec = nullptr;
    // the objects the tile held and did not meet again (they left the light, or are gone) go back to the camera's LOD
    for (const Id128& id : mPins.endRender(rec.key))
    {
        unpinObject(id);
    }
    // renderShadow must leave the framebuffer it was given (debug check of spec 4.8)
    llassert(LLRenderTarget::sCurFBO == mFboS);

    copyTile(mAtlasS, rec.rect, mAtlasL, rec.rect);
    // the avatar layer (P4): the avatars in the light are drawn over the copy, in L only; S stays the fixed layer
    const F32 avatar_ms = rec.holdsAvatar ? drawAvatars(rec, view, proj, g.origin, far_clip) : 0.f;
    mLastFrameAvatarDraws += rec.holdsAvatar ? rec.avatarCount : 0;

    // the pose this tile now holds: the lookup matrix is built from it
    LLProjectorShadows::Record& r = rec;
    r.drawn.originGlobal = gAgent.getPosGlobalFromAgent(g.origin);
    r.drawn.rot = g.rot;
    r.drawn.scale = g.scale;
    r.drawn.fov = g.fov;
    r.drawn.radius = g.radius;
    r.drawn.dist = g.dist;
    r.drawn.valid = true;
    r.drawnPose = r.curPose;
    r.drawnFrame = LLFrameTimer::getFrameCount();
    if (r.pending && cause != Cause::Lamp && cause != Cause::Avatar && cause != Cause::Lowres && cause != Cause::New)
    {
        // a change that reached the pool at the start of this frame (or the end of the last) is on screen after this render
        const F32 waited_ms = (F32)((LLTimer::getTotalSeconds() - r.dirtyTime) * 1000.0) + gFrameIntervalSeconds.value() * 1000.f;
        mDirtyAgeMsMax = llmax(mDirtyAgeMsMax, waited_ms);
        ++mDirtyRenders;
    }
    r.pending = false;
    r.dirtyFrame = 0;
    r.dirtyTime = 0.0;
    r.lowres = false;
    r.avatarInLayer = r.holdsAvatar;
    r.layerFrame = r.drawnFrame;
    const F32 ms = (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
    r.lastCpuMs = llmax(ms - avatar_ms, 0.f); // the fixed layer's cost; the avatar layer's is lastAvatarMs
    if (rec.holdsAvatar)
    {
        r.lastAvatarMs = avatar_ms;
    }
    mCostEmaMs += 0.2f * (r.lastCpuMs - mCostEmaMs);
    mLastFrameRenderMs += ms;
    frameLedger().add(ms);
    ++mLastFrameRenders;
    ++mTileRenders;
    mCauses.add(cause);
    if (dynamic)
    {
        ++mDynamicRenders;
        ++mLastFrameDynamic;
    }
    else
    {
        ++mStaticRenders;
        ++mLastFrameStatic;
    }
}

// The dynamic layer of one tile (P4): the avatars in the light, drawn into L over the tile's copy of S. The tile's cull comes from
// the light's pose the tile holds (view, proj, origin), the avatar partitions and the worn objects are the only things it draws
// (LLPipeline::LOCAL_SHADOW_DYNAMIC), and no camera test enters: an avatar behind the camera, or an impostor, casts like any other.
// Depth is not cleared (S's copy is in L); the depth test composes the two layers. Leaves L's framebuffer bound.
F32 LLProjectorShadows::drawAvatars(Record& rec, const glm::mat4& view, const glm::mat4& proj, const LLVector3& origin, F32 far_clip)
{
    const F64 cpu_start = LLTimer::getTotalSeconds();

    glBindFramebuffer(GL_FRAMEBUFFER, mFboL);
    LLRenderTarget::sCurFBO = mFboL;
    const S32 n = (S32)rec.rect.size;
    gGLViewport[0] = (S32)rec.rect.x;
    gGLViewport[1] = (S32)rec.rect.y;
    gGLViewport[2] = n;
    gGLViewport[3] = n;
    glViewport(gGLViewport[0], gGLViewport[1], n, n);
    LLRenderTarget::sCurResX = (U32)n;
    LLRenderTarget::sCurResY = (U32)n;
    LLLocalShadowGL::setTileOrigin((F32)rec.rect.x, (F32)rec.rect.y);
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_DYNAMIC;

    {
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(gGLViewport[0], gGLViewport[1], n, n);

        set_current_modelview(view);
        set_current_projection(proj);
        set_last_modelview(view);
        set_last_projection(proj);

        LLCamera shadow_cam = *LLViewerCamera::getInstance();
        shadow_cam.setFar(far_clip);
        shadow_cam.setOrigin(origin);
        LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

        static LLCullResult result;
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_SPOT_SHADOW0;
        LLPipeline::RenderSpotLight = rec.drawable;
        gPipeline.renderShadow(view, proj, shadow_cam, result, false);
        LLPipeline::RenderSpotLight = nullptr;
        gGL.flush(); // what the immediate-mode batch holds goes to the tile before the framebuffer changes
    }
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_NONE;
    llassert(LLRenderTarget::sCurFBO == mFboL);
    return (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
}

// The avatar layer alone: the tile's fixed layer is unchanged, so L's tile is S's tile again (the avatars of the last draw, which
// may have moved or left, are gone) and the avatars in the light now are drawn over it. From the pose the tile holds, the one its
// lookup matrix is built from.
void LLProjectorShadows::renderLayer(Record& rec)
{
    if (!rec.drawn.valid)
    {
        return;
    }
    const F64 cpu_start = LLTimer::getTotalSeconds();
    SpotGeom g;
    g.origin = gAgent.getPosAgentFromGlobal(rec.drawn.originGlobal);
    g.rot = rec.drawn.rot;
    g.scale = rec.drawn.scale;
    g.fov = rec.drawn.fov;
    g.radius = rec.drawn.radius;
    g.dist = rec.drawn.dist;
    glm::mat4 view, proj;
    F32 far_clip = 0.f;
    spot_matrices(g, view, proj, far_clip);

    copyTile(mAtlasS, rec.rect, mAtlasL, rec.rect);
    F32 avatar_ms = 0.f;
    if (rec.holdsAvatar)
    {
        avatar_ms = drawAvatars(rec, view, proj, g.origin, far_clip);
        rec.lastAvatarMs = avatar_ms;
    }
    rec.avatarInLayer = rec.holdsAvatar;
    rec.layerFrame = LLFrameTimer::getFrameCount();
    const F32 ms = (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
    mLastFrameRenderMs += ms;
    frameLedger().add(ms);
    ++mLastFrameRenders;
    ++mTileRenders;
    ++mLayerRenders;
    ++mLastFrameLayer;
    mLastFrameAvatarDraws += rec.holdsAvatar ? rec.avatarCount : 0;
    mCauses.add(Cause::Avatar);
}

namespace
{
    // The light's view volume for the change tests, from the pose the tile is drawn with: the pyramid of its render frustum
    // (spot_matrices: perspective(fov, scale.x / scale.y)), in agent space. It is the same region the tile's cull sees, so the
    // objects the LOD pin holds (the cull's, inside this volume) are the objects a change dirties the tile for. Camera
    // independent by construction: only the lamp's pose enters.
    Pyramid light_volume(const SpotGeom& g, F32 far_clip)
    {
        LLVector3 fwd(0.f, 0.f, -1.f), right(1.f, 0.f, 0.f), up(0.f, 1.f, 0.f);
        fwd *= g.rot;
        right *= g.rot;
        up *= g.rot;
        const F32 aspect = g.scale.mV[VY] > 0.f ? g.scale.mV[VX] / g.scale.mV[VY] : 1.f;
        const F32 tan_y = tanf(g.fov * 0.5f);
        return makePyramid(g.origin.mV, fwd.mV, right.mV, up.mV, tan_y * aspect, tan_y, llmax(g.dist, 0.001f), far_clip);
    }

    Pose light_pose(const SpotGeom& g, LLDrawable* drawable)
    {
        Pose p;
        LLVOVolume* volume = drawable ? drawable->getVOVolume() : nullptr;
        LLViewerRegion* region = volume ? volume->getRegion() : nullptr;
        // region-local and the region's handle, never agent space: a region crossing shifts agent space without moving the lamp
        LLVector3d local = gAgent.getPosGlobalFromAgent(g.origin);
        if (region)
        {
            p.region = region->getHandle();
            local -= region->getOriginGlobal();
        }
        for (int i = 0; i < 3; ++i)
        {
            p.pos[i] = (F32)local.mdV[i];
            p.scale[i] = g.scale.mV[i];
        }
        for (int i = 0; i < 4; ++i)
        {
            p.rot[i] = g.rot.mQ[i];
        }
        p.fov = g.fov;
        p.radius = g.radius;
        return p;
    }

    Id128 id_of(const LLUUID& uuid)
    {
        Id128 id;
        static_assert(sizeof(id) == UUID_BYTES, "an object id is 16 bytes");
        memcpy(&id, uuid.mData, sizeof(id));
        return id;
    }

    // one tile lets go of an object (a prim or a tree)
    void lower_pin(LLViewerObject* object)
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

    // A volume casts nothing into a tile when every face is fully transparent (and none has a glTF material, whose alpha is its
    // own): holding it at full detail would only cost triangles.
    bool volume_casts(const LLVOVolume* volume)
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
}

void LLProjectorShadows::unpinObject(const Id128& id)
{
    LLUUID uuid;
    memcpy(uuid.mData, &id, sizeof(id));
    lower_pin(gObjectList.findObject(uuid));
}

// The object of a drawable that is going away is no longer held by any tile: an object created later with the same id is a new
// object (its counter is zero) and must be pinned by its first render, not skipped as "already held". If the object lives on (its
// drawable was rebuilt), the counters this pool raised on it are lowered.
void LLProjectorShadows::forgetPinned(const LLDrawable* drawable)
{
    const LLViewerObject* object = drawable->getVObj();
    if (!object || LLApp::isExiting())
    {
        return;
    }
    const uint32_t n = mPins.forget(id_of(object->getID()));
    if (n == 0)
    {
        return;
    }
    LLViewerObject* live = gObjectList.findObject(object->getID());
    for (uint32_t i = 0; live && i < n; ++i)
    {
        lower_pin(live);
    }
}

void LLProjectorShadows::unpinTile(U32 key)
{
    const std::vector<Id128> held = mPins.releaseTile(key);
    if (LLApp::isExiting())
    {
        return; // the objects are going with the world: nothing to give back
    }
    for (const Id128& id : held)
    {
        unpinObject(id);
    }
}

void LLProjectorShadows::pinDrawable(LLDrawable* drawable)
{
    LLViewerObject* object = drawable && !drawable->isDead() ? drawable->getVObj().get() : nullptr;
    if (!object || object->isDead())
    {
        return;
    }
    LLVOVolume* volume = drawable->getVOVolume();
    LLVOTree* tree = volume ? nullptr : dynamic_cast<LLVOTree*>(object);
    if (!volume && !tree)
    {
        return;
    }
    if (volume && (volume->isRiggedMesh() || volume->getAvatar() || volume->isAttachment() || volume->isHUDAttachment() || !volume_casts(volume)))
    {
        return;
    }
    Aabb box;
    if (!LLShadowDirtyFeed::extentsOf(drawable, box) || !intersects(mRenderVolume, box))
    {
        return; // not in this light's volume: the object's detail is the camera's business
    }
    if (mPins.touch(mRenderingRec->key, id_of(object->getID())))
    {
        // raises it to the full detail and rebuilds once; the tile is drawn again with it, then holds
        if (volume)
        {
            volume->pinLampLOD();
        }
        else
        {
            tree->pinLampLOD();
        }
    }
}

// The LOD pin (spec 4.4, D6): every volume the tile is about to draw, inside the light's volume, is held at full detail. Without
// it, the caster's LOD follows the camera's distance (calcLOD) and the tile would have to be drawn again as the camera moves.
void LLProjectorShadows::pinCasters(LLCullResult& result)
{
    if (!mRenderingRec || !on_main_thread())
    {
        return;
    }
    auto pin_group = [this](LLSpatialGroup* group) {
        if (!group || group->isDead())
        {
            return;
        }
        for (LLSpatialGroup::element_iter i = group->getDataBegin(); i != group->getDataEnd(); ++i)
        {
            if ((*i)->hasDrawable())
            {
                pinDrawable((LLDrawable*)(*i)->getDrawable());
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

void LLProjectorShadows::update(LLCamera& camera)
{
    if (!on_main_thread() || LLApp::isExiting() || gCubeSnapshot)
    {
        return;
    }
    if (!wanted())
    {
        return;
    }
    const U32 frame = LLFrameTimer::getFrameCount();
    if (mFrame == frame)
    {
        return; // once a frame
    }
    mFrame = frame;
    if (!active())
    {
        if (LLTimer::getTotalSeconds() < mNextAllocTry || !ensureAllocated())
        {
            return;
        }
    }
    mLastFrameRenderMs = 0.f;
    mLastFrameRenders = 0;
    mLastFrameStatic = 0;
    mLastFrameDynamic = 0;
    mLastFrameLayer = 0;
    mLastFrameAvatarDraws = 0;
    frameLedger().begin(frame);

    // the scene's changes since the last frame (swapped before any render of this frame, so a change a render itself causes
    // lands in the next frame: F9)
    const std::vector<DirtyBox>& boxes = LLShadowDirtyFeed::beginFrame(frame);
    const bool everything = LLShadowDirtyFeed::everything();
    mDirtyBoxes = (U32)boxes.size();

    updateLightSet(camera);
    layoutChanged();

    // The lamps' poses, this frame's: from the lamp alone (never the camera). This is what a static scene still costs per frame:
    // one read of every lamp's spot parameters and pose (the pose-change detection). A lamp's matrices and volume are built
    // only when its pose changed (or agent space was shifted, which moves every volume), and the change index is rebuilt only
    // on a frame with dirty boxes, below.
    const U32 shifts = LLShadowDirtyFeed::shifts();
    const bool shifted = shifts != mShiftsSeen;
    mShiftsSeen = shifts;
    std::vector<const LLViewerObject*> lamp_roots; // the lamps' own linksets: fixtures, their changes always push (llshadowdirtyfeed)
    for (auto& entry : mRecords)
    {
        Record& rec = entry.second;
        if (LLViewerObject* lamp = rec.drawable.notNull() ? rec.drawable->getVObj().get() : nullptr)
        {
            LLViewerObject* lamp_root = lamp->getRootEdit();
            lamp_roots.push_back(lamp_root ? lamp_root : lamp);
        }
        SpotGeom g;
        if (!read_spot(rec.drawable, g))
        {
            rec.prevPoseValid = false;
            rec.volumeValid = false;
            continue;
        }
        rec.curPose = light_pose(g, rec.drawable);
        rec.lampMoving = rec.prevPoseValid && poseChanged(rec.prevPose, rec.curPose);
        rec.prevPose = rec.curPose;
        rec.prevPoseValid = true;
        rec.poseStale = rec.drawn.valid && poseChanged(rec.drawnPose, rec.curPose);
        if (shifted || !rec.volumeValid || poseChanged(rec.volumePose, rec.curPose))
        {
            glm::mat4 view, proj;
            F32 far_clip = 0.f;
            spot_matrices(g, view, proj, far_clip);
            rec.volume = light_volume(g, far_clip);
            rec.volumePose = rec.curPose;
            rec.volumeValid = true;
        }
    }
    LLShadowDirtyFeed::setLampRoots(LLShadowDirtyFeed::Owner::Projectors, lamp_roots);

    // which drawn tiles a change touched (the first cause that made a tile stale is kept until it is drawn); the index is lazy:
    // built only when boxes arrived or the list says "everything"
    if (!boxes.empty() || everything)
    {
        std::vector<TileVolume> volumes;
        volumes.reserve(mRecords.size());
        for (auto& entry : mRecords)
        {
            const Record& rec = entry.second;
            if (rec.placed && rec.drawnFrame != 0 && rec.volumeValid)
            {
                TileVolume tv;
                tv.key = rec.key;
                tv.pyramid = rec.volume;
                volumes.push_back(tv);
            }
        }
        mIndex.rebuild(volumes);
        std::vector<Hit> hits;
        mIndex.apply(boxes, everything, hits);
        for (const Hit& hit : hits)
        {
            auto it = mRecords.find(hit.key);
            if (it != mRecords.end() && !it->second.pending)
            {
                it->second.pending = true;
                it->second.pendingCause = hit.cause;
                it->second.dirtyFrame = frame;
                it->second.dirtyTime = LLTimer::getTotalSeconds();
            }
        }
    }

    // The avatars of the dynamic layer (P4), by position alone: every avatar that casts, wherever the camera looks. An avatar is paired
    // with every lamp whose volume its extents touch; nothing here reads the camera, the avatar's visibility or its impostor flag
    // (isVisible and isImpostor are the camera's: an avatar behind the camera, or one drawn as an impostor, still casts). A jelly
    // doll, an invisible or a too slow avatar and a UI avatar are not drawn by the avatar pool either.
    static LLCachedControl<bool> friends_only(gSavedSettings, "RenderAvatarFriendsOnly", false);
    std::vector<Aabb> avatar_boxes;
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* avatar = static_cast<LLVOAvatar*>(character);
        if (!avatar || avatar->isDead() || avatar->mDrawable.isNull() || avatar->isUIAvatar() || avatar->isTooSlow() ||
            avatar->getOverallAppearance() != LLVOAvatar::AOA_NORMAL ||
            (friends_only() && !avatar->isControlAvatar() && !avatar->isSelf() && !avatar->isBuddy())) // as LLDrawPoolAvatar::renderShadow
        {
            continue;
        }
        const LLVector4a* ext = avatar->mDrawable->getSpatialExtents();
        if (!ext)
        {
            continue;
        }
        Aabb b;
        for (int i = 0; i < 3; ++i)
        {
            b.lo[i] = ext[0].getF32ptr()[i];
            b.hi[i] = ext[1].getF32ptr()[i];
        }
        avatar_boxes.push_back(b);
    }

    // the work list: the verdict of every placed tile (llprojectorpool::classifyTile). A static scene has none.
    std::vector<StaticItem> static_items;
    std::vector<DynamicItem> dynamic_items;
    std::map<U32, Cause> causes;
    std::set<U32> layer_only; // the dynamic items that are an avatar layer alone (the others redraw the whole tile)
    mLightsWithoutTile = 0;
    mPendingTiles = 0;
    mAvatarTiles = 0;
    for (auto& entry : mRecords)
    {
        Record& rec = entry.second;
        if (rec.drawnFrame == 0)
        {
            ++mLightsWithoutTile;
        }
        if (!rec.placed)
        {
            continue;
        }
        rec.avatarCount = 0;
        for (const Aabb& b : avatar_boxes)
        {
            if (intersects(rec.volume, b))
            {
                ++rec.avatarCount; // also for a tile about to be drawn the first time: its picture holds the avatar
            }
        }
        rec.holdsAvatar = rec.avatarCount > 0;
        mAvatarTiles += rec.holdsAvatar ? 1 : 0;
        mPendingTiles += rec.pending ? 1 : 0;

        TileFacts facts;
        facts.never_drawn = rec.drawnFrame == 0;
        facts.pending = rec.pending;
        facts.pending_cause = rec.pendingCause;
        facts.pose_stale = rec.poseStale;
        facts.lamp_moving = rec.lampMoving;
        facts.lowres = rec.lowres;
        facts.holds_avatar = rec.holdsAvatar;
        facts.avatar_in_layer = rec.avatarInLayer;
        const TileVerdict verdict = classifyTile(facts);
        if (!verdict.needs_render && !verdict.needs_layer)
        {
            continue;
        }
        causes[rec.key] = verdict.cause;
        // the prediction of a full render: the fixed layer, plus the avatar layer when an avatar is in the light
        const F32 full_ms = rec.lastCpuMs + (rec.holdsAvatar ? rec.lastAvatarMs : 0.f);
        if (verdict.needs_layer)
        {
            DynamicItem item;
            item.key = rec.key;
            item.avatar = true;
            item.age_frames = frame - rec.layerFrame; // the lag of the avatar in this tile: frames since its layer was last drawn
            item.cam_dist = rec.camDist;
            item.last_cpu_ms = rec.lastAvatarMs;
            item.avatar_draws = rec.avatarCount;
            dynamic_items.push_back(item);
            layer_only.insert(rec.key);
        }
        else if (verdict.dynamic)
        {
            DynamicItem item;
            item.key = rec.key;
            item.avatar = rec.holdsAvatar;
            item.age_frames = frame - rec.drawnFrame;
            item.cam_dist = rec.camDist;
            item.last_cpu_ms = full_ms;
            item.avatar_draws = rec.avatarCount;
            dynamic_items.push_back(item);
        }
        else
        {
            StaticItem item;
            item.key = rec.key;
            item.never_drawn = rec.drawnFrame == 0;
            item.pending = true;
            item.cam_dist = rec.camDist;
            item.last_cpu_ms = full_ms;
            // the order of service: how long the tile has been waiting: frames since the first change that touched it, or, for a
            // tile whose only cause is its lamp's pose or a resize (no box), frames since it was drawn, so it cannot starve
            // behind tiles with dirty boxes
            item.age_frames = rec.dirtyFrame && frame >= rec.dirtyFrame ? frame - rec.dirtyFrame
                              : (rec.drawnFrame && frame >= rec.drawnFrame ? frame - rec.drawnFrame : 0u);
            static_items.push_back(item);
        }
    }
    // The budget: one CPU cap for the frame's tile work of every cache together (llprojectorpool::kFrameCapMs, split by
    // selectFrame: the dynamic layer first, up to kDynamicCapMs, the static tiles in what is left). The measured cost of a tile only
    // predicts how many fit; it does not set the budget. What an earlier cache spent in this frame is already off the cap.
    const FrameLedger& ledger = frameLedger();
    const FramePicks picks = selectFrame(static_items, dynamic_items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched());
    if (picks.fixed.empty() && picks.dynamic.empty())
    {
        return;
    }

    // one save and one restore of the GL state for the frame's tile renders (the last scope of the frame restores it)
    LLLocalShadowGL::Scope gl_scope;

    // L is a lighting unit's texture until the next lookup binds it again: not while it is written
    for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
    {
        LLTexUnit* tex_unit = gGL.getTexUnit(unit);
        if (tex_unit && tex_unit->getCurrTexture() == mAtlasL)
        {
            tex_unit->unbind(tex_unit->getCurrType());
        }
    }

    // the in-frame caps: a pick runs only while the measured time of the frame is inside the shared cap (the first pick of the frame
    // always does: the selection kept it inside the cap, or it is the progress pick) and, for the dynamic list, inside its own share
    auto run = [&](const std::vector<U32>& packed, bool dynamic) {
        F32 own_ms = 0.f;
        bool first_pick = true;
        for (U32 pk : packed)
        {
            // the first static pick is exempt from the measured cap: the selection kept it (or it is the progress pick)
            if ((ledger.left(kFrameCapMs) <= 0.f && (dynamic || !first_pick)) || (!first_pick && dynamic && own_ms >= kDynamicCapMs))
            {
                break;
            }
            const U32 key = unpackKey(pk);
            auto it = mRecords.find(key);
            if (it != mRecords.end() && it->second.placed)
            {
                const F32 before = mLastFrameRenderMs;
                if (layer_only.count(key))
                {
                    renderLayer(it->second);
                }
                else
                {
                    renderTile(it->second, causes[key], dynamic);
                }
                own_ms += mLastFrameRenderMs - before;
                first_pick = false;
            }
        }
    };
    run(picks.dynamic, true);
    run(picks.fixed, false);
}

void LLProjectorShadows::setupLight(LLGLSLShader& shader, LLDrawable* light)
{
    const Record* rec = nullptr;
    auto key = mKeyOf.find(light);
    if (key != mKeyOf.end())
    {
        auto it = mRecords.find(key->second);
        if (it != mRecords.end())
        {
            rec = &it->second;
        }
    }
    // a light with no drawn tile has no lookup (proj_shadow_idx -1): it is lit without a shadow until its first render
    const bool has_tile = active() && rec && rec->placed && rec->drawnFrame != 0 && rec->drawn.valid;
    shader.uniform1i(LLShaderMgr::PROJECTOR_SHADOW_INDEX, has_tile ? 0 : -1);
    shader.uniform1f(LLShaderMgr::PROJECTOR_SHADOW_FADE, 0.f); // the Legacy slots' fade; the pool's tiles do not fade in (D21)
    if (!has_tile)
    {
        return;
    }
    // the lookup matrix is the one of the pose the tile was drawn with (a lamp that moved since keeps a coherent shadow
    // until its tile is drawn again), taken into this frame's eye space: tile-local [0,1], so the shader's filter needs no
    // atlas term
    SpotGeom g;
    g.origin = gAgent.getPosAgentFromGlobal(rec->drawn.originGlobal);
    g.rot = rec->drawn.rot;
    g.scale = rec->drawn.scale;
    g.fov = rec->drawn.fov;
    g.radius = rec->drawn.radius;
    g.dist = rec->drawn.dist;
    glm::mat4 view, proj;
    F32 far_clip = 0.f;
    spot_matrices(g, view, proj, far_clip);
    const glm::mat4 trans(0.5f, 0.0f, 0.0f, 0.0f,
                          0.0f, 0.5f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.5f, 0.0f,
                          0.5f, 0.5f, 0.5f, 1.0f);
    const glm::mat4 m = trans * proj * view * glm::inverse(get_current_modelview());
    shader.uniformMatrix4fv(LLShaderMgr::PROJ_SHADOW_MATRIX, 1, GL_FALSE, glm::value_ptr(m));
    shader.uniform4f(LLShaderMgr::PROJ_SHADOW_TILE, (F32)rec->rect.x, (F32)rec->rect.y, (F32)rec->rect.size, 1.f / (F32)mAtlasSize);
    // the emitter's size for the penumbra: the face of the projector prim (its scale across the beam) of the pose the tile was
    // drawn with, clamped; the filter's largest radius is a length in the shader, so the level of the tile does not enter
    shader.uniform2f(LLShaderMgr::PROJ_SHADOW_LIGHT_SIZE, emitter_extent(rec->drawn.scale.mV[VX]), emitter_extent(rec->drawn.scale.mV[VY]));
}

void LLProjectorShadows::bindAtlas(LLGLSLShader& shader)
{
    if (!active())
    {
        return;
    }
    const S32 channel = shader.enableTexture(LLShaderMgr::PROJ_SHADOW_ATLAS, LLTexUnit::TT_TEXTURE);
    if (channel > -1)
    {
        gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, mAtlasL);
    }
}

LLProjectorShadows::Stats LLProjectorShadows::getStats() const
{
    Stats st;
    st.present = active();
    st.level = mLevel.level();
    st.atlasSize = mAtlasSize;
    st.lights = (U32)mRecords.size();
    st.lightsWithoutTile = mLightsWithoutTile;
    st.tileRenders = mTileRenders;
    st.repacks = mRepacks;
    st.costEmaMs = mCostEmaMs;
    st.lastFrameRenderMs = mLastFrameRenderMs;
    st.lastFrameRenders = mLastFrameRenders;
    st.staticRenders = mStaticRenders;
    st.dynamicRenders = mDynamicRenders;
    st.lastFrameStatic = mLastFrameStatic;
    st.lastFrameDynamic = mLastFrameDynamic;
    st.layerRenders = mLayerRenders;
    st.lastFrameLayer = mLastFrameLayer;
    st.lastFrameAvatarDraws = mLastFrameAvatarDraws;
    st.pinnedObjects = (U32)mPins.pinnedObjects();
    st.moverObjects = LLShadowDirtyFeed::moverObjects();
    for (const Id128& id : mPins.pinnedIds())
    {
        LLUUID uuid;
        memcpy(uuid.mData, &id, sizeof(id));
        if (LLViewerObject* object = gObjectList.findObject(uuid))
        {
            st.pinnedTriangles += object->getTriangleCount();
        }
    }
    st.pendingTiles = mPendingTiles;
    st.avatarTiles = mAvatarTiles;
    st.dirtyBoxesLastFrame = mDirtyBoxes;
    st.causes = mCauses;
    st.frame = LLFrameTimer::getFrameCount();
    st.dirtyAgeMsMax = mDirtyAgeMsMax;
    st.dirtyRenders = mDirtyRenders;
    const U32 frame = LLFrameTimer::getFrameCount();
    for (const auto& entry : mRecords)
    {
        const Record& rec = entry.second;
        // the oldest PENDING tile: frames since the first change that touched it (0 in a static scene; a tile that is merely not
        // redrawn is not stale)
        if (rec.placed && rec.drawnFrame != 0 && rec.pending && rec.dirtyFrame != 0 && frame >= rec.dirtyFrame)
        {
            st.maxStaleFrames = std::max(st.maxStaleFrames, frame - rec.dirtyFrame);
        }
    }
    return st;
}

std::vector<LLProjectorShadows::TileInfo> LLProjectorShadows::getTiles() const
{
    std::vector<TileInfo> tiles;
    tiles.reserve(mRecords.size());
    for (const auto& entry : mRecords)
    {
        const Record& rec = entry.second;
        TileInfo t;
        t.key = rec.key;
        t.x = rec.rect.x;
        t.y = rec.rect.y;
        t.size = rec.placed ? rec.rect.size : 0;
        t.drawnFrame = rec.drawnFrame;
        t.camDist = rec.camDist;
        t.pending = rec.pending;
        t.avatar = rec.holdsAvatar || rec.avatarInLayer;
        t.poseStale = rec.poseStale;
        t.lampMoving = rec.lampMoving;
        tiles.push_back(t);
    }
    return tiles;
}
