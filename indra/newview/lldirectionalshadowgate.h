// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file lldirectionalshadowgate.h
 * @brief When the directional (sun and moon) shadow cascades are not worth drawing: the light that reaches the
 *        ground is too dim for the shadow to show. Stochastic method only (Legacy never uses this).
 *
 * Renderer-free (no GL, no viewer types): LLEnvironment::updatePrimaryLight steps the gate once per main-thread
 * frame, LLPipeline::generateSunShadow skips the cascade pass while it says so, and the shader term
 * primary_shadow_strength is multiplied by its fade (it also weights the cloud shadow map, so the fade moves the cloud
 * shadows to the map's mean while E < 2 x THRESHOLD: invisible at that light level). indra/newview/tests/lldirectionalshadowgate_test.cpp checks it.
 *
 * The decision light is the sky's and the light's alone (never the camera, the view or the exposure):
 *     E = 3 x max(linear(primary light colour) channels) x sky sunlight scale
 * which is the factor pbrBaseLight gives a white surface facing the light in its brightest channel (sunlit x 3, sunlit =
 * the primary light's colour in linear light x RenderSkySunlightScale / RenderHDRSkySunlightScale; the shader lights each
 * channel on its own, so the largest channel is used, not the luminance). The cloud shadow map only lowers it
 * (transmittance <= 1) and is left out. Classic-mode lighting multiplies sunlit by a further 1.35 (softenLightF.glsl)
 * and is not included.
 *
 * Threshold. For a diffuse or rough surface a shadow changes a channel by at most E (albedo <= 1; the BRDF's 1/pi is left
 * out). One 8-bit step near black is 1/(255 x 12.92) = 3.0e-4 in linear light. The brightest exposure is RenderExposure's
 * clamp (4) times the dynamic exposure's maximum (2 x sqrt(gamma)), and the EEP validator allows gamma up to 20: a gain of
 * 4 x 2 x sqrt(20) = 35.8 (16 for gamma 4). E = 5e-6 therefore changes a diffuse pixel by at most 1.8e-4 = 0.6 of an 8-bit
 * step even then: invisible. A smooth specular peak can reach 10 x E (the clamp in pbrBaseLight), a few highlight pixels
 * of up to a few steps near black in that extreme; the threshold accepts this. The RenderUseExposureSkySettings path
 * (off by default) takes its maximum from the sky and is not bounded by gamma.
 *
 * Gate: skip below THRESHOLD; resume above RESUME (10 % above, so a light hovering at the threshold does not make the
 * pass flicker on and off). The fade is a pure function of E: 0 up to RESUME, smoothstep to 1 at FULL = 2 x THRESHOLD, 1
 * above. It multiplies primary_shadow_strength, so nothing pops: a skipped frame has fade 0 exactly (a skipped
 * frame has E <= RESUME), and the frame that resumes renders the cascades first (its fade is above 0).
 */

#ifndef LL_LLDIRECTIONALSHADOWGATE_H
#define LL_LLDIRECTIONALSHADOWGATE_H

#include <algorithm>
#include <cmath>

namespace LLDirectionalShadowGate
{
    constexpr double THRESHOLD = 5.0e-6;          // skip below this
    constexpr double RESUME = 1.1 * THRESHOLD;    // render again above this (hysteresis)
    constexpr double FULL = 2.0 * THRESHOLD;      // the shadow is at full strength from here

    inline double srgbToLinear(double c)
    {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

    // The decision light from the primary light's colour (sRGB encoding, as primary_light_color) and the sky sunlight scale.
    // A colour or scale that is not finite or is negative counts as 0, so the light is 0 and the cascades are skipped
    // (a NaN colour gives NaN lighting in the shaders anyway); step() passes through a non-finite E given to it directly.
    inline double decisionLight(double r, double g, double b, double sunlight_scale)
    {
        auto clean = [](double v) { return std::isfinite(v) ? (v > 0.0 ? v : 0.0) : 0.0; };
        const double lr = srgbToLinear(clean(r)), lg = srgbToLinear(clean(g)), lb = srgbToLinear(clean(b));
        const double lmax = std::max({ lr, lg, lb });
        return 3.0 * lmax * clean(sunlight_scale);
    }

    // The shadow's share of full strength for the decision light E (0 .. 1).
    inline double fade(double e)
    {
        if (!(e > RESUME))
        {
            return 0.0; // also NaN: callers handle that in step()
        }
        if (e >= FULL)
        {
            return 1.0;
        }
        const double x = (e - RESUME) / (FULL - RESUME);
        return x * x * (3.0 - 2.0 * x);
    }

    struct State
    {
        bool skipped = false;
    };

    struct Decision
    {
        bool skip;       // do not render the cascades this frame
        double strength; // the factor for primary_shadow_strength: 0 when skipped
    };

    // One step per main-thread frame. 'light_valid' false (no sky yet) or a non-finite E never skips.
    inline Decision step(State& state, double e, bool light_valid = true)
    {
        if (!light_valid || !std::isfinite(e))
        {
            state.skipped = false;
            return { false, 1.0 };
        }
        if (state.skipped)
        {
            if (e > RESUME)
            {
                state.skipped = false;
            }
        }
        else if (e < THRESHOLD)
        {
            state.skipped = true;
        }
        return { state.skipped, state.skipped ? 0.0 : fade(e) };
    }
}

#endif
