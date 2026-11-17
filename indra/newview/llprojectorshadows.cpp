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
#include "llviewercontrol.h"
#include "llvotree.h"
#include "llvovolume.h"
#include "pipeline.h"
#include "llframecounters.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

extern bool gCubeSnapshot; // a reflection probe render (pipeline.cpp)

using namespace llprojectorpool;

namespace
{
    constexpr U32 kTierSmall = kAtlasSmall; // the smallest atlas: the floor of the step-down on a failed allocation

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

    // The projection of one tile of a lamp: the lamp's own for a lamp of one tile (so the tile is drawn as it always was), a cell's off-axis
    // sub-frustum for a wide lamp: the cell's rectangle of the lamp's image plane widened by the guard (the same near and far planes
    // and the same depth function, so a cell's depth is the lamp's). nc: the tile's size in texels.
    glm::mat4 tile_proj(const SpotGeom& g, const CellGrid& grid, int ci, int cj, U32 nc)
    {
        const F32 near_clip = g.dist + 0.01f;
        const F32 far_clip = g.dist + g.radius * 1.5f;
        const F32 aspect = g.scale.mV[VX] / g.scale.mV[VY];
        if (!grid.cells())
        {
            return glm::perspective(g.fov, aspect, near_clip, far_clip);
        }
        const double ty = std::tan(0.5 * double(g.fov));
        const double tx = ty * double(aspect);
        const CellTan t = cellTanRect(grid, tx, ty, ci, cj, cellInterior(nc, grid.guard()), grid.guard());
        return glm::frustum(F32(t.x0 * near_clip), F32(t.x1 * near_clip), F32(t.y0 * near_clip), F32(t.y1 * near_clip), near_clip, far_clip);
    }

    // the cells of a lamp (one for a lamp of one tile): its grid from the tangents of its half angles
    CellGrid spot_grid(const SpotGeom& g)
    {
        const double ty = std::tan(0.5 * double(g.fov));
        return gridOf(ty * double(g.scale.mV[VX] / g.scale.mV[VY]), ty);
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
                rec.layerRectValid = false;
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
    mResizes = 0;
    mResizeRepacks = 0;
    mResizeDeferred = 0;
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
    dropLamp(key->second, drawable);
}

// one tile goes (its rect is freed, its LOD pins are given back); the lamp's entry in mKeyOf is dropLamp's
void LLProjectorShadows::dropRecord(std::map<U32, Record>::iterator it)
{
    Record& rec = it->second;
    if (rec.placed && active())
    {
        mBuddy.free(rec.rect);
    }
    unpinTile(rec.key);
    mRecords.erase(it);
}

// every tile of a lamp (its one tile, or its cells) and its entry in mKeyOf
void LLProjectorShadows::dropLamp(U32 lamp, const LLDrawable* drawable)
{
    for (U32 cell = 0; cell < kMaxCells; ++cell)
    {
        auto it = mRecords.find((lamp << 4) | cell);
        if (it != mRecords.end())
        {
            dropRecord(it);
        }
    }
    mKeyOf.erase(drawable);
}

// A tile's size changes (the camera position moved relative to it, or the level shifted): a new rect is taken beside the old one, the old
// content is blitted over (nearest: the tile is blocky until it is drawn again at its size, the lookup never loses it; a wide lamp's
// cell is blitted by its interior, so the content sits where the lookup reads it, and its guard strips hold the stretched edge until
// the redraw), then the old rect is freed. Growth takes this same path as shrinking (design 5.4 asked for draw-then-free for growth:
// not built; the redraw is the first static pick of the frame, so the blocky content is bounded by the scheduler). No whole-atlas repack:
// that is relayout's fallback, counted, when the buddy has no room for the new rect.
bool LLProjectorShadows::resizeInPlace(Record& rec, U32 want)
{
    Rect nr;
    if (!mBuddy.alloc(want, nr))
    {
        return false;
    }
    if (rec.drawnFrame != 0)
    {
        const U32 saved_fbo = LLRenderTarget::sCurFBO;
        for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
        {
            LLTexUnit* tex_unit = gGL.getTexUnit(unit);
            if (tex_unit && tex_unit->getCurrTexture() == mAtlasL)
            {
                tex_unit->unbind(tex_unit->getCurrType());
            }
        }
        const U32 guard = rec.grid.guard();
        blitTile(mFboS, rec.rect, mFboS, nr, guard); // S: the fixed layer (the rects are disjoint: the new one was allocated while the old is held)
        // L: its own old content, so an avatar layer stays (blocky) until the redraw; a tile with none holds S's content
        blitTile(rec.avatarInLayer ? mFboL : mFboS, rec.rect, mFboL, nr, guard);
        glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);
        rec.lowres = true;
        rec.layerRectValid = false; // the old footprint is in the old rect's texels
    }
    mBuddy.free(rec.rect);
    rec.rect = nr;
    ++mResizes;
    return true;
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
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector updateLightSet"); // <Vulkanstorm> multicore stage 1
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
        SpotGeom geom;
        if (!read_spot(drawable, geom))
        {
            return;
        }
        // the lamp's cells (one tile for a lamp up to about 93 degrees); a lamp edited to another grid starts again with the new cells
        const CellGrid grid = spot_grid(geom);
        U32 lamp = was_in ? known->second : 0u;
        if (was_in)
        {
            auto first = mRecords.find(lamp << 4);
            if (first == mRecords.end() || first->second.grid != grid)
            {
                dropLamp(lamp, drawable); // `known` is gone with it
                lamp = 0;
            }
        }
        if (lamp == 0)
        {
            // a record key is lamp << 4 | cell and the scheduler packs 28 bits: the lamp counter wraps inside 24 bits, skipping live ids
            constexpr U32 kLampIds = 1u << (kPoolKeyBits - 4);
            do
            {
                if (mNextKey >= kLampIds)
                {
                    mNextKey = 1;
                }
                lamp = mNextKey++;
            } while (mRecords.find(lamp << 4) != mRecords.end());
            mKeyOf[drawable] = lamp;
        }
        for (int cj = 0; cj < grid.gv; ++cj)
        {
            for (int ci = 0; ci < grid.gu; ++ci)
            {
                const U32 cell = cellIndex(ci, cj);
                auto found = mRecords.find((lamp << 4) | cell);
                Record* rec = nullptr;
                if (found != mRecords.end())
                {
                    rec = &found->second;
                }
                else
                {
                    Record fresh;
                    fresh.key = (lamp << 4) | cell;
                    fresh.lamp = lamp;
                    fresh.cell = cell;
                    fresh.ci = ci;
                    fresh.cj = cj;
                    fresh.grid = grid;
                    fresh.drawable = drawable;
                    fresh.lastCpuMs = coldItemMs(mCostEmaMs); // a never-drawn record's prediction leaves room for several per frame
                    rec = &(mRecords[fresh.key] = fresh);
                }
                rec->seen = true;
                rec->camDist = dist;
            }
        }
    };

    for (const LLPipeline::Light& light : gPipeline.mNearbyLights)
    {
        consider(light.drawable);
    }
    // lights that left mNearbyLights keep their record until their own drop distance (P14)
    std::set<LLDrawable*> held;
    for (auto& entry : mRecords)
    {
        if (!entry.second.seen)
        {
            held.insert(entry.second.drawable.get());
        }
    }
    for (LLDrawable* drawable : held)
    {
        consider(drawable);
    }
    // everything not seen is out: free its tiles (the buddy blocks merge back)
    std::vector<std::pair<U32, const LLDrawable*>> gone;
    for (const auto& entry : mRecords)
    {
        if (!entry.second.seen)
        {
            gone.emplace_back(entry.second.lamp, entry.second.drawable.get());
        }
    }
    for (const auto& lamp : gone)
    {
        dropLamp(lamp.first, lamp.second);
    }
}

void LLProjectorShadows::copyTile(U32 from_tex, const Rect& src, U32 to_tex, const Rect& dst)
{
    llassert(src.size == dst.size);
    // GL 4.3 core, in the OpenGL 4.6 floor (llglfloor.h)
    glCopyImageSubData(from_tex, GL_TEXTURE_2D, 0, (GLint)src.x, (GLint)src.y, 0, to_tex, GL_TEXTURE_2D, 0, (GLint)dst.x,
                       (GLint)dst.y, 0, (GLsizei)src.size, (GLsizei)src.size, 1);
}

// S -> L over a rectangle of a tile (C3). The same copy as copyTile, so the texels are the same bits.
void LLProjectorShadows::copyTileTexels(const Rect& tile, const llprojectorpool::TexelRect& texels)
{
    if (texels.empty())
    {
        return;
    }
    const GLint x = (GLint)tile.x + texels.x0, y = (GLint)tile.y + texels.y0;
    const GLsizei w = texels.x1 - texels.x0, h = texels.y1 - texels.y0;
    glCopyImageSubData(mAtlasS, GL_TEXTURE_2D, 0, x, y, 0, mAtlasL, GL_TEXTURE_2D, 0, x, y, 0, w, h, 1);
}

void LLProjectorShadows::copyAtlas(U32 from_tex, U32 to_tex)
{
    Rect all;
    all.size = mAtlasSize;
    copyTile(from_tex, all, to_tex, all);
}

// A tile into a tile of another size. For a cell (guard > 0) the whole tile goes first (the guard strips then hold the stretched edge,
// not stale data), then the interior over the interior: the lookup reads a cell at (guard + f * interior) / size, so the interiors are
// what must register across sizes.
void LLProjectorShadows::blitTile(U32 from_fbo, const Rect& src, U32 to_fbo, const Rect& dst, U32 guard)
{
    blitRect(from_fbo, src, to_fbo, dst);
    if (guard > 0 && src.size > 2 * guard && dst.size > 2 * guard)
    {
        Rect si = src, di = dst;
        si.x += guard;
        si.y += guard;
        si.size -= 2 * guard;
        di.x += guard;
        di.y += guard;
        di.size -= 2 * guard;
        blitRect(from_fbo, si, to_fbo, di);
    }
}

void LLProjectorShadows::blitRect(U32 from_fbo, const Rect& src, U32 to_fbo, const Rect& dst)
{
    LLGLDisable scissor(GL_SCISSOR_TEST); // a blit is clipped by the scissor test
    glBindFramebuffer(GL_READ_FRAMEBUFFER, from_fbo);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, to_fbo);
    glBlitFramebuffer((GLint)src.x, (GLint)src.y, (GLint)(src.x + src.size), (GLint)(src.y + src.size), (GLint)dst.x, (GLint)dst.y,
                      (GLint)(dst.x + dst.size), (GLint)(dst.y + dst.size), GL_DEPTH_BUFFER_BIT, GL_NEAREST);
}

// Applies a repack (spec 4.3), in one frame and with no name swap: every drawn tile S[old] -> L[new] (a copy at the same
// size, bit exact; a blit at another, by the cell's interior for a wide lamp, the tile is then low resolution until it is drawn again), then L -> S in full, so both
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
            blitTile(mFboS, move.src, mFboL, move.dst, it->second.grid.guard());
            it->second.lowres = true; // drawn again at its new size
        }
    }
    if (!plan.moves.empty())
    {
        copyAtlas(mAtlasL, mAtlasS);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, saved_fbo);

    // L's tiles were rewritten (or moved): the avatar footprints of the old layout say nothing about the new one, the next layer pass of
    // each tile copies it whole (C3)
    if (!plan.moves.empty())
    {
        for (auto& entry : mRecords)
        {
            entry.second.layerRectValid = false;
        }
    }

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
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector relayout"); // <Vulkanstorm> multicore stage 1
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
    U32 resizes_now = 0;
    for (auto& entry : mRecords)
    {
        Record& rec = entry.second;
        const U32 want = desiredSize(rec);
        if (rec.placed && rec.rect.size != want)
        {
            // the tile's size changed (the camera position moved, or the lamp was edited): in place when the buddy has room for the new
            // rect; a level change (need_relayout) or no room repacks the whole layout, which places this tile at its size too
            if (!need_relayout)
            {
                if (resizes_now >= kMaxResizesPerFrame)
                {
                    ++mResizeDeferred; // a burst (many tiles crossing their band together) is spread over frames: this one keeps its size
                    continue;
                }
                if (resizeInPlace(rec, want))
                {
                    ++resizes_now;
                }
                else
                {
                    need_relayout = true;
                    ++mResizeRepacks;
                }
            }
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
// LLPipeline::renderShadowImpl), so what the picture holds does not depend on the camera; only the tile's size follows the camera's
// position (a size change redraws it); the avatars are chosen by position alone.
void LLProjectorShadows::renderTile(Record& rec, Cause cause, bool dynamic)
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector renderTile"); // <Vulkanstorm> multicore stage 1
    SpotGeom g;
    if (!read_spot(rec.drawable, g))
    {
        return;
    }
    glm::mat4 view, proj;
    F32 far_clip = 0.f;
    spot_matrices(g, view, proj, far_clip);
    if (rec.grid.cells())
    {
        proj = tile_proj(g, rec.grid, rec.ci, rec.cj, rec.rect.size); // this cell's sub-frustum, guard included
    }

    const F64 cpu_start = LLTimer::getTotalSeconds();
    LL_PROFILE_GPU_ZONE("projector tile"); // <Vulkanstorm/> multicore stage 1b: one tile's fixed layer, copy and avatars

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
        LL_FRAME_COUNT_ADD(ProjectorTiles, 1); // <Vulkanstorm> multicore stage 1
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

    // S changed everywhere: the whole tile is copied, then the avatars are drawn over the copy, in L only (S stays the fixed layer)
    copyTile(mAtlasS, rec.rect, mAtlasL, rec.rect);
    F32 avatar_ms = 0.f;
    bool avatar_drawn = false;
    if (rec.holdsAvatar)
    {
        const F64 face_start = LLTimer::getTotalSeconds();
        const FaceAvatars face = faceAvatars(rec, view, proj, g.origin);
        avatar_ms = (F32)((LLTimer::getTotalSeconds() - face_start) * 1000.0);
        if (!face.rect.empty()) // an empty footprint draws nothing
        {
            avatar_ms += drawAvatars(rec, face, view, proj, g.origin, far_clip);
            avatar_drawn = true;
        }
        rec.layerRect = face.rect;
    }
    else
    {
        rec.layerRect = llprojectorpool::TexelRect();
    }
    rec.layerRectValid = true;
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
    if (avatar_drawn)
    {
        r.lastAvatarMs = llprojectorpool::smoothedCostMs(r.lastAvatarMs, avatar_ms);
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
namespace
{
    struct AvatarCaster
    {
        LLVOAvatar* avatar;
        Aabb box;
        bool own = false; // the user's own avatar (gAgentAvatarp): its faces are drawn every frame
    };

    // The avatars that cast into a light (as the avatar pool's renderShadow draws them): not dead, not a UI avatar, not too slow,
    // a normal appearance (no jelly doll, RLV silhouette, derendered or too complex avatar), and with RenderAvatarFriendsOnly only
    // a friend, the agent or a control avatar. Nothing here reads the camera.
    std::vector<AvatarCaster> casting_avatars()
    {
        static LLCachedControl<bool> friends_only(gSavedSettings, "RenderAvatarFriendsOnly", false);
        std::vector<AvatarCaster> out;
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
            AvatarCaster c;
            c.avatar = avatar;
            c.own = avatar->isSelf();
            for (int i = 0; i < 3; ++i)
            {
                c.box.lo[i] = ext[0].getF32ptr()[i];
                c.box.hi[i] = ext[1].getF32ptr()[i];
            }
            out.push_back(c);
        }
        return out;
    }
}

// The avatars a face draws, and the tile rectangle they cover (C3). The face draws at most A_max avatars, the nearest to the lamp (the
// scheduler counts min(avatars, A_max) draws for it). The rectangle is the avatars' extents, grown by a margin for the lag of the
// extents and the avatar's speed, projected through the tile's matrices; the whole tile when that is unknown, too large, or an avatar
// is an impostor (its extents refresh only every 32-64 frames).
LLProjectorShadows::FaceAvatars LLProjectorShadows::faceAvatars(const Record& rec, const glm::mat4& view, const glm::mat4& proj, const LLVector3& origin) const
{
    FaceAvatars face;
    std::vector<std::pair<F32, const AvatarCaster*>> in_light;
    const std::vector<AvatarCaster> casters = casting_avatars();
    for (const AvatarCaster& c : casters)
    {
        if (intersects(rec.volume, c.box))
        {
            const LLVector3 centre((c.box.lo[0] + c.box.hi[0]) * 0.5f, (c.box.lo[1] + c.box.hi[1]) * 0.5f, (c.box.lo[2] + c.box.hi[2]) * 0.5f);
            in_light.emplace_back((centre - origin).magVecSquared(), &c);
        }
    }
    std::sort(in_light.begin(), in_light.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<llprojectorpool::Aabb> boxes;
    const S32 period = LLVOAvatar::getExtentUpdatePeriod(); // how stale the extents can be
    bool whole = false;
    for (size_t i = 0; i < in_light.size() && i < kAvatarDrawsMax; ++i)
    {
        const AvatarCaster& c = *in_light[i].second;
        face.avatars.push_back(c.avatar);
        whole = whole || c.avatar->isImpostor();
        boxes.push_back(llprojectorpool::inflateBox(c.box, llprojectorpool::footprintMarginM(c.avatar->getVelocity().magVec(), period, c.box)));
    }
    const U32 n = rec.rect.size;
    if (whole)
    {
        face.rect = llprojectorpool::wholeTileRect(n);
    }
    else
    {
        const glm::mat4 clip = proj * view;
        face.rect = llprojectorpool::faceFootprint(glm::value_ptr(clip), boxes, n);
    }
    return face;
}

F32 LLProjectorShadows::drawAvatars(Record& rec, const FaceAvatars& face, const glm::mat4& view, const glm::mat4& proj, const LLVector3& origin, F32 far_clip)
{
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector drawAvatars"); // <Vulkanstorm> multicore stage 1
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

    std::vector<const LLVOAvatar*> face_avatars = face.avatars;
    LLPipeline::sLocalShadowAvatars = &face_avatars;

    {
        // the draw is limited to the footprint (C3): outside it L stays S's copy, which is what makes the next pass's copy exact
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(gGLViewport[0] + face.rect.x0, gGLViewport[1] + face.rect.y0, llmax(face.rect.x1 - face.rect.x0, 0), llmax(face.rect.y1 - face.rect.y0, 0));

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
        LL_FRAME_COUNT_ADD(ProjectorAvatarLayers, 1); // <Vulkanstorm> multicore stage 1
        gPipeline.renderShadow(view, proj, shadow_cam, result, false);
        LLPipeline::RenderSpotLight = nullptr;
        gGL.flush(); // what the immediate-mode batch holds goes to the tile before the framebuffer changes
    }
    LLPipeline::sLocalShadowPass = LLPipeline::LOCAL_SHADOW_NONE;
    LLPipeline::sLocalShadowAvatars = nullptr;
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
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector renderLayer"); // <Vulkanstorm> multicore stage 1
    LL_PROFILE_GPU_ZONE("projector layer"); // <Vulkanstorm/> multicore stage 1b: one tile's avatar layer redrawn
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
    if (rec.grid.cells())
    {
        proj = tile_proj(g, rec.grid, rec.ci, rec.cj, rec.rect.size);
    }

    // C3: restore what the last pass drew over (its rectangle) and what this one will draw over (the new rectangle), not the whole tile;
    // an unknown old rectangle (a fresh or repacked atlas) is the whole tile
    F32 avatar_ms = 0.f;
    FaceAvatars face;
    if (rec.holdsAvatar)
    {
        const F64 face_start = LLTimer::getTotalSeconds();
        face = faceAvatars(rec, view, proj, g.origin);
        avatar_ms = (F32)((LLTimer::getTotalSeconds() - face_start) * 1000.0);
    }
    copyTileTexels(rec.rect, llprojectorpool::layerCopyRect(rec.layerRectValid, rec.layerRect, face.rect, rec.rect.size));
    if (rec.holdsAvatar && !face.rect.empty()) // an empty footprint draws nothing, and says nothing about the cost of a draw
    {
        avatar_ms += drawAvatars(rec, face, view, proj, g.origin, far_clip);
        rec.lastAvatarMs = llprojectorpool::smoothedCostMs(rec.lastAvatarMs, avatar_ms);
    }
    rec.layerRect = face.rect; // empty when no avatar is in the light: L is S again
    rec.layerRectValid = true;
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

    // The volume of one tile for the change tests: a lamp of one tile has the lamp's (light_volume); a cell has its sub-pyramid. With the
    // guard (guard = true, at the tile's size nc) it is the region the tile's render sees: an object that only a neighbour's guard holds
    // changes this tile too. Without it, the cell's own interior: the camera position's distance (the sizing) and the avatar's cadence.
    Pyramid cell_volume(const SpotGeom& g, F32 far_clip, const CellGrid& grid, int ci, int cj, U32 nc, bool guard)
    {
        if (!grid.cells())
        {
            return light_volume(g, far_clip);
        }
        LLVector3 fwd(0.f, 0.f, -1.f), right(1.f, 0.f, 0.f), up(0.f, 1.f, 0.f);
        fwd *= g.rot;
        right *= g.rot;
        up *= g.rot;
        const double ty = std::tan(0.5 * double(g.fov));
        const double tx = ty * double(g.scale.mV[VX] / g.scale.mV[VY]);
        const uint32_t gd = guard ? grid.guard() : 0u;
        const CellTan t = cellTanRect(grid, tx, ty, ci, cj, cellInterior(nc, grid.guard()), gd);
        return makeFrustumPyramid(g.origin.mV, fwd.mV, right.mV, up.mV, float(t.x0), float(t.x1), float(t.y0), float(t.y1), llmax(g.dist, 0.001f), far_clip);
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
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector pinCasters"); // <Vulkanstorm> multicore stage 1
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
    LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector update"); // <Vulkanstorm> multicore stage 1: the whole per-frame pool update
    LL_PROFILE_GPU_ZONE("projector tiles"); // <Vulkanstorm/> multicore stage 1b: the GPU time of the whole pool update (Tracy builds)
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

    // The lamps' poses, this frame's: from the lamp alone (never the camera). This is what a static scene still costs per frame:
    // one read of every lamp's spot parameters and pose (the pose-change detection). A tile's interior volume is built only when its
    // lamp's pose changed (or agent space was shifted, which moves every volume), and the change index is rebuilt only on a frame
    // with dirty boxes, below. The camera's POSITION enters here and nowhere else: it picks each tile's size (cellWanted: its distance
    // to the tile's volume), never what the tile holds.
    const U32 shifts = LLShadowDirtyFeed::shifts();
    const bool shifted = shifts != mShiftsSeen;
    mShiftsSeen = shifts;
    const LLVector3 cam_origin = camera.getOrigin();
    const F32 cam_point[3] = { cam_origin.mV[VX], cam_origin.mV[VY], cam_origin.mV[VZ] };
    const F64 now_s = LLTimer::getTotalSeconds();
    std::vector<const LLViewerObject*> lamp_roots; // the lamps' own linksets: fixtures, their changes always push (llshadowdirtyfeed)
    U32 lamp_read = 0;
    bool lamp_ok = false;
    SpotGeom lamp_g;
    Pose lamp_pose;
    // the lamp's sizing inputs, read once per lamp with its geometry (every cell of a lamp shares them: the grid is the lamp's)
    F32 lamp_far_clip = 0.f;
    float lamp_origin[3] = {};
    float lamp_fwd[3] = {};
    double lamp_tan_w = 0.0;
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector sweep"); // <Vulkanstorm> hotfix: where the per-frame update spends its time
        for (auto& entry : mRecords)
        {
            Record& rec = entry.second;
            // the lamp's geometry and pose are read once for its tiles (they are consecutive: the key is lamp << 4 | cell)
            if (lamp_read != rec.lamp)
            {
                lamp_read = rec.lamp;
                if (LLViewerObject* lamp = rec.drawable.notNull() ? rec.drawable->getVObj().get() : nullptr)
                {
                    LLViewerObject* lamp_root = lamp->getRootEdit();
                    lamp_roots.push_back(lamp_root ? lamp_root : lamp);
                }
                lamp_ok = read_spot(rec.drawable, lamp_g);
                if (lamp_ok)
                {
                    lamp_pose = light_pose(lamp_g, rec.drawable);
                    lamp_far_clip = lamp_g.dist + lamp_g.radius * 1.5f; // spot_matrices' far plane
                    // the lamp's axial direction and the cell's wider axis: the camera's sizing input below
                    LLVector3 fwd(0.f, 0.f, -1.f);
                    fwd *= lamp_g.rot;
                    lamp_origin[0] = lamp_g.origin.mV[VX]; lamp_origin[1] = lamp_g.origin.mV[VY]; lamp_origin[2] = lamp_g.origin.mV[VZ];
                    lamp_fwd[0] = fwd.mV[VX]; lamp_fwd[1] = fwd.mV[VY]; lamp_fwd[2] = fwd.mV[VZ];
                    const double ty = std::tan(0.5 * double(lamp_g.fov));
                    const double tx = ty * double(lamp_g.scale.mV[VX] / lamp_g.scale.mV[VY]);
                    lamp_tan_w = std::max(2.0 * tx / rec.grid.gu, 2.0 * ty / rec.grid.gv); // the tile is square
                }
            }
            const SpotGeom& g = lamp_g;
            if (!lamp_ok)
            {
                rec.prevPoseValid = false;
                rec.volumeValid = false;
                continue;
            }
            rec.curPose = lamp_pose;
            rec.lampMoving = rec.prevPoseValid && poseChanged(rec.prevPose, rec.curPose);
            rec.prevPose = rec.curPose;
            rec.prevPoseValid = true;
            rec.poseStale = rec.drawn.valid && poseChanged(rec.drawnPose, rec.curPose);
            const F32 far_clip = lamp_far_clip;
            if (shifted || !rec.volumeValid || poseChanged(rec.volumePose, rec.curPose))
            {
                rec.core = cell_volume(g, far_clip, rec.grid, rec.ci, rec.cj, 0, false);
                rec.volumeNc = 0; // the tile's own volume (with its guard) is built below, at the tile's size
                rec.volumePose = rec.curPose;
                rec.volumeValid = true;
            }
            // the tile's size from the camera position: its distance to the tile's interior volume, and the lamp's axial depth of the camera
            rec.hyst.update(cellSizeRequest(lamp_origin, lamp_fwd, g.radius, g.dist, far_clip, lamp_tan_w, rec.core, rec.grid.guard(), cam_point, &rec.cellDist), now_s);
            rec.base = rec.hyst.size();
            if (rec.grid.cells())
            {
                rec.camDist = rec.cellDist; // the order of service: the nearest cells first (a single tile keeps the lamp sphere's distance, set in updateLightSet)
            }
        }
    }
    LLShadowDirtyFeed::setLampRoots(LLShadowDirtyFeed::Owner::Projectors, lamp_roots);

    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector layout"); // <Vulkanstorm> hotfix
        layoutChanged();
        // the volume of each tile as it is drawn (a cell's frustum is widened by its guard in texels, so its world width follows its size)
        for (auto& entry : mRecords)
        {
            Record& rec = entry.second;
            const U32 nc = rec.placed ? rec.rect.size : desiredSize(rec);
            if (rec.volumeValid && rec.volumeNc != nc)
            {
                SpotGeom g;
                if (read_spot(rec.drawable, g))
                {
                    F32 far_clip = g.dist + g.radius * 1.5f;
                    rec.volume = cell_volume(g, far_clip, rec.grid, rec.ci, rec.cj, nc, true);
                    rec.volumeNc = nc;
                }
            }
        }
    }

    // which drawn tiles a change touched (the first cause that made a tile stale is kept until it is drawn); the index is lazy:
    // built only when boxes arrived or the list says "everything"
    if (!boxes.empty() || everything)
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector index"); // <Vulkanstorm> hotfix
        std::vector<TileVolume> volumes;
        volumes.reserve(mRecords.size());
        for (auto& entry : mRecords)
        {
            const Record& rec = entry.second;
            if (rec.placed && rec.drawnFrame != 0 && rec.volumeValid && !rec.pending) // a pending tile's hit is discarded below
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
    // with every lamp whose volume its extents touch; the pairing reads neither the camera's view, nor the avatar's visibility, nor its
    // impostor flag (isVisible and isImpostor are the camera's: an avatar behind the camera, or one drawn as an impostor, still casts).
    // The camera's POSITION only orders the faces (below), and faceAvatars reads isImpostor() only to size the footprint (an impostor's
    // extents are stale, so it copies the whole tile). A jelly doll, an invisible or a too slow avatar and a UI avatar are not drawn by
    // the avatar pool either.
    const F32 camera_pos[3] = { camera.getOrigin().mV[VX], camera.getOrigin().mV[VY], camera.getOrigin().mV[VZ] };
    std::vector<Aabb> avatar_boxes, own_boxes;
    for (const AvatarCaster& c : casting_avatars())
    {
        (c.own ? own_boxes : avatar_boxes).push_back(c.box);
    }

    // the work list: the verdict of every placed tile (llprojectorpool::classifyTile). A static scene has none.
    std::vector<StaticItem> static_items;
    std::vector<DynamicItem> dynamic_items;
    std::set<U32> own_faces;  // the layer faces that hold the own avatar: they run every frame, past the cap
    std::set<U32> layer_only; // the dynamic items that are an avatar layer alone (the others redraw the whole tile)
    mLightsWithoutTile = 0;
    mPendingTiles = 0;
    mAvatarTiles = 0;
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector classify"); // <Vulkanstorm> hotfix: where the per-frame update spends its time
        // the avatar boxes that can touch the current lamp's tiles (its records are consecutive): the pairing below tests only these
        std::vector<Aabb> run_own, run_avatars;
        U32 run_lamp = 0;
        bool run_open = false;
        for (auto entry = mRecords.begin(); entry != mRecords.end(); ++entry)
        {
            Record& rec = entry->second;
            if (!run_open || rec.lamp != run_lamp)
            {
                run_open = true;
                run_lamp = rec.lamp;
                BoxFilter filter;
                for (auto next = entry; next != mRecords.end() && next->second.lamp == run_lamp; ++next)
                {
                    if (next->second.placed)
                    {
                        filter.add(next->second.volume.bounds);
                    }
                }
                filter.select(own_boxes, run_own);
                filter.select(avatar_boxes, run_avatars);
            }
            if (rec.drawnFrame == 0)
            {
                ++mLightsWithoutTile;
            }
            if (!rec.placed)
            {
                continue;
            }
            rec.avatarCount = 0;
            rec.avatarDist = std::numeric_limits<F32>::infinity();
            bool holds_own = false;
            for (const Aabb& b : run_own)
            {
                if (intersects(rec.volume, b))
                {
                    ++rec.avatarCount;
                    if (!rec.grid.cells() || cellHoldsOwn(rec.core, b))
                    {
                        holds_own = true; // the own avatar's face is drawn every frame (not ordered by distance): of a wide lamp's cells, the one holding the box's centre
                    }
                    else
                    {
                        // a cell the avatar's box overlaps but whose interior does not hold the box's centre: by deadline like the others, so the
                        // faces that run every frame past the cap are one per lamp for the own avatar, not every cell its width crosses
                        rec.avatarDist = std::min(rec.avatarDist, pointBoxDistance(camera_pos, b));
                    }
                }
            }
            for (const Aabb& b : run_avatars)
            {
                if (intersects(rec.volume, b))
                {
                    ++rec.avatarCount; // also for a tile about to be drawn the first time: its picture holds the avatar
                    // the order of the face's service: the camera POSITION's distance to its nearest other avatar (never the view direction)
                    rec.avatarDist = std::min(rec.avatarDist, pointBoxDistance(camera_pos, b));
                }
            }
            if (rec.avatarCount == 0)
            {
                rec.avatarDist = 0.f; // the avatar left: this face only restores L from S, which is cheap and first
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
            rec.verdictCause = verdict.cause; // read by this frame's pick (renderTile) only
            // the prediction of a full render: the fixed layer, plus the avatar layer when an avatar is in the light
            const F32 full_ms = rec.lastCpuMs + (rec.holdsAvatar ? rec.lastAvatarMs : 0.f);
            if (verdict.needs_layer)
            {
                DynamicItem item;
                item.key = rec.key;
                item.avatar = true;
                item.age_frames = frame - rec.layerFrame; // the lag of the avatar in this tile: frames since its layer was last drawn
                item.cam_dist = rec.avatarDist;
                item.last_cpu_ms = rec.holdsAvatar ? rec.lastAvatarMs : 0.f; // a face that only restores L from S costs the floor
                item.avatar_draws = rec.avatarCount;
                item.layer = true;
                item.own = holds_own;
                if (holds_own)
                {
                    own_faces.insert(rec.key);
                }
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
                item.avatar_draws = rec.holdsAvatar ? std::min<U32>(rec.avatarCount, kAvatarDrawsMax) : 0u;
                // the order of service: how long the tile has been waiting: frames since the first change that touched it, or, for a
                // tile whose only cause is its lamp's pose or a resize (no box), frames since it was drawn, so it cannot starve
                // behind tiles with dirty boxes
                item.age_frames = rec.dirtyFrame && frame >= rec.dirtyFrame ? frame - rec.dirtyFrame
                                  : (rec.drawnFrame && frame >= rec.drawnFrame ? frame - rec.drawnFrame : 0u);
                static_items.push_back(item);
            }
        }
    }
    // The budget: one CPU cap for the frame's tile work of every cache together (llprojectorpool::kFrameCapMs, split by
    // selectFrame: the dynamic layer first, up to kDynamicCapMs, the static tiles in what is left). The measured cost of a tile only
    // predicts how many fit; it does not set the budget. What an earlier cache spent in this frame is already off the cap.
    const FrameLedger& ledger = frameLedger();
    FramePicks picks;
    {
        LL_PROFILE_ZONE_NAMED_CATEGORY_PIPELINE("projector select"); // <Vulkanstorm> hotfix
        picks = selectFrame(static_items, dynamic_items, ledger.left(kFrameCapMs), kAvatarDrawsMax, ledger.untouched());
    }
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

    // the in-frame caps: a pick runs only while the measured time of the frame is inside the shared cap and, for the dynamic list,
    // inside its own share. The one exemption: the first pick of the frame when nothing else rendered this frame (no earlier cache,
    // no dynamic pick): the selection kept it inside the cap, or it is the progress pick (mayRunPick)
    bool only_own = true; // the dynamic picks that ran before a static pick, if all are the own avatar's: the progress head runs beside them
    for (U32 pk : picks.dynamic)
    {
        only_own = only_own && own_faces.count(unpackKey(pk)) > 0;
    }
    const bool alone = ledger.untouched() && only_own;
    const F32 dynamic_share = dynamicShareMs(!static_items.empty(), kFrameCapMs); // the whole cap when no static tile waits
    auto run = [&](const std::vector<U32>& packed, bool dynamic) {
        F32 own_ms = 0.f;
        bool first_pick = true;
        for (U32 pk : packed)
        {
            const U32 key = unpackKey(pk);
            const bool own = dynamic && own_faces.count(key) > 0; // the own avatar's face: exempt from the caps, drawn every frame
            if (!mayRunPick(ledger.left(kFrameCapMs), dynamic, first_pick, alone, own) || (!first_pick && dynamic && !own && own_ms >= dynamic_share))
            {
                break;
            }
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
                    renderTile(it->second, it->second.verdictCause, dynamic);
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
    // the lamp's tiles: its one tile, or its cells. The lookup matrix is one per lamp, built from the pose of the most recently drawn
    // tile; a cell drawn from another pose (the lamp moved since) reads as not drawn until it is drawn again (lit, no shadow), so that
    // no lookup mixes two poses.
    const Record* ref = nullptr;
    std::array<const Record*, kMaxCells> cell_rec{};
    auto key = mKeyOf.find(light);
    if (key != mKeyOf.end())
    {
        // the lamp's records are consecutive in the map (key = lamp << 4 | cell): the one tile's lamp has one, a wide lamp's has its cells
        for (auto it = mRecords.lower_bound(key->second << 4); it != mRecords.end() && (it->first >> 4) == key->second; ++it)
        {
            const U32 cell = it->first & 15u;
            const Record& r = it->second;
            if (!(active() && r.placed && r.drawnFrame != 0 && r.drawn.valid))
            {
                continue;
            }
            cell_rec[cell] = &r;
            if (!ref || r.drawnFrame > ref->drawnFrame)
            {
                ref = &r;
            }
        }
    }
    // a light with no drawn tile has no lookup (proj_shadow_idx -1): it is lit without a shadow until its first render
    const bool has_tile = ref != nullptr;
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
    g.origin = gAgent.getPosAgentFromGlobal(ref->drawn.originGlobal);
    g.rot = ref->drawn.rot;
    g.scale = ref->drawn.scale;
    g.fov = ref->drawn.fov;
    g.radius = ref->drawn.radius;
    g.dist = ref->drawn.dist;
    glm::mat4 view, proj;
    F32 far_clip = 0.f;
    spot_matrices(g, view, proj, far_clip);
    const glm::mat4 trans(0.5f, 0.0f, 0.0f, 0.0f,
                          0.0f, 0.5f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.5f, 0.0f,
                          0.5f, 0.5f, 0.5f, 1.0f);
    const glm::mat4 m = trans * proj * view * glm::inverse(get_current_modelview());
    shader.uniformMatrix4fv(LLShaderMgr::PROJ_SHADOW_MATRIX, 1, GL_FALSE, glm::value_ptr(m));
    const CellGrid& grid = ref->grid;
    if (!grid.cells())
    {
        shader.uniform4f(LLShaderMgr::PROJ_SHADOW_TILE, (F32)ref->rect.x, (F32)ref->rect.y, (F32)ref->rect.size, 1.f / (F32)mAtlasSize);
        shader.uniform4f(LLShaderMgr::PROJ_SHADOW_GRID, 1.f, 1.f, 0.f, 0.f);
    }
    else
    {
        // the cells: x, y, Nc, Ni of each (Nc 0: not drawn, or drawn from another pose than the matrix's), indexed row * 4 + column
        F32 cells[kMaxCells * 4] = {};
        for (U32 cell = 0; cell < kMaxCells; ++cell)
        {
            const Record* r = cell_rec[cell];
            if (r && !poseChanged(ref->drawnPose, r->drawnPose))
            {
                cells[cell * 4 + 0] = (F32)r->rect.x;
                cells[cell * 4 + 1] = (F32)r->rect.y;
                cells[cell * 4 + 2] = (F32)r->rect.size;
                cells[cell * 4 + 3] = (F32)cellInterior(r->rect.size, grid.guard());
            }
        }
        shader.uniform4f(LLShaderMgr::PROJ_SHADOW_TILE, 0.f, 0.f, 0.f, 1.f / (F32)mAtlasSize);
        shader.uniform4f(LLShaderMgr::PROJ_SHADOW_GRID, (F32)grid.gu, (F32)grid.gv, (F32)grid.guard(), 0.f);
        shader.uniform4fv(LLShaderMgr::PROJ_SHADOW_CELLS, kMaxCells, cells);
    }
    // the emitter's size for the penumbra: every lamp is a 1 cm light times RenderShadowLightSize (the sun's softness factor),
    // whatever its prim; the filter's largest radius is a length in the shader, so the level of the tile does not enter
    const F32 emitter = emitterExtent(LLPipeline::RenderShadowLightSize);
    shader.uniform2f(LLShaderMgr::PROJ_SHADOW_LIGHT_SIZE, emitter, emitter);
    // the emitter's place: the projection centre of the same drawn pose as the matrix (not the lamp's live pose), in eye space
    const glm::vec4 origin_eye = get_current_modelview() * glm::vec4(g.origin.mV[VX], g.origin.mV[VY], g.origin.mV[VZ], 1.0f);
    shader.uniform3f(LLShaderMgr::PROJ_SHADOW_ORIGIN, origin_eye.x, origin_eye.y, origin_eye.z);
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
    st.lights = (U32)mKeyOf.size();
    st.cells = (U32)mRecords.size();
    st.resizes = mResizes;
    st.resizeRepacks = mResizeRepacks;
    st.resizeDeferred = mResizeDeferred;
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
        t.lamp = rec.lamp;
        t.cell = rec.cell;
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
