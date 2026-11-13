/**
 * @file llstochastichistory.h
 * @brief The stochastic engine: the mask table and counted textures, and one producer's temporal history
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

#ifndef LL_LLSTOCHASTICHISTORY_H
#define LL_LLSTOCHASTICHISTORY_H

#include "llgl.h"
#include "llstochasticstate.h"
#include "glm/mat4x4.hpp"

class LLGLSLShader;

// The stochastic engine is shared by the producers that sample stochastically and accumulate over
// the frames: stochastic transparency (doc/oit/stochastic-transparency.md) and the Stochastic shadow
// method (doc/shadows/shadow-method.md). Its GLSL half is deferred/stochasticSampleF.glsl (the hash,
// the per-frame seed, the mask lookup in the table below: the transparency's) and
// deferred/stochasticResolveUtilF.glsl (the temporal resolve, shared); a producer adds its own
// stoch_current() and main(). The shadows' rays have their own noise; they take the history, the
// reprojection and the clamp from here. Its C++ half is this file: the S = 8 mask table, textures counted
// in the viewer's texture memory, and LLStochasticHistory.
//
// Main render thread only: everything here is made, used and freed by LLPipeline (the screen
// buffer allocation and release, the alpha pool's passes, shiftObjects).
namespace LLStochasticEngine
{
    // SAMPLES (8), SEEDS (2048) and maskTable() are in llstochasticstate.h (GL-free, unit tested)

    // The device runs stochastic transparency: GL 4.0 (gl_SampleMask, multisample textures) and GLSL 4.00.
    // One definition for the pipeline's allocation and the shader manager; logs once when a device
    // below it cannot (sorted alpha is used there: a capability gate, not a run-time fallback).
    bool deviceSupported();

    // No run-time fallback (owner decision Q8, 2026-11-13, replacing the 2026-11-12 exception): a stochastic
    // method never switches to another one on failure. A failed target allocation or shader build is logged,
    // the selected method stays, and the next natural point retries (LLPipeline::retryAlphaStochastic, the
    // next shader reload); meanwhile the pass that cannot run does no harm. Only a device below GL 4.0
    // (deviceSupported() false) runs Legacy and sorted alpha, by capability, as always.
    inline bool usable() { return deviceSupported(); }

    // Which resolve runs: the fragment resolve with its pre-pass target (option P; 160 B/px) on every
    // device, never changed at run time. OpenGL compute dispatches, with the glMemoryBarrier they need
    // between graphics passes, stall the frame while the camera or avatar moves, and OpenGL has no
    // async compute; compute belongs in the Vulkan renderer. The compute resolve (GL 4.3, GLSL 4.30,
    // entry points loaded; 144 B/px) stays compiled but unused, selected only for A/B tests by the
    // environment variable VKSTORM_STOCH_COMPUTE_RESOLVE=1 on a device that supports it (debug only,
    // not a setting). The two are the same method and give the same image (bit for bit on the
    // measured NVIDIA). A shader set of the selected path that does not build is an error (logged,
    // retried at the next shader reload); the other path is never taken in its place.
    // (The plain alpha pass then draws the blended faces meanwhile, see LLDrawPoolAlpha.)
    bool computeResolve();

    // The image unit a compute resolve writes the history through: the shader declares binding = 0
    // (alphaStochasticResolveC.glsl); image units are not texture units, LLTexUnit does not see them.
    constexpr U32 RESOLVE_IMAGE_UNIT = 0;

    // The one S = SAMPLES table (R32UI, SEEDS x (S + 1)), made by the first producer to ask and
    // freed with the last. False: it could not be made (nothing is kept).
    bool acquireMasks();
    void releaseMasks();
    U32  getMasks();                // the texture name, 0 when none is held

    // Textures made and freed here are counted in the viewer's texture memory at their real size
    // (LLImageGLMemory::alloc_tex_image_exact / free_tex_image, which read the texture bound on
    // gGL's unit 0): the engine's histories, the 8-sample targets, the mask table. 0: not made
    // (the GL error is read right after the allocation, and logged). counted_format sizes the
    // bytes where the storage format is not one LLImageGL knows or stores more than its bits
    // (DEPTH_COMPONENT24 is kept in 32 bits). data_format / data_type describe `data`; the defaults
    // suit the float formats and nullptr, an integer format needs its own (GL_RED_INTEGER,
    // GL_UNSIGNED_INT).
    U32  newTexture2D(GLenum format, U32 width, U32 height, GLenum counted_format,
                      const void* data = nullptr, GLenum data_format = GL_RGBA, GLenum data_type = GL_FLOAT);
    U32  newTextureMultisample(GLenum format, U32 width, U32 height, U32 samples, GLenum counted_format);
    void deleteTexture(U32& name);  // no unit keeps it as its current texture; uncounted; deleted; name = 0
}

// One producer's temporal history: two full-size colours (this frame's is written while the last
// frame's is read), made up front (method select, resize), the frame
// counter of the producer's seeds, and the rules that make the last frame's colour usable.
// A producer owns one instance. The frame protocol, in the producer's passes:
//   beginFrame(); the passes draw with seedFrame(); then, if temporal(), the resolve runs with
//   readTexture() bound and setResolveUniforms() set: a compute resolve with bindWriteImage() bound, is
//   dispatched, and endComputeWrite() follows it; a fragment resolve draws into writeFramebuffer();
//   endFrame() makes the result the latest (latestTexture()).
// The framebuffers exist only where the producer's resolve is a fragment pass (allocate(w, h, true)).
// A device runs one kind of writer for the history. A producer that writes by rendering after compute
// stores would also need GL_FRAMEBUFFER_BARRIER_BIT (framebuffer reads and writes after the stores).
class LLStochasticHistory
{
public:
    // format: GL_RGBA16F for the transparency and the shadows. weight: this producer's blend weight of the current frame.
    explicit LLStochasticHistory(GLenum format, F32 weight = 1.f / 8.f) : mFormat(format), mWeight(weight) {}
    // no GL in the destructor (the pipeline is a global that outlives the context): release() first;
    // the destructor warns if it was not
    ~LLStochasticHistory();
    LLStochasticHistory(const LLStochasticHistory&) = delete;
    LLStochasticHistory& operator=(const LLStochasticHistory&) = delete;

    // Both textures and their framebuffers, counted in the texture memory. False: nothing is kept,
    // and the producer drops its accumulation (the transparency keeps its S = 8 samples).
    // framebuffers: the producer's resolve is a fragment pass (writeFramebuffer()); a compute resolve needs none.
    bool allocate(U32 width, U32 height, bool framebuffers = false);
    void release();                                         // freed, uncounted, invalid
    bool isAllocated() const { return mTexture[0] && mTexture[1]; }
    bool matches(U32 width, U32 height) const { return isAllocated() && mState.sized(width, height); }

    void invalidate() { mState.invalidate(); }              // the origin shifted; the last view is in the old frame

    // A new frame of the producer. temporal: this frame accumulates (a history exists and
    // `enabled`, the producer's resolve being ready); then the seeds change every frame and
    // historyUsed() says whether the last frame's colour is read: it is when that frame ran the
    // method at this size (the stamp is the frame before this one).
    bool beginFrame(bool enabled = true);
    bool temporal() const { return mState.temporal(); }
    bool historyUsed() const { return mState.used(); }
    U32  seedFrame() const { return mState.seedFrame(); }   // the producer's frame counter, never 0 while temporal

    U32  readTexture() const { return mTexture[mState.index()]; }          // the last frame's colour

    // The compute resolve's write: bindWriteImage binds the texture that was not read as a write-only
    // image at `unit` (the format is the shader's rgba16f; asserted); endComputeWrite follows the
    // dispatch. Memory model (GL 4.3 section 7.12): imageStore writes are incoherent, no later command
    // sees them until a barrier names the access that reads them:
    //  - GL_TEXTURE_FETCH_BARRIER_BIT: texture fetches after the barrier see the stores, namely the
    //    composite of this frame and the next frame's resolve (it reads this result as its history).
    //  - GL_SHADER_IMAGE_ACCESS_BARRIER_BIT: image stores after the barrier wait for the accesses before
    //    it, texture fetches included (write after read). The two textures alternate, so the texture
    //    stored now is stored again two frames later, after this frame's composite and the next frame's
    //    resolve fetched it; the next frame's barrier lies between those fetches and that store.
    // Nothing else reads the history after the dispatch (no framebuffer, buffer or texture-update
    // access); the dispatch's own reads of the accumulation and depth, written by rendering into a
    // framebuffer, are ordered by the GL without a barrier (only image stores, atomics and storage
    // buffers are incoherent).
    U32  writeFramebuffer() const { return mFramebuffer[1 - mState.index()]; }   // the fragment resolve's target

    void bindWriteImage(U32 unit = LLStochasticEngine::RESOLVE_IMAGE_UNIT) const;
    void endComputeWrite(U32 unit = LLStochasticEngine::RESOLVE_IMAGE_UNIT) const;
    U32  latestTexture() const { return mTexture[mState.index()]; }        // after endFrame: this frame's, the composite's input

    // stoch_reproj (this frame's clip space to the last frame's: last projection x last view x
    // inverse(projection x view), from the current matrices), stoch_weight and stoch_history_valid.
    // The shader is bound. Takes this frame's camera for endFrame().
    void setResolveUniforms(LLGLSLShader& shader);
    // Takes this frame's camera without a shader (setResolveUniforms does it; so does a unit test).
    void setCamera(const glm::mat4& modelview, const glm::mat4& projection);
    glm::mat4 reprojection() const;                         // the stoch_reproj of the camera set last
    void endFrame();                                        // swap, valid, stamp, this frame's camera kept (the current one if not set)

    U64  bytes() const;                                     // the two textures, as counted

private:
    GLenum mFormat;
    F32 mWeight;
    U32 mTexture[2] = { 0, 0 };
    U32 mFramebuffer[2] = { 0, 0 };    // only with allocate(w, h, true)
    U32 mWidth = 0;
    U32 mHeight = 0;
    LLStochasticFrameState mState;
    bool mCameraSet = false;    // setResolveUniforms / setCamera ran this frame
    glm::mat4 mLastModelView = glm::mat4(1.f);
    glm::mat4 mLastProjection = glm::mat4(1.f);
    glm::mat4 mModelView = glm::mat4(1.f);     // this frame's, taken by setResolveUniforms for endFrame
    glm::mat4 mProjection = glm::mat4(1.f);
};

#endif // LL_LLSTOCHASTICHISTORY_H
