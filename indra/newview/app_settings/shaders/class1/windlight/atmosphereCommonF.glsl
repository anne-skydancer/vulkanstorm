/**
 * @file class1\windlight\atmosphereCommonF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The scattering core of the physical atmosphere (stage AT2-0 of the item-4 plan): the GLSL twin of LLAtmosphericLight::LLScatter
 * (llatmosphericlight.h). Functions only. It is NOT registered with the shader manager and no viewer shader includes it yet
 * (AT2-1 attaches it to the atmospherics features); it is compiled and run on the GPU by
 * scripts/light/gen_atmosphere_scatter_vectors.py, whose vectors hold the C++ equal to it
 * (indra/newview/tests/llatmosphericscatter_test.cpp).
 *
 * Needs physicalAtmosphereF.glsl before it (PAAtmosphere pa_atm, pa_transmittance, PA_GL_X): the extinction. This file adds what
 * a sky needs on top of it, the scattering. Units km, z up, the observer at (0, 0, Rg + h); linear colours.
 *
 * Switches for the test harness: PA_LIGHT_T_EXACT takes the lights' transmittance from pa_transmittance instead of the table;
 * PA_MS_OVERRIDE lets the harness define vec3 pa_ms_at(float r, float mu_s) (before this file) instead of the table lookup.
 *
 * pa_march_seg is the one march of the sky view, the froxels, the far table and the vectors; atmosphereAP is the aerial perspective
 * the froxel pass and the cloud trace share: the march is split where the ray leaves the 0.25 km haze slab (a uniform march skips
 * the slab) and is quadratic beyond it. The reference integrals pa_ms_texel and pa_sky_irradiance_texel are the single-pass forms of
 * what AT2-1 splits over several fragments (one per texel x direction, then a reduction).
 */

const float PA_PI = 3.14159265358979;
const float PA_AP_SLAB_STEP_SHARE = 0.4;     // llatmosphericlight.h AP_SLAB_STEP_SHARE
const float PA_AP_INSIDE_POWER = 1.25;       // AP_INSIDE_POWER

uniform vec3  pa_rayleigh_scat;              // 1/km at density 1 = albedo x rayleigh_ext
uniform float pa_aerosol_scat;
uniform float pa_slab_scat;
uniform vec3  pa_ms_scale;
uniform vec3  pa_glow_phase;                 // glow.x, glow.z, 1 / its integral over the sphere; z = 0: Cornette-Shanks
uniform float pa_aniso;                      // the Mie anisotropy, for Cornette-Shanks
uniform float pa_max_phase;
uniform float pa_f_ms_max;                   // 0.9
uniform float pa_ground_albedo;              // 0.2
uniform sampler2D pa_trans_lut;              // 256 x 64
uniform sampler2D pa_ms_lut;                 // 32 x 32 (the table's size is read from the texture)

struct PALights
{
    vec3  dir[2];                            // unit, z up: 0 the sun, 1 the moon
    vec3  E[2];                              // irradiance above the atmosphere (linear)
    float disc[2];                           // sin of the angular radius
    int   count;
};
uniform PALights pa_lights;

// ---------------------------------------------------------------------------------------------------
// geometry and the transmittance table
// ---------------------------------------------------------------------------------------------------

float pa_horizon_mu(float h_km)
{
    float rg = pa_atm.planet_radius_km;
    float h0 = clamp(h_km, 0.0, pa_atm.top_radius_km - rg);
    return -sqrt(h0 * (2.0 * rg + h0)) / (rg + h0);
}

// distance along d from o to the sphere of radius R: the first hit ahead, else the far one, -1 if none
float pa_ray_sphere(vec3 o, vec3 d, float R)
{
    float b = dot(o, d);
    float c = dot(o, o) - R * R;
    float disc = b * b - c;
    if (disc < 0.0)
    {
        return -1.0;
    }
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    return t0 > 0.0 ? t0 : (t1 > 0.0 ? t1 : -1.0);
}

// (r, mu) -> (u, v): u the distance to the top between its minimum and the horizon's, v = sqrt(rho / H)
vec2 pa_trans_uv(float r, float mu)
{
    float rg = pa_atm.planet_radius_km;
    float rt = pa_atm.top_radius_km;
    float hh = sqrt(max(0.0, rt * rt - rg * rg));
    float rho = sqrt(max(0.0, r * r - rg * rg));
    float disc = r * r * (mu * mu - 1.0) + rt * rt;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dmin = rt - r;
    float dmax = rho + hh;
    return vec2((d - dmin) / max(dmax - dmin, 1e-6), hh > 0.0 ? sqrt(rho / hh) : 0.0);
}

void pa_trans_rmu(vec2 uv, out float r, out float mu)
{
    float rg = pa_atm.planet_radius_km;
    float rt = pa_atm.top_radius_km;
    float h2 = max(0.0, rt * rt - rg * rg);
    float hh = sqrt(h2);
    float rho = hh * uv.y * uv.y;
    r = sqrt(rho * rho + rg * rg);
    float dmin = rt - r;
    float dmax = rho + hh;
    float d = dmin + uv.x * (dmax - dmin);
    mu = d == 0.0 ? 1.0 : clamp((h2 - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

// the table texel (i, j)'s ray: mu at least a hair above the horizon's
void pa_trans_texel_ray(ivec2 ij, ivec2 size, out float r, out float mu)
{
    pa_trans_rmu(vec2(ij) / vec2(size - ivec2(1)), r, mu);
    mu = max(mu, pa_horizon_mu(r - pa_atm.planet_radius_km) + 1e-6);
}

// what the table pass writes: the physical transmittance (no disc, no excess) at the texel's ray
vec3 pa_trans_texel(ivec2 ij, ivec2 size)
{
    float r;
    float mu;
    pa_trans_texel_ray(ij, size, r, mu);
    return pa_transmittance(r - pa_atm.planet_radius_km, mu, 0.0, 0.0);
}

// bilinear on texel centres: uv in [0, 1] covers the texel centres, not the texture's edges
vec2 pa_lut_uv(vec2 uv, vec2 size)
{
    return (clamp(uv, 0.0, 1.0) * (size - vec2(1.0)) + vec2(0.5)) / size;
}

vec3 pa_T_lut(float r, float mu)
{
    return texture(pa_trans_lut, pa_lut_uv(pa_trans_uv(r, mu), vec2(textureSize(pa_trans_lut, 0)))).rgb;
}

// transmittance from x to the top of the atmosphere towards light i, 0 behind the planet (soft over the disc), the excess factor of
// the capped columns included
vec3 pa_light_T(vec3 x, PALights lt, int i)
{
    float r = length(x);
    float mu = dot(x, lt.dir[i]) / r;
#ifdef PA_LIGHT_T_EXACT
    return pa_transmittance(r - pa_atm.planet_radius_km, mu, lt.disc[i], 1.0);
#else
    float mu_h = pa_horizon_mu(r - pa_atm.planet_radius_km);
    float d = lt.disc[i];
    float vis = smoothstep(mu_h - d, mu_h + d, mu);
    if (!(vis > 0.0))
    {
        return vec3(0.0);
    }
    vec3 t = pa_T_lut(r, mu) * vis;
    if (pa_atm.excess.x > 0.0 || pa_atm.excess.y > 0.0 || pa_atm.excess.z > 0.0)
    {
        t *= exp(-pa_atm.excess / max(min(mu, 1.0) - mu_h, 1e-6));
    }
    return t;
#endif
}

// ---------------------------------------------------------------------------------------------------
// the medium and the phase functions
// ---------------------------------------------------------------------------------------------------

void pa_medium(float h_km, out vec3 s_r, out float s_m, out vec3 ext)
{
    h_km = max(h_km, 0.0);
    float dr = pa_density(pa_atm.rayleigh, h_km);
    float dm = pa_density(pa_atm.mie, h_km);
    float doz = pa_density(pa_atm.absorption, h_km);
    float dh = pa_atm.slab_top_km > 0.0 ? 1.0 - smoothstep(0.75 * pa_atm.slab_top_km, pa_atm.slab_top_km, h_km) : 0.0;
    s_r = pa_rayleigh_scat * dr;
    s_m = pa_aerosol_scat * dm + pa_slab_scat * dh;
    float grey = pa_atm.aerosol_ext * dm + pa_atm.slab_ext * dh;
    ext = pa_atm.rayleigh_ext * dr + vec3(grey) + pa_atm.ozone_ext * doz;
}

float pa_phase_r(float c)
{
    return 3.0 / (16.0 * PA_PI) * (1.0 + c * c);
}

float pa_phase_m(float c)
{
    if (pa_glow_phase.z > 0.0)
    {
        return min((pow(max(1.0 - c, 0.001) * pa_glow_phase.x, pa_glow_phase.y) + 0.25) * pa_glow_phase.z, pa_max_phase);
    }
    float g = pa_aniso;
    float g2 = g * g;
    float p = 3.0 / (8.0 * PA_PI) * ((1.0 - g2) * (1.0 + c * c)) / ((2.0 + g2) * pow(max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
    return min(p, pa_max_phase);
}

#ifndef PA_MS_OVERRIDE
// the multiple-scattering table Psi(r, mu_s): x = mu_s, y = (r - Rg) / (Rt - Rg)
vec3 pa_ms_at(float r, float mu_s)
{
    float rg = pa_atm.planet_radius_km;
    vec2 uv = vec2(mu_s * 0.5 + 0.5, clamp((r - rg) / (pa_atm.top_radius_km - rg), 0.0, 1.0));
    return texture(pa_ms_lut, pa_lut_uv(uv, vec2(textureSize(pa_ms_lut, 0)))).rgb;
}
#endif

// ---------------------------------------------------------------------------------------------------
// the march
// ---------------------------------------------------------------------------------------------------

// o + t d for t in [t0, t1] in n steps, midpoints, step i ending at (i / n)^power of the segment (1 uniform, 2 quadratic from t0);
// accumulates the in-scattered radiance and the transmittance of the segment
void pa_march_seg(vec3 o, vec3 d, float t0, float t1, int n, float power, PALights lt, inout vec3 Lin, inout vec3 T)
{
    float len = t1 - t0;
    if (n <= 0 || !(len > 0.0))
    {
        return;
    }
    float pr[2];
    float pm[2];
    for (int i = 0; i < lt.count; ++i)
    {
        float c = dot(d, lt.dir[i]);
        pr[i] = pa_phase_r(c);
        pm[i] = pa_phase_m(c);
    }
    float prev = 0.0;
    for (int i = 0; i < n; ++i)
    {
        float a = float(i + 1) / float(n);
        float am = (float(i) + 0.5) / float(n);
        float u = power == 1.0 ? a : (power == 2.0 ? a * a : pow(a, power));
        float um = power == 1.0 ? am : (power == 2.0 ? am * am : pow(am, power));
        float dt = (u - prev) * len;
        float t = t0 + um * len;
        prev = u;
        vec3 x = o + d * t;
        float r = length(x);
        vec3 up = x / r;
        vec3 s_r;
        float s_m;
        vec3 ext;
        pa_medium(r - pa_atm.planet_radius_km, s_r, s_m, ext);
        vec3 s_tot = s_r + vec3(s_m);
        vec3 S = vec3(0.0);
        for (int l = 0; l < lt.count; ++l)
        {
            vec3 single = pa_light_T(x, lt, l) * (s_r * pr[l] + vec3(s_m * pm[l]));
            S += lt.E[l] * (single + pa_ms_at(r, dot(up, lt.dir[l])) * s_tot * pa_ms_scale);
        }
        vec3 Ts = exp(-ext * dt);
        Lin += T * (S - S * Ts) / max(ext, vec3(1e-7));
        T *= Ts;
    }
}

// distance along d from o to the slab's top (the sphere Rg + slab_top_km), the first crossing ahead in (0, tmax), else -1
float pa_slab_cross(vec3 o, vec3 d, float tmax)
{
    float R = pa_atm.planet_radius_km + pa_atm.slab_top_km;
    float b = dot(o, d);
    float c = dot(o, o) - R * R;
    float disc = b * b - c;
    if (disc < 0.0)
    {
        return -1.0;
    }
    float s = sqrt(disc);
    float t0 = -b - s;
    float t1 = -b + s;
    float t = t0 > 1e-4 ? t0 : t1;
    return (t > 1e-4 && t < tmax) ? t : -1.0;
}

// The aerial perspective along d (unit, z up) from an observer at h_obs_km for dist_km: the in-scattered radiance and the
// transmittance. 12 steps meet 1 % of a converged march (the C++ test, R-ACC).
void atmosphereAP(vec3 d, float dist_km, float h_obs_km, int steps, PALights lt, out vec3 Lin, out vec3 T)
{
    Lin = vec3(0.0);
    T = vec3(1.0);
    vec3 o = vec3(0.0, 0.0, pa_atm.planet_radius_km + max(h_obs_km, 0.0));
    float tc = pa_slab_cross(o, d, dist_km);
    // one segment when the ray never meets the slab's top, else two: the observer to the top, the top to the end. One call site of the
    // march (a shader compiler inlines each, and each inlines pa_transmittance's cut search in the exact build).
    int n0 = min(max(int(float(steps) * PA_AP_SLAB_STEP_SHARE + 0.5), 2), max(steps - 2, 2));
    bool inside = (length(o) - pa_atm.planet_radius_km) < pa_atm.slab_top_km;
    for (int seg = 0; seg < (tc < 0.0 ? 1 : 2); ++seg)
    {
        float t0 = seg == 0 ? 0.0 : tc;
        float t1 = (seg == 0 && tc >= 0.0) ? tc : dist_km;
        int n = tc < 0.0 ? steps : (seg == 0 ? n0 : steps - n0);
        float power = (seg == 0 && tc >= 0.0 && inside) ? PA_AP_INSIDE_POWER : 2.0;
        pa_march_seg(o, d, t0, t1, n, power, lt, Lin, T);
    }
}

void atmosphereAP(vec3 d, float dist_km, float h_obs_km, int steps, out vec3 Lin, out vec3 T)
{
    atmosphereAP(d, dist_km, h_obs_km, steps, pa_lights, Lin, T);
}

// ---------------------------------------------------------------------------------------------------
// the reference integrals the LUT passes are held to (single pass; AT2-1 splits them)
// ---------------------------------------------------------------------------------------------------

// the multiple-scattering texel (r, mu_s) per unit irradiance: L2 / (1 - min(f_ms, pa_f_ms_max)) over n x n uniform directions of
// the sphere, 20 uniform steps each, the ground at pa_ground_albedo. f_ms_out: the largest channel of f_ms.
vec3 pa_ms_texel(float r, float mu_s, int n_dirs, int n_steps, out float f_ms_out)
{
    vec3 x0 = vec3(0.0, 0.0, r);
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s);
    lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0);
    lt.E[1] = vec3(0.0);
    lt.disc[0] = pa_lights.disc[0];
    lt.disc[1] = pa_lights.disc[1];
    lt.count = 1;
    const float pu = 1.0 / (4.0 * PA_PI);
    float rg = pa_atm.planet_radius_km;
    vec3 L2 = vec3(0.0);
    vec3 fms = vec3(0.0);
    for (int i = 0; i < n_dirs; ++i)
    {
        for (int j = 0; j < n_dirs; ++j)
        {
            float ct = 1.0 - 2.0 * (float(j) + 0.5) / float(n_dirs);
            float st = sqrt(1.0 - ct * ct);
            float ph = 2.0 * PA_PI * (float(i) + 0.5) / float(n_dirs);
            vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
            float tg = pa_ray_sphere(x0, d, rg);
            float tmax = tg > 0.0 ? tg : max(pa_ray_sphere(x0, d, pa_atm.top_radius_km), 0.0);
            float dt = tmax / float(n_steps);
            vec3 T = vec3(1.0);
            vec3 L = vec3(0.0);
            vec3 Fi = vec3(0.0);
            // the steps, then the ground point as one more iteration (one call site of the light's transmittance)
            for (int k = 0; k <= n_steps; ++k)
            {
                bool ground = k == n_steps;
                if (ground && !(tg > 0.0))
                {
                    break;
                }
                vec3 x = ground ? x0 + d * tg : x0 + d * ((float(k) + 0.5) * dt);
                vec3 lT = pa_light_T(x, lt, 0);
                if (ground)
                {
                    vec3 n = x / length(x);
                    L += T * lT * (max(dot(n, lt.dir[0]), 0.0) * pa_ground_albedo / PA_PI);
                    break;
                }
                vec3 s_r;
                float s_m;
                vec3 ext;
                pa_medium(length(x) - rg, s_r, s_m, ext);
                vec3 s = s_r + vec3(s_m);
                vec3 Ts = exp(-ext * dt);
                vec3 w = (vec3(1.0) - Ts) / max(ext, vec3(1e-7));
                L += T * s * lT * w * pu;
                Fi += T * s * w;
                T *= Ts;
            }
            L2 += L;
            fms += Fi * pu;
        }
    }
    float inv = 1.0 / float(n_dirs * n_dirs);
    L2 *= inv;
    fms *= 4.0 * PA_PI * inv;
    f_ms_out = max(fms.x, max(fms.y, fms.z));
    return L2 / (vec3(1.0) - min(fms, vec3(pa_f_ms_max)));
}

// the sky irradiance texel E(h, mu_s) per unit irradiance of the sun on a horizontal surface at altitude h_km: pi x the mean radiance
// over n x n cosine-weighted directions of the upper hemisphere, `steps` quadratic steps to the top
vec3 pa_sky_irradiance_texel(float h_km, float mu_s, int n_dirs, int steps)
{
    vec3 x0 = vec3(0.0, 0.0, pa_atm.planet_radius_km + max(h_km, 0.0));
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s);
    lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0);
    lt.E[1] = vec3(0.0);
    lt.disc[0] = pa_lights.disc[0];
    lt.disc[1] = pa_lights.disc[1];
    lt.count = 1;
    vec3 sum = vec3(0.0);
    for (int i = 0; i < n_dirs; ++i)
    {
        for (int j = 0; j < n_dirs; ++j)
        {
            float ct = sqrt(1.0 - (float(j) + 0.5) / float(n_dirs));
            float st = sqrt((float(j) + 0.5) / float(n_dirs));
            float ph = 2.0 * PA_PI * (float(i) + 0.5) / float(n_dirs);
            vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
            vec3 Lin = vec3(0.0);
            vec3 T = vec3(1.0);
            pa_march_seg(x0, d, 0.0, max(pa_ray_sphere(x0, d, pa_atm.top_radius_km), 0.0), steps, 2.0, lt, Lin, T);
            sum += Lin;
        }
    }
    return sum * (PA_PI / float(n_dirs * n_dirs));
}

// the sky along d from an observer at h_obs_km: `steps` uniform midpoint steps to the ground or the top (the sky-view LUT's texel),
// plus the ground at pa_ground_albedo where the ray meets it, lit by the lights' T and the sky irradiance (exact texel here)
vec3 pa_sky_radiance(vec3 d, float h_obs_km, int steps)
{
    float rg = pa_atm.planet_radius_km;
    vec3 o = vec3(0.0, 0.0, rg + max(h_obs_km, 0.0));
    float tg = pa_ray_sphere(o, d, rg);
    float tmax = tg > 0.0 ? tg : max(pa_ray_sphere(o, d, pa_atm.top_radius_km), 0.0);
    vec3 Lin = vec3(0.0);
    vec3 T = vec3(1.0);
    pa_march_seg(o, d, 0.0, tmax, steps, 1.0, pa_lights, Lin, T);
    if (tg > 0.0)
    {
        vec3 p = o + d * tg;
        vec3 n = p / length(p);
        vec3 E = vec3(0.0);
        for (int l = 0; l < pa_lights.count; ++l)
        {
            float mu = dot(n, pa_lights.dir[l]);
            E += pa_lights.E[l] * (pa_light_T(p, pa_lights, l) * max(mu, 0.0) + pa_sky_irradiance_texel(0.0, mu, 8, 16));
        }
        Lin += T * E * (pa_ground_albedo / PA_PI);
    }
    return Lin;
}
