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

    // the viewer's GL state the pool changes while it renders, put back at the end
    struct SavedState
    {
        U32 fbo = 0;
        U32 resX = 0, resY = 0;
        S32 viewport[4] = {};
        glm::mat4 view, proj, lastView, lastProj;
        LLViewerCamera::eCameraID cameraId = LLViewerCamera::CAMERA_WORLD;
    };
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
            // every record starts again: a tile has to be drawn into the new atlas
            for (auto& entry : mRecords)
            {
                Record& rec = entry.second;
                rec.placed = false;
                rec.drawnFrame = 0;
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
    freeAtlases();
    clear(); // the records hold drawables: dropped with the tiles (a later allocation starts from the light set)
    mBuddy.reset(kTierSmall);
    mLevel.reset();
    mAllocFailed = false;
    mTileRenders = 0;
    mRepacks = 0;
}

void LLProjectorShadows::clear()
{
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
            fresh.lastCpuMs = mCostEmaMs;
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

void LLProjectorShadows::setTileOrigin(F32 x, F32 y)
{
    // the programs that dither the semi-transparent casters of a tile by the texel of the tile (not of the atlas)
    LLGLSLShader* programs[] = {
        &gDeferredShadowAlphaMaskProgram, &gDeferredShadowFullbrightAlphaMaskProgram, &gDeferredShadowGLTFAlphaBlendProgram,
        &gDeferredAvatarAlphaShadowProgram, &gDeferredAvatarAlphaMaskShadowProgram
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

// One tile: the light's view (its pose now), the fixed objects and the avatars into S, then S -> L for the tile. The cull
// frustum comes from the light's matrices. The tile's size and view come from the light; the caster LOD and the avatars'
// visibility still follow the camera until P3/P4.
void LLProjectorShadows::renderTile(Record& rec)
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
    setTileOrigin((F32)rec.rect.x, (F32)rec.rect.y);

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
    // renderShadow must leave the framebuffer it was given (debug check of spec 4.8)
    llassert(LLRenderTarget::sCurFBO == mFboS);

    copyTile(mAtlasS, rec.rect, mAtlasL, rec.rect);

    // the pose this tile now holds: the lookup matrix is built from it
    LLProjectorShadows::Record& r = rec;
    r.drawn.originGlobal = gAgent.getPosGlobalFromAgent(g.origin);
    r.drawn.rot = g.rot;
    r.drawn.scale = g.scale;
    r.drawn.fov = g.fov;
    r.drawn.radius = g.radius;
    r.drawn.dist = g.dist;
    r.drawn.valid = true;
    r.drawnFrame = LLFrameTimer::getFrameCount();
    r.drawnTime = LLTimer::getTotalSeconds();
    const F32 ms = (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
    r.lastCpuMs = ms;
    mCostEmaMs += 0.2f * (ms - mCostEmaMs);
    mLastFrameRenderMs += ms;
    ++mLastFrameRenders;
    ++mTileRenders;
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

    updateLightSet(camera);
    layoutChanged();

    // the work list: every placed tile is an item (stage P2 has no change tracking, so each is a full render, in the order
    // of llprojectorpool::selectStatic: never drawn first, then the oldest first by frames, the nearest only breaking a tie)
    const F64 now = LLTimer::getTotalSeconds();
    std::vector<StaticItem> items;
    items.reserve(mRecords.size());
    mLightsWithoutTile = 0;
    for (const auto& entry : mRecords)
    {
        const Record& rec = entry.second;
        if (rec.drawnFrame == 0)
        {
            ++mLightsWithoutTile;
        }
        if (!rec.placed)
        {
            continue;
        }
        StaticItem item;
        item.key = rec.key;
        item.never_drawn = rec.drawnFrame == 0;
        item.pending = true;
        item.stale_ms = (F32)((now - rec.drawnTime) * 1000.0);
        item.cam_dist = rec.camDist;
        item.last_cpu_ms = rec.lastCpuMs;
        item.age_frames = rec.drawnFrame == 0 ? 0u : frame - rec.drawnFrame;
        items.push_back(item);
    }
    // The budget: a fixed CPU time per frame (llprojectorpool::kStaticCapMs). The measured cost of a tile only predicts how many
    // fit; it does not set the budget (it used to, at 2 x the average, which let the nearest tiles use it up every frame).
    const std::vector<U32> picks = selectStatic(items, kStaticCapMs);
    if (picks.empty())
    {
        return;
    }

    SavedState saved;
    saved.fbo = LLRenderTarget::sCurFBO;
    saved.resX = LLRenderTarget::sCurResX;
    saved.resY = LLRenderTarget::sCurResY;
    for (int i = 0; i < 4; ++i)
    {
        saved.viewport[i] = gGLViewport[i];
    }
    saved.view = get_current_modelview();
    saved.proj = get_current_projection();
    saved.lastView = get_last_modelview();
    saved.lastProj = get_last_projection();
    saved.cameraId = LLViewerCamera::sCurCameraID;

    // L is a lighting unit's texture until the next lookup binds it again: not while it is written
    for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
    {
        LLTexUnit* tex_unit = gGL.getTexUnit(unit);
        if (tex_unit && tex_unit->getCurrTexture() == mAtlasL)
        {
            tex_unit->unbind(tex_unit->getCurrType());
        }
    }

    // the in-frame cap (section 4.5): the first pick always runs, the others only while the measured time is inside the budget
    const F32 frame_budget_ms = kStaticCapMs;
    bool first_pick = true;
    for (U32 key : picks)
    {
        if (!first_pick && mLastFrameRenderMs >= frame_budget_ms)
        {
            break;
        }
        auto it = mRecords.find(key);
        if (it != mRecords.end() && it->second.placed)
        {
            renderTile(it->second);
            first_pick = false;
        }
    }

    // put back what the tile renders changed
    setTileOrigin(0.f, 0.f);
    gDeferredShadowCubeProgram.bind(); // the program renderShadow leaves bound
    glBindFramebuffer(GL_FRAMEBUFFER, saved.fbo);
    LLRenderTarget::sCurFBO = saved.fbo;
    for (int i = 0; i < 4; ++i)
    {
        gGLViewport[i] = saved.viewport[i];
    }
    glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
    glScissor(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]); // not the last tile's box
    LLRenderTarget::sCurResX = saved.resX;
    LLRenderTarget::sCurResY = saved.resY;
    set_current_modelview(saved.view);
    set_current_projection(saved.proj);
    set_last_modelview(saved.lastView);
    set_last_projection(saved.lastProj);
    LLViewerCamera::sCurCameraID = saved.cameraId;
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
    const U32 frame = LLFrameTimer::getFrameCount();
    for (const auto& entry : mRecords)
    {
        const Record& rec = entry.second;
        if (rec.placed && rec.drawnFrame != 0 && frame >= rec.drawnFrame)
        {
            st.maxStaleFrames = std::max(st.maxStaleFrames, frame - rec.drawnFrame);
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
        tiles.push_back(t);
    }
    return tiles;
}
