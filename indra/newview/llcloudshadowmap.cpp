/**
 * @file llcloudshadowmap.cpp
 * @brief The cloud transmittance map: cloud shadows on the ground from the flat cloud layer
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

#include "llcloudshadowmap.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llcloudshadow.h"
#include "lldrawpoolwlsky.h"
#include "llenvironment.h"
#include "llframetimer.h"
#include "llglslshader.h"
#include "llrender.h"
#include "llsettingssky.h"
#include "llsky.h"
#include "llviewercontrol.h"
#include "llviewershadermgr.h"
#include "llviewertexture.h"
#include "llvosky.h"
#include "pipeline.h"

#include <glm/gtc/type_ptr.hpp>

extern bool gCubeSnapshot;

namespace
{
const F32 FADE_WIDTH = 0.0625f; // the share of the map's width over which it fades to its mean at the edge
const F64 SUN_ANGULAR_TAN = 0.0093; // tan of the sun's angular radius (0.53 degrees across): the penumbra per metre of height
}

void LLCloudShadowMap::cleanup()
{
    mTarget.release();
    mAllocated = false;
    mActive = false;
    mBuiltFrame = U64(-1);
}

void LLCloudShadowMap::update()
{
    static LLCachedControl<F32> strength(gSavedSettings, "RenderCloudShadowStrength", 1.f);
    static LLCachedControl<F32> penumbra_height(gSavedSettings, "RenderCloudShadowPenumbraHeight", 1500.f);
    static LLCachedControl<F32> extent_setting(gSavedSettings, "RenderCloudShadowExtent", 4096.f);

    const U64 frame = LLFrameTimer::getFrameCount();
    if (mBuiltFrame == frame)
    {
        return;
    }
    mBuiltFrame = frame;
    mActive = false;

    if (strength <= 0.f)
    {
        return;
    }

    // exactly the conditions under which the sky draws clouds (LLDrawPoolWLSky::renderSkyCloudsDeferred)
    LLSettingsSky::ptr_t psky = LLEnvironment::instance().getCurrentSky();
    if (!psky || !gPipeline.canUseWindLightShaders() || !gPipeline.hasRenderType(LLPipeline::RENDER_TYPE_CLOUDS) ||
        !gSky.mVOSkyp || !gSky.mVOSkyp->getCloudNoiseTex() || LLDrawPoolWLSky::usesHDRISky() ||
        psky->getCloudScale() < 0.001f || !gCloudShadowMapProgram.isComplete())
    {
        return;
    }

    LL_PROFILE_GPU_ZONE("cloud shadow map");

    if (!mAllocated)
    {
        mAllocated = mTarget.allocate(LLCloudShadow::MAP_RESOLUTION, LLCloudShadow::MAP_RESOLUTION, GL_R8, false, LLTexUnit::TT_TEXTURE, LLTexUnit::TMG_AUTO);
        if (!mAllocated)
        {
            return;
        }
    }

    // the layout follows the agent's camera, not the view being rendered (cube snapshots and mirrors
    // read the same map), and is a function of where that camera is in the world
    const F64 extent = llclamp((F64) extent_setting, 512.0, 16384.0);
    const LLVector3d camera = gAgentCamera.getCameraPositionGlobal();
    mLayout = LLCloudShadow::layout_for_camera({ camera.mdV[VX], camera.mdV[VY] }, extent);

    // the light: the sun, or the moon at night, as the shaders' lightnorm
    LLVector3 light = LLEnvironment::instance().getLightDirection();
    if (light.mV[VZ] < 0.f)
    { // below the horizon the light is out; keep the dome maths valid
        light.mV[VZ] = 0.f;
    }
    if (light.normalize() < 1e-6f)
    {
        light.set(0.f, 0.f, 1.f);
    }
    const LLCloudShadow::Vec3 L = { light.mV[VX], light.mV[VY], light.mV[VZ] };

    const F64 cloud_scale = psky->getCloudScale();
    const LLCloudShadow::Vec2 o = LLCloudShadow::dome_offset(L);
    const LLCloudShadow::Vec2 tc0 = LLCloudShadow::layer_texcoord({ mLayout.origin.x + o.x, mLayout.origin.y + o.y }, cloud_scale);
    const F64 k = mLayout.extent / (2.0 * LLCloudShadow::DOME_RADIUS * cloud_scale);

    mTarget.bindTarget();
    {
        LLGLDepthTest depth(GL_FALSE, GL_FALSE);
        LLGLDisable blend(GL_BLEND);
        LLGLDisable cull(GL_CULL_FACE);
        LLGLDisable scissor(GL_SCISSOR_TEST);

        gCloudShadowMapProgram.bind();
        LLDrawPoolWLSky::bindCloudNoise(&gCloudShadowMapProgram, psky);

        static const LLStaticHashedString sTc0("ctm_tc0");
        static const LLStaticHashedString sSpan("ctm_tc_span");
        static const LLStaticHashedString sAlt("ctm_altitude_blend");
        gCloudShadowMapProgram.uniform2f(sTc0, (F32) tc0.x, (F32) tc0.y);
        gCloudShadowMapProgram.uniform2f(sSpan, (F32) k, (F32) -k);
        gCloudShadowMapProgram.uniform1f(sAlt, (F32) LLCloudShadow::altitude_blend(L, psky->getMaxY()));

        gPipeline.mScreenTriangleVB->setBuffer();
        gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

        gCloudShadowMapProgram.unbind();
    }
    mTarget.flush(); // generates the mips: the penumbra's blur and the mean

    // where the map lies in the agent frame, for the lookup's matrix
    const LLVector3 origin_agent = gAgent.getPosAgentFromGlobal(LLVector3d(mLayout.origin.x, mLayout.origin.y, 0.0));
    mOriginAgent[0] = origin_agent.mV[VX];
    mOriginAgent[1] = origin_agent.mV[VY];

    // the edge softness: a layer H_pen up and a light of angular size 0.0093 * sun_scale, as a mip level
    const F32 light_scale = psky->getIsSunUp() ? psky->getSunScale() : psky->getMoonScale();
    const F64 penumbra = (F64) penumbra_height * SUN_ANGULAR_TAN * llmax(light_scale, 0.f);
    mLod = (F32) llmax(0.0, std::log2(llmax(penumbra, 1e-3) / mLayout.texel()));
    mStrength = llclamp((F32) strength, 0.f, 1.f);
    mActive = true;
}

void LLCloudShadowMap::bind(LLGLSLShader& shader)
{
    S32 channel = shader.enableTexture(LLShaderMgr::CLOUD_SHADOW_MAP, LLTexUnit::TT_TEXTURE);
    if (channel < 0)
    {
        return;
    }

    const bool on = mActive && mAllocated;
    if (on)
    {
        mTarget.bindTexture(0, channel, LLTexUnit::TFO_TRILINEAR);
        gGL.getTexUnit(channel)->setTextureAddressMode(LLTexUnit::TAM_CLAMP);
    }
    else
    {
        gGL.getTexUnit(channel)->bindFast(LLViewerFetchedTexture::sWhiteImagep);
    }

    // probes and cube snapshots get the mean, never a passing shadow
    const F32 mean_only = gCubeSnapshot ? 1.f : 0.f;
    shader.uniform4f(LLShaderMgr::CLOUD_SHADOW_PARAMS, on ? mStrength : 0.f, mean_only, FADE_WIDTH, mLod);

    if (on && mean_only < 0.5f)
    {
        // eye space -> the map's coordinate: the inverse of this view's model-view takes the eye
        // position to agent coordinates, then (east, north) minus the map's origin over its extent
        // the inverse is kept while the view stays the same (many shaders bind in one pass)
        static glm::mat4 last_view(0.f);
        static glm::dmat4 inv_view(1.0);
        const glm::mat4 view = get_current_modelview();
        if (view != last_view)
        {
            last_view = view;
            inv_view = glm::inverse(glm::dmat4(view));
        }
        const F64 s = 1.0 / mLayout.extent;
        glm::dmat4 to_uv(0.0);
        to_uv[0][0] = s;           // uv.x = (x - ox) / extent
        to_uv[3][0] = -mOriginAgent[0] * s;
        to_uv[1][1] = s;           // uv.y = (y - oy) / extent
        to_uv[3][1] = -mOriginAgent[1] * s;
        to_uv[3][3] = 1.0;
        const glm::mat4 m = glm::mat4(to_uv * inv_view);
        shader.uniformMatrix4fv(LLShaderMgr::CLOUD_SHADOW_MAT, 1, GL_FALSE, glm::value_ptr(m));
    }
}
