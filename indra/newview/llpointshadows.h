/**
 * @file llpointshadows.h
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

#ifndef LL_LLPOINTSHADOWS_H
#define LL_LLPOINTSHADOWS_H

#include <map>
#include <unordered_map>
#include <vector>

#include "llpointer.h"
#include "v3math.h"
#include "v3dmath.h"
#include "lldrawable.h"
#include "lllocalshadows.h"
#include "llpointshadowpool.h"
#include "llprojectorshadowpool.h"
#include "llvertexbuffer.h"

class LLCamera;
class LLCullResult;
class LLGLSLShader;
class LLVOAvatar;

// Stage L4 of the lamp shadows: every point light within the shadows' reach of the camera's position has six faces (a cube shadow map
// around its centre, aligned to the agent axes) as tiles of the point pool's own depth atlases (owner decision D3: a separate atlas, 4096
// from 3 GB of video memory, 2048 below): S holds the faces as drawn (the fixed layer), L is S plus the avatars in the light (the dynamic
// layer), the only texture the lighting programs read (pointLightF.glsl and multiPointLightF.glsl, samplePointShadow). A face's size comes
// from its lamp alone (llpointpool::faceBaseSize); the camera's position only picks which lamps shrink first when the atlas is over its
// budget (the projectors' pressure valve, one band per lamp, floor 128), and a size change is drawn before it is shown (draw-then-swap).
// The machinery is the projector pool's (llprojectorshadowpool.h): the change feed and index, the LOD pin, the 5 s hold of a lamp switched
// off, the valve, the verdict, the avatar layer and the shared scheduler (LLLocalShadows::updateFrame: one selection over both pools under
// the one cap). What is point-specific is here: the faces, the set of a lamp's six faces the lookup reads (all drawn from one pose and one
// radius: a lamp's first fill draws its faces one item each, a lamp that moved draws all six in one render), the emitter left out of its
// own faces by identity (D4: the rest of its linkset casts), casters drawn from both sides, the own avatar's every-frame faces (P2), and a
// lamp nearer the camera's position taking the faces of a farther one when the atlas is full at the floor.
// Doc: doc/shadows/point-light-shadows.md.
//
// Faces are drawn one at a time (a tile render each: its own cull, its own submit). The instanced form of D7 (D', several faces per submit)
// and one cull per lamp are a later step; they change the CPU cost of a whole lamp's render, not a face's picture.
//
// Main render thread only. Every entry point returns at once off it, or while the viewer exits; nothing here is reached from
// LLRender::shutdown, LLImageGL or LLVertexBuffer::cleanupClass, which worker threads with shared contexts run (the sVBCache lesson,
// #403). The destructor makes no GL call: release() runs with the context alive (LLPipeline::cleanup, destroyGL, a method, detail or
// setting change).
class LLPointShadows
{
public:
    LLPointShadows();
    ~LLPointShadows();

    // The pool runs for the Stochastic method with shadow detail 2 (the projector pool's test), on an OpenGL 4.6 context, with the debug
    // setting RenderShadowPointLights (on by default, no Preferences entry: decision D8); the same test the shader manager makes for the
    // POINT_SHADOW_POOL permutation of the point light programs. Off, the point light passes are today's.
    static bool wanted();

    bool active() const { return mAtlasS != 0 && mAtlasL != 0; }

    // Idempotent: makes the atlases when they are wanted and not there (4096, 2048 below 3 GB of video memory; one tier smaller if an
    // allocation fails; never the method changes: the lamps are lit without a shadow and the allocation is tried again, backing off).
    bool ensureAllocated();
    // Frees the atlases, their framebuffers, every face and lamp (they hold drawables and LOD pins). Context alive, main thread.
    void release();
    // Drops every lamp and face (and their pins). Does not touch the atlases.
    void clear();

    // The frame's work (LLLocalShadows::updateFrame, as LLProjectorShadows): prepare appends this pool's work (kPoolPoints); renderPick
    // renders one pick (a face, its avatar layer, or a whole lamp) and says whether anything was drawn; endFrame lets go of the frame's lists.
    void prepare(LLCamera& camera, const std::vector<LLLocalShadowCaster>& casters, std::vector<llprojectorpool::StaticItem>& statics,
                 std::vector<llprojectorpool::DynamicItem>& dynamics);
    void beginRenders();
    bool renderPick(U32 key, bool dynamic, F32& ms);
    void endFrame();

    // From the deferred lighting pass: one light's lookup uniforms (pointLightF.glsl, the light drawn as a box), and a batch's
    // (multiPointLightF.glsl, the lights the camera is inside, `count` of them in the order of the program's light arrays). A lamp without
    // a complete set of drawn faces gets a far plane of 0 (lit, no lookup).
    void setupLight(LLGLSLShader& shader, LLDrawable* light);
    void setupBatch(LLGLSLShader& shader, const std::vector<LLDrawable*>& lights);

    // From LLPipeline::bindShadowMaps: the live atlas on the program's sampler (if it has one), and the frame's view-to-agent rotation and
    // the emitter's size.
    void bindAtlas(LLGLSLShader& shader);

    // From LLPipeline::unlinkDrawable (a drawable is going away): its lamp and faces are dropped, its pins forgotten.
    void forget(const LLDrawable* drawable);

    // From LLPipeline::renderShadow, after the face's cull and sort (a point face render only; the calls return at once otherwise): the
    // LOD pin of the casters in the face's volume, and the emitter left out of the draws (LLVertexBuffer::sSkipRanges: its faces' index
    // ranges in the buffers they share with its spatial group).
    void pinCasters(LLCullResult& result);
    void skipEmitter();

private:
    struct Face
    {
        llprojectorpool::Rect rect;
        bool placed = false;
        U32 want = 0;              // this frame's size (the valve's)
        U32 drawnFrame = 0;        // 0: never drawn
        U32 set = 0;               // the lamp's set (Lamp::set) this face was drawn for: the lookup reads the lamp when all six hold its set
        llprojectorpool::Pyramid volume; // the face's render frustum (guard included) at volumeN, from the lamp's pose and face radius
        U32 volumeN = 0;
        bool pending = false;      // a dirty box touched it since it was drawn
        llprojectorpool::Cause pendingCause = llprojectorpool::Cause::Other;
        llprojectorpool::Cause verdictCause = llprojectorpool::Cause::Other;
        U32 dirtyFrame = 0;
        bool holdsAvatar = false;
        bool holdsOwn = false;     // this frame: the own avatar's box meets its volume
        bool ownEvery = false;     // this frame: its avatar layer runs every frame (P2, within the own faces' draws)
        U32 avatarCount = 0;
        bool avatarInLayer = false; // L holds an avatar of this face over S
        F32 lastAvatarMs = 0.f;
        F32 avatarDist = 0.f;
        llprojectorpool::TexelRect layerRect;
        bool layerRectValid = false;
        U32 layerFrame = 0;
        F32 lastCpuMs = 0.f;
        bool layerOnly = false;    // this frame's item is its avatar layer alone
        bool needsRender = false;  // this frame's verdict (a complete lamp): its fixed layer is drawn again
        bool needsLayer = false;   // ... or its avatar layer alone
    };

    struct Lamp
    {
        U32 id = 0;
        LLPointer<LLDrawable> drawable;
        Face faces[llpointpool::kFaces];
        // The set of faces: the pose and radius every face of the set is drawn from (a fill draws the faces one at a time, a moved lamp all
        // six at once); `set` counts the sets. The lookup reads the lamp once all six faces hold the set (drawnValid), with this centre (global
        // coordinates: a region crossing does not move it) and far plane.
        U32 set = 0;               // 0: none started
        LLVector3d setCentre;
        F32 setRadius = 0.f;
        llprojectorpool::Pose setPose;
        bool drawnValid = false;
        llprojectorpool::Pose curPose, prevPose;
        bool prevPoseValid = false;
        bool lampMoving = false;   // its centre moved since the last frame
        bool setStale = false;     // its centre or face radius is not the set's
        LLVector3 centre;          // this frame's (agent)
        F32 radius = 0.f;          // this frame's
        F32 faceRadius = 0.f;      // the radius its faces are drawn for (llpointpool::faceRadius: a drop waits, a candle never redraws)
        F64 radiusBelowSince = -1.0;
        U32 base = llpointpool::kFaceMin; // the faces' size from the lamp
        int band = llprojectorpool::kMaxBand;
        F32 camDist = 0.f;         // the camera position's distance to the lamp's lit sphere
        bool seen = false;
        bool off = false;          // S2: switched off, black or at radius 0: its faces are held (not drawn, not resized) ...
        F64 offSince = 0.0;        // ... since then (llprojectorpool::kOffHoldSeconds)
        bool volumesValid = false;
        llprojectorpool::Pose volumePose;
        F32 volumeRadius = 0.f;
        bool wholeItem = false;    // this frame's item draws the whole lamp
        bool ownEvery = false;     // this frame: one of the kOwnEveryFrameLamps lamps that light the own avatar most (P2)
        U32 ownRank = 0;           // its place in the strength order at the own avatar (0 the strongest), when it lights it
    };

    bool allocateAtlases(U32 size);
    void freeAtlases();
    void updateLightSet(LLCamera& camera);
    void layout(F32 dt_seconds);
    bool compact();
    void evictFor(std::vector<Lamp*>& waiting);
    void resetFaces(Lamp& lamp);
    bool resizeDue(const Face& face) const;
    bool lampDrawn(const Lamp& lamp) const;
    void copyTile(U32 from_tex, const llprojectorpool::Rect& src, U32 to_tex, const llprojectorpool::Rect& dst);
    void copyTileTexels(const llprojectorpool::Rect& tile, const llprojectorpool::TexelRect& texels);
    void startSet(Lamp& lamp);
    bool renderLamp(Lamp& lamp, llprojectorpool::Cause cause);
    bool renderFace(Lamp& lamp, int f, const LLVector3& centre, F32 radius, llprojectorpool::Cause cause);
    bool renderLayer(Lamp& lamp, int f);
    struct FaceAvatars
    {
        std::vector<const LLVOAvatar*> avatars;
        llprojectorpool::TexelRect rect;
    };
    FaceAvatars faceAvatars(const Face& face, const float clip[16], const LLVector3& centre) const;
    F32 drawAvatars(Lamp& lamp, int f, const FaceAvatars& avatars, const LLVector3& centre, F32 radius);
    void setFaceTarget(U32 fbo, const llprojectorpool::Rect& rect);
    void dropLamp(std::map<U32, Lamp>::iterator it);
    void holdOff(Lamp& lamp, const LLVector3& cam_pos, F32 reach, F64 now);
    void unpinFace(U32 key);
    void lookupUniforms(const LLDrawable* light, F32 lamp[4], F32 faces[8]) const;

    U32 mAtlasS = 0;   // the faces as drawn: the fixed layer
    U32 mAtlasL = 0;   // the live atlas: S plus the avatars (the lookups read it)
    U32 mFboS = 0;
    U32 mFboL = 0;
    U32 mAtlasSize = 0;

    llprojectorpool::BuddyAtlas mBuddy;
    llprojectorpool::LevelController mLevel;
    llprojectorpool::GrowthHold mGrowthHold;
    std::vector<llprojectorpool::ValveTile> mValveTiles; // layout()'s scratch
    std::vector<Face*> mValveFaces;
    std::map<U32, Lamp> mLamps;                          // by lamp id
    std::unordered_map<const LLDrawable*, U32> mIdOf;
    U32 mNextId = 1;
    U32 mFrame = 0;
    U32 mShiftsSeen = 0;
    // one face render's CPU ms (running average), the prediction of a face never drawn: it starts at the scheduler's prior for an item
    // without history (kDefaultItemMs); the frame's in-render check bounds what a low first guess can let through to one face over the cap
    F32 mCostEmaMs = llprojectorpool::kDefaultItemMs;
    F64 mNextAllocTry = 0.0;
    bool mAllocFailed = false;
    F64 mAllocBackoff = 5.0;
    F32 mLastFrameRenderMs = 0.f;

    llprojectorpool::PinBook mPins;
    llprojectorpool::ChangeIndex mIndex;
    // the face being drawn (pinCasters and skipEmitter read them; null outside a point face render)
    const Face* mRenderingFace = nullptr;
    U32 mRenderingKey = 0;
    llprojectorpool::Pyramid mRenderVolume;
    std::vector<LLVertexBuffer::SkipRange> mSkip;
    const std::vector<LLLocalShadowCaster>* mCasters = nullptr; // the frame's casting avatars (prepare to endFrame)
};

#endif // LL_LLPOINTSHADOWS_H
