/**
 * @file class1\windlight\atmosphereTablesF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * Stage AT2-1: the passes that fill the physical atmosphere's tables (LLAtmosphereLUTs, llatmosphereluts.cpp). Fragment passes on
 * one full-screen triangle each, no compute (the owner measured GL compute hurting the frame rate while moving). The atmosphere is
 * the clear-day base of llatmospherebase.h, read from the `Atmosphere` uniform block by atmosphereCommonF.glsl, which is linked in
 * as its own unit. AT_PASS selects the pass (a permutation); the sizes are defines from LLAtmosphereLUTs (llatmosphereluts.h), which
 * allocates the targets with the same constants (the sizes below are theirs):
 *
 *   1  T        256 x 64   the transmittance T(h, mu) (pa_trans_texel)
 *   2  MS split 256 x 256  one fragment per (Psi texel, direction): 32 x 32 texels x 8 x 8 directions; out 0 L, out 1 the transfer
 *   3  MS       32 x 32    the multiple scattering Psi(h, mu_s): the 64 directions of each texel summed (pa_ms_resolve)
 *   4  E split  512 x 128  one fragment per (E texel, direction): 64 x 16 texels x 8 x 8 cosine directions
 *   5  E        64 x 16    the sky irradiance E(h, mu_s): the 64 directions summed
 *   6  sky view 192 x 108  the sky's radiance from an observer on the ground (pa_sky_radiance, at_steps steps), in the dome's frame
 *
 * The texels are the C++'s: TransmittanceLUT::texel_ray, MsLUT::h_of / mu_of, SkyIrradianceLUT::h_of and LLSkyFilter's sky-view
 * parameterisation (llskyfilter.h); scripts/light/check_atmosphere_tables.py holds every table to the C++ on the GPU.
 */

#ifndef AT_PASS
#define AT_PASS 1
#endif
#if !defined(AT_T_W) || !defined(AT_T_H) || !defined(AT_MS_W) || !defined(AT_MS_H) || !defined(AT_E_W) || !defined(AT_E_H) || !defined(AT_DIRS) || !defined(AT_SKY_W) || !defined(AT_SKY_H)
#error the tables' sizes are defines (LLAtmosphereLUTs)
#endif

layout(location = 0) out vec4 frag_data0;
#if AT_PASS == 2
layout(location = 1) out vec4 frag_data1;
#endif

uniform sampler2D at_part0;                  // the split passes' partials (reductions)
uniform sampler2D at_part1;
uniform int at_steps;                        // the sky view's march (RenderAtmosphereSkyViewSteps)

float pa_top_height();
vec3 pa_trans_texel(ivec2 ij, ivec2 size);
void pa_ms_dir(float h_km, float mu_s, ivec2 ij, int n_dirs, int n_steps, out vec3 L, out vec3 Fi);
vec3 pa_ms_resolve(vec3 L_sum, vec3 Fi_sum, int n_dirs, out float f_ms_out);
vec3 pa_sky_irradiance_dir(float h_km, float mu_s, ivec2 ij, int n_dirs, int steps);
vec3 pa_sky_radiance(vec3 d, float h_obs_km, int steps);

const int AT_MS_STEPS = 20;                  // MS_STEPS_DEFAULT
const int AT_E_STEPS = 16;
const ivec2 AT_MS_SIZE = ivec2(AT_MS_W, AT_MS_H);
const ivec2 AT_E_SIZE = ivec2(AT_E_W, AT_E_H);
const ivec2 AT_SKY_SIZE = ivec2(AT_SKY_W, AT_SKY_H);

// Psi's texel (i, j): mu_s = i / (w - 1) x 2 - 1, h = max(j / (h - 1), 1e-3) x top (MsLUT::mu_of, h_of)
void at_ms_texel_params(ivec2 t, out float h, out float mu_s)
{
    mu_s = float(t.x) / float(AT_MS_SIZE.x - 1) * 2.0 - 1.0;
    h = max(float(t.y) / float(AT_MS_SIZE.y - 1), 1e-3) * pa_top_height();
}

// E's texel (i, j): mu_s as Psi's, h = top x (j / (h - 1))^2 (SkyIrradianceLUT::h_of)
void at_e_texel_params(ivec2 t, out float h, out float mu_s)
{
    mu_s = float(t.x) / float(AT_E_SIZE.x - 1) * 2.0 - 1.0;
    float v = float(t.y) / float(AT_E_SIZE.y - 1);
    h = pa_top_height() * v * v;
}

// the sky-view texel (i, j)'s direction in the dome's frame (y up, the frame of skyV.glsl's rel_pos): azimuth u = i / (w - 1) over
// [-pi, pi] from +x towards +z, latitude v = j / (h - 1) mapped as Hillaire's, l = sign(v') v'^2 pi / 2, v' = 2 v - 1 (rows crowd at the
// horizon); LLSkyFilter::sky_view_dir is the twin
vec3 at_sky_view_dir(ivec2 t)
{
    vec2 uv = vec2(t) / vec2(AT_SKY_SIZE - ivec2(1));
    float v = uv.y * 2.0 - 1.0;
    float el = sign(v) * v * v * 0.5 * 3.14159265358979;
    float az = (uv.x * 2.0 - 1.0) * 3.14159265358979;
    return vec3(cos(el) * cos(az), sin(el), cos(el) * sin(az));
}

void main()
{
    ivec2 p = ivec2(gl_FragCoord.xy);
#if AT_PASS == 1
    frag_data0 = vec4(pa_trans_texel(p, ivec2(AT_T_W, AT_T_H)), 1.0);
#elif AT_PASS == 2
    float h;
    float mu_s;
    at_ms_texel_params(p / AT_DIRS, h, mu_s);
    vec3 L;
    vec3 Fi;
    pa_ms_dir(h, mu_s, p % AT_DIRS, AT_DIRS, AT_MS_STEPS, L, Fi);
    frag_data0 = vec4(L, 0.0);
    frag_data1 = vec4(Fi, 0.0);
#elif AT_PASS == 3
    ivec2 base = p * AT_DIRS;
    vec3 L_sum = vec3(0.0);
    vec3 Fi_sum = vec3(0.0);
    for (int j = 0; j < AT_DIRS; ++j)
    {
        for (int i = 0; i < AT_DIRS; ++i)
        {
            L_sum += texelFetch(at_part0, base + ivec2(i, j), 0).rgb;
            Fi_sum += texelFetch(at_part1, base + ivec2(i, j), 0).rgb;
        }
    }
    float f_ms;
    frag_data0 = vec4(pa_ms_resolve(L_sum, Fi_sum, AT_DIRS, f_ms), f_ms);
#elif AT_PASS == 4
    float h;
    float mu_s;
    at_e_texel_params(p / AT_DIRS, h, mu_s);
    frag_data0 = vec4(pa_sky_irradiance_dir(h, mu_s, p % AT_DIRS, AT_DIRS, AT_E_STEPS), 0.0);
#elif AT_PASS == 5
    ivec2 base = p * AT_DIRS;
    vec3 sum = vec3(0.0);
    for (int j = 0; j < AT_DIRS; ++j)
    {
        for (int i = 0; i < AT_DIRS; ++i)
        {
            sum += texelFetch(at_part0, base + ivec2(i, j), 0).rgb;
        }
    }
    frag_data0 = vec4(sum * (3.14159265358979 / float(AT_DIRS * AT_DIRS)), 1.0);
#else
    // the dome's frame (x, y up, z) to the atmosphere's (z up): LLEnvironment::toLightNorm's inverse, (x, y, z)_sky = (z, x, y)_dome
    vec3 d = at_sky_view_dir(p);
    frag_data0 = vec4(pa_sky_radiance(vec3(d.z, d.x, d.y), 0.0, at_steps), 1.0);
#endif
}
