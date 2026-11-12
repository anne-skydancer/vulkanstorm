/**
 * @file lllocallight.h
 * @brief The local lights' shadow rules that do not need a renderer: emitter size and exclusion zone
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

#ifndef LL_LLLOCALLIGHT_H
#define LL_LLLOCALLIGHT_H

// Header-only and free of GL and viewer types, so that indra/newview/tests/lllocallight_test.cpp can check the
// rules standalone. The Stochastic method's local light visibility (shadowStochasticUtil.glsl, stTransmittance)
// takes its per-light lengths from here: the radius of the emitter its rays go toward, which sets the penumbra, and
// r_excl, how close to the light's eye a stored depth still counts as an occluder.
//
// What is excluded, exactly. A projector's own prim never shades its light: it is not drawn into the projector's map
// (LLPipeline::stateSort skips it) and the map's near plane lies at the prim's face. Everything else casts, linked
// parts of the fixture (a shade, a housing) and any other object included. So a projector's r_excl is only the stretch
// from the eye (the apex, behind the face) to the face plus a small margin, where the map holds no depth anyway; the
// margin keeps the face's own rim and the depth precision from shading the light. A point light has no near plane, so
// its zone is its own drawable's half extent (U2; unused until then).

#include <algorithm>
#include <cmath>

namespace LLLocalLight
{
    constexpr float EMITTER_MIN = 0.02f;      // metres: the smallest emitter radius s
    constexpr float EMITTER_MAX = 0.5f;       // metres: the largest
    constexpr float EXCLUSION_MAX = 1.0f;     // metres: the longest point light exclusion
    constexpr float APEX_MAX = 50.f;          // metres: the longest eye-to-face stretch taken
    // A projector's margin past its face. The map's near plane is 1 cm beyond the face; 5 cm covers it and the depth
    // precision there, and is small enough that a shade 10 cm or more in front of the face still casts.
    constexpr float PROJECTOR_MARGIN = 0.05f;

    // The projectors' emitter radius of stage U0: today's fixed 0.075 m (half of the 0.15 m wide emitter). Stage U2
    // replaces it by projectorEmitterRadius for all local lights together.
    constexpr float PROJECTOR_EMITTER_U0 = 0.075f;

    inline float clampEmitter(float s)
    {
        return std::min(std::max(s, EMITTER_MIN), EMITTER_MAX);
    }

    // A projector's emitter radius from the prim's face (scale x and y): the radius of the disc of the same area
    inline float projectorEmitterRadius(float scale_x, float scale_y)
    {
        constexpr float pi = 3.14159265358979f;
        return clampEmitter(std::sqrt(std::max(scale_x, 0.f) * std::max(scale_y, 0.f) / pi));
    }

    // A point light's emitter radius from its prim's scale: half the median extent
    inline float pointEmitterRadius(float scale_x, float scale_y, float scale_z)
    {
        const float hi = std::max(scale_x, std::max(scale_y, scale_z));
        const float lo = std::min(scale_x, std::min(scale_y, scale_z));
        const float median = scale_x + scale_y + scale_z - hi - lo;
        return clampEmitter(0.5f * median);
    }

    // The distance of a projector's eye (apex) behind the prim's face: the frustum of the field of view fov (radians)
    // is as wide as the face (scale_y) at the face. A field of view of zero or less, or a huge stretch, is capped.
    inline float projectorApexToFace(float scale_y, float fov)
    {
        const float t = std::tan(0.5f * std::min(std::max(fov, 1.0e-3f), 3.1f));
        return std::min(0.5f * std::max(scale_y, 0.f) / t, APEX_MAX);
    }

    // A projector's r_excl, the distance from its eye: the stretch to the face and the margin. Nothing else is excluded.
    inline float projectorExclusion(float scale_y, float fov)
    {
        return projectorApexToFace(scale_y, fov) + PROJECTOR_MARGIN;
    }

    // A point light's r_excl from its centre (U2): half the extent of the light's own drawable. LLDrawable::getRadius is
    // the length of the prim's scale vector (the full diagonal, LLVOVolume::updateRadius), so half of it, within the
    // emitter radius s and EXCLUSION_MAX.
    inline float exclusionRadius(float drawable_radius, float s)
    {
        return std::min(std::max(0.5f * drawable_radius, s), EXCLUSION_MAX);
    }
}

#endif // LL_LLLOCALLIGHT_H
