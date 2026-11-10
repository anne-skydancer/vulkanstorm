/**
 * @file class1\windlight\physicalAtmosphereF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The physical atmosphere's transmittance (stage AT0 of the item-4 plan): the GLSL twin of
 * LLAtmosphericLight::LLAtmosphere::transmittance (llatmosphericlight.h). Functions only. It is
 * NOT registered with the shader manager and no viewer shader includes it yet (AT2 does); it is
 * compiled and run on the GPU by scripts/light/gen_transmittance_vectors.py, whose vectors hold
 * the C++ equal to it (indra/newview/tests/llatmosphericlight_test.cpp).
 *
 * pa_transmittance(h_km, mu, disc_sin): the transmittance, per channel, from altitude h_km above
 * the ground towards a body at zenith cosine mu, to the top of the atmosphere (the ground does not
 * occlude), times the share of the body's disc that is above the horizon. The atmosphere is the
 * uniform pa_atm, which LLAtmosphere holds field for field. Altitudes are in km, the profiles'
 * widths and scales in metres, as in the EEP settings. Segmented 8-point Gauss-Legendre along the
 * path, cut at every profile kink, at the slab's 0.75 H and H, at 4, 10 and 20 scale heights of every
 * exponential layer, where a ray crosses the ground and at the perigee of a downward ray.
 */

struct PADensityProfile
{
    int   count;            // 1 or 2 layers
    float width[2];         // m; layer 0 is used below it
    float exp_term[2];
    float exp_scale[2];     // 1/m
    float linear_term[2];   // 1/m
    float constant_term[2];
};

struct PAAtmosphere
{
    vec3  rayleigh_ext;         // 1/km at density 1
    float aerosol_ext;
    float slab_ext;             // ground haze slab, 1/km
    float slab_top_km;
    vec3  ozone_ext;
    PADensityProfile rayleigh;
    PADensityProfile mie;
    PADensityProfile absorption;
    float planet_radius_km;
    float top_radius_km;
};

uniform PAAtmosphere pa_atm;

#define PA_MAX_ALT 32
#define PA_MAX_CUTS 67          // 2 roots per altitude, the perigee and the two ends

// an exponential layer is cut at these multiples of its scale height
const float PA_EXP_CUTS[3] = float[3](4.0, 10.0, 20.0);

const float PA_GL_X[8] = float[8](-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                   0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363);
const float PA_GL_W[8] = float[8](0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                  0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763);

// clamp(exp_term e^(exp_scale h) + linear h + constant, 0, 1); layer 0 below its width
float pa_density(PADensityProfile p, float h_km)
{
    float h = h_km * 1000.0;
    int i = (p.count > 1 && h >= p.width[0]) ? 1 : 0;
    // no exp when exp_term is 0 (0 * inf is NaN); the exponent is bounded so e^x stays finite
    float e = p.exp_term[i] != 0.0 ? p.exp_term[i] * exp(clamp(p.exp_scale[i] * h, -80.0, 80.0)) : 0.0;
    float d = e + p.linear_term[i] * h + p.constant_term[i];
    return clamp(d, 0.0, 1.0);
}

void pa_add_altitude(inout float alts[PA_MAX_ALT], inout int n, float h_km, float top_km)
{
    if (h_km > 0.0 && h_km < top_km && n < PA_MAX_ALT)
    {
        alts[n] = h_km;
        ++n;
    }
}

// the altitudes where a profile needs a cut: the layer boundary, the clamps of its linear layers
// (inside the layer) and, for an exponential layer, 4, 10 and 20 scale heights
void pa_profile_cuts(PADensityProfile p, float top_km, inout float alts[PA_MAX_ALT], inout int n)
{
    int layers = p.count > 1 ? 2 : 1;
    if (layers == 2)
    {
        pa_add_altitude(alts, n, p.width[0] * 0.001, top_km);
    }
    for (int i = 0; i < layers; ++i)
    {
        if (p.exp_term[i] == 0.0 && p.linear_term[i] != 0.0)
        {
            float lo = (i == 0) ? 0.0 : p.width[0];
            float hi = (layers == 2 && i == 0) ? p.width[0] : 1e30;
            for (int level = 0; level < 2; ++level)
            {
                float h = (float(level) - p.constant_term[i]) / p.linear_term[i];
                if (h > lo && h < hi)
                {
                    pa_add_altitude(alts, n, h * 0.001, top_km);
                }
            }
        }
    }
    for (int i = 0; i < layers; ++i)
    {
        if (p.exp_term[i] != 0.0 && p.exp_scale[i] < 0.0)
        {
            for (int k = 0; k < 3; ++k)
            {
                pa_add_altitude(alts, n, PA_EXP_CUTS[k] / -p.exp_scale[i] * 0.001, top_km);
            }
        }
    }
}

// path length s to altitude alt: s^2 + 2 b s - k = 0 with b = r0 mu, k = (alt - h0)(2 Rg + alt + h0);
// s_lo <= s_hi, -1 when absent
void pa_path_roots(float b, float h0, float rg, float alt, out float s_lo, out float s_hi)
{
    float k = (alt - h0) * (2.0 * rg + alt + h0);
    float disc = b * b + k;
    s_lo = -1.0;
    s_hi = -1.0;
    if (disc < 0.0)
    {
        return;
    }
    float sq = sqrt(disc);
    if (b >= 0.0)
    {
        s_hi = (b + sq) > 0.0 ? k / (b + sq) : -1.0;
    }
    else
    {
        s_hi = -b + sq;
        s_lo = s_hi > 0.0 ? -k / s_hi : -1.0;
    }
}

vec3 pa_transmittance(float h_km, float mu, float disc_sin)
{
    float rg = pa_atm.planet_radius_km;
    float top = pa_atm.top_radius_km - rg;
    float h0 = clamp(h_km, 0.0, top);
    mu = clamp(mu, -1.0, 1.0);
    float d = clamp(disc_sin, 0.0, 1.0);
    float r0 = rg + h0;
    float q0 = h0 * (2.0 * rg + h0);          // r0^2 - Rg^2, without the cancellation
    float mu_h = -sqrt(q0) / r0;
    float vis = d > 0.0 ? smoothstep(mu_h - d, mu_h + d, mu) : (mu >= mu_h ? 1.0 : 0.0);
    if (!(vis > 0.0))
    {
        return vec3(0.0);
    }

    float b = r0 * mu;
    float unused;
    float path;
    pa_path_roots(b, h0, rg, top, unused, path);
    if (!(path > 0.0))
    {
        return vec3(vis);
    }

    float alts[PA_MAX_ALT];
    int na = 0;
    alts[na++] = 0.0;  // where a ray below the horizon crosses the ground (the altitude is clamped there)
    if (pa_atm.slab_ext > 0.0 && pa_atm.slab_top_km > 0.0 && pa_atm.slab_top_km < top)
    {
        alts[na++] = 0.75 * pa_atm.slab_top_km;
        alts[na++] = pa_atm.slab_top_km;
    }
    pa_profile_cuts(pa_atm.rayleigh, top, alts, na);
    pa_profile_cuts(pa_atm.mie, top, alts, na);
    pa_profile_cuts(pa_atm.absorption, top, alts, na);

    float cut[PA_MAX_CUTS];
    int n = 0;
    cut[n++] = 0.0;
    cut[n++] = path;
    for (int i = 0; i < na; ++i)
    {
        float s_lo;
        float s_hi;
        pa_path_roots(b, h0, rg, alts[i], s_lo, s_hi);
        if (s_lo > 0.0 && s_lo < path)
        {
            cut[n++] = s_lo;
        }
        if (s_hi > 0.0 && s_hi < path)
        {
            cut[n++] = s_hi;
        }
    }
    if (b < 0.0 && -b < path)
    {
        cut[n++] = -b;  // the perigee
    }
    for (int i = 1; i < n; ++i)  // insertion sort
    {
        float v = cut[i];
        int j = i - 1;
        while (j >= 0 && cut[j] > v)
        {
            cut[j + 1] = cut[j];
            --j;
        }
        cut[j + 1] = v;
    }

    vec3 tau = vec3(0.0);
    float slab_lo = 0.75 * pa_atm.slab_top_km;
    for (int i = 0; i + 1 < n; ++i)
    {
        float hw = 0.5 * (cut[i + 1] - cut[i]);
        float mid = 0.5 * (cut[i + 1] + cut[i]);
        if (!(hw > 0.0))
        {
            continue;
        }
        for (int k = 0; k < 8; ++k)
        {
            float s = mid + hw * PA_GL_X[k];
            float r = sqrt(max(r0 * r0 + 2.0 * b * s + s * s, 0.0));
            float h = max((q0 + 2.0 * b * s + s * s) / (r + rg), 0.0);  // altitude, without the cancellation
            float dr = pa_density(pa_atm.rayleigh, h);
            float dm = pa_density(pa_atm.mie, h);
            float dz = pa_density(pa_atm.absorption, h);
            float ds = pa_atm.slab_top_km > 0.0 ? 1.0 - smoothstep(slab_lo, pa_atm.slab_top_km, h) : 0.0;
            vec3 grey = vec3(pa_atm.aerosol_ext * dm + pa_atm.slab_ext * ds);
            tau += hw * PA_GL_W[k] * (pa_atm.rayleigh_ext * dr + grey + pa_atm.ozone_ext * dz);
        }
    }
    return exp(-tau) * vis;
}
