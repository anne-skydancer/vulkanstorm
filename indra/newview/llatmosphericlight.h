// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llatmosphericlight.h
 * @brief The sun's and the sky's light on a surface (`sunlit` and `amblit`), as the viewer's
 *        calcAtmosphericVars / calcAtmosphericVarsLinear compute them. Renderer-free.
 *
 * A transcription of class1/windlight/atmosphericsFuncs.glsl (the terms that do not depend on
 * the surface position: sunlit and amblit; additive and atten are not part of it). The
 * viewer still runs the GLSL. This header is the reference that the next light stages (the
 * light study, doc/light/light-model-study.md, stages L2-L6) change first and the Vulkan
 * renderer will share, and indra/newview/tests/llatmosphericlight_test.cpp holds it equal to
 * the GLSL: the test's vectors come from the GLSL itself (scripts/light/
 * gen_atmosphere_vectors.py) and are checked in CI without a GPU.
 *
 * Arithmetic is in double; the GLSL's is float, so the test allows 1e-4 relative.
 */

#ifndef LL_LLATMOSPHERICLIGHT_H
#define LL_LLATMOSPHERICLIGHT_H

#include <algorithm>
#include <cmath>

namespace LLAtmosphericLight
{
struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
};

// The uniforms atmosphericsFuncs.glsl reads for these two terms. Colours are the viewer's
// uniform values (sunlight_color etc.), `light_y` is lightnorm.y (the sine of the light's
// elevation; the light direction is clamped to z >= -0.1 by LLEnvironment::getClampedLightNorm).
struct Inputs
{
    Vec3 sunlight_color;
    Vec3 moonlight_color;
    int sun_up_factor = 1;
    Vec3 ambient_color;
    Vec3 blue_density;
    double haze_density = 0.0;
    double cloud_shadow = 0.0;
    double density_multiplier = 0.0;
    double max_y = 0.0;
    double light_y = 1.0;
    double sky_sunlight_scale = 1.0; // RenderSkySunlightScale / RenderHDRSkySunlightScale
    double sky_ambient_scale = 1.0;  // RenderSkyAmbientScale
    int classic_mode = 0;
};

struct Result
{
    Vec3 sunlit; // sRGB when classic_mode, else linear (both scaled by sky_sunlight_scale)
    Vec3 amblit; // linear when classic_mode == 0; else sRGB. Includes ambientLighting().
};

inline double srgb_to_linear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// ambientLighting(): a touch of light opposite the sun, so shadowed areas keep some detail.
// `n_dot_l` is dot(norm, light_dir).
inline double ambient_lighting(double n_dot_l)
{
    double a = std::min(std::fabs(n_dot_l), 1.0);
    a *= 0.5;
    a *= a;
    return 1.0 - a;
}

// calcAtmosphericVars() reduced to sunlit and amblit (sRGB), before calcAtmosphericVarsLinear.
inline void calc_atmospheric_vars(const Inputs& in, Vec3& sunlit, Vec3& amblit)
{
    const Vec3& light = in.sun_up_factor == 1 ? in.sunlight_color : in.moonlight_color;
    const double haze = in.haze_density * 0.25;
    const double above_horizon_factor = 1.0 / std::max(1e-6, in.light_y);
    const double bd[3] = { in.blue_density.x, in.blue_density.y, in.blue_density.z };
    const double lc[3] = { light.x, light.y, light.z };
    const double amb[3] = { in.ambient_color.x, in.ambient_color.y, in.ambient_color.z };
    double s[3], a[3];
    for (int i = 0; i < 3; ++i)
    {
        const double light_atten = (bd[i] + haze) * (in.density_multiplier * in.max_y);
        s[i] = lc[i] * std::exp(-light_atten * above_horizon_factor);
        // increase ambient when there are more clouds
        const double tmp_ambient = amb[i] + (1.0 - amb[i]) * in.cloud_shadow * 0.5;
        a[i] = std::pow(tmp_ambient, 0.9) * 0.57;
    }
    sunlit = { s[0], s[1], s[2] };
    amblit = { a[0], a[1], a[2] };
}

// calcAtmosphericVarsLinear(): `n_dot_l` is dot(norm, light_dir).
inline Result calc_sunlit_amblit(const Inputs& in, double n_dot_l)
{
    Result r;
    calc_atmospheric_vars(in, r.sunlit, r.amblit);

    const double f = ambient_lighting(n_dot_l);
    r.amblit = { r.amblit.x * f, r.amblit.y * f, r.amblit.z * f };

    if (in.classic_mode < 1)
    {
        const Vec3 l = { srgb_to_linear(r.amblit.x), srgb_to_linear(r.amblit.y), srgb_to_linear(r.amblit.z) };
        const double lum = l.x * 0.2126 + l.y * 0.7152 + l.z * 0.0722;
        r.amblit = { lum, lum, lum };
        r.sunlit = { srgb_to_linear(r.sunlit.x), srgb_to_linear(r.sunlit.y), srgb_to_linear(r.sunlit.z) };
    }

    // multiply to get similar colours as when the "scaleSoftClip" implementation was doubling
    // colour values (allows for mixing of light sources other than sunlight, e.g. probes)
    r.sunlit = { r.sunlit.x * in.sky_sunlight_scale, r.sunlit.y * in.sky_sunlight_scale, r.sunlit.z * in.sky_sunlight_scale };
    r.amblit = { r.amblit.x * in.sky_ambient_scale, r.amblit.y * in.sky_ambient_scale, r.amblit.z * in.sky_ambient_scale };
    return r;
}
} // namespace LLAtmosphericLight

#endif // LL_LLATMOSPHERICLIGHT_H
