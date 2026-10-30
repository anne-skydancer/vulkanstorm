/**
 * @file lldrawpoolwlsky.cpp
 * @brief LLDrawPoolWLSky class implementation
 *
 * $LicenseInfo:firstyear=2007&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "lldrawpoolwlsky.h"

#include "llerror.h"
#include "llface.h"
#include "llimage.h"
#include "llrender.h"
#include "llenvironment.h"
#include "llglslshader.h"
#include "llgl.h"

#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewercamera.h"
#include "pipeline.h"
#include "llsky.h"
#include "llvowlsky.h"
#include "llsettingsvo.h"
#include "llviewercontrol.h"
// <Vulkanstorm> the real night sky
#include "lldir.h"
#include "llfile.h"
#include "llimagegl.h"
#include "llnightsky.h"
// </Vulkanstorm>

extern bool gCubeSnapshot;

static LLStaticHashedString sCamPosLocal("camPosLocal");
static LLStaticHashedString sCustomAlpha("custom_alpha");

static LLGLSLShader* cloud_shader = NULL;
static LLGLSLShader* sky_shader   = NULL;
static LLGLSLShader* sun_shader   = NULL;
static LLGLSLShader* moon_shader  = NULL;

static float sStarTime;

// <Vulkanstorm> The real night sky (llnightsky.cpp), drawn by
// deferred/skyF.glsl from two float textures made on first use and freed
// with the GL context (LLDrawPoolWLSky::cleanupGL):
// - the cells: the first star of each cube map cell, then the end, one
//   per texel (R32F, exact below 2^24);
// - the stars, binned: two texels each, direction + intensity, then
//   colour + size (RGBA32F).
namespace
{
    constexpr S32 NIGHT_SKY_CELLS_WIDTH = 64;    // skyF.glsl NIGHT_SKY_CELLS_WIDTH
    constexpr S32 NIGHT_SKY_STARS_WIDTH = 1024;  // skyF.glsl NIGHT_SKY_STARS_WIDTH

    U32 sNightSkyTextures[2] = { 0, 0 }; // cells, stars
    bool sNightSkyLoadTried = false;     // once per GL context, success or not

    bool use_real_night_sky()
    {
        static LLCachedControl<U32> night_sky(gSavedSettings, "RenderNightSky", 1);
        return night_sky == 1;
    }

    void upload_night_sky_texture(U32 texture, GLint internal_format, GLenum format, S32 width, S32 height, const std::vector<F32>& data)
    {
        LLTexUnit* unit = gGL.getTexUnit(0);
        unit->bindManual(LLTexUnit::TT_TEXTURE, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, 0);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_FLOAT, data.data());
        unit->unbind(LLTexUnit::TT_TEXTURE);
    }

    // The star tables, made at the first night sky drawn, before a shader
    // is bound (the upload uses texture unit 0). False when the catalogue
    // is missing or damaged: then the night sky has no stars.
    bool load_night_sky()
    {
        if (sNightSkyLoadTried)
        {
            return sNightSkyTextures[0] != 0;
        }
        sNightSkyLoadTried = true;

        const std::string path = gDirUtilp->getExpandedFilename(LL_PATH_APP_SETTINGS, "stars", "stars.bin");
        llifstream file(path.c_str(), std::ios::in | std::ios::binary);
        const std::vector<U8> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        std::vector<LLNightSky::CatalogueStar> catalogue;
        std::string error;
        if (!file.is_open() || !LLNightSky::parse(bytes.data(), bytes.size(), catalogue, error))
        {
            LL_WARNS("NightSky") << path << ": " << (file.is_open() ? error : std::string("cannot open"))
                                 << "; the night sky has no stars" << LL_ENDL;
            return false;
        }

        std::vector<U32> offsets;
        std::vector<LLNightSky::GpuStar> binned;
        LLNightSky::bin(LLNightSky::gpuStars(catalogue), offsets, binned);

        const S32 cells_height = ((S32)offsets.size() + NIGHT_SKY_CELLS_WIDTH - 1) / NIGHT_SKY_CELLS_WIDTH;
        std::vector<F32> cells((size_t)NIGHT_SKY_CELLS_WIDTH * cells_height, (F32)binned.size());
        for (size_t i = 0; i < offsets.size(); ++i)
        {
            cells[i] = (F32)offsets[i];
        }

        const S32 stars_height = llmax(1, ((S32)binned.size() * 2 + NIGHT_SKY_STARS_WIDTH - 1) / NIGHT_SKY_STARS_WIDTH);
        std::vector<F32> stars((size_t)NIGHT_SKY_STARS_WIDTH * stars_height * 4, 0.f);
        for (size_t i = 0; i < binned.size(); ++i)
        {
            std::copy(binned[i].pos, binned[i].pos + 4, stars.begin() + i * 8);
            std::copy(binned[i].color, binned[i].color + 4, stars.begin() + i * 8 + 4);
        }

        LLImageGL::generateTextures(2, sNightSkyTextures);
        upload_night_sky_texture(sNightSkyTextures[0], GL_R32F, GL_RED, NIGHT_SKY_CELLS_WIDTH, cells_height, cells);
        upload_night_sky_texture(sNightSkyTextures[1], GL_RGBA32F, GL_RGBA, NIGHT_SKY_STARS_WIDTH, stars_height, stars);
        LL_INFOS("NightSky") << catalogue.size() << " stars, " << binned.size() << " in "
                             << LLNightSky::CELLS << " cells" << LL_ENDL;
        return true;
    }

    // The night sky's uniforms and textures for this frame. Off in
    // reflection probe snapshots, as Firestorm's stars are.
    void set_night_sky(LLGLSLShader* shader, const LLSettingsSky::ptr_t& psky)
    {
        static LLStaticHashedString sNightSkyAxes("night_sky_axes");
        static LLStaticHashedString sNightSkyParams("night_sky_params");

        F32 gain = 0.f;
        if (use_real_night_sky() && !gCubeSnapshot && load_night_sky())
        {
            // LLDrawPoolWLSky::renderStarsDeferred passes star_brightness /
            // 500, which starsF.glsl turns into smoothstep(0, 0.9, ...) * 32:
            // full brightness from about 100, dimmer only below. The same
            // curve, without the 32 times overdrive.
            const F32 x = llclamp(psky->getStarBrightness() / 500.f / 0.9f, 0.f, 1.f);
            gain = llmin(x * x * (3.f - 2.f * x) * 8.f, 1.f);
        }
        if (gain > 0.f)
        {
            LLVector3 axes[3];
            LLNightSky::axes(psky->getSunDirection(), LLNightSky::unixDaysNow(), axes);
            F32 axes_data[9];
            for (S32 i = 0; i < 3; ++i)
            {
                std::copy(axes[i].mV, axes[i].mV + 3, axes_data + i * 3);
            }
            shader->uniform3fv(sNightSkyAxes, 3, axes_data);

            S32 channel = shader->enableTexture(LLShaderMgr::NIGHT_SKY_CELLS);
            if (channel > -1)
            {
                gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, sNightSkyTextures[0]);
            }
            channel = shader->enableTexture(LLShaderMgr::NIGHT_SKY_STARS);
            if (channel > -1)
            {
                gGL.getTexUnit(channel)->bindManual(LLTexUnit::TT_TEXTURE, sNightSkyTextures[1]);
            }
        }
        // the stars' scintillation clock, wrapped to keep its precision
        const F32 time = (F32)fmod(LLFrameTimer::getElapsedSeconds(), 3600.0);
        shader->uniform4f(sNightSkyParams, gain, 1.f, time, 0.f);
    }
}
// </Vulkanstorm>

LLDrawPoolWLSky::LLDrawPoolWLSky(void) :
    LLDrawPool(POOL_WL_SKY)
{
}

LLDrawPoolWLSky::~LLDrawPoolWLSky()
{
}

LLViewerTexture *LLDrawPoolWLSky::getDebugTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::beginDeferredPass(S32 pass)
{
    sky_shader = &gDeferredWLSkyProgram;
    cloud_shader = &gDeferredWLCloudProgram;

    sun_shader = &gDeferredWLSunProgram;

    moon_shader = &gDeferredWLMoonProgram;
}

void LLDrawPoolWLSky::endDeferredPass(S32 pass)
{
    sky_shader   = nullptr;
    cloud_shader = nullptr;
    sun_shader   = nullptr;
    moon_shader  = nullptr;

    // clear the depth buffer so haze shaders can use unwritten depth as a mask
    glClear(GL_DEPTH_BUFFER_BIT);
}

void LLDrawPoolWLSky::renderDome(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader * shader) const
{
    llassert_always(NULL != shader);

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.pushMatrix();

    //chop off translation
    if (LLPipeline::sReflectionRender && camPosLocal.mV[2] > 256.f)
    {
        gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], 256.f-camPosLocal.mV[2]*0.5f);
    }
    else
    {
        gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
    }


    // the windlight sky dome works most conveniently in a coordinate system
    // where Y is up, so permute our basis vectors accordingly.
    gGL.rotatef(120.f, 1.f / F_SQRT3, 1.f / F_SQRT3, 1.f / F_SQRT3);

    gGL.scalef(0.333f, 0.333f, 0.333f);

    gGL.translatef(0.f,-camHeightLocal, 0.f);

    // Draw WL Sky
    shader->uniform3f(sCamPosLocal, 0.f, camHeightLocal, 0.f);

    gSky.mVOWLSkyp->drawDome();

    gGL.matrixMode(LLRender::MM_MODELVIEW);
    gGL.popMatrix();
}

extern LLPointer<LLImageGL> gEXRImage;

static bool use_hdri_sky()
{
    static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
    static LLCachedControl<bool> irradiance_only(gSavedSettings, "RenderHDRIIrradianceOnly", false);

    return gCubeSnapshot && (!irradiance_only || !gPipeline.mReflectionMapManager.isRadiancePass()) ? gEXRImage.notNull() : // always use HDRI for reflection probes when available
        gEXRImage.notNull() ? hdri_split > 0.f : // fallback to EEP sky when split screen is zero
        false; // no HDRI available, always use EEP sky

}

void LLDrawPoolWLSky::renderSkyHazeDeferred(const LLVector3& camPosLocal, F32 camHeightLocal) const
{
    if (!gSky.mVOSkyp)
    {
        return;
    }

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY))
    {
        if (use_hdri_sky())
        {
            sky_shader = &gEnvironmentMapProgram;
            sky_shader->bind();
            S32 idx = sky_shader->enableTexture(LLShaderMgr::ENVIRONMENT_MAP);
            if (idx > -1)
            {
                gGL.getTexUnit(idx)->bind(gEXRImage);
            }

            static LLCachedControl<F32> hdri_exposure(gSavedSettings, "RenderHDRIExposure", 0.0f);
            static LLCachedControl<F32> hdri_rotation(gSavedSettings, "RenderHDRIRotation", 0.f);
            static LLCachedControl<F32> hdri_split(gSavedSettings, "RenderHDRISplitScreen", 1.f);
            static LLStaticHashedString hdri_split_screen("hdri_split_screen");

            LLMatrix3 rot;
            rot.setRot(0.f, hdri_rotation*DEG_TO_RAD, 0.f);

            sky_shader->uniform1f(LLShaderMgr::SKY_HDR_SCALE, powf(2.f, hdri_exposure));
            sky_shader->uniformMatrix3fv(LLShaderMgr::DEFERRED_ENV_MAT, 1, GL_FALSE, (F32*) rot.mMatrix);
            sky_shader->uniform1f(hdri_split_screen, gCubeSnapshot ? 1.f : hdri_split);
        }
        else
        {
            // <Vulkanstorm> make the night sky's tables before the shader
            // binds its textures: the upload uses texture unit 0
            if (use_real_night_sky())
            {
                load_night_sky();
            }
            // </Vulkanstorm>
            sky_shader->bind();
        }

        LLGLSPipelineDepthTestSkyBox sky(true, true);

        sky_shader->uniform1i(LLShaderMgr::CUBE_SNAPSHOT, gCubeSnapshot ? 1 : 0);

        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        LLViewerTexture* rainbow_tex = gSky.mVOSkyp->getRainbowTex();
        LLViewerTexture* halo_tex  = gSky.mVOSkyp->getHaloTex();

        sky_shader->bindTexture(LLShaderMgr::RAINBOW_MAP, rainbow_tex);
        sky_shader->bindTexture(LLShaderMgr::HALO_MAP,  halo_tex);

        F32 moisture_level  = (float)psky->getSkyMoistureLevel();
        F32 droplet_radius  = (float)psky->getSkyDropletRadius();
        F32 ice_level       = (float)psky->getSkyIceLevel();

        // hobble halos and rainbows when there's no light source to generate them
        if (!psky->getIsSunUp() && !psky->getIsMoonUp())
        {
            moisture_level = 0.0f;
            ice_level      = 0.0f;
        }

        sky_shader->uniform1f(LLShaderMgr::MOISTURE_LEVEL, moisture_level);
        sky_shader->uniform1f(LLShaderMgr::DROPLET_RADIUS, droplet_radius);
        sky_shader->uniform1f(LLShaderMgr::ICE_LEVEL, ice_level);

        sky_shader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

        sky_shader->uniform1i(LLShaderMgr::SUN_UP_FACTOR, psky->getIsSunUp() ? 1 : 0);

        // <Vulkanstorm> the real night sky; the HDRI sky has none
        if (sky_shader == &gDeferredWLSkyProgram)
        {
            set_night_sky(sky_shader, psky);
        }
        // </Vulkanstorm>

        /// Render the skydome
        renderDome(origin, camHeightLocal, sky_shader);

        sky_shader->unbind();
    }
}

void LLDrawPoolWLSky::renderStarsDeferred(const LLVector3& camPosLocal) const
{
    if (!gSky.mVOSkyp || use_hdri_sky())
    {
        return;
    }

    // <Vulkanstorm> the real night sky draws its stars in the sky dome
    // (renderSkyHazeDeferred); these are Firestorm's random ones
    if (use_real_night_sky())
    {
        return;
    }
    // </Vulkanstorm>

    LLGLSPipelineBlendSkyBox gls_sky(true, false);

    gGL.setSceneBlendType(LLRender::BT_ADD_WITH_ALPHA);

    F32 star_alpha = LLEnvironment::instance().getCurrentSky()->getStarBrightness() / 500.0f;

    // If start_brightness is not set, exit
    if(star_alpha < 0.001f)
    {
        LL_DEBUGS("SKY") << "star_brightness below threshold." << LL_ENDL;
        return;
    }

    gDeferredStarProgram.bind();

    LLViewerTexture* tex_a = gSky.mVOSkyp->getBloomTex();
    LLViewerTexture* tex_b = gSky.mVOSkyp->getBloomTexNext();

    F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();

    if (tex_a && (!tex_b || (tex_a == tex_b)))
    {
        // Bind current and next sun textures
        gGL.getTexUnit(0)->bind(tex_a);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        blend_factor = 0;
    }
    else if (tex_b && !tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_b);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
        blend_factor = 0;
    }
    else if (tex_b != tex_a)
    {
        gGL.getTexUnit(0)->bind(tex_a);
        gGL.getTexUnit(1)->bind(tex_b);
    }

    gGL.pushMatrix();
    gGL.translatef(camPosLocal.mV[0], camPosLocal.mV[1], camPosLocal.mV[2]);
    gGL.rotatef(gFrameTimeSeconds*0.01f, 0.f, 0.f, 1.f);
    gDeferredStarProgram.uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

    if (LLPipeline::sReflectionRender)
    {
        star_alpha = 1.0f;
    }
    gDeferredStarProgram.uniform1f(sCustomAlpha, star_alpha);

    sStarTime = (F32)LLFrameTimer::getElapsedSeconds() * 0.5f;

    gDeferredStarProgram.uniform1f(LLShaderMgr::WATER_TIME, sStarTime);

    gSky.mVOWLSkyp->drawStars();

    gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
    gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

    gDeferredStarProgram.unbind();

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderSkyCloudsDeferred(const LLVector3& camPosLocal, F32 camHeightLocal, LLGLSLShader* cloudshader) const
{
    if (use_hdri_sky())
    {
        return;
    }

    if (gPipeline.canUseWindLightShaders() && gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) && gSky.mVOSkyp && gSky.mVOSkyp->getCloudNoiseTex())
    {
        LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

        LLGLSPipelineBlendSkyBox pipeline(true, true);

        cloudshader->bind();

        LLPointer<LLViewerTexture> cloud_noise      = gSky.mVOSkyp->getCloudNoiseTex();
        LLPointer<LLViewerTexture> cloud_noise_next = gSky.mVOSkyp->getCloudNoiseTexNext();

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        F32 cloud_variance = psky ? (F32)psky->getCloudVariance() : 0.0f;
        F32 blend_factor   = psky ? (F32)psky->getBlendFactor() : 0.0f;

        if (psky->getCloudScrollRate().isExactlyZero())
        {
            blend_factor = 0.f;
        }

        // if we even have sun disc textures to work with...
        if (cloud_noise || cloud_noise_next)
        {
            if (cloud_noise && (!cloud_noise_next || (cloud_noise == cloud_noise_next)))
            {
                // Bind current and next sun textures
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                blend_factor = 0;
            }
            else if (cloud_noise_next && !cloud_noise)
            {
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise_next, LLTexUnit::TT_TEXTURE);
                blend_factor = 0;
            }
            else if (cloud_noise_next != cloud_noise)
            {
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP, cloud_noise, LLTexUnit::TT_TEXTURE);
                cloudshader->bindTexture(LLShaderMgr::CLOUD_NOISE_MAP_NEXT, cloud_noise_next, LLTexUnit::TT_TEXTURE);
            }
        }

        cloudshader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
        cloudshader->uniform1f(LLShaderMgr::CLOUD_VARIANCE, cloud_variance);
        cloudshader->uniform1f(LLShaderMgr::SUN_MOON_GLOW_FACTOR, psky->getSunMoonGlowFactor());

        /// Render the skydome
        renderDome(camPosLocal, camHeightLocal, cloudshader);

        cloudshader->unbind();

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);
    }
}

void LLDrawPoolWLSky::renderHeavenlyBodies()
{
    if (!gSky.mVOSkyp || use_hdri_sky()) return;

    LLGLSPipelineBlendSkyBox gls_skybox(true, true); // SL-14113 we need moon to write to depth to clip stars behind

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();
    gGL.pushMatrix();
    gGL.translatef(origin.mV[0], origin.mV[1], origin.mV[2]);

    LLFace * face = gSky.mVOSkyp->mFace[LLVOSky::FACE_SUN];

    F32 blend_factor = (F32)LLEnvironment::instance().getCurrentSky()->getBlendFactor();
    bool can_use_vertex_shaders = gPipeline.shadersLoaded();
    bool can_use_windlight_shaders = gPipeline.canUseWindLightShaders();


    if (gSky.mVOSkyp->getSun().getDraw() && face && face->getGeomCount())
    {
        LLPointer<LLViewerTexture> tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLPointer<LLViewerTexture> tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

        gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

        // if we even have sun disc textures to work with...
        if (tex_a || tex_b)
        {
            // if and only if we have a texture defined, render the sun disc
            if (can_use_vertex_shaders && can_use_windlight_shaders)
            {
                sun_shader->bind();

                if (tex_a && (!tex_b || (tex_a == tex_b)))
                {
                    // Bind current and next sun textures
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (tex_b && !tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                    blend_factor = 0;
                }
                else if (tex_b != tex_a)
                {
                    sun_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                    sun_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                }

                LLColor4 color(gSky.mVOSkyp->getSun().getInterpColor());

                sun_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
                sun_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);

                face->renderIndexed();

                gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
                gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

                sun_shader->unbind();
            }
        }
    }

    face = gSky.mVOSkyp->mFace[LLVOSky::FACE_MOON];

    if (gSky.mVOSkyp->getMoon().getDraw() && face && face->getTexture(LLRender::DIFFUSE_MAP) && face->getGeomCount() && moon_shader)
    {
        LLViewerTexture* tex_a = face->getTexture(LLRender::DIFFUSE_MAP);
        LLViewerTexture* tex_b = face->getTexture(LLRender::ALTERNATE_DIFFUSE_MAP);

        LLColor4 color(gSky.mVOSkyp->getMoon().getInterpColor());

        if (can_use_vertex_shaders && can_use_windlight_shaders && (tex_a || tex_b))
        {
            moon_shader->bind();

            if (tex_a && (!tex_b || (tex_a == tex_b)))
            {
                // Bind current and next sun textures
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                //blend_factor = 0;
            }
            else if (tex_b && !tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
                //blend_factor = 0;
            }
            else if (tex_b != tex_a)
            {
                moon_shader->bindTexture(LLShaderMgr::DIFFUSE_MAP, tex_a, LLTexUnit::TT_TEXTURE);
                //moon_shader->bindTexture(LLShaderMgr::ALTERNATE_DIFFUSE_MAP, tex_b, LLTexUnit::TT_TEXTURE);
            }

            LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();

            F32 moon_brightness = (float)psky->getMoonBrightness();

            moon_shader->uniform1f(LLShaderMgr::MOON_BRIGHTNESS, moon_brightness);
            moon_shader->uniform3fv(LLShaderMgr::MOONLIGHT_COLOR, 1, gSky.mVOSkyp->getMoon().getColor().mV);
            moon_shader->uniform4fv(LLShaderMgr::DIFFUSE_COLOR, 1, color.mV);
            //moon_shader->uniform1f(LLShaderMgr::BLEND_FACTOR, blend_factor);
            moon_shader->uniform3fv(LLShaderMgr::DEFERRED_MOON_DIR, 1, psky->getMoonDirection().mV); // shader: moon_dir

            face->renderIndexed();

            gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
            gGL.getTexUnit(1)->unbind(LLTexUnit::TT_TEXTURE);

            moon_shader->unbind();
        }
    }

    gGL.popMatrix();
}

void LLDrawPoolWLSky::renderDeferred(S32 pass)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_DRAWPOOL; //LL_RECORD_BLOCK_TIME(FTM_RENDER_WL_SKY);
    if (!gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_SKY) || gSky.mVOSkyp.isNull())
    {
        return;
    }

    // TODO: remove gSky.mVOSkyp and fold sun/moon into LLVOWLSky
    gSky.mVOSkyp->updateGeometry(gSky.mVOSkyp->mDrawable);

    const F32 camHeightLocal = LLEnvironment::instance().getCamHeight();

    LLVector3 const & origin = LLViewerCamera::getInstance()->getOrigin();

    if (gPipeline.canUseWindLightShaders())
    {
        renderSkyHazeDeferred(origin, camHeightLocal);
        renderHeavenlyBodies();
        if (!gCubeSnapshot)
        {
            renderStarsDeferred(origin);
        }

        if (!gCubeSnapshot || gPipeline.mReflectionMapManager.isRadiancePass()) // don't draw clouds in irradiance maps to avoid popping
        {
            renderSkyCloudsDeferred(origin, camHeightLocal, cloud_shader);
        }
    }
}



LLViewerTexture* LLDrawPoolWLSky::getTexture()
{
    return NULL;
}

void LLDrawPoolWLSky::resetDrawOrders()
{
}

//static
void LLDrawPoolWLSky::cleanupGL()
{
    // <Vulkanstorm> the night sky's star tables, made again on next use
    if (sNightSkyTextures[0] != 0)
    {
        LLImageGL::deleteTextures(2, sNightSkyTextures);
        sNightSkyTextures[0] = sNightSkyTextures[1] = 0;
    }
    sNightSkyLoadTried = false;
    // </Vulkanstorm>
}

//static
void LLDrawPoolWLSky::restoreGL()
{
}
