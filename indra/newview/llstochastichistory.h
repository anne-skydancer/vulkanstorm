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
#include "glm/mat4x4.hpp"

#include <vector>

class LLGLSLShader;

// The stochastic engine is shared by the producers that sample stochastically and accumulate over
// the frames: stochastic transparency now (doc/oit/stochastic-transparency.md), the Stochastic
// shadow method later. Its GLSL half is deferred/stochasticSampleF.glsl (the hash, the per-frame
// seed, the mask lookup in the table below) and deferred/stochasticResolveUtilF.glsl (the temporal
// resolve); a producer adds its own stoch_current() and main(). Its C++ half is this file: the
// S = 8 mask table, textures counted in the viewer's texture memory, and LLStochasticHistory.
//
// Main render thread only: everything here is made, used and freed by LLPipeline (the screen
// buffer allocation and release, the alpha pool's passes, shiftObjects).
namespace LLStochasticEngine
{
    constexpr U32 SAMPLES = 8;      // samples in all; the table's S
    constexpr U32 SEEDS = 2048;     // columns of the table (STOCH_SEEDS in stochasticSampleF.glsl)

    // The table of masks for `samples` bits: entry [r][seed] is a random r-subset of the bits, from a
    // fixed generator (splitmix64, a partial Fisher-Yates shuffle), the same on every launch: the
    // author's Aurora Viewer, oit_stochastic.rs mask_table.
    std::vector<U32> maskTable(U32 samples);

    // The one S = SAMPLES table (R32UI, SEEDS x (S + 1)), made by the first producer to ask and
    // freed with the last. False: it could not be made (nothing is kept).
    bool acquireMasks();
    void releaseMasks();
    U32  getMasks();                // the texture name, 0 when none is held

    // Textures made and freed here are counted in the viewer's texture memory
    // (LLImageGLMemory::alloc_tex_image / free_tex_image, which read the texture bound on gGL's
    // unit 0): the engine's histories, the 8-sample targets, the mask table. 0: not made.
    // counted_format sizes the bytes where the storage format is not one LLImageGL knows or stores
    // more than its bits (DEPTH_COMPONENT24 is kept in 32 bits).
    U32  newTexture2D(GLenum format, U32 width, U32 height, GLenum counted_format,
                      const void* data = nullptr, GLenum data_format = GL_RGBA, GLenum data_type = GL_FLOAT);
    U32  newTextureMultisample(GLenum format, U32 width, U32 height, U32 samples, GLenum counted_format);
    void deleteTexture(U32& name);  // no unit keeps it as its current texture; uncounted; deleted; name = 0
}

// One producer's temporal history: two full-size colours (this frame's is written while the last
// frame's is read) and their framebuffers, made up front (method select, resize), the frame
// counter of the producer's seeds, and the rules that make the last frame's colour usable.
// A producer owns one instance. The frame protocol, in the producer's passes:
//   beginFrame(); the passes draw with seedFrame(); then, if temporal(), the resolve writes
//   writeFramebuffer() with readTexture() bound and setResolveUniforms() set, and endFrame()
//   makes the result the latest (latestTexture()).
class LLStochasticHistory
{
public:
    explicit LLStochasticHistory(GLenum format) : mFormat(format) {}   // GL_RGBA16F for the transparency
    // no GL in the destructor (the pipeline is a global that outlives the context): release() first
    ~LLStochasticHistory() = default;
    LLStochasticHistory(const LLStochasticHistory&) = delete;
    LLStochasticHistory& operator=(const LLStochasticHistory&) = delete;

    // Both textures and their framebuffers, counted in the texture memory. False: nothing is kept,
    // and the producer drops its accumulation (the transparency keeps its S = 8 samples).
    bool allocate(U32 width, U32 height);
    void release();                                         // freed, uncounted, invalid
    bool isAllocated() const { return mTexture[0] && mTexture[1]; }
    bool matches(U32 width, U32 height) const { return isAllocated() && mWidth == width && mHeight == height; }

    void invalidate() { mValid = false; }                   // the origin shifted; the last view is in the old frame

    // A new frame of the producer. temporal: this frame accumulates (a history exists and
    // `enabled`, the producer's resolve being ready); then the seeds change every frame and
    // historyUsed() says whether the last frame's colour is read: it is when that frame ran the
    // method at this size (the stamp is the frame before this one).
    bool beginFrame(bool enabled = true);
    bool temporal() const { return mTemporal; }
    bool historyUsed() const { return mUsed; }
    U32  seedFrame() const { return mTemporal ? mFrame : 0; }   // the producer's frame counter, never 0 while temporal

    U32  readTexture() const { return mTexture[mIndex]; }          // the last frame's colour
    U32  writeFramebuffer() const { return mFramebuffer[1 - mIndex]; }
    U32  latestTexture() const { return mTexture[mIndex]; }        // after endFrame: this frame's, the composite's input

    // stoch_reproj (this frame's clip space to the last frame's: last projection x last view x
    // inverse(projection x view), from the current matrices), stoch_weight (1/8) and
    // stoch_history_valid. The shader is bound.
    void setResolveUniforms(LLGLSLShader& shader);
    void endFrame();                                        // swap, valid, stamp, this frame's camera kept

    U64  bytes() const;                                     // the two textures, as counted

private:
    GLenum mFormat;
    U32 mTexture[2] = { 0, 0 };
    U32 mFramebuffer[2] = { 0, 0 };
    U32 mWidth = 0;
    U32 mHeight = 0;
    U32 mIndex = 0;             // the latest
    U32 mStamp = 0;             // the frame count of the last resolve
    U32 mFrame = 0;             // the frame counter of the seeds, never 0
    bool mValid = false;
    bool mTemporal = false;
    bool mUsed = false;
    glm::mat4 mLastModelView = glm::mat4(1.f);
    glm::mat4 mLastProjection = glm::mat4(1.f);
    glm::mat4 mModelView = glm::mat4(1.f);     // this frame's, taken by setResolveUniforms for endFrame
    glm::mat4 mProjection = glm::mat4(1.f);
};

#endif // LL_LLSTOCHASTICHISTORY_H
