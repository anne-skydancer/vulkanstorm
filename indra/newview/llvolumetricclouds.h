/**
 * @file llvolumetricclouds.h
 * @brief The volumetric clouds (stage L7 of the light study, doc/light/volumetric-clouds.md): the noise's upload,
 *        the weather map, the trace, the temporal reconstruction and the composite, as OpenGL fragment passes.
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

#ifndef LL_LLVOLUMETRICCLOUDS_H
#define LL_LLVOLUMETRICCLOUDS_H

#include "llcloudscape.h"
#include "llrendertarget.h"

#include <glm/mat4x4.hpp>

#include <memory>

class LLGLSLShader;

// The stack (owner, 2026-11-18): the physical clear base sky, the automatically generated volumetric clouds (this
// class), the cloud kind and variability layer (llcloudscape.h, from the EEP cloud sliders), the EEP atmospheric
// filter (the clouds' light and haze are the flat layer's own terms, read from the sky's uniforms). Everything lives in
// the passes' uniforms and textures, so a photo (snapshot, tiled high resolution, 360, saved) draws the same clouds.
//
// With RenderVolumetricClouds on (the Preferences toggle, on by GPU class through the feature tables), once the noise
// file is loaded (a worker thread) and uploaded (8 slices a frame), the main view and the photos draw these clouds and
// the sky's pool skips the flat layer there; cube snapshots (reflection probes, mirrors) keep the flat layer. The cloud
// shadow map takes its producer from here while active (llcloudshadowmap.cpp). A damaged or missing noise file means
// the flat layer, with a notice once per session.
//
// Main (GL) thread only. GL objects are made on first use and released by release() (LLPipeline::releaseGLBuffers).
class LLVolumetricClouds
{
public:
    LLVolumetricClouds();
    ~LLVolumetricClouds();

    // Once a frame (the first call of the frame does the work): whether the clouds draw this frame, the noise's load
    // and upload, the bands from the sky's sliders and the weather map's refresh. It follows the agent's camera (not
    // the view being rendered), like the cloud shadow map.
    void update();

    // Whether the main view and the photos draw the volumetric clouds this frame (then the flat layer is skipped there).
    bool active();

    // The trace (live: a quarter of the half-resolution pixels a frame, then the reconstruction; a photo, gSnapshot: the
    // whole frame in one still pass) and the composite onto the sky's pixels of the bound HDR target (`width` x
    // `height`, its depth the deferred depth). `deferred` holds the deferred depth the trace reads. Not in cube
    // snapshots (it does nothing there).
    void render(LLRenderTarget& deferred, U32 width, U32 height);

    // The cloud shadow map's producer: the bands and the noise for `shader` (a program linked with
    // cloudsVolFuncsF.glsl). False when the clouds are not active this frame. unbindNoise() after the draw.
    bool bindBands(LLGLSLShader& shader);
    void unbindNoise(LLGLSLShader& shader);

    // Every GL object (main thread; LLPipeline::releaseGLBuffers). They are made again on the next use.
    void release();

private:
    struct NoiseLoad;

    enum class NoiseState
    {
        NONE,
        LOADING,
        UPLOADING,
        READY,
        FAILED
    };

    bool programsReady() const;
    bool ensureNoise();
    void uploadNoise();
    void bindNoise(LLGLSLShader& shader);
    void setBandUniforms(LLGLSLShader& shader);
    void drawWeather(LLRenderTarget& target, U32 first_row, U32 rows);
    bool refreshWeather();
    bool allocateLive(U32 width, U32 height);
    void releaseTargets();

    NoiseState mNoiseState = NoiseState::NONE;
    std::shared_ptr<NoiseLoad> mLoad;
    U32 mUploadZ = 0;
    U32 mBaseTex = 0;
    U32 mErosionTex = 0;
    U32 mCurlTex = 0;
    double mSliceMean[LLCloudscape::NOISE_SLICES] = {}; // the base noise's G channel, per z slice

    // the weather map, redrawn in strips, double-buffered: the trace reads the front; a new size or period (cloud
    // variance turned on or off) is built in the back a strip a frame and swapped in, so nothing flashes and no frame
    // draws a whole 2048^2 map
    LLRenderTarget mWeather[2];
    U32 mWeatherFront = 0;
    bool mFrontValid = false;   // the front holds a whole map
    U32 mFrontSize = 0;         // its side in texels
    S32 mFrontTiles = 0;        // its period in the creator's tiles
    bool mBackBuilding = false; // the back is being built
    U64 mBackKey = 0;           // what the back's first strip was made of
    U64 mWeatherKey = 0;        // what the front's last cycle of strips started from
    U64 mWeatherNextKey = 0;    // what the map is made of now
    U32 mWeatherRow = 0;        // the next strip of the cycle (front) or of the build (back)
    bool mWeatherCycle = false;
    F64 mWeatherGrowthStep = -1.0;

    // live: the trace (RGBA16F + R16F, W/4 x H/4) and the half-resolution history (ping-pong); photos: the still target
    LLRenderTarget mTrace;
    LLRenderTarget mHistory[2];
    LLRenderTarget mStill;
    U32 mLiveWidth = 0;
    U32 mLiveHeight = 0;
    U32 mHistoryIndex = 0;
    bool mHistoryValid = false;
    U32 mFrame = 0;
    U64 mLastLiveFrame = U64(-1);
    glm::mat4 mPrevVP = glm::mat4(1.f);
    F64 mPrevCamera[2] = { 0.0, 0.0 };

    // this frame's bands and their uniforms (cloudsVolFuncsF.glsl)
    LLCloudscape::Scape mScape;
    LLCloudscape::Sliders mSliders;
    F32 mLayer[12] = {};
    F32 mShape[12] = {};
    F32 mPlace[12] = {};
    F32 mNoise[12] = {};
    F32 mMarch[12] = {};
    F32 mMap[4] = {};
    F32 mLight[4] = {};
    F32 mGain[4] = {};
    F64 mCamera[2] = { 0.0, 0.0 }; // the agent camera (global, km east and north)

    // the weather pass's uniforms
    F32 mWeatherSky[4] = {};
    F32 mWeatherDensity1[4] = {};
    F32 mWeatherDensity2[4] = {};
    F32 mWeatherCover[4] = {};
    F32 mWeatherGrowth[4] = {};
    F32 mWeatherMean[4] = {};

    F64 mGrowthTime = 0.0; // s on the scroll clock (paused with the scroll)
    F64 mLastClock = -1.0;
    U64 mUpdatedFrame = U64(-1);
    bool mActive = false;
};

#endif // LL_LLVOLUMETRICCLOUDS_H
