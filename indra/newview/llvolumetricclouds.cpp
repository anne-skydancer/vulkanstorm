/**
 * @file llvolumetricclouds.cpp
 * @brief The volumetric clouds (stage L7 of the light study, doc/light/volumetric-clouds.md)
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

#include "llvolumetricclouds.h"

#include "llagentcamera.h"
#include "llcloudnoise.h"
#include "lldrawpoolwlsky.h"
#include "llenvironment.h"
#include "llframetimer.h"
#include "llglslshader.h"
#include "llimagegl.h"
#include "llnotificationsutil.h"
#include "llrender.h"
#include "llsettingssky.h"
#include "llsky.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llvosky.h"
#include "pipeline.h"
#include "workqueue.h"

#include <glm/gtc/type_ptr.hpp>

#include <atomic>

extern bool gCubeSnapshot;
extern bool gSnapshot;
extern LLPointer<LLImageGL> gEXRImage;

namespace
{
constexpr U32 BASE_SLICES_A_FRAME = 8;   // 8 x 64 KB of the 8 MB base noise a frame: 16 frames, no hitch
constexpr U32 WEATHER_STRIP = 128;       // rows of the weather map redrawn a frame while it refreshes
constexpr F64 WEATHER_GROWTH_S = 2.0;    // the growth clock's step between two refreshes of the map
constexpr F64 TELEPORT_KM = 1.0;         // a camera move (or a jump of the clouds' drift) above this drops the history
constexpr S32 STILL_SAMPLES = 4;         // a photo's sub-samples (the Aurora Viewer's still mode)
constexpr U32 STILL_STRIP_PIXELS = 1u << 20; // a photo's still trace is drawn about a megapixel at a time: one draw of a
                                             // large photo (up to 7680^2) could outlast the driver's watchdog
constexpr F32 MAX_FRAME_S = 0.5f;        // the growth clock's longest step (a stall does not age the clouds)

const LLStaticHashedString sLayer("cv_layer");
const LLStaticHashedString sShape("cv_shape");
const LLStaticHashedString sPlace("cv_place");
const LLStaticHashedString sNoise("cv_noise");
const LLStaticHashedString sMarch("cv_march");
const LLStaticHashedString sMap("cv_map");
const LLStaticHashedString sLight("cv_light");
const LLStaticHashedString sGain("cv_gain");
const LLStaticHashedString sInvVP("cv_inv_vp");
const LLStaticHashedString sPrevVP("cv_prev_vp");
const LLStaticHashedString sSizes("cv_sizes");
const LLStaticHashedString sFrame("cv_frame");
const LLStaticHashedString sDelta("cv_delta");
const LLStaticHashedString sWeatherSky("cvw_sky");
const LLStaticHashedString sWeatherDensity1("cvw_density1");
const LLStaticHashedString sWeatherDensity2("cvw_density2");
const LLStaticHashedString sWeatherCover("cvw_cover");
const LLStaticHashedString sWeatherGrowth("cvw_growth");
const LLStaticHashedString sWeatherMean("cvw_mean");

void set4(F32* v, F64 x, F64 y, F64 z, F64 w)
{
    v[0] = (F32) x;
    v[1] = (F32) y;
    v[2] = (F32) z;
    v[3] = (F32) w;
}

LLCloudscape::Tuning read_tuning()
{
    static LLCachedControl<F32> middle_scale(gSavedSettings, "RenderCloudscapeMiddleScale", 0.15f);
    static LLCachedControl<F32> high_scale(gSavedSettings, "RenderCloudscapeHighScale", 0.05f);
    static LLCachedControl<F32> congestus(gSavedSettings, "RenderCloudscapeCongestusDensity", 1.f);
    static LLCachedControl<F32> cumulonimbus(gSavedSettings, "RenderCloudscapeCumulonimbusDensity", 2.f);
    static LLCachedControl<F32> ac_cover(gSavedSettings, "RenderCloudscapeAltocumulusCover", 0.f);
    static LLCachedControl<F32> ci_base(gSavedSettings, "RenderCloudscapeCirrusBase", 0.f);
    static LLCachedControl<F32> ci_variance(gSavedSettings, "RenderCloudscapeCirrusVariance", 0.8f);
    static LLCachedControl<F32> patch_base(gSavedSettings, "RenderCloudscapePatchBase", 0.3f);
    static LLCachedControl<F32> patch_variance(gSavedSettings, "RenderCloudscapePatchVariance", 0.4f);
    static LLCachedControl<F32> growth(gSavedSettings, "RenderCloudscapeGrowth", 0.3f);
    static LLCachedControl<F32> lifetime(gSavedSettings, "RenderCloudscapeLifetime", 1200.f);
    static LLCachedControl<F32> shear(gSavedSettings, "RenderCloudscapeShearHeight", 4.f);

    // a finite value within a sane range, or the default (debug settings can hold anything); an etage threshold of 0
    // never lifts the layer, and the high one is at most the middle one (LLCloudscape::altitude_of)
    auto pick = [](F32 v, double lo, double hi, double fallback) { return std::isfinite(v) ? llclamp((double) v, lo, hi) : fallback; };
    LLCloudscape::Tuning t;
    t.middle_scale = pick(middle_scale, 0.0, 3.0, t.middle_scale);
    t.high_scale = pick(high_scale, 0.0, t.middle_scale, t.high_scale);
    t.congestus_density = pick(congestus, 0.0, 3.0, t.congestus_density);
    t.cumulonimbus_density = pick(cumulonimbus, t.congestus_density + 0.01, 3.5, t.cumulonimbus_density);
    t.altocumulus_cover = pick(ac_cover, 0.0, 1.0, t.altocumulus_cover);
    t.cirrus_base = pick(ci_base, 0.0, 1.0, t.cirrus_base);
    t.cirrus_variance = pick(ci_variance, 0.0, 1.0, t.cirrus_variance);
    t.patch_base = pick(patch_base, 0.0, 1.0, t.patch_base);
    t.patch_variance = pick(patch_variance, 0.0, 1.0, t.patch_variance);
    t.growth = pick(growth, 0.0, 1.0, t.growth);
    t.lifetime_s = pick(lifetime, 60.0, 86400.0, t.lifetime_s);
    t.shear_height_km = pick(shear, 0.5, 100.0, t.shear_height_km);
    return t;
}

void draw_screen_triangle()
{
    gPipeline.mScreenTriangleVB->setBuffer();
    gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
}

// The composite's state, for the live composite and a photo's still trace: the sky's pixels of the bound target (the
// screen triangle at the far plane, LEQUAL against the deferred depth, no depth writes), blended dst * T + L
// (GL_ONE, GL_SRC_ALPHA) with the alpha (the glow) untouched. Everything is put back as it was, the blend factors too:
// they are read, set and restored on GL itself, which leaves LLRender's cache of them (it has no getter) true; nothing
// in between draws through LLRender's blend state.
struct CompositeState
{
    LLGLDepthTest depth{ GL_TRUE, GL_FALSE, GL_LEQUAL };
    LLGLEnable blend{ GL_BLEND };
    LLGLDisable cull{ GL_CULL_FACE };
    GLint factors[4] = {};

    CompositeState()
    {
        gGL.flush();
        glGetIntegerv(GL_BLEND_SRC_RGB, &factors[0]);
        glGetIntegerv(GL_BLEND_DST_RGB, &factors[1]);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &factors[2]);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &factors[3]);
        glBlendFunc(GL_ONE, GL_SRC_ALPHA);
        gGL.setColorMask(true, false);
    }

    ~CompositeState()
    {
        gGL.flush();
        glBlendFuncSeparate((GLenum) factors[0], (GLenum) factors[1], (GLenum) factors[2], (GLenum) factors[3]);
        gGL.setColorMask(true, true);
    }
};
} // namespace

// The worker's result: the file's bytes, its parse and the base noise's slice means.
struct LLVolumetricClouds::NoiseLoad
{
    std::atomic<int> state{ 0 }; // 0 working, 1 loaded, -1 failed
    std::vector<uint8_t> bytes;
    LLCloudNoise::Parsed parsed;
    double slice_mean[LLCloudscape::NOISE_SLICES] = {};
    std::string path;
    std::string error;
};

LLVolumetricClouds::LLVolumetricClouds() = default;

// GL objects are released by release() while the context lives (LLPipeline::releaseGLBuffers); nothing here.
LLVolumetricClouds::~LLVolumetricClouds() = default;

bool LLVolumetricClouds::programsReady() const
{
    return gCloudsVolWeatherProgram.isComplete() && gCloudsVolTraceProgram.isComplete() && gCloudsVolStillProgram.isComplete()
        && gCloudsVolReconProgram.isComplete() && gCloudsVolCompositeProgram.isComplete();
}

void LLVolumetricClouds::releaseTargets()
{
    mWeather.release();
    mWeatherTiles = 0;
    mWeatherCycle = false;
    mTrace.release();
    mHistory[0].release();
    mHistory[1].release();
    mLiveWidth = mLiveHeight = 0;
    mHistoryValid = false;
}

void LLVolumetricClouds::release()
{
    releaseTargets();
    U32 textures[3] = { mBaseTex, mErosionTex, mCurlTex };
    for (U32 tex : textures)
    {
        if (tex)
        {
            // no unit may keep the name as its current texture (bindManual's cache), or a texture that reuses it
            // would never be bound
            for (U32 unit = 0; unit < LL_NUM_TEXTURE_LAYERS; ++unit)
            {
                LLTexUnit* tex_unit = gGL.getTexUnit(unit);
                if (tex_unit && tex_unit->getCurrTexture() == tex)
                {
                    tex_unit->unbind(tex_unit->getCurrType());
                }
            }
            LLImageGL::deleteTextures(1, &tex);
        }
    }
    mBaseTex = mErosionTex = mCurlTex = 0;
    // a load still running on the worker keeps its own reference and is dropped when it ends
    mLoad.reset();
    mUploadZ = 0;
    if (mNoiseState != NoiseState::FAILED)
    {
        mNoiseState = NoiseState::NONE;
    }
    mActive = false;
    mUpdatedFrame = U64(-1);
}

bool LLVolumetricClouds::ensureNoise()
{
    if (mNoiseState == NoiseState::READY)
    {
        return true;
    }
    if (mNoiseState == NoiseState::FAILED)
    {
        return false;
    }
    if (mNoiseState == NoiseState::NONE)
    {
        // read and check the 8.6 MB file off the main thread (LLCloudNoise::loadInto touches no shared state)
        mLoad = std::make_shared<NoiseLoad>();
        std::shared_ptr<NoiseLoad> load = mLoad;
        auto work = [load]()
        {
            LL_PROFILE_ZONE_NAMED("volumetric clouds: noise load");
            if (!LLCloudNoise::loadInto(load->bytes, load->parsed, load->path, load->error))
            {
                load->state.store(-1, std::memory_order_release);
                return;
            }
            // the mean of the base noise's G channel per z slice: the growth field's mean (llcloudscape.h)
            const LLCloudNoise::Texture& base = load->parsed.base;
            const size_t plane = (size_t) base.width * base.height;
            for (U32 z = 0; z < base.depth && z < (U32) LLCloudscape::NOISE_SLICES; ++z)
            {
                const uint8_t* p = base.texels + plane * 4 * z;
                uint64_t sum = 0;
                for (size_t i = 0; i < plane; ++i)
                {
                    sum += p[i * 4 + 1];
                }
                load->slice_mean[z] = (double) sum / (255.0 * (double) plane);
            }
            load->state.store(1, std::memory_order_release);
        };
        LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("General");
        mNoiseState = NoiseState::LOADING;
        if (!queue || !queue->post(work))
        {
            work(); // no worker queue (only while it shuts down): load here, once
        }
        return false;
    }
    if (mNoiseState == NoiseState::LOADING)
    {
        const int state = mLoad ? mLoad->state.load(std::memory_order_acquire) : -1;
        if (state == 0)
        {
            return false;
        }
        if (state < 0)
        {
            // what a damaged file means once the clouds draw (fatal, the flat layer with a notice, or a notice only) is
            // pending the owner's decision (doc/light/volumetric-clouds.md): until then the flat layer and a notice
            LL_WARNS("Render") << "Cloud noise file " << (mLoad ? mLoad->path : std::string()) << ": " << (mLoad ? mLoad->error : std::string())
                               << ": the flat clouds are drawn instead of the volumetric ones (reinstalling the viewer restores the file)" << LL_ENDL;
            mLoad.reset();
            mNoiseState = NoiseState::FAILED;
            static bool notice_shown = false; // once per session
            if (!notice_shown)
            {
                notice_shown = true;
                LLNotificationsUtil::add("CloudNoiseDamaged");
            }
            return false;
        }
        static_assert(LLCloudNoise::BASE_SIZE == (uint32_t) LLCloudscape::NOISE_SLICES, "the slice means follow the base noise's depth");
        std::copy(mLoad->slice_mean, mLoad->slice_mean + LLCloudscape::NOISE_SLICES, mSliceMean);

        // the textures: repeat on every axis, linear, no mipmaps (the trace reads level 0)
        const LLCloudNoise::Parsed& noise = mLoad->parsed;
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
        LLImageGL::generateTextures(1, &mBaseTex);
        LLImageGL::generateTextures(1, &mErosionTex);
        LLImageGL::generateTextures(1, &mCurlTex);
        LLTexUnit* unit = gGL.getTexUnit(0);
        auto params = [](GLenum target)
        {
            glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_REPEAT);
            if (target == GL_TEXTURE_3D)
            {
                glTexParameteri(target, GL_TEXTURE_WRAP_R, GL_REPEAT);
            }
            glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, 0);
        };
        // each texture is counted in the texture memory (exact sizes; LLImageGL::deleteTextures frees the count)
        unit->bindManual(LLTexUnit::TT_TEXTURE_3D, mBaseTex);
        unit->activate();
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, noise.base.width, noise.base.height, noise.base.depth);
        LLImageGLMemory::alloc_tex_image_exact(noise.base.width, noise.base.height, GL_RGBA8, noise.base.depth);
        params(GL_TEXTURE_3D);
        unit->unbind(LLTexUnit::TT_TEXTURE_3D);
        unit->bindManual(LLTexUnit::TT_TEXTURE_3D, mErosionTex);
        unit->activate();
        glTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, noise.erosion.width, noise.erosion.height, noise.erosion.depth);
        LLImageGLMemory::alloc_tex_image_exact(noise.erosion.width, noise.erosion.height, GL_RGBA8, noise.erosion.depth);
        glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, noise.erosion.width, noise.erosion.height, noise.erosion.depth, GL_RGBA, GL_UNSIGNED_BYTE, noise.erosion.texels);
        params(GL_TEXTURE_3D);
        unit->unbind(LLTexUnit::TT_TEXTURE_3D);
        unit->bindManual(LLTexUnit::TT_TEXTURE, mCurlTex);
        unit->activate();
        glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, noise.curl.width, noise.curl.height);
        LLImageGLMemory::alloc_tex_image_exact(noise.curl.width, noise.curl.height, GL_RGBA8, 1);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, noise.curl.width, noise.curl.height, GL_RGBA, GL_UNSIGNED_BYTE, noise.curl.texels);
        params(GL_TEXTURE_2D);
        unit->unbind(LLTexUnit::TT_TEXTURE);
        mUploadZ = 0;
        mNoiseState = NoiseState::UPLOADING;
    }
    uploadNoise();
    if (mUploadZ < LLCloudNoise::BASE_SIZE)
    {
        return false;
    }
    mLoad.reset(); // the 8.6 MB of the file
    mNoiseState = NoiseState::READY;
    return true;
}

void LLVolumetricClouds::uploadNoise()
{
    if (!mLoad || !mBaseTex)
    {
        return;
    }
    LL_PROFILE_ZONE_NAMED("volumetric clouds: noise upload");
    const LLCloudNoise::Texture& base = mLoad->parsed.base;
    const U32 z0 = mUploadZ;
    const U32 z1 = llmin(z0 + BASE_SLICES_A_FRAME, (U32) base.depth);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
    LLTexUnit* unit = gGL.getTexUnit(0);
    unit->bindManual(LLTexUnit::TT_TEXTURE_3D, mBaseTex);
    glTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, z0, base.width, base.height, z1 - z0, GL_RGBA, GL_UNSIGNED_BYTE,
                    base.texels + (size_t) base.width * base.height * 4 * z0);
    unit->unbind(LLTexUnit::TT_TEXTURE_3D);
    mUploadZ = z1;
}

void LLVolumetricClouds::update()
{
    const U64 frame = LLFrameTimer::getFrameCount();
    if (mUpdatedFrame == frame)
    {
        return;
    }
    mUpdatedFrame = frame;
    const bool was_active = mActive;
    mActive = false;

    static LLCachedControl<bool> enabled(gSavedSettings, "RenderVolumetricClouds", false);
    if (!enabled)
    {
        if (mNoiseState != NoiseState::NONE || mWeather.isComplete() || mLiveWidth)
        {
            // off: the noise (8.5 MB of VRAM) and the targets go; a failed file is tried again when it is turned on
            mNoiseState = NoiseState::NONE;
            release();
            mUpdatedFrame = frame;
        }
        return;
    }

    // the conditions under which the flat layer draws (LLDrawPoolWLSky::renderDeferred and renderSkyCloudsDeferred), for
    // the main view: this runs once a frame whichever view asks first, so the HDRI test is the main view's, not
    // use_hdri_sky()'s
    static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    if (!psky || !gPipeline.canUseWindLightShaders() || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY)
        || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) || !gSky.mVOSkyp || !gSky.mVOSkyp->getCloudNoiseTex()
        || (gEXRImage.notNull() && hdri_split > 0.f) || !programsReady())
    {
        return;
    }
    if (!ensureNoise())
    {
        return;
    }

    // the cloud kind and variability layer
    mSliders.coverage = psky->getCloudShadow();
    mSliders.scale = psky->getCloudScale();
    mSliders.variance = psky->getCloudVariance();
    const LLColor3 density1 = psky->getCloudPosDensity1();
    const LLColor3 density2 = psky->getCloudPosDensity2();
    mSliders.density = density1.mV[2];
    mSliders.detail = density2.mV[2];
    mScape = LLCloudscape::make(mSliders, read_tuning(), true);
    if (mScape.count == 0)
    {
        return; // the flat layer draws nothing either
    }

    // the scroll clock: the growth stops with the scroll (paused, or a sky whose clouds stand still)
    const F64 now = LLFrameTimer::getElapsedSeconds();
    const LLVector2 rate = psky->getCloudScrollRate();
    if (mLastClock >= 0.0 && !LLEnvironment::instance().isCloudScrollPaused() && !rate.isExactlyZero())
    {
        mGrowthTime += llclamp(now - mLastClock, 0.0, (F64) MAX_FRAME_S);
    }
    mLastClock = now;

    // the agent's camera (not the view being rendered: probes and mirrors read the same clouds), km east and north
    const LLVector3d camera = gAgentCamera.getCameraPositionGlobal();
    mCamera[0] = camera.mdV[VX] / 1000.0;
    mCamera[1] = camera.mdV[VY] / 1000.0;

    // the drift: the flat layer's scroll (cloudsF.glsl: the pattern moves east with the scroll's x, north with its
    // y; llsettingsvo.cpp flips x into the uniform), in km of the creator's tile, faster aloft (wind shear)
    const LLVector2 scroll = LLEnvironment::instance().getCloudScrollDelta();
    const F64 tile = mScape.tile_km;
    mDrift[0] = (F64) scroll.mV[VX] * tile;
    mDrift[1] = (F64) scroll.mV[VY] * tile;
    F64 wind_x = rate.mV[VX];
    F64 wind_y = rate.mV[VY];
    const F64 wind_len = sqrt(wind_x * wind_x + wind_y * wind_y);
    if (wind_len > 1e-6)
    {
        wind_x /= wind_len;
        wind_y /= wind_len;
    }
    else
    {
        wind_x = 1.0;
        wind_y = 0.0;
    }

    // the clouds' own light (stage L3: the sun keeps lighting them after the ground's sunset), y up
    const LLEnvironment::PrimaryLightState& primary = LLEnvironment::instance().getPrimaryLight();
    LLVector3 light = primary.valid ? primary.cloud_dir : LLEnvironment::instance().getLightDirection();
    if (light.normalize() < 1e-6f)
    {
        light.set(0.f, 0.f, 1.f);
    }
    set4(mLight, light.mV[VX], light.mV[VZ], light.mV[VY], 0.0);

    static LLCachedControl<F32> sun_gain(gSavedSettings, "RenderVolumetricCloudsSunGain", 4.5f);
    static LLCachedControl<F32> sky_gain(gSavedSettings, "RenderVolumetricCloudsSkyGain", 0.9f);
    const F32 sun = sun_gain;
    const F32 sky = sky_gain;
    set4(mGain, std::isfinite(sun) ? llclamp(sun, 0.f, 50.f) : 4.5f, std::isfinite(sky) ? llclamp(sky, 0.f, 50.f) : 0.9f, 0.0, 0.0);

    // the weather pass's inputs
    const LLCloudscape::Band* middle = nullptr;
    const LLCloudscape::Band* high = nullptr;
    for (S32 i = 0; i < mScape.count; ++i)
    {
        if (mScape.bands[i].channel == 1)
        {
            middle = &mScape.bands[i];
        }
        else if (mScape.bands[i].channel == 2)
        {
            high = &mScape.bands[i];
        }
    }
    double z[3];
    double mean[3];
    for (S32 b = 0; b < 3; ++b)
    {
        z[b] = LLCloudscape::growth_z(mGrowthTime, mScape.growth_rate, b);
        mean[b] = LLCloudscape::slice_mean_at(z[b], mSliceMean);
    }
    set4(mWeatherSky, mSliders.coverage, mSliders.scale, mSliders.variance, mScape.tiles_per_map);
    set4(mWeatherDensity1, density1.mV[0], density1.mV[1], mScape.map_size, 0.5 - 1.0 / llmax(mSliders.scale, LLCloudscape::LAYER_MIN));
    set4(mWeatherDensity2, density2.mV[0], density2.mV[1], density2.mV[2], 0.0);
    set4(mWeatherCover, middle ? middle->cover : 0.0, high ? high->cover : 0.0, mScape.patch, mScape.growth);
    set4(mWeatherGrowth, z[0], z[1], z[2], mScape.tiles_per_map);
    set4(mWeatherMean, mean[0], mean[1], mean[2], 0.0);

    // what the map is made of: the sliders, the bands, the cloud textures as they load, the day cycle's blend
    const double d1[3] = { density1.mV[0], density1.mV[1], density1.mV[2] };
    const double d2[3] = { density2.mV[0], density2.mV[1], density2.mV[2] };
    U64 key = LLCloudscape::weather_key(mSliders, mScape, d1, d2);
    for (LLViewerTexture* tex : { gSky.mVOSkyp->getCloudNoiseTex(), gSky.mVOSkyp->getCloudNoiseTexNext() })
    {
        key = LLCloudscape::hash_mix(key, (U64) (uintptr_t) tex);
        key = LLCloudscape::hash_mix(key, tex ? (U64) tex->getTexName() : 0);
        key = LLCloudscape::hash_mix(key, tex ? (U64) (S64) tex->getDiscardLevel() : 0);
    }
    key = LLCloudscape::hash_mix(key, rate.isExactlyZero() ? 0 : LLCloudscape::quantise(psky->getBlendFactor(), 1.0 / 256.0));
    mWeatherNextKey = key;
    if (!refreshWeather())
    {
        return; // no map (its target could not be allocated): the flat layer draws
    }

    // the main view's targets, made here so that a failed allocation leaves the flat layer drawing this very frame (a
    // photo needs none: its still trace draws straight onto the lit image)
    if (!gSnapshot && !allocateLive(gPipeline.mMainRT.screen.getWidth(), gPipeline.mMainRT.screen.getHeight()))
    {
        return;
    }

    // the bands' uniforms
    const F64 map_km = mWeatherTiles * tile;
    for (S32 i = 0; i < LLCloudscape::MAX_BANDS; ++i)
    {
        const LLCloudscape::Band& b = mScape.bands[i];
        if (i >= mScape.count)
        {
            set4(mLayer + i * 4, 0.0, 1.0, 0.0, 0.0);
            set4(mShape + i * 4, 0.0, 0.3, 0.0, LLCloudscape::BASE_KM);
            set4(mPlace + i * 4, 0.0, 0.0, 0.0, 0.0);
            set4(mNoise + i * 4, 0.0, 0.0, 1.0, 0.0);
            set4(mMarch + i * 4, 0.0, 0.0, 0.0, 0.0);
            continue;
        }
        // the band's origin in the world: the camera less the band's drift (km, double); the noises read it in their
        // own frame, the wind's for cirrus fibres (LLCloudscape::noise_offset)
        const F64 east = mCamera[0] - mDrift[0] * b.drift;
        const F64 north = mCamera[1] - mDrift[1] * b.drift;
        F64 noise_x = 0.0;
        F64 noise_y = 0.0;
        LLCloudscape::noise_offset(east, north, b.stretch, wind_x, wind_y, noise_x, noise_y);
        set4(mLayer + i * 4, b.base_km, b.top_km, b.shape, 1.0);
        set4(mShape + i * 4, b.sigma, b.erosion, b.anvil, LLCloudscape::BASE_KM * b.noise_scale);
        set4(mPlace + i * 4, LLCloudscape::wrap_km(east, map_km), LLCloudscape::wrap_km(north, map_km), b.sheet, b.channel);
        set4(mNoise + i * 4, noise_x, noise_y, b.stretch, b.far_km);
        set4(mMarch + i * 4, b.steps, b.light_steps, b.step_cap, b.noise_blend);
    }
    set4(mMap, map_km, wind_x, wind_y, mScape.count);

    mActive = true;
    if (!was_active)
    {
        mHistoryValid = false;
    }
}

bool LLVolumetricClouds::active()
{
    update();
    return mActive;
}

void LLVolumetricClouds::bindNoise(LLGLSLShader& shader)
{
    S32 channel = shader.enableTexture(LLShaderMgr::CLOUD_VOL_BASE, LLTexUnit::TT_TEXTURE_3D);
    if (channel > -1)
    {
        gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE_3D, mBaseTex);
    }
    channel = shader.enableTexture(LLShaderMgr::CLOUD_VOL_EROSION, LLTexUnit::TT_TEXTURE_3D);
    if (channel > -1)
    {
        gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE_3D, mErosionTex);
    }
    channel = shader.enableTexture(LLShaderMgr::CLOUD_VOL_CURL);
    if (channel > -1)
    {
        gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, mCurlTex);
    }
    channel = shader.enableTexture(LLShaderMgr::CLOUD_VOL_WEATHER);
    if (channel > -1 && mWeather.isComplete())
    {
        mWeather.bindTexture(0, channel, LLTexUnit::TFO_BILINEAR);
        gGL.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_WRAP);
    }
}

void LLVolumetricClouds::unbindNoise(LLGLSLShader& shader)
{
    for (S32 uniform : { (S32) LLShaderMgr::CLOUD_VOL_BASE, (S32) LLShaderMgr::CLOUD_VOL_EROSION })
    {
        const S32 channel = shader.getTextureChannel(uniform);
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->unbind(LLTexUnit::TT_TEXTURE_3D);
        }
    }
    for (S32 uniform : { (S32) LLShaderMgr::CLOUD_VOL_CURL, (S32) LLShaderMgr::CLOUD_VOL_WEATHER })
    {
        const S32 channel = shader.getTextureChannel(uniform);
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->unbind(LLTexUnit::TT_TEXTURE);
        }
    }
}

void LLVolumetricClouds::setBandUniforms(LLGLSLShader& shader)
{
    shader.uniform4fv(sLayer, LLCloudscape::MAX_BANDS, mLayer);
    shader.uniform4fv(sShape, LLCloudscape::MAX_BANDS, mShape);
    shader.uniform4fv(sPlace, LLCloudscape::MAX_BANDS, mPlace);
    shader.uniform4fv(sNoise, LLCloudscape::MAX_BANDS, mNoise);
    shader.uniform4fv(sMarch, LLCloudscape::MAX_BANDS, mMarch);
    shader.uniform4fv(sMap, 1, mMap);
    shader.uniform4fv(sLight, 1, mLight);
    shader.uniform4fv(sGain, 1, mGain);
}

void LLVolumetricClouds::drawWeather(U32 first_row, U32 rows)
{
    LL_PROFILE_GPU_ZONE("volumetric clouds: weather");
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    LLRenderTarget& target = mWeather;
    const U32 size = target.getWidth();
    if (!psky || first_row >= size)
    {
        return;
    }
    // the strip is a scissor; the caller's scissor box is put back (it may be the UI's)
    GLint scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    target.bindTarget();
    {
        LLGLDepthTest depth(GL_FALSE, GL_FALSE);
        LLGLDisable blend(GL_BLEND);
        LLGLDisable cull(GL_CULL_FACE);
        LLGLEnable scissor(GL_SCISSOR_TEST);
        glScissor(0, (GLint) first_row, (GLsizei) size, (GLsizei) llmin(rows, size - first_row));

        LLGLSLShader& shader = gCloudsVolWeatherProgram;
        shader.bind();
        LLDrawPoolWLSky::bindCloudNoise(&shader, psky); // the cloud texture blended with the next one, as the flat layer
        const S32 channel = shader.enableTexture(LLShaderMgr::CLOUD_VOL_BASE, LLTexUnit::TT_TEXTURE_3D);
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE_3D, mBaseTex);
        }
        shader.uniform4fv(sWeatherSky, 1, mWeatherSky);
        shader.uniform4fv(sWeatherDensity1, 1, mWeatherDensity1);
        shader.uniform4fv(sWeatherDensity2, 1, mWeatherDensity2);
        shader.uniform4fv(sWeatherCover, 1, mWeatherCover);
        shader.uniform4fv(sWeatherGrowth, 1, mWeatherGrowth);
        shader.uniform4fv(sWeatherMean, 1, mWeatherMean);
        draw_screen_triangle();
        if (channel > -1)
        {
            gGL.getTexUnit(channel)->unbind(LLTexUnit::TT_TEXTURE_3D);
        }
        shader.unbind();
        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
    }
    target.flush();
    glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
}

bool LLVolumetricClouds::refreshWeather()
{
    const U32 size = (U32) mScape.map_size;
    const S32 tiles = mScape.tiles_per_map;
    const F64 growth_step = floor(mGrowthTime / WEATHER_GROWTH_S);

    if (!mWeather.isComplete() || mWeather.getWidth() != size || mWeatherTiles != tiles)
    {
        // a map of a new side or period (the first one, or cloud variance turned on or off: rare) is drawn whole, in
        // this frame, from this frame's sky: 0.3 ms at 512^2, 0.47 ms at 2048^2 on the RTX 5070 Ti, once
        mWeather.release();
        mWeatherTiles = 0;
        mWeatherCycle = false;
        if (!mWeather.allocate(size, size, GL_RGBA8, false))
        {
            return false;
        }
        // the map is periodic: it repeats in the trace
        LLTexUnit* unit = gGL.getTexUnit(0);
        unit->bindManual(LLTexUnit::TT_TEXTURE, mWeather.getTexture());
        unit->setTextureAddressMode(LLTexUnit::TAM_WRAP);
        unit->unbind(LLTexUnit::TT_TEXTURE);
        drawWeather(0, size);
        mWeatherTiles = tiles;
        mWeatherKey = mWeatherNextKey;
        mWeatherGrowthStep = growth_step;
        return true;
    }

    // a change, or the growth clock's next step: one cycle of strips (one a frame); a change during a cycle starts the
    // next cycle when this one ends, so a continuous change (a day cycle's blend) keeps the strips going
    if (!mWeatherCycle && (mWeatherNextKey != mWeatherKey || growth_step != mWeatherGrowthStep))
    {
        mWeatherKey = mWeatherNextKey;
        mWeatherGrowthStep = growth_step;
        mWeatherRow = 0;
        mWeatherCycle = true;
    }
    if (mWeatherCycle)
    {
        drawWeather(mWeatherRow, WEATHER_STRIP);
        mWeatherRow += WEATHER_STRIP;
        if (mWeatherRow >= size)
        {
            mWeatherCycle = false;
        }
    }
    return true;
}

bool LLVolumetricClouds::allocateLive(U32 width, U32 height)
{
    if (mLiveWidth == width && mLiveHeight == height && mTrace.isComplete() && mHistory[0].isComplete() && mHistory[1].isComplete())
    {
        return true;
    }
    mTrace.release();
    mHistory[0].release();
    mHistory[1].release();
    mLiveWidth = mLiveHeight = 0;
    mHistoryValid = false;
    if (!width || !height)
    {
        return false;
    }

    const U32 half_w = (width + 1) / 2;
    const U32 half_h = (height + 1) / 2;
    const U32 trace_w = (half_w + 1) / 2;
    const U32 trace_h = (half_h + 1) / 2;
    const bool ok = mTrace.allocate(trace_w, trace_h, GL_RGBA16F, false) && mTrace.addColorAttachment(GL_R16F)
                 && mHistory[0].allocate(half_w, half_h, GL_RGBA16F, false) && mHistory[1].allocate(half_w, half_h, GL_RGBA16F, false);
    if (!ok)
    {
        mTrace.release();
        mHistory[0].release();
        mHistory[1].release();
        return false;
    }
    LLTexUnit* unit = gGL.getTexUnit(0);
    for (LLRenderTarget& history : mHistory)
    {
        unit->bindManual(LLTexUnit::TT_TEXTURE, history.getTexture());
        unit->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
        unit->unbind(LLTexUnit::TT_TEXTURE);
    }
    mLiveWidth = width;
    mLiveHeight = height;
    return true;
}

void LLVolumetricClouds::render(LLRenderTarget& deferred, U32 width, U32 height)
{
    if (gCubeSnapshot || !active() || !width || !height)
    {
        return;
    }
    LL_PROFILE_ZONE_SCOPED_CATEGORY_PIPELINE;
    LL_PROFILE_GPU_ZONE("volumetric clouds");

    // the view's rotation-only view-projection (the clouds are sky: no translation) and its inverse
    const glm::mat4 modelview = get_current_modelview();
    const glm::mat4 projection = get_current_projection();
    const glm::mat4 rotation = glm::mat4(glm::mat3(modelview));
    const glm::mat4 vp = projection * rotation;
    const glm::mat4 inv_vp = glm::inverse(vp);

    if (gSnapshot)
    {
        renderStill(inv_vp, width, height);
        return;
    }
    if (!allocateLive(width, height))
    {
        return; // made in update() at the main view's size: only a size the frame changed itself fails here, once
    }

    {
        LLGLDepthTest depth(GL_FALSE, GL_FALSE);
        LLGLDisable blend(GL_BLEND);
        LLGLDisable cull(GL_CULL_FACE);

        const U64 frame = LLFrameTimer::getFrameCount();
        const F64 move_x = mCamera[0] - mPrevCamera[0];
        const F64 move_y = mCamera[1] - mPrevCamera[1];
        // the creator's band's drift with the wind since the last frame (km): its clouds moved with it
        const F64 drift_x = mDrift[0] - mPrevDrift[0];
        const F64 drift_y = mDrift[1] - mPrevDrift[1];
        // the history holds when the last frame drew the clouds, the camera did not jump and the clouds did not jump
        // (a new sky that resets the scroll)
        const bool continuous = mHistoryValid && mLastLiveFrame + 1 == frame && move_x * move_x + move_y * move_y < TELEPORT_KM * TELEPORT_KM
                             && drift_x * drift_x + drift_y * drift_y < TELEPORT_KM * TELEPORT_KM;

        {
            LL_PROFILE_GPU_ZONE("volumetric clouds: trace");
            mTrace.bindTarget();
            LLGLSLShader& shader = gCloudsVolTraceProgram;
            shader.bind();
            bindNoise(shader);
            setBandUniforms(shader);
            shader.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &deferred, true);
            shader.uniformMatrix4fv(sInvVP, 1, GL_FALSE, glm::value_ptr(inv_vp));
            shader.uniform4f(sSizes, (F32) width, (F32) height, (F32) mHistory[0].getWidth(), (F32) mHistory[0].getHeight());
            shader.uniform4f(sFrame, (F32) (mFrame % 4096), 0.f, 0.f, 0.f);
            draw_screen_triangle();
            unbindNoise(shader);
            shader.unbind();
            mTrace.flush();
        }
        const U32 next = mHistoryIndex ^ 1;
        {
            LL_PROFILE_GPU_ZONE("volumetric clouds: reconstruction");
            mHistory[next].bindTarget();
            LLGLSLShader& shader = gCloudsVolReconProgram;
            shader.bind();
            shader.bindTexture(LLShaderMgr::DEFERRED_DEPTH, &deferred, true);
            shader.bindTexture(LLShaderMgr::CLOUD_VOL_TRACE, &mTrace, false, LLTexUnit::TFO_POINT, 0);
            shader.bindTexture(LLShaderMgr::CLOUD_VOL_TDEPTH, &mTrace, false, LLTexUnit::TFO_POINT, 1);
            shader.bindTexture(LLShaderMgr::CLOUD_VOL_HISTORY, &mHistory[mHistoryIndex], false, LLTexUnit::TFO_BILINEAR);
            shader.uniformMatrix4fv(sInvVP, 1, GL_FALSE, glm::value_ptr(inv_vp));
            shader.uniformMatrix4fv(sPrevVP, 1, GL_FALSE, glm::value_ptr(mPrevVP));
            shader.uniform4f(sSizes, (F32) width, (F32) height, (F32) mHistory[next].getWidth(), (F32) mHistory[next].getHeight());
            shader.uniform4f(sFrame, (F32) (mFrame % 4096), continuous ? 1.f : 0.f, 0.f, 0.f);
            // where a cloud seen now was a frame ago, against the last camera: the clouds' heights follow the camera's,
            // so only its horizontal move parallaxes them, less their own drift with the wind (the creator's band's;
            // the faster drift aloft and the growth come through the clamp)
            shader.uniform4f(sDelta, (F32) (move_x - drift_x), (F32) (move_y - drift_y), 0.f, continuous ? 1.f : 0.f);
            draw_screen_triangle();
            shader.unbind();
            mHistory[next].flush();
        }
        mHistoryIndex = next;
        mHistoryValid = true;
        mLastLiveFrame = frame;
        mPrevVP = vp;
        mPrevCamera[0] = mCamera[0];
        mPrevCamera[1] = mCamera[1];
        mPrevDrift[0] = mDrift[0];
        mPrevDrift[1] = mDrift[1];
        ++mFrame;
    }

    composite(mHistory[mHistoryIndex], width, height);
}

void LLVolumetricClouds::composite(LLRenderTarget& image, U32 width, U32 height)
{
    // dst * T + L on the sky's pixels, from the half-resolution image
    LL_PROFILE_GPU_ZONE("volumetric clouds: composite");
    CompositeState state;
    LLGLSLShader& shader = gCloudsVolCompositeProgram;
    shader.bind();
    shader.bindTexture(LLShaderMgr::CLOUD_VOL_HISTORY, &image, false, LLTexUnit::TFO_BILINEAR);
    shader.uniform4f(sSizes, (F32) width, (F32) height, 0.f, 0.f);
    draw_screen_triangle();
    shader.unbind();
}

void LLVolumetricClouds::renderStill(const glm::mat4& inv_vp, U32 width, U32 height)
{
    // A photo or a tile of one: every sky pixel traced in full (no history), blended straight onto the lit image as
    // the composite is; the jitter is keyed on the pixel's place in the whole photo (the tile's first pixel from the
    // camera's zoom sub-region, bottom row first). Drawn in strips of rows, a flush between them, so that no draw of a
    // large photo runs long (the whole trace of a 7680^2 photo is about 0.3-0.7 s on an RTX 5070 Ti).
    LL_PROFILE_GPU_ZONE("volumetric clouds: still");
    F32 origin_x = 0.f;
    F32 origin_y = 0.f;
    const LLViewerCamera& camera = *LLViewerCamera::getInstance();
    if (camera.getZoomFactor() > 1.f)
    {
        const S32 across = llceil(camera.getZoomFactor());
        const S32 sub = camera.getZoomSubRegion();
        origin_x = (F32) ((sub % across) * (S32) width);
        origin_y = (F32) ((sub / across) * (S32) height);
    }
    LLGLSLShader& shader = gCloudsVolStillProgram;
    shader.bind();
    bindNoise(shader);
    setBandUniforms(shader);
    shader.uniformMatrix4fv(sInvVP, 1, GL_FALSE, glm::value_ptr(inv_vp));
    shader.uniform4f(sSizes, (F32) width, (F32) height, 0.f, 0.f);
    shader.uniform4f(sFrame, 0.f, origin_x, origin_y, (F32) STILL_SAMPLES);

    // the caller's scissor box is put back (it may be the UI's)
    GLint scissor_box[4];
    glGetIntegerv(GL_SCISSOR_BOX, scissor_box);
    {
        CompositeState state;
        LLGLEnable scissor(GL_SCISSOR_TEST);
        const U32 rows = llmax(STILL_STRIP_PIXELS / width, 1u);
        for (U32 y = 0; y < height; y += rows)
        {
            glScissor(0, (GLint) y, (GLsizei) width, (GLsizei) llmin(rows, height - y));
            draw_screen_triangle();
            glFlush();
        }
    }
    glScissor(scissor_box[0], scissor_box[1], scissor_box[2], scissor_box[3]);
    unbindNoise(shader);
    shader.unbind();
}

bool LLVolumetricClouds::bindBands(LLGLSLShader& shader)
{
    if (!active())
    {
        return false;
    }
    bindNoise(shader);
    setBandUniforms(shader);
    return true;
}
