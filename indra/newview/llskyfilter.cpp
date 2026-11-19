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
#include "llsettingssky.h"
#include "llviewercontrol.h"

namespace
{
LLSkyFilter::Vec3 to_vec(const LLColor3& c)
{
    return { c.mV[0], c.mV[1], c.mV[2] };
}

LLSkyFilter::Vec3 to_vec(const LLVector4& v)
{
    return { v.mV[0], v.mV[1], v.mV[2] };
}
} // namespace

// The values the dome's program (the sky's shader group) receives this frame: LLSettingsVOSky::applyToUniforms, then applySpecial, the
// group-wide list (SG_ANY) applied before the sky's own (SG_SKY), and within a list the vec3 entries after the vec4 ones
// (LLShaderUniforms::apply), so the later write of a uniform wins. The main view's values (the irradiance pass of a cube snapshot zeroes
// the ambient and desaturates; the tables are the main view's).
bool LLSkyFilter::current_inputs(Inputs& in)
{
    const LLEnvironment& env = LLEnvironment::instance();
    LLSettingsSky::ptr_t psky = env.getCurrentSky();
    if (!psky)
    {
        return false;
    }

    static LLCachedControl<bool> should_auto_adjust(gSavedSettings, "RenderSkyAutoAdjustLegacy", false);
    static LLCachedControl<F32> auto_adjust_ambient_scale(gSavedSettings, "RenderSkyAutoAdjustAmbientScale", 0.75f);
    static LLCachedControl<F32> auto_adjust_hdr_scale(gSavedSettings, "RenderSkyAutoAdjustHDRScale", 2.f);
    static LLCachedControl<F32> auto_adjust_blue_horizon_scale(gSavedSettings, "RenderSkyAutoAdjustBlueHorizonScale", 1.f);
    static LLCachedControl<F32> auto_adjust_blue_density_scale(gSavedSettings, "RenderSkyAutoAdjustBlueDensityScale", 1.f);

    in.lightnorm = to_vec(env.getClampedLightNorm());
    // SG_SKY: the raw sunlight and moonlight colours (the auto-adjusted sunlight goes to SG_ANY, which SG_SKY's own value overrides)
    in.sunlight_color = to_vec(psky->getSunlightColor());
    in.moonlight_color = to_vec(psky->getMoonlightColor());

    LLColor3 ambient(psky->getTotalAmbient());
    LLColor3 blue_horizon = psky->getBlueHorizon();
    LLColor3 blue_density = psky->getBlueDensity();
    F32 hdr_scale = 1.f;
    if (psky->getReflectionProbeAmbiance() != 0.f)
    {
        hdr_scale = sqrtf(psky->getGamma()) * 2.0f;
    }
    else if (psky->canAutoAdjust() && should_auto_adjust)
    {
        ambient *= (F32)auto_adjust_ambient_scale;
        hdr_scale = auto_adjust_hdr_scale;
        blue_horizon *= (F32)auto_adjust_blue_horizon_scale;
        blue_density *= (F32)auto_adjust_blue_density_scale;
    }
    in.ambient_color = to_vec(ambient);
    in.blue_horizon = to_vec(blue_horizon);
    in.blue_density = to_vec(blue_density);
    in.haze_horizon = psky->getHazeHorizon();
    in.haze_density = psky->getHazeDensity();
    in.cloud_shadow = psky->getCloudShadow();
    in.density_multiplier = psky->getDensityMultiplier();
    in.max_y = psky->getMaxY();
    in.glow = to_vec(psky->getGlow());
    const LLEnvironment::PrimaryLightState& primary = env.getPrimaryLight();
    in.sun_moon_blend = primary.band_weight;
    in.sun_glow_norm = to_vec(env.getClampedSunNorm());
    in.sky_hdr_scale = hdr_scale;
    in.classic_mode = (psky->canAutoAdjust() && !should_auto_adjust) ? 1 : 0;
    return true;
}
