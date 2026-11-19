/**
 * @file llskyfilter.cpp
 * @brief Stage AT2-1: the EEP filter's inputs as the sky's shader group gets them
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

#include "llskyfilter.h"

#include "llenvironment.h"
#include "llshadermgr.h"

// The values the dome's program (the sky's shader group, SG_SKY) receives. LLEnvironment::updateShaderUniforms applies the sky's and
// the water's group-wide lists (SG_ANY), then the sky's and the water's SG_SKY lists, each with LLShaderUniforms::apply (integers,
// floats, vec4s, vec3s, each in the order they were set), so the last write of a uniform wins: this walks them the same way. Reading the
// lists, rather than redoing llsettingsvo.cpp's branches (applyToUniforms, applySpecial: the auto-adjust scales, the probe ambiance's
// HDR scale, the classic mode), keeps the filter on exactly what the dome gets. The lists hold the main view's values except in a
// reflection probe's irradiance pass (zero ambient, desaturated colours, sky_hdr_scale left unset), where LLAtmosphereLUTs does not
// read them.
bool LLSkyFilter::current_inputs(Inputs& in)
{
    const LLEnvironment& env = LLEnvironment::instance();
    const LLShaderUniforms* lists[] = { &env.mSkyUniforms[LLGLSLShader::SG_ANY], &env.mWaterUniforms[LLGLSLShader::SG_ANY],
                                        &env.mSkyUniforms[LLGLSLShader::SG_SKY], &env.mWaterUniforms[LLGLSLShader::SG_SKY] };
    bool complete = true;
    auto vec3_of = [&](S32 index, Vec3& out) {
        bool found = false;
        for (const LLShaderUniforms* list : lists)
        {
            for (const LLShaderUniforms::Vector3Setting& u : list->mVector3s)
            {
                if (u.mUniform == index)
                {
                    out = { u.mValue.mV[0], u.mValue.mV[1], u.mValue.mV[2] };
                    found = true;
                }
            }
        }
        complete = complete && found;
    };
    auto float_of = [&](S32 index, double& out) {
        bool found = false;
        for (const LLShaderUniforms* list : lists)
        {
            for (const LLShaderUniforms::FloatSetting& u : list->mFloats)
            {
                if (u.mUniform == index)
                {
                    out = u.mValue;
                    found = true;
                }
            }
        }
        complete = complete && found;
    };
    auto int_of = [&](S32 index, int& out) {
        bool found = false;
        for (const LLShaderUniforms* list : lists)
        {
            for (const LLShaderUniforms::IntSetting& u : list->mIntegers)
            {
                if (u.mUniform == index)
                {
                    out = u.mValue;
                    found = true;
                }
            }
        }
        complete = complete && found;
    };

    vec3_of(LLShaderMgr::LIGHTNORM, in.lightnorm);
    vec3_of(LLShaderMgr::SUNLIGHT_COLOR, in.sunlight_color);
    vec3_of(LLShaderMgr::MOONLIGHT_COLOR, in.moonlight_color);
    vec3_of(LLShaderMgr::AMBIENT, in.ambient_color);
    vec3_of(LLShaderMgr::BLUE_HORIZON, in.blue_horizon);
    vec3_of(LLShaderMgr::BLUE_DENSITY, in.blue_density);
    float_of(LLShaderMgr::HAZE_HORIZON, in.haze_horizon);
    float_of(LLShaderMgr::HAZE_DENSITY, in.haze_density);
    float_of(LLShaderMgr::CLOUD_SHADOW, in.cloud_shadow);
    float_of(LLShaderMgr::DENSITY_MULTIPLIER, in.density_multiplier);
    float_of(LLShaderMgr::MAX_Y, in.max_y);
    vec3_of(LLShaderMgr::GLOW, in.glow);
    float_of(LLShaderMgr::SUN_MOON_BLEND, in.sun_moon_blend);
    vec3_of(LLShaderMgr::SUN_GLOW_NORM, in.sun_glow_norm);
    float_of(LLShaderMgr::SKY_HDR_SCALE, in.sky_hdr_scale);
    int_of(LLShaderMgr::CLASSIC_MODE, in.classic_mode);
    return complete;
}
