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
#include "llgl.h"
#include "llglslshader.h"
#include "llrendertarget.h"
#include "llshadermgr.h"
#include "llspatialpartition.h"
#include "llstochastichistory.h"
#include "llthread.h"
#include "lltimer.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llvoavatar.h"
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

    // What the pool makes of a light. On: a point light it shadows (a light that is not a projector, with a radius whose lit size reaches
    // past the faces' near plane and a colour, on a prim that is not worn: D11). Off: such a lamp switched off (its light parameters are
    // not in use, so the radius reads 0), turned black or given a radius too small to light anything past the near plane, which the draw
    // loop skips or lights nothing with: its faces are held for a while (S2). Gone: not a lamp the pool shadows.
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
        if (!(radius > 0.f) || !(faceFar(radius) > kNearM) || volume->getLightLinearColor().magVecSquared() < 0.001f)
        {
            return LampState::Off; // a lit size at or below the near plane would draw faces whose far plane is before their near one
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
    // crossing shifts agent space without moving the lamp)
    Pose lamp_pose(LLDrawable* drawable, const LLVector3& centre)
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
        return lampPose(handle, l);
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

    // a lamp is drawn whole or face by face once all six of its faces have room (never read with faces missing)
    template <class F>
    bool all_placed(const F (&faces)[kFaces])
    {
        for (const F& face : faces)
        {
            if (!face.placed)
            {
                return false;
            }
        }
        return true;
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
    // GL 4.6 (the floor of new work since 2026-11-15; the faces' copies are glCopyImageSubData, GL 4.3 core): an older context lights the
    // point lights without their shadows, the method and every other pass unchanged
    return LLProjectorShadows::wanted() && LLPipeline::RenderShadowPointLights && gGLManager.mGLVersion >= 4.59f;
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
    // D3: 4096 from 3 GB of video memory, 2048 below, within GL_MAX_TEXTURE_SIZE and what the face rects pack (llpointpool::pointAtlasTier);
    // a failed allocation tries the one below, and the smallest failing leaves the lamps without a shadow (never another method)
    U32 size = pointAtlasTier(gGLManager.mVRAM, (U32)llmax(gGLManager.mGLMaxTextureSize, 0));
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
}

void LLPointShadows::forget(const LLDrawable* drawable)
{
    if (!drawable || !on_main_thread())
    {
        return;
    }
    LLLocalShadows::forgetPinned(mPins, drawable);
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

// A lamp gives its faces up (a nearer lamp takes the room, evictFor): their rects are freed, their pins given back, and the lamp waits for
// room like an arrival, lit without a shadow until it is drawn again. The faces keep their cost predictions.
void LLPointShadows::resetFaces(Lamp& lamp)
{
    for (U32 f = 0; f < kFaces; ++f)
    {
        Face& face = lamp.faces[f];
        if (face.placed && active())
        {
            mBuddy.free(face.rect);
        }
        unpinFace(faceKey(lamp.id, f));
        Face fresh;
        fresh.lastCpuMs = face.lastCpuMs;
        fresh.lastAvatarMs = face.lastAvatarMs;
        face = fresh;
    }
    lamp.drawnValid = false;
    lamp.volumesValid = false; // rebuilt at the faces' sizes once they are placed again
}

// every face of the lamp is placed and drawn for its set: the faces the lookup can read (one pose and one radius)
bool LLPointShadows::lampDrawn(const Lamp& lamp) const
{
    if (!lamp.drawnValid || lamp.set == 0)
    {
        return false;
    }
    for (const Face& face : lamp.faces)
    {
        if (!face.placed || face.drawnFrame == 0 || face.set != lamp.set)
        {
            return false;
        }
    }
    return true;
}

// A new set of faces from this frame's pose and face radius: the lookup reads the lamp again once all six are drawn for it.
void LLPointShadows::startSet(Lamp& lamp)
{
    lamp.set = lamp.set + 1 ? lamp.set + 1 : 1; // 0 is "none"
    lamp.setPose = lamp.curPose;
    lamp.setCentre = gAgent.getPosGlobalFromAgent(lamp.centre);
    lamp.setRadius = lamp.faceRadius;
    lamp.setStale = false;
    lamp.drawnValid = false;
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
    if (holdOffFrame(lamp.off, lamp.offSince, now, inRange(true, camera_distance(lamp.drawable, cam_pos, lamp.radius), reach)))
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
                face.lastCpuMs = coldItemMs(mCostEmaMs); // a never-drawn face's prediction: the measured average of a face render
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

// a whole face rect between the atlases, bit for bit (both are D32F of one size)
void LLPointShadows::copyTile(U32 from_tex, const Rect& src, U32 to_tex, const Rect& dst)
{
    llassert(src.size == dst.size);
    LLLocalShadowGL::copyTexels(from_tex, (S32)src.x, (S32)src.y, to_tex, (S32)dst.x, (S32)dst.y, (S32)src.size, (S32)src.size);
}

// S -> L over a rectangle of a face (C3)
void LLPointShadows::copyTileTexels(const Rect& tile, const TexelRect& texels)
{
    if (texels.empty())
    {
        return;
    }
    const S32 x = (S32)tile.x + texels.x0, y = (S32)tile.y + texels.y0;
    LLLocalShadowGL::copyTexels(mAtlasS, x, y, mAtlasL, x, y, texels.x1 - texels.x0, texels.y1 - texels.y0);
}

// A compaction (the projectors' rule, on the shared llprojectorpool::planRepack): every placed face at its own size in a fresh layout
// (descending sizes pack a buddy atlas without a gap), the drawn ones moved by bit-exact copies S[old] -> L[new], then L -> S in full.
// Runs when an arriving lamp's faces find no block although the free texels would hold them, or a growth has found none for a while.
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
    return true;
}

// The atlas is full even at the floor (every pending shrink drawn would still leave the waiting lamps without room): the waiting lamps
// nearest the camera's position take the faces of the placed lamps farthest from it, one farther by the reach's hysteresis
// (llpointpool::evicts), the farthest first. An evicted lamp waits like any arrival (lit without a shadow) and takes faces again when room
// comes back or when it is the nearer one. `waiting` keeps the lamps that are still without room.
void LLPointShadows::evictFor(std::vector<Lamp*>& waiting)
{
    std::sort(waiting.begin(), waiting.end(), [](const Lamp* a, const Lamp* b) {
        return a->camDist != b->camDist ? sortDist(a->camDist) < sortDist(b->camDist) : a->id < b->id;
    });
    std::vector<Lamp*> placed;
    for (auto& entry : mLamps)
    {
        if (all_placed(entry.second.faces))
        {
            placed.push_back(&entry.second);
        }
    }
    std::sort(placed.begin(), placed.end(), [](const Lamp* a, const Lamp* b) {
        return a->camDist != b->camDist ? sortDist(a->camDist) > sortDist(b->camDist) : a->id > b->id;
    });
    auto place = [this](Lamp& lamp) {
        Rect got[kFaces];
        U32 done = 0;
        for (U32 f = 0; f < kFaces; ++f)
        {
            if (lamp.faces[f].placed)
            {
                continue;
            }
            if (!mBuddy.alloc(lamp.faces[f].want, got[f]))
            {
                for (U32 g = 0; g < f; ++g)
                {
                    if (done & (1u << g))
                    {
                        mBuddy.free(got[g]);
                    }
                }
                return false;
            }
            done |= 1u << f;
        }
        for (U32 f = 0; f < kFaces; ++f)
        {
            if (done & (1u << f))
            {
                lamp.faces[f].rect = got[f];
                lamp.faces[f].placed = true;
            }
        }
        return true;
    };
    auto need = [](const Lamp& lamp) {
        uint64_t sum = 0;
        for (const Face& face : lamp.faces)
        {
            sum += face.placed ? 0 : uint64_t(face.want) * face.want;
        }
        return sum;
    };
    std::vector<Lamp*> left;
    size_t next = 0;
    for (Lamp* lamp : waiting)
    {
        bool done = place(*lamp);
        while (!done && next < placed.size() && evicts(lamp->camDist, placed[next]->camDist))
        {
            resetFaces(*placed[next++]);
            done = place(*lamp) || (mBuddy.freeTexels() >= need(*lamp) && compact() && place(*lamp));
        }
        if (!done)
        {
            left.push_back(lamp); // the lamps farther than this one evict no more than it did
        }
    }
    waiting.swap(left);
}

// The faces' sizes and placement (the projectors' rules, llprojectorshadows.cpp layout): the valve's pressure from the faces' lamp sizes and
// the lamps' distance bands (one band per lamp, floor 128), each face's wanted size, and the arriving lamps placed at theirs, all six faces
// or none (an arrival that does not fit whole waits for the shrinks the renders draw; when not even those make room, the nearer lamps take
// the farther lamps' faces: evictFor). A placed face is never resized here: its own render draws the new size before it is swapped in.
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
                // all of the lamp's faces or none: faces placed alone would hold space nobody reads
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
                // no render frees the room: the atlas cannot hold every lamp even at the floor
                evictFor(waiting);
                if (!waiting.empty())
                {
                    LL_WARNS_ONCE("PointShadows") << "point light faces do not fit the atlas even at the smallest size; the lamps farthest "
                                                     "from the camera's position have no shadow" << LL_ENDL;
                }
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

// One face from a pose (the lamp's centre and face radius): the fixed objects into S (one cull and one submit with the face's frustum, the
// casters drawn from both sides, the emitter prim left out by identity), then S -> L for the face, then the avatars in its light over it in
// L. A face whose wanted size differs is drawn at the new size into a new rect (draw-then-swap). The face then holds the lamp's set.
// False when nothing was drawn.
bool LLPointShadows::renderFace(Lamp& lamp, int f, const LLVector3& centre, F32 radius, Cause cause)
{
    Face& face = lamp.faces[f];
    const F64 cpu_start = LLTimer::getTotalSeconds();
    if (resizeDue(face))
    {
        const Rect old = face.rect;
        mBuddy.free(old);
        Rect nr;
        if (!mBuddy.alloc(face.want, nr))
        {
            llassert(false); // resizeDue found the block
            if (!mBuddy.alloc(old.size, nr))
            {
                face.placed = false;
                face.drawnFrame = 0;
                return false;
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
    LLLocalShadows::unpinObjects(mPins.endRender(key));
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
    face.set = lamp.set;
    face.pending = false;
    face.dirtyFrame = 0;
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
    return true;
}

// A whole lamp: a new set of its six faces drawn from this frame's pose and face radius (a lamp that moved, or one that moves while it has
// no set), which the lookup then reads; until this render the lookup reads the faces of the set they hold, so no lookup mixes two poses.
bool LLPointShadows::renderLamp(Lamp& lamp, Cause cause)
{
    if (!(lamp.faceRadius > 0.f))
    {
        return false;
    }
    if (!all_placed(lamp.faces))
    {
        return false; // layout places a lamp's faces all or none
    }
    startSet(lamp);
    bool all = true;
    for (U32 f = 0; f < kFaces; ++f)
    {
        all = renderFace(lamp, (int)f, lamp.centre, lamp.setRadius, cause) && all;
    }
    lamp.drawnValid = all; // a face that could not be drawn is drawn by the next frame's fill items
    return true;
}

// The avatars a face draws, and the tile rectangle they cover (the projectors' rule, LLLocalShadows::tileAvatars).
LLPointShadows::FaceAvatars LLPointShadows::faceAvatars(const Face& face, const float clip[16], const LLVector3& centre) const
{
    FaceAvatars out;
    if (mCasters)
    {
        LLLocalShadows::tileAvatars(*mCasters, face.volume, clip, centre, face.rect.size, out.avatars, out.rect);
    }
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
// the light now are drawn over it, from the set the face holds. False when nothing was drawn (no set).
bool LLPointShadows::renderLayer(Lamp& lamp, int f)
{
    if (!lamp.drawnValid)
    {
        return false;
    }
    Face& face = lamp.faces[f];
    const F64 cpu_start = LLTimer::getTotalSeconds();
    const LLVector3 centre = gAgent.getPosAgentFromGlobal(lamp.setCentre);
    const F32 radius = lamp.setRadius;
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
    return true;
}

void LLPointShadows::unpinFace(U32 key)
{
    LLLocalShadows::unpinObjects(mPins.releaseTile(key));
}

// The LOD pin (the projectors' rule, LLLocalShadows::pinCasters): every volume the face is about to draw, inside the face's volume, is held
// at full detail, so a camera that moves changes no face's content. The emitter is not drawn into its own faces: it is not held for them.
void LLPointShadows::pinCasters(LLCullResult& result)
{
    if (!mRenderingFace || !on_main_thread())
    {
        return;
    }
    LLLocalShadows::pinCasters(result, mPins, mRenderingKey, mRenderVolume, LLPipeline::RenderSpotLight);
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
    frameLedger().begin(frame);
    mCasters = &casters; // until endFrame

    // the scene's changes since the last frame (the feed swaps them once a frame, for both pools)
    const std::vector<DirtyBox>& boxes = LLShadowDirtyFeed::beginFrame(frame);
    const bool everything = LLShadowDirtyFeed::everything();

    updateLightSet(camera);

    // The lamps' poses, this frame's: from the lamp alone (its centre and the radius its faces are drawn for, never its rotation). The
    // camera's POSITION enters here and nowhere else: its distance to the lamp's lit sphere is the valve's band (which lamps shrink first
    // while the atlas is over its budget), the order of a fill and of an eviction, never what a face holds nor, while the atlas has room,
    // its size.
    const U32 shifts = LLShadowDirtyFeed::shifts();
    const bool shifted = shifts != mShiftsSeen;
    mShiftsSeen = shifts;
    const LLVector3 cam_origin = camera.getOrigin();
    const F64 now = LLTimer::getTotalSeconds();
    std::vector<const LLViewerObject*> lamp_roots;
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        LLViewerObject* object = lamp.drawable.notNull() ? lamp.drawable->getVObj().get() : nullptr;
        LLViewerObject* root = object ? object->getRootEdit() : nullptr;
        root = root ? root : object;
        LLVOVolume* volume = lamp.drawable.notNull() ? lamp.drawable->getVOVolume() : nullptr;
        const F32 radius = volume ? volume->getLightRadius() : 0.f;
        if (lamp.off || !(radius > 0.f) || !std::isfinite(radius))
        {
            if (root)
            {
                lamp_roots.push_back(root); // S2: a held lamp's faces still collect its linkset's changes
            }
            if (!lamp.off)
            {
                lamp.prevPoseValid = false;
                lamp.volumesValid = false;
            }
            continue; // S2: a lamp that is off keeps the pose and the volumes it had: its faces still collect the scene's changes
        }
        lamp.centre = lamp.drawable->getPositionAgent();
        lamp.radius = radius;
        lamp.curPose = lamp_pose(lamp.drawable, lamp.centre);
        lamp.lampMoving = lamp.prevPoseValid && poseChanged(lamp.prevPose, lamp.curPose);
        lamp.prevPose = lamp.curPose;
        lamp.prevPoseValid = true;
        // the lamp's own linkset is the fixture of its faces: its changes always push (llshadowdirtyfeed). Not while the lamp moves: it is
        // drawn whole from its new pose anyway, and every other lamp leaves a moving linkset out of what it draws, so the pushes of a boat
        // or a car carrying a lamp would only mark faces and tiles that nothing in them changed (the feed skips them as any mover's)
        if (root && !lamp.lampMoving)
        {
            lamp_roots.push_back(root);
        }
        lamp.faceRadius = faceRadius(lamp.set != 0 ? lamp.setRadius : 0.f, radius, lamp.radiusBelowSince, now);
        lamp.setStale = lamp.set != 0 && (poseChanged(lamp.setPose, lamp.curPose) || lamp.faceRadius != lamp.setRadius);
        if (shifted || !lamp.volumesValid || poseChanged(lamp.volumePose, lamp.curPose) || lamp.volumeRadius != lamp.faceRadius)
        {
            lamp.base = faceBaseSize(lamp.faceRadius); // the faces' size from the lamp alone: only its reach changes it
            lamp.volumePose = lamp.curPose;
            lamp.volumeRadius = lamp.faceRadius;
            lamp.volumesValid = true;
            for (Face& face : lamp.faces)
            {
                face.volumeN = 0; // rebuilt below, at the face's size
            }
        }
        lamp.camDist = lampDistance(cam_origin.mV, lamp.centre.mV, faceFar(lamp.faceRadius));
        lamp.band = nextBand(lamp.band, lamp.camDist);
    }
    LLShadowDirtyFeed::setLampRoots(LLShadowDirtyFeed::Owner::Points, lamp_roots);

    layout(gFrameIntervalSeconds.value());
    // each face's volume at its size (the guard's width in metres follows it), from the lamp's current pose and face radius
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
                face.volume = facePyramid(lamp.centre.mV, (int)f, (float)faceTanHalf(n), kNearM, faceFar(lamp.faceRadius));
                face.volumeN = n;
            }
        }
    }

    // which drawn faces a change touched (the first cause that made a face stale is kept until it is drawn); the changes of the lamp's own
    // emitter leave its faces alone (TileVolume::ignore: they never draw it), so a LOD switch of the bulb as the camera walks or a
    // blinker's alpha redraws nothing of its own lamp
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
                    tv.ignore = entry.second.drawable.get();
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
            }
        }
    }

    // The avatars, by position alone: every avatar that casts is paired with every face whose volume its extents touch; the camera's
    // POSITION only orders the faces. The own avatar's faces run every frame under the kOwnEveryFrameLamps lamps that light it most (P2):
    // every face of such a lamp its box meets, within kOwnAvatarDrawsMax avatar draws for all of them.
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
        entry.second.ownRank = 0;
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
                                                      faceFar(lamp.faceRadius), volume->getLightFalloff(kDeferredFalloff))
                                     : 0.f;
                own_lamps.emplace_back(lamp.id, e);
            }
        }
    }
    {
        std::vector<uint32_t> own_rank;
        const std::vector<bool> own_every = ownEveryFrame(own_lamps, &own_rank);
        for (size_t i = 0; i < own_lamps.size(); ++i)
        {
            auto it = mLamps.find(own_lamps[i].first);
            if (it != mLamps.end())
            {
                it->second.ownEvery = own_every[i];
                it->second.ownRank = own_rank[i];
            }
        }
    }

    // Each lamp's faces this frame: the avatars in their light, and, for a lamp whose set is complete and still, each face's verdict. A
    // lamp waits for room until all six faces are placed (it is drawn whole or face by face, never read with faces missing).
    std::vector<OwnFace> own_faces; // the candidates of the own avatar's every-frame faces
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        lamp.wholeItem = false;
        for (Face& face : lamp.faces)
        {
            face.needsRender = face.needsLayer = face.ownEvery = false;
        }
        if (lamp.off || !lamp.volumesValid || !all_placed(lamp.faces))
        {
            continue; // S2: a lamp that is off is not drawn (its changes wait until it is on again); a lamp without room waits
        }
        const bool still = lampDrawn(lamp) && !lamp.setStale && !lamp.lampMoving;
        // the avatars that can touch the lamp's faces (BoxFilter over its faces' bounds), then each face's
        BoxFilter filter;
        for (const Face& face : lamp.faces)
        {
            filter.add(face.volume.bounds);
        }
        std::vector<Aabb> run_own, run_avatars;
        filter.select(own_boxes, run_own);
        filter.select(avatar_boxes, run_avatars);
        for (U32 f = 0; f < kFaces; ++f)
        {
            Face& face = lamp.faces[f];
            face.layerOnly = false;
            face.avatarCount = 0;
            face.holdsOwn = false;
            face.avatarDist = std::numeric_limits<F32>::infinity();
            for (const Aabb& b : run_own)
            {
                if (intersects(face.volume, b))
                {
                    ++face.avatarCount;
                    face.holdsOwn = true;
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
            face.holdsAvatar = face.avatarCount > 0;
            face.verdictCause = Cause::Other;
            if (!still)
            {
                continue; // drawn whole or filled face by face below
            }
            TileFacts facts;
            facts.pending = face.pending;
            facts.pending_cause = face.pendingCause;
            facts.lowres = resizeDue(face);
            facts.holds_avatar = face.holdsAvatar;
            facts.avatar_in_layer = face.avatarInLayer;
            const TileVerdict verdict = classifyTile(facts);
            face.verdictCause = verdict.cause;
            face.needsRender = verdict.needs_render;
            face.needsLayer = verdict.needs_layer;
            if (face.needsLayer && face.holdsOwn && lamp.ownEvery)
            {
                OwnFace c;
                c.rank = lamp.ownRank;
                c.key = faceKey(lamp.id, f);
                c.draws = face.avatarCount;
                own_faces.push_back(c);
            }
        }
    }
    const std::vector<bool> own_within = ownFacesWithinBudget(own_faces);
    for (size_t i = 0; i < own_faces.size(); ++i)
    {
        auto it = mLamps.find(lampOf(own_faces[i].key));
        if (it != mLamps.end())
        {
            it->second.faces[faceOf(own_faces[i].key)].ownEvery = own_within[i];
        }
    }

    // the work list
    for (auto& entry : mLamps)
    {
        Lamp& lamp = entry.second;
        if (lamp.off || !lamp.volumesValid || !all_placed(lamp.faces))
        {
            continue;
        }
        const bool complete = lampDrawn(lamp);
        U32 avatars_total = 0;
        bool any_avatar = false;
        F32 whole_ms = 0.f;
        U32 oldest = frame;
        for (Face& face : lamp.faces)
        {
            if (face.holdsOwn && !face.ownEvery)
            {
                // under a fainter lamp, or past the own faces' draws, the own avatar's face goes by the near avatars' deadline, the
                // strongest lamp first
                face.avatarDist = std::min(face.avatarDist, 0.001f * F32(lamp.ownRank));
            }
            if (face.avatarCount == 0)
            {
                face.avatarDist = 0.f; // the avatar left: this face only restores L from S, which is cheap and first
            }
            avatars_total += face.avatarCount;
            any_avatar = any_avatar || face.holdsAvatar;
            whole_ms += face.lastCpuMs + (face.holdsAvatar ? face.lastAvatarMs : 0.f);
            if (face.drawnFrame != 0)
            {
                oldest = std::min(oldest, face.drawnFrame);
            }
        }
        if (lamp.lampMoving || (complete && lamp.setStale))
        {
            // the whole lamp, a new set from this frame's pose: a lamp that moves every frame is the dynamic schedule's, a lamp that moved
            // (or whose radius grew past its faces) the static one's (oldest first), and a lamp moving before its first set is complete
            // the static one's first fill (nearest first)
            lamp.wholeItem = true;
            const U32 key = faceKey(lamp.id, kLampItem);
            if (lamp.lampMoving && complete)
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
                item.never_drawn = !complete;
                item.pending = true;
                item.cam_dist = lamp.camDist;
                item.last_cpu_ms = whole_ms;
                item.avatar_draws = std::min<U32>(avatars_total, kAvatarDrawsMax);
                item.age_frames = !complete ? 0u : (frame >= oldest ? frame - oldest : 0u);
                static_items.push_back(item);
            }
            continue;
        }
        if (!complete)
        {
            // The first fill (or a fill restarted: the lamp moved and stopped, or its radius grew past the set's, before the set was
            // complete): one static item per face not yet drawn for the set, each a face's cost, nearest lamp first. A lamp's six faces as
            // one item would cost six faces, above the dynamic layer's share and often the whole cap, and take the frame from the avatars.
            // The lookup reads the lamp once its sixth face is drawn.
            if (lamp.set == 0 || lamp.setStale)
            {
                startSet(lamp);
            }
            for (U32 f = 0; f < kFaces; ++f)
            {
                Face& face = lamp.faces[f];
                if (face.drawnFrame != 0 && face.set == lamp.set)
                {
                    continue;
                }
                StaticItem item;
                item.key = faceKey(lamp.id, f);
                item.pool = kPoolPoints;
                item.never_drawn = true;
                item.pending = true;
                item.cam_dist = lamp.camDist;
                item.last_cpu_ms = face.lastCpuMs + (face.holdsAvatar ? face.lastAvatarMs : 0.f);
                item.avatar_draws = face.holdsAvatar ? std::min<U32>(face.avatarCount, kAvatarDrawsMax) : 0u;
                static_items.push_back(item);
            }
            continue;
        }
        for (U32 f = 0; f < kFaces; ++f)
        {
            Face& face = lamp.faces[f];
            if (!face.needsRender && !face.needsLayer)
            {
                continue;
            }
            const U32 key = faceKey(lamp.id, f);
            if (face.needsLayer)
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
                item.own = face.ownEvery;
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
    bool drawn = false;
    if (f == kLampItem)
    {
        if (!lamp.wholeItem)
        {
            return false;
        }
        drawn = renderLamp(lamp, lampDrawn(lamp) ? Cause::Lamp : Cause::New);
    }
    else if (f < kFaces)
    {
        Face& face = lamp.faces[f];
        if (!face.placed || lamp.set == 0)
        {
            return false;
        }
        const LLVector3 set_centre = gAgent.getPosAgentFromGlobal(lamp.setCentre);
        if (!lamp.drawnValid)
        {
            // a fill: the face from the set's pose and radius; the set is complete, and the lookup reads it, once its sixth face is drawn
            if (face.drawnFrame != 0 && face.set == lamp.set)
            {
                return false;
            }
            drawn = renderFace(lamp, (int)f, set_centre, lamp.setRadius, Cause::New);
            bool complete = true;
            for (const Face& other : lamp.faces)
            {
                complete = complete && other.placed && other.drawnFrame != 0 && other.set == lamp.set;
            }
            lamp.drawnValid = complete;
        }
        else if (face.layerOnly)
        {
            drawn = renderLayer(lamp, (int)f);
        }
        else if (face.verdictCause == Cause::Lowres && !resizeDue(face))
        {
            // a pick whose only cause is a growth whose block an earlier resize of this frame took: nothing in the face changed; it grows
            // once a block is free (its avatar layer still runs)
            if (face.holdsAvatar || face.avatarInLayer)
            {
                drawn = renderLayer(lamp, (int)f);
            }
        }
        else
        {
            // one face from the set the lamp's faces hold, so the set stays one pose
            drawn = renderFace(lamp, (int)f, set_centre, lamp.setRadius, face.verdictCause);
        }
    }
    ms = mLastFrameRenderMs - before;
    return drawn;
}

void LLPointShadows::endFrame()
{
    mCasters = nullptr; // no avatar pointer outlives the frame
}

// The lookup's uniforms of one lamp: xyz its centre in this frame's eye space (of the set its faces hold), w its faces' far plane (0: no
// complete set of drawn faces, lit without a lookup); faces: the six packed rects (llpointpool::packFaceRect), two unused.
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
    const LLVector3 c = gAgent.getPosAgentFromGlobal(lamp.setCentre);
    const glm::vec4 eye = get_current_modelview() * glm::vec4(c.mV[VX], c.mV[VY], c.mV[VZ], 1.f);
    lamp4[0] = eye.x;
    lamp4[1] = eye.y;
    lamp4[2] = eye.z;
    lamp4[3] = faceFar(lamp.setRadius);
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
