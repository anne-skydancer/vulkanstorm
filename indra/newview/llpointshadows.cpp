/**
 * @file llpointshadows.cpp
 * @brief The point lights' shadow faces (Stochastic shadow method): six faces per point light, as tiles of their own atlases
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

#include "llpointshadows.h"

#include "llshadowdirtyfeed.h"
#include "lllocalshadows.h"
#include "llprojectorshadows.h"

#include "llagent.h"
#include "llapp.h"
#include "llface.h"
#include "llframetimer.h"
#include "llglslshader.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llspatialpartition.h"
#include "llstochastichistory.h"
#include "llthread.h"
#include "lltimer.h"
#include "llviewercamera.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llvoavatar.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "pipeline.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/gtc/type_ptr.hpp>

extern bool gCubeSnapshot; // a reflection probe render (pipeline.cpp)

using namespace llprojectorpool;
using namespace llpointpool;

namespace
{
    constexpr U32 kTierSmall = kAtlasSmall;
    constexpr F32 kDeferredFalloff = 0.5f; // pipeline.cpp's DEFERRED_LIGHT_FALLOFF: the fudge the deferred pass reads the falloff with

    // What the pool makes of a light. On: a point light it shadows (a light that is not a projector, with a radius and a colour, on a prim
    // that is not worn: D11). Off: such a lamp switched off (its light parameters are not in use, so the radius reads 0), turned black or
    // given a radius of 0, which the draw loop skips: its faces are held for a while (S2). Gone: not a lamp the pool shadows.
    enum class LampState
    {
        On,
        Off,
        Gone
    };

    LampState lamp_state(LLDrawable* drawable)
    {
        if (!drawable || drawable->isDead())
        {
            return LampState::Gone;
        }
        LLVOVolume* volume = drawable->getVOVolume();
        if (!volume || volume->isDead() || volume->isLightSpotlight())
        {
            return LampState::Gone; // a projector is the projector pool's
        }
        if (volume->isAttachment() || volume->isHUDAttachment() || volume->getAvatar() != nullptr)
        {
            return LampState::Gone;
        }
        const F32 radius = volume->getLightRadius();
        if (!(radius > 0.f) || volume->getLightLinearColor().magVecSquared() < 0.001f)
        {
            return LampState::Off;
        }
        if (!std::isfinite(radius) || !drawable->getPositionAgent().isFinite())
        {
            return LampState::Gone;
        }
        return LampState::On;
    }

    F32 camera_distance(LLDrawable* drawable, const LLVector3& cam_pos, F32 radius)
    {
        return llmax(0.f, (drawable->getPositionAgent() - cam_pos).length() - radius);
    }

    // the pose of a lamp's faces (llpointpool::lampPose): its centre region-local with the region's handle, never agent space (a region
    // crossing shifts agent space without moving the lamp), and its radius
    Pose lamp_pose(LLDrawable* drawable, const LLVector3& centre, F32 radius)
    {
        LLVOVolume* volume = drawable ? drawable->getVOVolume() : nullptr;
        LLViewerRegion* region = volume ? volume->getRegion() : nullptr;
        LLVector3d local = gAgent.getPosGlobalFromAgent(centre);
        uint64_t handle = 0;
        if (region)
        {
            handle = region->getHandle();
            local -= region->getOriginGlobal();
        }
        const float l[3] = { (F32)local.mdV[VX], (F32)local.mdV[VY], (F32)local.mdV[VZ] };
        return lampPose(handle, l, radius);
    }

    F32 luminance(const LLColor3& c)
    {
        return 0.2126f * c.mV[VRED] + 0.7152f * c.mV[VGREEN] + 0.0722f * c.mV[VBLUE];
    }

    glm::mat4 face_view(int f, const LLVector3& centre)
    {
        float m[16];
        faceView(f, centre.mV, m);
        return glm::make_mat4(m);
    }

    glm::mat4 face_proj(U32 n, F32 far_m)
    {
        float m[16];
        faceProjection(faceTanHalf(n), kNearM, far_m, m);
        return glm::make_mat4(m);
    }

    void unbind_from_units(U32 tex)
    {
        for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
        {
            LLTexUnit* tex_unit = gGL.getTexUnit(unit);
            if (tex_unit && tex_unit->getCurrTexture() == tex)
            {
                tex_unit->unbind(tex_unit->getCurrType());
            }
        }
    }
}

LLPointShadows::LLPointShadows()
{
}

LLPointShadows::~LLPointShadows()
{
    // no GL here (the pipeline is a global that outlives the context): release() runs first
    llassert(mAtlasS == 0 && mAtlasL == 0 && mFboS == 0 && mFboL == 0);
}

// static
bool LLPointShadows::wanted()
{
    return LLProjectorShadows::wanted() && LLPipeline::RenderShadowPointLights;
}

bool LLPointShadows::allocateAtlases(U32 size)
{
    while (glGetError() != GL_NO_ERROR) {}
    // D32F, counted in the viewer's texture memory at the real size, NEAREST, no mips (newTexture2D)
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
        LL_WARNS("PointShadows") << "point light shadow atlas " << size << "x" << size << " could not be set up (framebuffer "
                                 << (ok ? "complete" : "incomplete") << ", GL error " << error << ")" << LL_ENDL;
        while (glGetError() != GL_NO_ERROR) {}
        freeAtlases();
        return false;
    }
    mAtlasSize = size;
    mBuddy.reset(size);
    mLevel.reset();
    mGrowthHold.reset();
    return true;
}

void LLPointShadows::freeAtlases()
{
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
    LLStochasticEngine::deleteTexture(mAtlasS);
    LLStochasticEngine::deleteTexture(mAtlasL);
    mAtlasSize = 0;
}

bool LLPointShadows::ensureAllocated()
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
    // D3: 4096 from 3 GB of video memory, 2048 below, within GL_MAX_TEXTURE_SIZE (the projectors' tiers without their 8192 step: the face
    // rects are packed for an atlas of at most 4096, llpointpool::packFaceRect); a failed allocation tries the one below, and the smallest
    // failing leaves the lamps without a shadow (never another method)
    U32 size = atlasTier(gGLManager.mVRAM, (U32)llmax(gGLManager.mGLMaxTextureSize, 0), false);
    size = llmin(size, kPackAtlasMax);
    const U32 chosen = size;
    for (; size >= kTierSmall; size >>= 1)
    {
        if (allocateAtlases(size))
        {
            mAllocFailed = false;
            LLShadowDirtyFeed::setTracking(LLShadowDirtyFeed::Owner::Points, true);
            llassert(mLamps.empty() && mIdOf.empty());
            LL_INFOS("PointShadows") << "point light shadow faces: two " << size << "x" << size << " depth atlases ("
                                     << (atlasBytes(size) >> 20) << " MiB), tier " << chosen << " chosen for " << gGLManager.mVRAM
                                     << " MB of video memory, GL_MAX_TEXTURE_SIZE " << gGLManager.mGLMaxTextureSize << LL_ENDL;
            return true;
        }
        if (!mAllocFailed)
        {
            LL_WARNS("PointShadows") << "point light shadow atlases of " << size << "x" << size << " could not be allocated" << LL_ENDL;
        }
    }
    if (!mAllocFailed)
    {
        LL_WARNS("PointShadows") << "point light shadows unavailable: the point lights are lit without shadows; the method stays selected "
                                    "and the allocation is tried again" << LL_ENDL;
    }
    mAllocBackoff = mAllocFailed ? llmin(mAllocBackoff * 2.0, 60.0) : 5.0;
    mAllocFailed = true;
    mNextAllocTry = LLTimer::getTotalSeconds() + mAllocBackoff;
    return false;
}

void LLPointShadows::release()
{
    if (!on_main_thread())
    {
        llassert(false);
        return;
    }
    LLShadowDirtyFeed::setTracking(LLShadowDirtyFeed::Owner::Points, false);
    freeAtlases();
    clear();
    mBuddy.reset(kTierSmall);
    mLevel.reset();
    mGrowthHold.reset();
    mAllocFailed = false;
    mFaceRenders = 0;
    mLampRenders = 0;
    mLayerRenders = 0;
    mResizes = 0;
    mCompactions = 0;
    mCauses = CauseCounts();
}

void LLPointShadows::clear()
{
    for (auto& entry : mLamps)
    {
        for (U32 f = 0; f < kFaces; ++f)
        {
            unpinFace(faceKey(entry.first, f));
        }
    }
    mLamps.clear();
    mIdOf.clear();
    mLampsWithoutFaces = 0;
}

void LLPointShadows::forget(const LLDrawable* drawable)
{
    if (!drawable || !on_main_thread())
    {
        return;
    }
    forgetPinned(drawable);
    auto id = mIdOf.find(drawable);
    if (id != mIdOf.end())
    {
        auto it = mLamps.find(id->second);
        if (it != mLamps.end())
        {
            dropLamp(it);
        }
        else
        {
            mIdOf.erase(id);
        }
    }
}

// a lamp goes: its faces' rects are freed, their LOD pins given back
void LLPointShadows::dropLamp(std::map<U32, Lamp>::iterator it)
{
    Lamp& lamp = it->second;
    for (U32 f = 0; f < kFaces; ++f)
    {
        Face& face = lamp.faces[f];
        if (face.placed && active())
        {
            mBuddy.free(face.rect);
        }
        unpinFace(faceKey(lamp.id, f));
    }
    auto id = mIdOf.find(lamp.drawable.get());
    if (id != mIdOf.end() && id->second == lamp.id)
    {
        mIdOf.erase(id);
    }
    mLamps.erase(it);
}

// every face of the lamp is placed and drawn: the set the lookup can read (one pose: a lamp's faces are drawn from its drawn pose, or all
// six at once from a new one)
bool LLPointShadows::lampDrawn(const Lamp& lamp) const
{
    if (!lamp.drawnValid)
    {
        return false;
    }
    for (const Face& face : lamp.faces)
    {
        if (!face.placed || face.drawnFrame == 0)
        {
            return false;
        }
    }
    return true;
}

// a resize is due when a face's wanted size is not its drawn one and the atlas gives the new size once its own rect is freed (a shrink
// always can; a growth waits for a block): its render draws the new size before the lookup reads it (draw-then-swap)
bool LLPointShadows::resizeDue(const Face& face) const
{
    return face.placed && face.want >= kFaceMin && face.want != face.rect.size &&
           (face.want < face.rect.size || mBuddy.largestAfterFree(face.rect) >= face.want);
}

// S2: a lamp that is off keeps its faces while the hold lasts and it is in range by the reach it had when it was last on
void LLPointShadows::holdOff(Lamp& lamp, const LLVector3& cam_pos, F32 reach, F64 now)
{
    if (holdOffFrame(lamp.off, lamp.offSince, now, inRange(true, camera_distance(lamp.drawable, cam_pos, lamp.curPose.radius), reach)))
    {
        lamp.seen = true;
    }
}

// The light set (the projectors' rule, llprojectorshadows.cpp updateLightSet): the position-based nearby lights plus every lamp that
// already has faces, in range of the camera's position (add at the reach, drop beyond reach + max(2 m, 10 %)); no view test, no
// screen area, no count cap. A lamp that goes off keeps its faces for a while (holdOff, S2).
void LLPointShadows::updateLightSet(LLCamera& camera)
{
    const LLVector3 cam_pos = camera.getOrigin();
    const F32 reach = spotReach(LLPipeline::RenderFarClip);
    const F64 now = LLTimer::getTotalSeconds();
    for (auto& entry : mLamps)
    {
        entry.second.seen = false;
    }
    auto consider = [&](LLDrawable* drawable) {
        const LampState state = lamp_state(drawable);
        if (state == LampState::Gone)
        {
            return;
        }
        auto known = mIdOf.find(drawable);
        const bool was_in = known != mIdOf.end();
        if (state == LampState::Off)
        {
            if (was_in)
            {
                auto it = mLamps.find(known->second);
                if (it != mLamps.end())
                {
                    holdOff(it->second, cam_pos, reach, now); // a lamp never seen on gets no faces
                }
            }
            return;
        }
        LLVOVolume* volume = drawable->getVOVolume();
        const F32 dist = camera_distance(drawable, cam_pos, volume->getLightRadius());
        if (!inRange(was_in, dist, reach))
        {
            return;
        }
        U32 id = was_in ? known->second : 0u;
        if (id != 0 && mLamps.find(id) == mLamps.end())
        {
            mIdOf.erase(drawable);
            id = 0;
        }
        if (id == 0)
        {
            // a face key is id << 3 | face and the scheduler packs 28 bits: the id counter wraps inside 25 bits, skipping live ids
            do
            {
                if (mNextId >= kLampIds)
                {
                    mNextId = 1;
                }
                id = mNextId++;
            } while (mLamps.find(id) != mLamps.end());
            Lamp fresh;
            fresh.id = id;
            fresh.drawable = drawable;
            for (Face& face : fresh.faces)
            {
                face.lastCpuMs = coldItemMs(mCostEmaMs); // a never-drawn face's prediction leaves room for several per frame
            }
            mLamps[id] = fresh;
            mIdOf[drawable] = id;
        }
        Lamp& lamp = mLamps[id];
        lamp.seen = true;
        lamp.off = false; // on again: its faces serve at once (S2)
    };
    for (const LLPipeline::Light& light : gPipeline.mNearbyLights)
    {
        consider(light.drawable);
    }
    // lamps that left mNearbyLights keep their faces until their own drop distance
    std::vector<LLDrawable*> held;
    for (auto& entry : mLamps)
    {
        if (!entry.second.seen)
        {
            held.push_back(entry.second.drawable.get());
        }
    }
    for (LLDrawable* drawable : held)
    {
        consider(drawable);
    }
    for (auto it = mLamps.begin(); it != mLamps.end();)
    {
        auto next = std::next(it);
        if (!it->second.seen)
        {
            dropLamp(it);
        }
        it = next;
    }
}

void LLPointShadows::copyTile(U32 from_tex, const Rect& src, U32 to_tex, const Rect& dst)
{
    llassert(src.size == dst.size);
    if (gGLManager.mGLVersion >= 4.29f)
    {
        glCopyImageSubData(from_tex, GL_TEXTURE_2D, 0, (GLint)src.x, (GLint)src.y, 0, to_tex, GL_TEXTURE_2D, 0, (GLint)dst.x,
                           (GLint)dst.y, 0, (GLsizei)src.size, (GLsizei)src.size, 1);
    }
    else
    {
        LLGLDisable scissor(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, from_tex == mAtlasS ? mFboS : mFboL);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to_tex == mAtlasS ? mFboS : mFboL);
        glBlitFramebuffer((GLint)src.x, (GLint)src.y, (GLint)(src.x + src.size), (GLint)(src.y + src.size), (GLint)dst.x, (GLint)dst.y,
                          (GLint)(dst.x + dst.size), (GLint)(dst.y + dst.size), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
}

void LLPointShadows::copyTileTexels(const Rect& tile, const TexelRect& texels)
{
    if (texels.empty())
    {
        return;
    }
    const GLint x = (GLint)tile.x + texels.x0, y = (GLint)tile.y + texels.y0;
    const GLsizei w = texels.x1 - texels.x0, h = texels.y1 - texels.y0;
    if (gGLManager.mGLVersion >= 4.29f)
    {
        glCopyImageSubData(mAtlasS, GL_TEXTURE_2D, 0, x, y, 0, mAtlasL, GL_TEXTURE_2D, 0, x, y, 0, w, h, 1);
    }
    else
    {
        LLGLDisable scissor(GL_SCISSOR_TEST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, mFboS);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, mFboL);
        glBlitFramebuffer(x, y, x + w, y + h, x, y, x + w, y + h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }
}

// A compaction (the projectors' rule): every placed face at its own size in a fresh layout (descending sizes pack a buddy atlas without a
// gap), the drawn ones moved by bit-exact copies S[old] -> L[new], then L -> S in full. Runs when an arriving lamp's faces find no block
// although the free texels would hold them, or a growth has found none for a while.
bool LLPointShadows::compact()
{
    std::vector<LayoutRequest> requests;
    for (const auto& entry : mLamps)
    {
        for (U32 f = 0; f < kFaces; ++f)
        {
            const Face& face = entry.second.faces[f];
            if (!face.placed)
            {
                continue;
            }
            LayoutRequest q;
            q.key = faceKey(entry.first, f);
            q.size = face.rect.size;
            q.has_old = true;
            q.old_rect = face.rect;
            q.drawn = face.drawnFrame != 0;
            requests.push_back(q);
        }
    }
    const RepackPlan plan = planRepack(requests, mAtlasSize);
    if (!plan.ok)
    {
        llassert(false); // they fitted before, and descending sizes leave no gap
        return false;
    }
    const U32 saved_fbo = LLRenderTarget::sCurFBO;
    unbind_from_units(mAtlasL);
    if (!plan.moves.empty())
    {
        // L's faces that hold avatars are S's again before the moves: the whole-atlas copy L -> S must not carry an avatar into S
        for (auto& entry : mLamps)
        {
            for (Face& face : entry.second.faces)
            {
                if (face.placed && face.drawnFrame != 0 && face.avatarInLayer)
                {
                    copyTile(mAtlasS, face.rect, mAtlasL, face.rect);
                }
            }
        }
        for (const Move& move : plan.moves)
        {
            llassert(move.copy); // a compaction keeps every size
            copyTile(mAtlasS, move.src, mAtlasL, move.dst);
        }
        Rect all;
        all.size = mAtlasSize;
        copyTile(mAtlasL, all, mAtlasS, all);
        for (auto& entry : mLamps)
        {
            for (Face& face : entry.second.faces)
            {
                face.layerRectValid = false; // the footprints were in the old layout's texels
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);
    LLRenderTarget::sCurFBO = saved_fbo;
    mBuddy = plan.buddy;
    for (size_t i = 0; i < requests.size(); ++i)
    {
        auto it = mLamps.find(lampOf(requests[i].key));
        if (it != mLamps.end())
        {
            Face& face = it->second.faces[faceOf(requests[i].key)];
            face.rect = plan.placed[i];
            face.placed = true;
        }
    }
    ++mCompactions;
    return true;
}

// The faces' sizes and placement (the projectors' rules, llprojectorshadows.cpp layout): the valve's pressure from the faces' lamp sizes and
// the lamps' distance bands (one band per lamp, floor 128), each face's wanted size, and the arriving lamps placed at theirs, all six faces
// or none (a lamp is drawn whole: an arrival that does not fit whole waits for the shrinks the renders draw, nearest lamp first). A placed
// face is never resized here: its own render draws the new size before it is swapped in.
void LLPointShadows::layout(F32 dt_seconds)
{
    mValveTiles.clear();
    mValveFaces.clear();
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        for (Face& face : lamp.faces)
        {
            ValveTile tile;
            tile.floor = kFaceMin;
            if (lamp.off)
            {
                // S2: a lamp that is off keeps its faces at their sizes (they count; the valve does not size them)
                if (face.placed)
                {
                    tile.base = tile.fixed = face.rect.size;
                    mValveTiles.push_back(tile);
                    mValveFaces.push_back(nullptr);
                }
                continue;
            }
            tile.base = lamp.base;
            tile.band = lamp.band;
            mValveTiles.push_back(tile);
            mValveFaces.push_back(&face);
        }
    }
    mLevel.update(mValveTiles, mAtlasSize, dt_seconds);
    for (size_t i = 0; i < mValveTiles.size(); ++i)
    {
        if (mValveFaces[i])
        {
            mValveFaces[i]->want = valveSize(mValveTiles[i], mLevel.level());
        }
    }
    mValveFaces.clear();

    // the arrivals: lamps with a face not placed, the largest faces first (descending sizes pack the buddy best), then the nearest
    std::vector<Lamp*> waiting;
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        if (lamp.off)
        {
            continue;
        }
        for (const Face& face : lamp.faces)
        {
            if (!face.placed)
            {
                waiting.push_back(&lamp);
                break;
            }
        }
    }
    if (!waiting.empty())
    {
        std::sort(waiting.begin(), waiting.end(), [](const Lamp* a, const Lamp* b) {
            const U32 wa = a->faces[0].want, wb = b->faces[0].want;
            if (wa != wb)
            {
                return wa > wb;
            }
            return a->camDist != b->camDist ? sortDist(a->camDist) < sortDist(b->camDist) : a->id < b->id;
        });
        auto place = [this](std::vector<Lamp*>& list) {
            std::vector<Lamp*> left;
            for (Lamp* lamp : list)
            {
                // all of the lamp's faces or none: a lamp is drawn whole, so faces placed alone would hold space nobody reads
                Rect got[kFaces];
                bool ok = true;
                U32 placed_now = 0;
                for (U32 f = 0; f < kFaces && ok; ++f)
                {
                    Face& face = lamp->faces[f];
                    if (face.placed)
                    {
                        continue;
                    }
                    ok = mBuddy.alloc(face.want, got[f]);
                    if (ok)
                    {
                        placed_now |= 1u << f;
                    }
                }
                for (U32 f = 0; f < kFaces; ++f)
                {
                    if (placed_now & (1u << f))
                    {
                        if (ok)
                        {
                            lamp->faces[f].rect = got[f];
                            lamp->faces[f].placed = true;
                        }
                        else
                        {
                            mBuddy.free(got[f]);
                        }
                    }
                }
                if (!ok)
                {
                    left.push_back(lamp);
                }
            }
            list.swap(left);
        };
        auto texels = [](const std::vector<Lamp*>& list) {
            uint64_t sum = 0;
            for (const Lamp* lamp : list)
            {
                for (const Face& face : lamp->faces)
                {
                    sum += face.placed ? 0 : uint64_t(face.want) * face.want;
                }
            }
            return sum;
        };
        place(waiting);
        if (!waiting.empty() && mBuddy.freeTexels() >= texels(waiting) && compact())
        {
            place(waiting);
        }
        if (!waiting.empty())
        {
            uint64_t shrinking = 0;
            for (const auto& entry : mLamps)
            {
                if (entry.second.off)
                {
                    continue;
                }
                for (const Face& face : entry.second.faces)
                {
                    if (face.placed)
                    {
                        shrinking += shrinkTexels(face.rect.size, face.want);
                    }
                }
            }
            if (arrivalShortfall(mBuddy.freeTexels(), shrinking, texels(waiting)))
            {
                LL_WARNS_ONCE("PointShadows") << "point light faces do not fit the atlas even at the smallest size; the farthest lamps have no shadow" << LL_ENDL;
            }
        }
    }

    bool blocked = false;
    for (const auto& entry : mLamps)
    {
        if (entry.second.off)
        {
            continue;
        }
        for (const Face& face : entry.second.faces)
        {
            if (face.placed && growthBlocked(mBuddy, face.rect, face.want))
            {
                blocked = true;
                break;
            }
        }
        if (blocked)
        {
            break;
        }
    }
    if (mGrowthHold.update(blocked, dt_seconds))
    {
        compact();
    }
}

void LLPointShadows::setFaceTarget(U32 fbo, const Rect& rect)
{
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    LLRenderTarget::sCurFBO = fbo;
    const S32 n = (S32)rect.size;
    gGLViewport[0] = (S32)rect.x;
    gGLViewport[1] = (S32)rect.y;
    gGLViewport[2] = n;
    gGLViewport[3] = n;
    glViewport(gGLViewport[0], gGLViewport[1], n, n);
    LLRenderTarget::sCurResX = (U32)n;
    LLRenderTarget::sCurResY = (U32)n;
    LLLocalShadowGL::setTileOrigin((F32)rect.x, (F32)rect.y);
}

// One face from a pose (the lamp's centre and radius): the fixed objects into S (one cull and one submit with the face's frustum, the
// casters drawn from both sides, the emitter prim left out by identity), then S -> L for the face, then the avatars in its light over it in
// L. A face whose wanted size differs is drawn at the new size into a new rect (draw-then-swap). Returns the CPU ms.
F32 LLPointShadows::renderFace(Lamp& lamp, int f, const LLVector3& centre, F32 radius, Cause cause)
{
    Face& face = lamp.faces[f];
    const F64 cpu_start = LLTimer::getTotalSeconds();
    if (resizeDue(face))
    {
        const Rect old = face.rect;
        mBuddy.free(old);
        Rect nr;
        if (mBuddy.alloc(face.want, nr))
        {
            ++mResizes;
        }
        else
        {
            llassert(false); // resizeDue found the block
            if (!mBuddy.alloc(old.size, nr))
            {
                face.placed = false;
                face.drawnFrame = 0;
                return 0.f;
            }
        }
        face.rect = nr;
        face.layerRectValid = false;
    }
    const U32 n = face.rect.size;
    const F32 far_m = faceFar(radius);
    const glm::mat4 view = face_view(f, centre);
    const glm::mat4 proj = face_proj(n, far_m);
    face.volume = facePyramid(centre.mV, f, (float)faceTanHalf(n), kNearM, far_m);
    face.volumeN = n;

    setFaceTarget(mFboS, face.rect);
    const U32 key = faceKey(lamp.id, (U32)f);
    mPins.beginRender(key);
    mRenderingFace = &face;
    mRenderingKey = key;
    mRenderVolume = face.volume;
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_STATIC;
    LLPipeline::sLocalShadowTwoSided = true;
    {
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(gGLViewport[0], gGLViewport[1], (S32)n, (S32)n);
        {
            LLGLDepthTest depth(GL_TRUE, GL_TRUE, GL_LESS);
            glClear(GL_DEPTH_BUFFER_BIT); // only the face: the scissor
        }
        set_current_modelview(view);
        set_current_projection(proj);
        set_last_modelview(view);
        set_last_projection(proj);

        LLCamera shadow_cam = *LLViewerCamera::getInstance();
        shadow_cam.setFar(far_m);
        shadow_cam.setOrigin(centre);
        LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

        static LLCullResult result;
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_SPOT_SHADOW0;
        LLPipeline::RenderSpotLight = lamp.drawable;
        gPipeline.renderShadow(view, proj, shadow_cam, result, false);
        LLPipeline::RenderSpotLight = nullptr;
        LLVertexBuffer::sSkipRanges = nullptr; // set by skipEmitter inside the render; never past it
        mSkip.clear();
        gGL.flush();
    }
    LLPipeline::sLocalShadowTwoSided = false;
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_NONE;
    mRenderingFace = nullptr;
    for (const Id128& id : mPins.endRender(key))
    {
        unpinObject(id);
    }
    llassert(LLRenderTarget::sCurFBO == mFboS);

    copyTile(mAtlasS, face.rect, mAtlasL, face.rect);
    F32 avatar_ms = 0.f;
    bool avatar_drawn = false;
    if (face.holdsAvatar)
    {
        const F64 face_start = LLTimer::getTotalSeconds();
        const glm::mat4 clip = proj * view;
        const FaceAvatars avatars = faceAvatars(face, glm::value_ptr(clip), centre);
        avatar_ms = (F32)((LLTimer::getTotalSeconds() - face_start) * 1000.0);
        if (!avatars.rect.empty())
        {
            avatar_ms += drawAvatars(lamp, f, avatars, centre, radius);
            avatar_drawn = true;
        }
        face.layerRect = avatars.rect;
    }
    else
    {
        face.layerRect = TexelRect();
    }
    face.layerRectValid = true;

    face.drawnFrame = LLFrameTimer::getFrameCount();
    face.pending = false;
    face.dirtyFrame = 0;
    face.dirtyTime = 0.0;
    face.avatarInLayer = face.holdsAvatar;
    face.layerFrame = face.drawnFrame;
    const F32 ms = (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
    face.lastCpuMs = llmax(ms - avatar_ms, 0.f);
    if (avatar_drawn)
    {
        face.lastAvatarMs = smoothedCostMs(face.lastAvatarMs, avatar_ms);
    }
    mCostEmaMs += 0.2f * (face.lastCpuMs - mCostEmaMs);
    mLastFrameRenderMs += ms;
    frameLedger().add(ms);
    ++mLastFrameRenders;
    ++mFaceRenders;
    mCauses.add(cause);
    return ms;
}

// A whole lamp: its six faces drawn from this frame's pose (its first fill, or a move), which the lookup then reads; until this render
// completes the lookup reads the faces of the pose they were drawn with, so no lookup mixes two poses.
void LLPointShadows::renderLamp(Lamp& lamp, Cause cause)
{
    if (!(lamp.radius > 0.f))
    {
        return;
    }
    const LLVector3 centre = lamp.centre;
    const F32 radius = lamp.radius;
    F32 total = 0.f;
    for (U32 f = 0; f < kFaces; ++f)
    {
        if (!lamp.faces[f].placed)
        {
            return; // layout places a lamp's faces all or none
        }
    }
    for (U32 f = 0; f < kFaces; ++f)
    {
        total += renderFace(lamp, (int)f, centre, radius, cause);
    }
    lamp.drawnCentre = gAgent.getPosGlobalFromAgent(centre);
    lamp.drawnRadius = radius;
    lamp.drawnPose = lamp.curPose;
    lamp.drawnValid = true;
    lamp.poseStale = false;
    lamp.lastLampMs = total;
    ++mLampRenders;
}

// The avatars a face draws, and the tile rectangle they cover (the projectors' rule): at most A_max, the nearest to the light; the
// rectangle the avatars' grown extents cover through the face's matrices, the whole face when that is unknown, too large or an impostor.
LLPointShadows::FaceAvatars LLPointShadows::faceAvatars(const Face& face, const float clip[16], const LLVector3& centre) const
{
    FaceAvatars out;
    if (!mCasters)
    {
        return out;
    }
    std::vector<std::pair<F32, const LLLocalShadowCaster*>> in_light;
    for (const LLLocalShadowCaster& c : *mCasters)
    {
        if (intersects(face.volume, c.box))
        {
            const LLVector3 mid((c.box.lo[0] + c.box.hi[0]) * 0.5f, (c.box.lo[1] + c.box.hi[1]) * 0.5f, (c.box.lo[2] + c.box.hi[2]) * 0.5f);
            in_light.emplace_back((mid - centre).magVecSquared(), &c);
        }
    }
    std::sort(in_light.begin(), in_light.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<Aabb> boxes;
    const S32 period = LLVOAvatar::getExtentUpdatePeriod();
    bool whole = false;
    for (size_t i = 0; i < in_light.size() && i < kAvatarDrawsMax; ++i)
    {
        const LLLocalShadowCaster& c = *in_light[i].second;
        out.avatars.push_back(c.avatar);
        whole = whole || c.avatar->isImpostor();
        boxes.push_back(inflateBox(c.box, footprintMarginM(c.avatar->getVelocity().magVec(), period, c.box)));
    }
    const U32 n = face.rect.size;
    out.rect = whole ? wholeTileRect(n) : faceFootprint(clip, boxes, n);
    return out;
}

// The dynamic layer of one face: the avatars in the light drawn into L over the face's copy of S, from the pose the face holds (the
// avatar partitions and the worn objects only, LOCAL_SHADOW_DYNAMIC; both sides of their surfaces), scissored to the footprint.
F32 LLPointShadows::drawAvatars(Lamp& lamp, int f, const FaceAvatars& avatars, const LLVector3& centre, F32 radius)
{
    const F64 cpu_start = LLTimer::getTotalSeconds();
    Face& face = lamp.faces[f];
    const U32 n = face.rect.size;
    const F32 far_m = faceFar(radius);
    const glm::mat4 view = face_view(f, centre);
    const glm::mat4 proj = face_proj(n, far_m);
    setFaceTarget(mFboL, face.rect);
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_DYNAMIC;
    LLPipeline::sLocalShadowTwoSided = true;
    std::vector<const LLVOAvatar*> face_avatars = avatars.avatars;
    LLPipeline::sLocalShadowAvatars = &face_avatars;
    {
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(gGLViewport[0] + avatars.rect.x0, gGLViewport[1] + avatars.rect.y0, llmax(avatars.rect.x1 - avatars.rect.x0, 0),
                  llmax(avatars.rect.y1 - avatars.rect.y0, 0));
        set_current_modelview(view);
        set_current_projection(proj);
        set_last_modelview(view);
        set_last_projection(proj);

        LLCamera shadow_cam = *LLViewerCamera::getInstance();
        shadow_cam.setFar(far_m);
        shadow_cam.setOrigin(centre);
        LLViewerCamera::updateFrustumPlanes(shadow_cam, false, false, true);

        static LLCullResult result;
        LLViewerCamera::sCurCameraID = LLViewerCamera::CAMERA_SPOT_SHADOW0;
        LLPipeline::RenderSpotLight = lamp.drawable;
        gPipeline.renderShadow(view, proj, shadow_cam, result, false);
        LLPipeline::RenderSpotLight = nullptr;
        gGL.flush();
    }
    LLPipeline::sLocalShadowTwoSided = false;
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_NONE;
    LLPipeline::sLocalShadowAvatars = nullptr;
    llassert(LLRenderTarget::sCurFBO == mFboL);
    return (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
}

// The avatar layer alone: the face's fixed layer is unchanged, so L's face is S's again where the last avatars were, and the avatars in
// the light now are drawn over it, from the pose the face holds.
void LLPointShadows::renderLayer(Lamp& lamp, int f)
{
    if (!lamp.drawnValid)
    {
        return;
    }
    Face& face = lamp.faces[f];
    const F64 cpu_start = LLTimer::getTotalSeconds();
    const LLVector3 centre = gAgent.getPosAgentFromGlobal(lamp.drawnCentre);
    const F32 radius = lamp.drawnRadius;
    F32 avatar_ms = 0.f;
    FaceAvatars avatars;
    if (face.holdsAvatar)
    {
        const F64 face_start = LLTimer::getTotalSeconds();
        const glm::mat4 clip = face_proj(face.rect.size, faceFar(radius)) * face_view(f, centre);
        avatars = faceAvatars(face, glm::value_ptr(clip), centre);
        avatar_ms = (F32)((LLTimer::getTotalSeconds() - face_start) * 1000.0);
    }
    copyTileTexels(face.rect, layerCopyRect(face.layerRectValid, face.layerRect, avatars.rect, face.rect.size));
    if (face.holdsAvatar && !avatars.rect.empty())
    {
        avatar_ms += drawAvatars(lamp, f, avatars, centre, radius);
        face.lastAvatarMs = smoothedCostMs(face.lastAvatarMs, avatar_ms);
    }
    face.layerRect = avatars.rect;
    face.layerRectValid = true;
    face.avatarInLayer = face.holdsAvatar;
    face.layerFrame = LLFrameTimer::getFrameCount();
    const F32 ms = (F32)((LLTimer::getTotalSeconds() - cpu_start) * 1000.0);
    mLastFrameRenderMs += ms;
    frameLedger().add(ms);
    ++mLastFrameRenders;
    ++mLayerRenders;
    mCauses.add(Cause::Avatar);
}

void LLPointShadows::unpinObject(const Id128& id)
{
    LLUUID uuid;
    memcpy(uuid.mData, &id, sizeof(id));
    LLLocalShadows::lowerPin(gObjectList.findObject(uuid));
}

void LLPointShadows::forgetPinned(const LLDrawable* drawable)
{
    const LLViewerObject* object = drawable->getVObj();
    if (!object || LLApp::isExiting())
    {
        return;
    }
    const uint32_t n = mPins.forget(LLLocalShadows::idOf(object->getID()));
    if (n == 0)
    {
        return;
    }
    LLViewerObject* live = gObjectList.findObject(object->getID());
    for (uint32_t i = 0; live && i < n; ++i)
    {
        LLLocalShadows::lowerPin(live);
    }
}

void LLPointShadows::unpinFace(U32 key)
{
    const std::vector<Id128> held = mPins.releaseTile(key);
    if (LLApp::isExiting())
    {
        return;
    }
    for (const Id128& id : held)
    {
        unpinObject(id);
    }
}

void LLPointShadows::pinDrawable(LLDrawable* drawable)
{
    LLViewerObject* object = drawable && !drawable->isDead() ? drawable->getVObj().get() : nullptr;
    if (!object || object->isDead() || drawable == LLPipeline::RenderSpotLight)
    {
        return; // the emitter is not drawn into its own faces
    }
    LLVOVolume* volume = drawable->getVOVolume();
    LLVOTree* tree = volume ? nullptr : dynamic_cast<LLVOTree*>(object);
    if (!volume && !tree)
    {
        return;
    }
    if (volume && (volume->isRiggedMesh() || volume->getAvatar() || volume->isAttachment() || volume->isHUDAttachment() || !LLLocalShadows::volumeCasts(volume)))
    {
        return;
    }
    Aabb box;
    if (!LLShadowDirtyFeed::extentsOf(drawable, box) || !intersects(mRenderVolume, box))
    {
        return;
    }
    if (mPins.touch(mRenderingKey, LLLocalShadows::idOf(object->getID())))
    {
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

// The LOD pin (the projectors' rule): every volume the face is about to draw, inside the face's volume, is held at full detail, so a camera
// that moves changes no face's content.
void LLPointShadows::pinCasters(LLCullResult& result)
{
    if (!mRenderingFace || !on_main_thread())
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

// D4: the prim that emits the light casts nothing into its own faces (it is the light), the rest of its linkset does. The pipeline's
// stateSort skip of RenderSpotLight only reaches a drawable drawn on its own; a prim in a spatial group is drawn from the group's
// batches, which hold its faces between its neighbours' (the same texture and state are batched). So its faces' index ranges are left out
// of the draws (LLVertexBuffer::sSkipRanges), read after the render's sort, when the group's buffers are final for this render.
void LLPointShadows::skipEmitter()
{
    if (!mRenderingFace || LLPipeline::sLocalShadowPass != LLPipeline::LOCAL_SHADOW_STATIC || !on_main_thread())
    {
        return;
    }
    mSkip.clear();
    LLDrawable* emitter = LLPipeline::RenderSpotLight;
    if (emitter && !emitter->isDead())
    {
        for (S32 i = 0; i < emitter->getNumFaces(); ++i)
        {
            const LLFace* face = emitter->getFace(i);
            const LLVertexBuffer* buffer = face ? face->getVertexBuffer() : nullptr;
            if (!buffer || face->getIndicesStart() < 0 || face->getIndicesCount() == 0)
            {
                continue;
            }
            LLVertexBuffer::SkipRange r;
            r.buffer = buffer;
            r.begin = (U32)face->getIndicesStart();
            r.end = r.begin + face->getIndicesCount();
            mSkip.push_back(r);
        }
    }
    LLVertexBuffer::sSkipRanges = mSkip.empty() ? nullptr : &mSkip;
}

void LLPointShadows::prepare(LLCamera& camera, const std::vector<LLLocalShadowCaster>& casters, std::vector<StaticItem>& static_items,
                             std::vector<DynamicItem>& dynamic_items)
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
    frameLedger().begin(frame);
    mCasters = &casters; // until endFrame

    // the scene's changes since the last frame (the feed swaps them once a frame, for both pools)
    const std::vector<DirtyBox>& boxes = LLShadowDirtyFeed::beginFrame(frame);
    const bool everything = LLShadowDirtyFeed::everything();

    updateLightSet(camera);

    // The lamps' poses, this frame's: from the lamp alone (its centre and radius, never its rotation). The camera's POSITION enters here and
    // nowhere else: its distance to the lamp's lit sphere is the valve's band (which lamps shrink first while the atlas is over its budget)
    // and the order of a fill, never what a face holds nor, while the atlas has room, its size.
    const U32 shifts = LLShadowDirtyFeed::shifts();
    const bool shifted = shifts != mShiftsSeen;
    mShiftsSeen = shifts;
    const LLVector3 cam_origin = camera.getOrigin();
    std::vector<const LLViewerObject*> lamp_roots;
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        if (LLViewerObject* object = lamp.drawable.notNull() ? lamp.drawable->getVObj().get() : nullptr)
        {
            LLViewerObject* root = object->getRootEdit();
            lamp_roots.push_back(root ? root : object);
        }
        LLVOVolume* volume = lamp.drawable.notNull() ? lamp.drawable->getVOVolume() : nullptr;
        const F32 radius = volume ? volume->getLightRadius() : 0.f;
        if (lamp.off || !(radius > 0.f) || !std::isfinite(radius))
        {
            if (!lamp.off)
            {
                lamp.prevPoseValid = false;
                lamp.volumesValid = false;
            }
            continue; // S2: a lamp that is off keeps the pose and the volumes it had: its faces still collect the scene's changes
        }
        lamp.centre = lamp.drawable->getPositionAgent();
        lamp.radius = radius;
        lamp.curPose = lamp_pose(lamp.drawable, lamp.centre, radius);
        lamp.lampMoving = lamp.prevPoseValid && poseChanged(lamp.prevPose, lamp.curPose);
        lamp.prevPose = lamp.curPose;
        lamp.prevPoseValid = true;
        lamp.poseStale = lamp.drawnValid && poseChanged(lamp.drawnPose, lamp.curPose);
        if (shifted || !lamp.volumesValid || poseChanged(lamp.volumePose, lamp.curPose))
        {
            lamp.base = faceBaseSize(radius); // the faces' size from the lamp alone: only its reach changes it
            lamp.volumePose = lamp.curPose;
            lamp.volumesValid = true;
            for (Face& face : lamp.faces)
            {
                face.volumeN = 0; // rebuilt below, at the face's size
            }
        }
        lamp.camDist = lampDistance(cam_origin.mV, lamp.centre.mV, faceFar(radius));
        lamp.band = nextBand(lamp.band, lamp.camDist);
    }
    LLShadowDirtyFeed::setLampRoots(LLShadowDirtyFeed::Owner::Points, lamp_roots);

    layout(gFrameIntervalSeconds.value());
    // each face's volume at its size (the guard's width in metres follows it), from the lamp's current pose
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        if (lamp.off || !lamp.volumesValid)
        {
            continue;
        }
        for (U32 f = 0; f < kFaces; ++f)
        {
            Face& face = lamp.faces[f];
            const U32 n = face.placed ? face.rect.size : face.want;
            if (n >= kFaceMin && face.volumeN != n)
            {
                face.volume = facePyramid(lamp.centre.mV, (int)f, (float)faceTanHalf(n), kNearM, faceFar(lamp.radius));
                face.volumeN = n;
            }
        }
    }

    // which drawn faces a change touched (the first cause that made a face stale is kept until it is drawn)
    if (!boxes.empty() || everything)
    {
        std::vector<TileVolume> volumes;
        for (const auto& entry : mLamps)
        {
            for (U32 f = 0; f < kFaces; ++f)
            {
                const Face& face = entry.second.faces[f];
                if (face.placed && face.drawnFrame != 0 && face.volumeN != 0 && !face.pending)
                {
                    TileVolume tv;
                    tv.key = faceKey(entry.first, f);
                    tv.pyramid = face.volume;
                    volumes.push_back(tv);
                }
            }
        }
        mIndex.rebuild(volumes);
        std::vector<Hit> hits;
        mIndex.apply(boxes, everything, hits);
        for (const Hit& hit : hits)
        {
            auto it = mLamps.find(lampOf(hit.key));
            if (it == mLamps.end())
            {
                continue;
            }
            Face& face = it->second.faces[faceOf(hit.key)];
            if (!face.pending)
            {
                face.pending = true;
                face.pendingCause = hit.cause;
                face.dirtyFrame = frame;
                face.dirtyTime = LLTimer::getTotalSeconds();
            }
        }
    }

    // The avatars, by position alone: every avatar that casts is paired with every face whose volume its extents touch; the camera's
    // POSITION only orders the faces. The own avatar's faces run every frame under the kOwnEveryFrameLamps lamps that light it most (P2).
    const F32 camera_pos[3] = { cam_origin.mV[VX], cam_origin.mV[VY], cam_origin.mV[VZ] };
    std::vector<Aabb> avatar_boxes, own_boxes;
    for (const LLLocalShadowCaster& c : casters)
    {
        (c.own ? own_boxes : avatar_boxes).push_back(c.box);
    }
    std::vector<std::pair<U32, F32>> own_lamps; // (lamp id, irradiance at the own avatar) of the lamps whose faces hold it
    for (auto& entry : mLamps)
    {
        entry.second.ownEvery = false;
    }
    if (!own_boxes.empty())
    {
        const Aabb& own = own_boxes.front();
        const LLVector3 own_mid((own.lo[0] + own.hi[0]) * 0.5f, (own.lo[1] + own.hi[1]) * 0.5f, (own.lo[2] + own.hi[2]) * 0.5f);
        for (auto& entry : mLamps)
        {
            Lamp& lamp = entry.second;
            if (lamp.off || !lamp.volumesValid)
            {
                continue;
            }
            bool touches = false;
            for (const Face& face : lamp.faces)
            {
                touches = touches || (face.volumeN != 0 && intersects(face.volume, own));
            }
            if (touches)
            {
                LLVOVolume* volume = lamp.drawable->getVOVolume();
                const F32 e = volume ? lampIrradiance(luminance(volume->getLightLinearColor()), (own_mid - lamp.centre).length(),
                                                      faceFar(lamp.radius), volume->getLightFalloff(kDeferredFalloff))
                                     : 0.f;
                own_lamps.emplace_back(lamp.id, e);
            }
        }
    }
    std::vector<uint32_t> own_rank;
    const std::vector<bool> own_every = ownEveryFrame(own_lamps, &own_rank);
    std::map<U32, U32> own_rank_of;
    for (size_t i = 0; i < own_lamps.size(); ++i)
    {
        auto it = mLamps.find(own_lamps[i].first);
        if (it != mLamps.end())
        {
            it->second.ownEvery = own_every[i];
        }
        own_rank_of[own_lamps[i].first] = own_rank[i];
    }

    mLampsWithoutFaces = 0;
    mPendingFaces = 0;
    mAvatarFaces = 0;
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        lamp.wholeItem = false;
        if (!lampDrawn(lamp))
        {
            ++mLampsWithoutFaces;
        }
        if (lamp.off || !lamp.volumesValid)
        {
            continue; // S2: a lamp that is off is not drawn: its changes wait until it is on again
        }
        bool all_placed = true, any_never = false;
        for (const Face& face : lamp.faces)
        {
            all_placed = all_placed && face.placed;
            any_never = any_never || face.drawnFrame == 0 || !lamp.drawnValid;
        }
        if (!all_placed)
        {
            continue; // waits for room: a lamp is drawn whole
        }
        // the avatars that can touch the lamp's faces (BoxFilter over its faces' bounds), then each face's
        BoxFilter filter;
        for (const Face& face : lamp.faces)
        {
            filter.add(face.volume.bounds);
        }
        std::vector<Aabb> run_own, run_avatars;
        filter.select(own_boxes, run_own);
        filter.select(avatar_boxes, run_avatars);
        U32 avatars_total = 0;
        bool any_avatar = false;
        F32 whole_ms = 0.f;
        U32 oldest = frame;
        for (U32 f = 0; f < kFaces; ++f)
        {
            Face& face = lamp.faces[f];
            face.layerOnly = false;
            face.avatarCount = 0;
            face.avatarDist = std::numeric_limits<F32>::infinity();
            bool holds_own = false;
            for (const Aabb& b : run_own)
            {
                if (intersects(face.volume, b))
                {
                    ++face.avatarCount;
                    // the own avatar's face is the one its box's centre falls in; the others its box overlaps go by deadline
                    const float d[3] = { 0.5f * (b.lo[0] + b.hi[0]) - lamp.centre.mV[VX], 0.5f * (b.lo[1] + b.hi[1]) - lamp.centre.mV[VY],
                                         0.5f * (b.lo[2] + b.hi[2]) - lamp.centre.mV[VZ] };
                    float xyw[3];
                    if (pickFace(d, xyw) == (int)f)
                    {
                        holds_own = true;
                    }
                    else
                    {
                        face.avatarDist = std::min(face.avatarDist, pointBoxDistance(camera_pos, b));
                    }
                }
            }
            for (const Aabb& b : run_avatars)
            {
                if (intersects(face.volume, b))
                {
                    ++face.avatarCount;
                    face.avatarDist = std::min(face.avatarDist, pointBoxDistance(camera_pos, b));
                }
            }
            if (holds_own && !lamp.ownEvery)
            {
                // under a fainter lamp the own avatar's face goes by the near avatars' deadline, the strongest first
                const auto rank = own_rank_of.find(lamp.id);
                face.avatarDist = std::min(face.avatarDist, 0.001f * F32(rank != own_rank_of.end() ? rank->second : 0u));
            }
            if (face.avatarCount == 0)
            {
                face.avatarDist = 0.f; // the avatar left: this face only restores L from S, which is cheap and first
            }
            face.holdsAvatar = face.avatarCount > 0;
            mAvatarFaces += face.holdsAvatar ? 1 : 0;
            mPendingFaces += face.pending ? 1 : 0;
            avatars_total += face.avatarCount;
            any_avatar = any_avatar || face.holdsAvatar;
            whole_ms += face.lastCpuMs + (face.holdsAvatar ? face.lastAvatarMs : 0.f);
            if (face.drawnFrame != 0)
            {
                oldest = std::min(oldest, face.drawnFrame);
            }
            face.verdictCause = Cause::Other;
            if (any_never || lamp.poseStale || lamp.lampMoving)
            {
                continue; // the whole lamp is drawn below
            }
            TileFacts facts;
            facts.pending = face.pending;
            facts.pending_cause = face.pendingCause;
            facts.lowres = resizeDue(face);
            facts.holds_avatar = face.holdsAvatar;
            facts.avatar_in_layer = face.avatarInLayer;
            const TileVerdict verdict = classifyTile(facts);
            if (!verdict.needs_render && !verdict.needs_layer)
            {
                continue;
            }
            face.verdictCause = verdict.cause;
            const U32 key = faceKey(lamp.id, f);
            if (verdict.needs_layer)
            {
                DynamicItem item;
                item.key = key;
                item.pool = kPoolPoints;
                item.avatar = true;
                item.age_frames = frame - face.layerFrame;
                item.cam_dist = face.avatarDist;
                item.last_cpu_ms = face.holdsAvatar ? face.lastAvatarMs : 0.f;
                item.avatar_draws = face.avatarCount;
                item.layer = true;
                item.own = holds_own && lamp.ownEvery;
                face.layerOnly = true;
                dynamic_items.push_back(item);
            }
            else
            {
                StaticItem item;
                item.key = key;
                item.pool = kPoolPoints;
                item.pending = true;
                item.cam_dist = lamp.camDist;
                item.last_cpu_ms = face.lastCpuMs + (face.holdsAvatar ? face.lastAvatarMs : 0.f);
                item.avatar_draws = face.holdsAvatar ? std::min<U32>(face.avatarCount, kAvatarDrawsMax) : 0u;
                item.age_frames = face.dirtyFrame && frame >= face.dirtyFrame ? frame - face.dirtyFrame
                                  : (face.drawnFrame && frame >= face.drawnFrame ? frame - face.drawnFrame : 0u);
                static_items.push_back(item);
            }
        }
        if (any_never || lamp.poseStale || lamp.lampMoving)
        {
            // the whole lamp, from this frame's pose: a first fill and a moved lamp's redraw are the static schedule's (nearest first, then
            // oldest), a lamp that moves every frame the dynamic one's
            lamp.wholeItem = true;
            const U32 key = faceKey(lamp.id, kLampItem);
            if (lamp.lampMoving && !any_never)
            {
                DynamicItem item;
                item.key = key;
                item.pool = kPoolPoints;
                item.avatar = any_avatar;
                item.age_frames = frame >= oldest ? frame - oldest : 0u;
                item.cam_dist = lamp.camDist;
                item.last_cpu_ms = whole_ms;
                item.avatar_draws = std::min<U32>(avatars_total, kAvatarDrawsMax);
                dynamic_items.push_back(item);
            }
            else
            {
                StaticItem item;
                item.key = key;
                item.pool = kPoolPoints;
                item.never_drawn = any_never;
                item.pending = true;
                item.cam_dist = lamp.camDist;
                item.last_cpu_ms = whole_ms;
                item.avatar_draws = std::min<U32>(avatars_total, kAvatarDrawsMax);
                item.age_frames = any_never ? 0u : (frame >= oldest ? frame - oldest : 0u);
                static_items.push_back(item);
            }
        }
    }
}

void LLPointShadows::beginRenders()
{
    if (active())
    {
        unbind_from_units(mAtlasL); // L is a lighting unit's texture until the next lookup binds it again: not while it is written
    }
}

bool LLPointShadows::renderPick(U32 key, bool /*dynamic: a lamp moving every frame is drawn whole either way*/, F32& ms)
{
    ms = 0.f;
    auto it = mLamps.find(lampOf(key));
    if (!active() || it == mLamps.end())
    {
        return false;
    }
    Lamp& lamp = it->second;
    const U32 f = faceOf(key);
    const F32 before = mLastFrameRenderMs;
    if (f == kLampItem)
    {
        if (!lamp.wholeItem)
        {
            return false;
        }
        renderLamp(lamp, lampDrawn(lamp) ? Cause::Lamp : Cause::New);
    }
    else if (f < kFaces)
    {
        Face& face = lamp.faces[f];
        if (!face.placed || !lamp.drawnValid)
        {
            return false;
        }
        if (face.layerOnly)
        {
            renderLayer(lamp, (int)f);
        }
        else
        {
            // a pick whose only cause is a growth whose block an earlier resize of this frame took: nothing in the face changed; it grows
            // once a block is free (its avatar layer still runs)
            if (face.verdictCause == Cause::Lowres && !resizeDue(face))
            {
                if (face.holdsAvatar || face.avatarInLayer)
                {
                    renderLayer(lamp, (int)f);
                }
            }
            else
            {
                // one face from the pose the lamp's faces hold, so the set stays one pose
                renderFace(lamp, (int)f, gAgent.getPosAgentFromGlobal(lamp.drawnCentre), lamp.drawnRadius, face.verdictCause);
            }
        }
    }
    else
    {
        return false;
    }
    ms = mLastFrameRenderMs - before;
    return true;
}

void LLPointShadows::endFrame()
{
    mCasters = nullptr; // no avatar pointer outlives the frame
}

// The lookup's uniforms of one lamp: xyz its centre in this frame's eye space (of the pose its faces were drawn with), w its faces' far plane
// (0: no complete set of drawn faces, lit without a lookup); faces: the six packed rects (llpointpool::packFaceRect), two unused.
void LLPointShadows::lookupUniforms(const LLDrawable* light, F32 lamp4[4], F32 faces[8]) const
{
    for (int i = 0; i < 4; ++i)
    {
        lamp4[i] = 0.f;
    }
    for (int i = 0; i < 8; ++i)
    {
        faces[i] = 0.f;
    }
    if (!active())
    {
        return;
    }
    auto id = mIdOf.find(light);
    if (id == mIdOf.end())
    {
        return;
    }
    auto it = mLamps.find(id->second);
    if (it == mLamps.end() || !lampDrawn(it->second))
    {
        return;
    }
    const Lamp& lamp = it->second;
    for (U32 f = 0; f < kFaces; ++f)
    {
        faces[f] = packFaceRect(lamp.faces[f].rect);
        if (faces[f] == 0.f)
        {
            return; // never: a face of the point atlas packs (lamp4 stays 0: lit)
        }
    }
    const LLVector3 c = gAgent.getPosAgentFromGlobal(lamp.drawnCentre);
    const glm::vec4 eye = get_current_modelview() * glm::vec4(c.mV[VX], c.mV[VY], c.mV[VZ], 1.f);
    lamp4[0] = eye.x;
    lamp4[1] = eye.y;
    lamp4[2] = eye.z;
    lamp4[3] = faceFar(lamp.drawnRadius);
}

void LLPointShadows::setupLight(LLGLSLShader& shader, LLDrawable* light)
{
    F32 lamp[4], faces[8];
    lookupUniforms(light, lamp, faces);
    shader.uniform4fv(LLShaderMgr::POINT_SHADOW_LAMP, 1, lamp);
    shader.uniform4fv(LLShaderMgr::POINT_SHADOW_FACES, 2, faces);
}

void LLPointShadows::setupBatch(LLGLSLShader& shader, const std::vector<LLDrawable*>& lights)
{
    const U32 count = (U32)llmin(lights.size(), (size_t)LL_DEFERRED_MULTI_LIGHT_COUNT);
    F32 lamps[LL_DEFERRED_MULTI_LIGHT_COUNT * 4];
    F32 faces[LL_DEFERRED_MULTI_LIGHT_COUNT * 8];
    for (U32 i = 0; i < count; ++i)
    {
        lookupUniforms(lights[i], lamps + 4 * i, faces + 8 * i);
    }
    if (count > 0)
    {
        shader.uniform4fv(LLShaderMgr::POINT_SHADOW_LAMP, count, lamps);
        shader.uniform4fv(LLShaderMgr::POINT_SHADOW_FACES, 2 * count, faces);
    }
}

void LLPointShadows::bindAtlas(LLGLSLShader& shader)
{
    if (!active())
    {
        return;
    }
    const S32 channel = shader.enableTexture(LLShaderMgr::POINT_SHADOW_ATLAS, LLTexUnit::TT_TEXTURE);
    if (channel < 0)
    {
        return;
    }
    gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, mAtlasL);
    // view-space directions to agent axes: the rotation of this frame's view, transposed (the view is rigid)
    const glm::mat3 to_agent = glm::transpose(glm::mat3(get_current_modelview()));
    shader.uniformMatrix3fv(LLShaderMgr::POINT_SHADOW_VIEW_TO_AGENT, 1, GL_FALSE, glm::value_ptr(to_agent));
    // the emitter: the point lamp's base (4.5 cm) times RenderShadowLightSize (the Shadow softness), whatever its prim
    shader.uniform2f(LLShaderMgr::POINT_SHADOW_PARAMS, emitterExtent(kPointLamp, LLPipeline::RenderShadowLightSize), 1.f / (F32)mAtlasSize);
}

LLPointShadows::Stats LLPointShadows::getStats() const
{
    Stats st;
    st.present = active();
    st.level = mLevel.level();
    st.atlasSize = mAtlasSize;
    st.lamps = (U32)mLamps.size();
    st.lampsWithoutFaces = mLampsWithoutFaces;
    st.faceRenders = mFaceRenders;
    st.lampRenders = mLampRenders;
    st.layerRenders = mLayerRenders;
    st.resizes = mResizes;
    st.compactions = mCompactions;
    st.costEmaMs = mCostEmaMs;
    st.lastFrameRenderMs = mLastFrameRenderMs;
    st.lastFrameRenders = mLastFrameRenders;
    st.pendingFaces = mPendingFaces;
    st.avatarFaces = mAvatarFaces;
    st.pinnedObjects = (U32)mPins.pinnedObjects();
    st.causes = mCauses;
    return st;
}
