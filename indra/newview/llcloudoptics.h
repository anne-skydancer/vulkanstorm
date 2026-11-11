// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llcloudoptics.h
 * @brief Cloud optics (stage L2b of the light study, doc/light/cloud-shadows.md): how much of a light
 *        gets through a cloud of a given drawn opacity, as a direct beam and as diffuse light.
 *        Renderer-free; the single source of the constants, which the cloud shadow map's shader
 *        takes from LLCloudShadowMap as one uniform (ctm_optics).
 *
 * The flat cloud layer draws an opacity alpha (cloudsF.glsl). It is read as the transmittance of a
 * layer of optical depth tau, 1 - alpha = exp(-tau / tau_v), with tau_v the optical depth of "one
 * e-fold of drawn opacity" (default 7.6: the visibility threshold of thin cirrus, tau 0.03, equals
 * one 8-bit step of the drawn alpha). alpha = 1 is capped at one step short of opaque (tau = tau_v ln 255).
 *
 * The light through the layer, with g = 0.85 for water droplets (delta-Eddington: the forward peak,
 * a share g^2 of the scattering, stays in the beam):
 *   beam    exp(-(1 - g^2) tau)                the direct light, what casts the shadow
 *   total   1 / (1 + 3/4 (1 - g) tau)          all of the light that leaves the base (Eddington, no absorption)
 *   diffuse total - beam                       the veil under the cloud (>= 0 for every tau)
 * The rest, 1 - total, is sent back up. The shader multiplies the light by the beam and L4 adds the
 * diffuse light. A layer that is clear (alpha 0) has beam 1 and diffuse 0 exactly.
 *
 * Doubles throughout; the shader repeats the same maths in 32 bit floats.
 */

#ifndef LL_LLCLOUDOPTICS_H
#define LL_LLCLOUDOPTICS_H

#include <algorithm>
#include <cmath>

namespace LLCloudOptics
{
// the asymmetry of water droplets (Stephens, AT622 notes section 15: 0.8 - 0.85)
constexpr double G_WATER = 0.85; // (ice, 0.75, was only the rejected alternative: one g for the whole layer)
// the forward peak of delta-Eddington (Joseph, Wiscombe & Weinman 1976)
constexpr double F_PEAK = G_WATER * G_WATER;
// Eddington's coefficient of the total transmittance for a collimated light. Not verified at the source: the diffuse-incidence
// two-stream form has 1/2 (Stephens, AT622 notes eq. 15.20b); 3/4 is the collimated form, kept by the owner's decision
constexpr double EDDINGTON_TOTAL = 0.75;
// the optical depth of one e-fold of drawn opacity (setting RenderCloudOpticalDepth)
constexpr double DEFAULT_VISUAL_DEPTH = 7.6;
// the drawn layer's 8-bit opacity step: alpha is capped at 1 - ALPHA_STEP
constexpr double ALPHA_STEP = 1.0 / 255.0;

struct Transmittance
{
    double beam;    // direct light
    double diffuse; // diffuse light under the cloud
};

// the optical depth of a layer drawn with opacity alpha
inline double tau(double alpha, double tau_v)
{
    return -tau_v * std::log(std::max(1.0 - alpha, ALPHA_STEP));
}

inline double beam(double t)
{
    return std::exp(-(1.0 - F_PEAK) * t);
}

inline double total(double t)
{
    return 1.0 / (1.0 + EDDINGTON_TOTAL * (1.0 - G_WATER) * t);
}

inline double diffuse(double t)
{
    return std::max(total(t) - beam(t), 0.0);
}

// the inverse of tau(): the opacity a flat layer draws for a column of optical depth t (for the volumetric clouds)
inline double alpha(double t, double tau_v)
{
    return 1.0 - std::exp(-t / tau_v);
}

inline Transmittance optics(double a, double tau_v)
{
    const double t = tau(a, tau_v);
    return { beam(t), diffuse(t) };
}

// ctm_optics of cloudShadowMapF.glsl: (tau_v, 1 - g^2, 3/4 (1 - g), alpha step)
struct ShaderParams
{
    float visual_depth, beam_coefficient, total_coefficient, alpha_step;
};

inline ShaderParams shader_params(double tau_v)
{
    return { (float) tau_v, (float) (1.0 - F_PEAK), (float) (EDDINGTON_TOTAL * (1.0 - G_WATER)), (float) ALPHA_STEP };
}
}

#endif // LL_LLCLOUDOPTICS_H
