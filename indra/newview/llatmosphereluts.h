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
// tables (1 x 108 each, today's look per elevation, llskyfilter.h) and the cast-light level k follow the sky. Each rebuild is a few
// fragment passes on the GPU when its key changes: A and B on any change of their inputs (every frame while a day cycle moves the sun),
// the rest on a quantised key. At steady state a frame costs the keys and a 512-byte compare.
//
// The filter's inputs are the dome's own uniforms (LLSkyFilter::current_inputs reads LLEnvironment's lists), taken where the lists hold
// the main view's values (every view but a reflection probe's irradiance pass) and kept: the key is formed from them and the filter's
// passes get them as their uniforms, so the key and the tables cannot disagree, and any view can build the tables.
//
// Everything is the main (GL) thread's. The GL objects are created when first needed and released by release() from
// LLPipeline::releaseLUTBuffers (shader reloads, restoreGL, the GL shutdown): never from a shutdown() that worker GL contexts run.
class LLAtmosphereLUTs
{
public:
    // The tables' sizes, the one place the viewer has them: the targets are allocated with them and the passes get them as defines
    // (llviewershadermgr.cpp). llatmosphereluts.cpp holds them to the C++ twin's (TransmittanceLUT, MS_DIRS_DEFAULT, LLSkyFilter).
    static constexpr int TRANS_W = 256, TRANS_H = 64;   // T
    static constexpr int MS_W = 32, MS_H = 32;          // Psi (LLScatter::build_ms_lut's default)
    static constexpr int E_W = 64, E_H = 16;            // E (LLScatter::build_e_lut's default)
    static constexpr int DIRS = 8;                      // directions per Psi and E texel in the split passes, 8 x 8
    static constexpr int SKY_W = 192, SKY_H = 108;      // the sky view; A and B are 1 x SKY_H, one row per elevation

    LLAtmosphereLUTs();
    ~LLAtmosphereLUTs();

    // Once per main frame, right after the sky's uniforms are set (Env Update): the filter's inputs, the `Atmosphere` block (the lights)
    // and every table whose key changed. Not during a cube snapshot.
    void update();

    // The tables exist: builds any that are missing now, so that the first view that needs them (a mirror or a hero probe ahead of the
    // main frame's update) never reads tables that were never built. The filter's need its inputs: kept from the main frame, or read in
    // this view unless it is a reflection probe's irradiance pass (only possible before the session's first main frame).
    void ensureTables();

    void release();

    bool isBuilt() const { return mBaseBuilt && mSkyViewKey != 0 && mFilterKey != 0 && mCastLightKey != 0; }

    // the uniform buffer the table passes and AT2-2's sky read (bound at LLGLSLShader::UB_ATMOSPHERE)
    U32 getBlockBuffer() const { return mUBO; }
    LLRenderTarget& transmittance() { return mTrans; }
    LLRenderTarget& multipleScattering() { return mMs; }
    LLRenderTarget& skyIrradiance() { return mE; }
    LLRenderTarget& skyView() { return mSkyView; }
    LLRenderTarget& filterTables() { return mFilter; }   // 1 x SKY_H: attachment 0 A, 1 B
    LLRenderTarget& castLight() { return mCastLight; }   // 1 x 1: base irradiance, filtered irradiance, k, defined

private:
    bool allocate();
    bool updateBlock(bool force);
    void refreshInputs();
    void buildBase();
    void buildSkyView();
    void buildFilter();
    void buildCastLight();
    void build();

    // the clear-day base (its lights follow the sky), the block as last uploaded and the filter's inputs: llatmospherebase.h and
    // llskyfilter.h, kept out of this header (pipeline.h includes it)
    struct State;
    std::unique_ptr<State> mState;
    U32 mUBO = 0;
    LLRenderTarget mTrans;
    LLRenderTarget mMsPart;                       // MS_W x DIRS by MS_H x DIRS, two attachments: L and the transfer (only while the base is built)
    LLRenderTarget mMs;
    LLRenderTarget mEPart;                        // E_W x DIRS by E_H x DIRS (only while the base is built)
    LLRenderTarget mE;
    LLRenderTarget mSkyView;
    LLRenderTarget mFilter;
    LLRenderTarget mRows;                         // 1 x SKY_H / 2: the cast-light sums per row
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
