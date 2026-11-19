/**
 * @file llatmosphereluts.h
 * @brief Stage AT2-1: the physical atmosphere's tables and the EEP filter's tables on the GPU
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

#ifndef LL_LLATMOSPHERELUTS_H
#define LL_LLATMOSPHERELUTS_H

#include "llrendertarget.h"

#include <cstdint>
#include <memory>

// Stage AT2-1 of the atmosphere (doc/light/atmosphere-study.md section 7): the tables, with no visible change; AT2-2 draws the sky
// from them. The physics is one clear high-pressure day under every EEP sky (llatmospherebase.h), so its transmittance T (256 x 64),
// multiple scattering Psi (32 x 32, a split pass and a reduction) and sky irradiance E (64 x 16, the same) are built once; the sky view
// (192 x 108: the base's sky from an observer on the ground, the slab-split march) follows the lights, and the EEP filter's A and B
// tables (192 x 108 each, today's look per direction, llskyfilter.h) and the cast-light level k follow the sky. Each rebuild is a few
// fragment passes on the GPU when its quantised key changes; at steady state a frame costs a key and a 512-byte compare.
//
// Everything is the main (GL) thread's. The GL objects are created when first needed and released by release() from
// LLPipeline::releaseLUTBuffers (shader reloads, restoreGL, the GL shutdown): never from a shutdown() that worker GL contexts run.
class LLAtmosphereLUTs
{
public:
    LLAtmosphereLUTs();
    ~LLAtmosphereLUTs();

    // Once per main frame, right after the sky's uniforms are set (Env Update): the `Atmosphere` block (the lights and the camera height)
    // and every table whose key changed. Not during a cube snapshot.
    void update();

    // The tables exist: builds any that are missing now, so that the first view that needs them (a mirror or a hero probe ahead of the
    // main frame's update) never reads tables that were never built. Inside a cube snapshot the filter's tables are left to the main
    // frame (the sky's group has the main view's uniforms only there).
    void ensureTables();

    void release();

    bool isBuilt() const { return mBaseBuilt && mSkyViewKey != 0 && mFilterKey != 0; }

    // the uniform buffer the table passes and AT2-2's sky read (bound at LLGLSLShader::UB_ATMOSPHERE)
    U32 getBlockBuffer() const { return mUBO; }
    LLRenderTarget& transmittance() { return mTrans; }
    LLRenderTarget& multipleScattering() { return mMs; }
    LLRenderTarget& skyIrradiance() { return mE; }
    LLRenderTarget& skyView() { return mSkyView; }
    LLRenderTarget& filterTables() { return mFilter; }   // attachment 0 A, 1 B
    LLRenderTarget& castLight() { return mCastLight; }   // 1 x 1: base irradiance, filtered irradiance, k, defined

private:
    bool allocate();
    bool updateBlock(bool force);
    void buildBase();
    void buildSkyView();
    void buildFilter();
    void buildCastLight();
    void build(bool allow_filter);

    // the clear-day base (its lights follow the sky) and the block as last uploaded: llatmospherebase.h, kept out of this header
    // (pipeline.h includes it)
    struct State;
    std::unique_ptr<State> mState;
    U32 mUBO = 0;
    LLRenderTarget mTrans;
    LLRenderTarget mMsPart;                       // 256 x 256, two attachments: L and the transfer per direction (only while the base is built)
    LLRenderTarget mMs;
    LLRenderTarget mEPart;                        // 512 x 128 (only while the base is built)
    LLRenderTarget mE;
    LLRenderTarget mSkyView;
    LLRenderTarget mFilter;
    LLRenderTarget mRows;                         // 1 x 54: the cast-light sums per row
    LLRenderTarget mCastLight;
    bool mAllocated = false;
    bool mBlockValid = false;
    bool mBaseBuilt = false;
    bool mFailed = false;                         // a program is missing: logged once, nothing is built
    uint64_t mSkyViewKey = 0;                     // 0: not built
    uint64_t mFilterKey = 0;
    uint64_t mCastLightKey = 0;
    uint64_t mLightKey = 0;                       // the lights of the current block
    U64 mUpdatedFrame = U64(-1);
};

#endif // LL_LLATMOSPHERELUTS_H
