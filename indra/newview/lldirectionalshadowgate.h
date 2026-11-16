// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file lldirectionalshadowgate.h
 * @brief When the directional (sun and moon) shadow cascades are not worth drawing: the light that reaches the
 *        ground is too dim for the shadow to show. Stochastic method only (Legacy never uses this).
 *
 * Renderer-free (no GL, no viewer types): LLEnvironment::updatePrimaryLight steps the gate once per main-thread
 * frame, LLPipeline::generateSunShadow skips the cascade pass while it says so, and the shader term
 * primary_shadow_strength is multiplied by its fade. indra/newview/tests/lldirectionalshadowgate_test.cpp checks it.
 *
 * The decision light is the sky's and the light's alone (never the camera, the view or the exposure):
 *     E = 3 x luminance(linear(primary light colour)) x sky sunlight scale
 * which is the factor pbrBaseLight gives a white surface facing the light (sunlit x 3, sunlit = the primary light's
 * colour in linear light x RenderSkySunlightScale / RenderHDRSkySunlightScale). The cloud shadow map only lowers it
 * (transmittance <= 1) and is left out, so E is an upper bound of the light on any surface and the gate never skips a
 * shadow that could show.
 *
 * Threshold. A shadow changes a pixel by at most E (albedo <= 1; the BRDF's 1/pi is left out, so this is an upper
 * bound). One 8-bit step near black is 1/(255 x 12.92) = 3.0e-4 in linear light. The brightest exposure is
 * RenderExposure's clamp (4) times the dynamic exposure's maximum (2 x sqrt(gamma), 4 for gamma 4): a gain of 16, with the
 * tonemap no steeper than 1 near black. E = 1e-5 therefore changes a pixel by at most 1.6e-4 = about half an 8-bit step
 * in the brightest exposure: invisible.
 *
 * Gate: skip below THRESHOLD; resume above RESUME (10 % above, so a light hovering at the threshold does not make the
 * pass flicker on and off). The fade is a pure function of E: 0 up to RESUME, smoothstep to 1 at FULL = 2 x THRESHOLD, 1
 * above. It multiplies primary_shadow_strength, so nothing pops: a skipped frame has fade 0 exactly (a skipped
 * frame has E <= RESUME), and the frame that resumes renders the cascades first (its fade is above 0).
 */

#ifndef LL_LLDIRECTIONALSHADOWGATE_H
#define LL_LLDIRECTIONALSHADOWGATE_H

#include <cmath>

namespace LLDirectionalShadowGate
{
    constexpr double THRESHOLD = 1.0e-5;          // skip below this
    constexpr double RESUME = 1.1 * THRESHOLD;    // render again above this (hysteresis)
    constexpr double FULL = 2.0 * THRESHOLD;      // the shadow is at full strength from here

    inline double srgbToLinear(double c)
    {
        return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
    }

    // The decision light from the primary light's colour (sRGB encoding, as primary_light_color) and the sky sunlight scale.
    // A colour or scale that is not finite or is negative counts as 0 (the sky is then not lit at all, as in the shaders'
    // clamps), except that nothing here skips on a non-finite value (see step).
    inline double decisionLight(double r, double g, double b, double sunlight_scale)
    {
        auto clean = [](double v) { return std::isfinite(v) ? (v > 0.0 ? v : 0.0) : 0.0; };
        const double lr = srgbToLinear(clean(r)), lg = srgbToLinear(clean(g)), lb = srgbToLinear(clean(b));
        const double lum = 0.2126 * lr + 0.7152 * lg + 0.0722 * lb;
        return 3.0 * lum * clean(sunlight_scale);
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
