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
 * (indra/newview/tests/llatmosphericscatter_test.cpp, which also checks this file's SHA-256 against the one the vectors were made with).
 *
 * Needs physicalAtmosphereF.glsl before it (PAAtmosphere pa_atm, pa_transmittance, pa_density): the extinction. This file adds what
 * a sky needs on top of it, the scattering. Units km, z up, the observer at (0, 0, Rg + h0); linear colours.
 *
 * ALTITUDES. A position at the planet's radius (6360 km) has 0.5 m of float resolution, and the haze slab is 250 m thick, so no
 * function here forms r - Rg: the observer's height h0 is carried, and the altitude of the point at path length t is
 * q / (r + Rg) with q = h0 (2 Rg + h0) + 2 b t + t^2 = |x|^2 - Rg^2 and b = (Rg + h0) d.z; the sphere intersections are quadratics
 * in altitudes solved without cancellation. The transmittance table is indexed by the altitude h and mu.
 *
 * Switches for the test harness: PA_LIGHT_T_EXACT takes the lights' transmittance from pa_transmittance instead of the table;
 * PA_MS_OVERRIDE lets the harness define vec3 pa_ms_at(float h_km, float mu_s) (before this file) instead of the table lookup.
 *
 * pa_march_seg is the one march of the sky view, the froxels, the far table and the vectors; atmosphereAP is the aerial perspective
 * the froxel pass and the cloud trace share: the march is split where the ray leaves the 0.25 km haze slab (a uniform march skips
 * the slab) and is quadratic beyond it. The reference integrals pa_ms_texel and pa_sky_irradiance_texel are the single-pass forms of
 * what AT2-1 splits over several fragments (one per texel x direction, then a reduction).
 */

const float PA_PI = 3.14159265358979;
const float PA_AP_POWER_NEAR = 1.2;          // llatmosphericlight.h AP_POWER_NEAR, AP_POWER_FAR, AP_WEIGHT_FLOOR, AP_DENSE_RATIO, AP_DENSE_TAU
const float PA_AP_POWER_FAR = 1.1;
const float PA_AP_WEIGHT_FLOOR = 0.2;
const float PA_AP_DENSE_RATIO = 2.0;
const float PA_AP_DENSE_TAU = 2.0;

uniform vec3  pa_rayleigh_scat;              // 1/km at density 1 = albedo x rayleigh_ext
uniform float pa_aerosol_scat;
uniform float pa_slab_scat;
uniform vec3  pa_ms_scale;
uniform vec3  pa_glow_phase;                 // glow.x, glow.z, 1 / its integral over the sphere; z = 0: Cornette-Shanks
uniform float pa_aniso;                      // the Mie anisotropy, for Cornette-Shanks
uniform float pa_max_phase;
uniform float pa_f_ms_max;                   // 0.6 (ScatterBounds::max_f_ms)
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

float pa_top_height()
{
    return pa_atm.top_radius_km - pa_atm.planet_radius_km;
}

float pa_horizon_mu(float h_km)
{
    float rg = pa_atm.planet_radius_km;
    float h0 = clamp(h_km, 0.0, pa_top_height());
    return -sqrt(h0 * (2.0 * rg + h0)) / (rg + h0);
}

// the roots t0 <= t1 of t^2 + 2 b t + c = 0 without cancellation; false if there are none
bool pa_quad_roots(float b, float c, out float t0, out float t1)
{
    t0 = 0.0;
    t1 = 0.0;
    float disc = b * b - c;
    if (!(disc >= 0.0))
    {
        return false;
    }
    float s = sqrt(disc);
    float q = -(b + (b >= 0.0 ? s : -s));
    float r2 = q != 0.0 ? c / q : q;
    t0 = min(q, r2);
    t1 = max(q, r2);
    return true;
}

// distance along d from the observer (height h0) to the sphere of altitude `alt`: the first hit ahead, else the far one, -1 if none
float pa_ray_alt(float h0, vec3 d, float alt)
{
    float rg = pa_atm.planet_radius_km;
    float t0;
    float t1;
    if (!pa_quad_roots((rg + h0) * d.z, (h0 - alt) * (2.0 * rg + h0 + alt), t0, t1))
    {
        return -1.0;
    }
    return t0 > 0.0 ? t0 : (t1 > 0.0 ? t1 : -1.0);
}

// the distance to the planet's ground along d, -1 if the ray does not meet it; an observer on the ground looking below its
// horizon is AT the ground (0), not at the far side of the chord through the planet
float pa_ground_distance(float h0, vec3 d)
{
    if (!(h0 > 0.0))
    {
        return d.z < 0.0 ? 0.0 : -1.0;
    }
    return pa_ray_alt(h0, d, 0.0);
}

// the ground, else the top of the atmosphere
float pa_ray_length(float h0, vec3 d)
{
    float tg = pa_ground_distance(h0, d);
    return tg >= 0.0 ? tg : max(pa_ray_alt(h0, d, pa_top_height()), 0.0);
}

// the slab's top: the first crossing ahead in (0, tmax), else -1
float pa_slab_cross(float h0, vec3 d, float tmax)
{
    if (!(pa_atm.slab_top_km > 0.0))
    {
        return -1.0;
    }
    float rg = pa_atm.planet_radius_km;
    float hs = pa_atm.slab_top_km;
    float t0;
    float t1;
    if (!pa_quad_roots((rg + h0) * d.z, (h0 - hs) * (2.0 * rg + h0 + hs), t0, t1))
    {
        return -1.0;
    }
    float t = t0 > 1e-4 ? t0 : t1;
    return (t > 1e-4 && t < tmax) ? t : -1.0;
}

// the point at path length t along d from the observer at height h0: its position (for the directions) and its altitude
float pa_path_point(float h0, vec3 d, float t, out vec3 x)
{
    float rg = pa_atm.planet_radius_km;
    float q0 = h0 * (2.0 * rg + h0);
    float b = (rg + h0) * d.z;
    float q = max(q0 + t * (2.0 * b + t), -rg * rg * 0.999999);
    float r = sqrt(rg * rg + q);
    x = vec3(d.x * t, d.y * t, rg + h0 + d.z * t);
    return q / (r + rg);
}

// (h, mu) -> (u, v): u the distance to the top between its minimum and the horizon's, v = sqrt(rho / H)
vec2 pa_trans_uv(float h, float mu)
{
    float rg = pa_atm.planet_radius_km;
    float rt = pa_atm.top_radius_km;
    float top = rt - rg;
    float hh = sqrt(max(0.0, top * (rt + rg)));
    h = clamp(h, 0.0, top);
    float r = rg + h;
    float rho = sqrt(h * (2.0 * rg + h));
    float disc = (top - h) * (rt + r) + r * r * mu * mu;
    float d = max(0.0, -r * mu + sqrt(max(disc, 0.0)));
    float dmin = top - h;
    float dmax = rho + hh;
    return vec2((d - dmin) / max(dmax - dmin, 1e-6), hh > 0.0 ? sqrt(rho / hh) : 0.0);
}

void pa_trans_rmu(vec2 uv, out float h, out float mu)
{
    float rg = pa_atm.planet_radius_km;
    float rt = pa_atm.top_radius_km;
    float top = rt - rg;
    float h2 = max(0.0, top * (rt + rg));
    float hh = sqrt(h2);
    float rho = hh * uv.y * uv.y;
    float r = sqrt(rho * rho + rg * rg);
    h = rho * rho / (r + rg);
    float dmin = top - h;
    float dmax = rho + hh;
    float d = dmin + uv.x * (dmax - dmin);
    mu = d == 0.0 ? 1.0 : clamp((h2 - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
}

// the table texel (i, j)'s ray: mu at least a hair above the horizon's
void pa_trans_texel_ray(ivec2 ij, ivec2 size, out float h, out float mu)
{
    pa_trans_rmu(vec2(ij) / vec2(size - ivec2(1)), h, mu);
    mu = max(mu, pa_horizon_mu(h) + 1e-6);
}

// what the table pass writes: the physical transmittance (no disc, no excess) at the texel's ray
vec3 pa_trans_texel(ivec2 ij, ivec2 size)
{
    float h;
    float mu;
    pa_trans_texel_ray(ij, size, h, mu);
    return pa_transmittance(h, mu, 0.0, 0.0);
}

// bilinear on texel centres: uv in [0, 1] covers the texel centres, not the texture's edges
vec2 pa_lut_uv(vec2 uv, vec2 size)
{
    return (clamp(uv, 0.0, 1.0) * (size - vec2(1.0)) + vec2(0.5)) / size;
}

vec3 pa_T_lut(float h, float mu)
{
    return texture(pa_trans_lut, pa_lut_uv(pa_trans_uv(h, mu), vec2(textureSize(pa_trans_lut, 0)))).rgb;
}

// transmittance from the point x (at altitude h) to the top of the atmosphere towards light i, 0 behind the planet (soft over the
// disc), the excess factor of the capped columns included
vec3 pa_light_T(vec3 x, float h, PALights lt, int i)
{
    float mu = dot(x, lt.dir[i]) / length(x);
#ifdef PA_LIGHT_T_EXACT
    return pa_transmittance(h, mu, lt.disc[i], 1.0);
#else
    float mu_h = pa_horizon_mu(h);
    float d = lt.disc[i];
    float vis = smoothstep(mu_h - d, mu_h + d, mu);
    if (!(vis > 0.0))
    {
        return vec3(0.0);
    }
    vec3 t = pa_T_lut(h, mu) * vis;
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
// the multiple-scattering table Psi(h, mu_s): x = mu_s, y = h / (Rt - Rg)
vec3 pa_ms_at(float h_km, float mu_s)
{
    vec2 uv = vec2(mu_s * 0.5 + 0.5, clamp(h_km / pa_top_height(), 0.0, 1.0));
    return texture(pa_ms_lut, pa_lut_uv(uv, vec2(textureSize(pa_ms_lut, 0)))).rgb;
}
#endif

// ---------------------------------------------------------------------------------------------------
// the march
// ---------------------------------------------------------------------------------------------------

// the step edge at a in [0, 1]: a^power from the start, or mirrored (1 - (1 - a)^power) when the end is the dense one
float pa_spacing(float a, float power, bool dense_at_end)
{
    float x = dense_at_end ? 1.0 - a : a;
    float y = power == 1.0 ? x : (power == 2.0 ? x * x : pow(x, power));
    return dense_at_end ? 1.0 - y : y;
}

// from the observer's height h0 along d for t in [t0, t1] in n steps, midpoints, step i ending at (i / n)^power of the segment
// (1 uniform, 2 quadratic, dense at its start, or at its end when dense_at_end); accumulates the in-scattered radiance and the
// transmittance of the segment
void pa_march_seg(float h0, vec3 d, float t0, float t1, int n, float power, bool dense_at_end, PALights lt, inout vec3 Lin, inout vec3 T)
{
    float len = t1 - t0;
    if (n <= 0 || !(len > 0.0))
    {
        return;
    }
    int nl = min(max(lt.count, 0), 2);
    float pr[2];
    float pm[2];
    for (int i = 0; i < nl; ++i)
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
        float u = pa_spacing(a, power, dense_at_end);
        float um = pa_spacing(am, power, dense_at_end);
        float dt = (u - prev) * len;
        float t = t0 + um * len;
        prev = u;
        vec3 x;
        float h = pa_path_point(h0, d, t, x);
        vec3 up = x / length(x);
        vec3 s_r;
        float s_m;
        vec3 ext;
        pa_medium(h, s_r, s_m, ext);
        vec3 s_tot = s_r + vec3(s_m);
        vec3 S = vec3(0.0);
        for (int l = 0; l < nl; ++l)
        {
            vec3 single = pa_light_T(x, h, lt, l) * (s_r * pr[l] + vec3(s_m * pm[l]));
            S += lt.E[l] * (single + pa_ms_at(h, dot(up, lt.dir[l])) * s_tot * pa_ms_scale);
        }
        vec3 Ts = exp(-ext * dt);
        Lin += T * (S - S * Ts) / max(ext, vec3(1e-7));
        T *= Ts;
    }
}

float pa_ext_y(float h0, vec3 d, float t)
{
    vec3 x;
    float h = pa_path_point(h0, d, t, x);
    vec3 s_r;
    float s_m;
    vec3 ext;
    pa_medium(h, s_r, s_m, ext);
    return ext.y;
}

// A part [a, b] of a ray: its optical depth (green channel, Simpson's rule on the medium's extinction); dense_at_end: the steps crowd to
// the end b when the air there is more than PA_AP_DENSE_RATIO times as dense as at a and the part is thin (llatmosphericlight.h ray_segment)
float pa_ray_segment(float h0, vec3 d, float a, float b, out bool dense_at_end)
{
    float ea = pa_ext_y(h0, d, a);
    float eb = pa_ext_y(h0, d, b);
    float tau = (ea + 4.0 * pa_ext_y(h0, d, 0.5 * (a + b)) + eb) / 6.0 * (b - a);
    dense_at_end = eb > PA_AP_DENSE_RATIO * ea && tau < PA_AP_DENSE_TAU;
    return tau;
}

// The aerial perspective along d (unit, z up) from an observer at h_obs_km for dist_km: the in-scattered radiance and the
// transmittance (llatmosphericlight.h atmosphere_ap says how the steps are placed and the domain it meets 1 % over).
void atmosphereAP(vec3 d, float dist_km, float h_obs_km, int steps, PALights lt, out vec3 Lin, out vec3 T)
{
    Lin = vec3(0.0);
    T = vec3(1.0);
    float h0 = clamp(h_obs_km, 0.0, pa_top_height());
    dist_km = clamp(dist_km, 0.0, 1e4);
    steps = clamp(steps, 4, 65536);
    float tc = pa_slab_cross(h0, d, dist_km);
    // one segment when the ray never meets the slab's top, else two: the observer to the top, the top to the end. One call site of the
    // march (a shader compiler inlines each, and each inlines pa_transmittance's cut search in the exact build).
    bool dense1 = false;
    bool dense2 = false;
    int n0 = steps;
    if (tc >= 0.0)
    {
        float tau1 = pa_ray_segment(h0, d, 0.0, tc, dense1);
        float tau2 = pa_ray_segment(h0, d, tc, dist_km, dense2);
        float w1 = 1.0 - exp(-tau1);
        float w2 = exp(-tau1) * (1.0 - exp(-tau2));
        n0 = min(max(int(float(steps) * (w1 + PA_AP_WEIGHT_FLOOR) / (w1 + w2 + 2.0 * PA_AP_WEIGHT_FLOOR) + 0.5), 2), steps - 2);
    }
    else
    {
        pa_ray_segment(h0, d, 0.0, dist_km, dense2);
    }
    for (int seg = 0; seg < (tc < 0.0 ? 1 : 2); ++seg)
    {
        float t0 = seg == 0 ? 0.0 : tc;
        float t1 = (seg == 0 && tc >= 0.0) ? tc : dist_km;
        int n = tc < 0.0 ? steps : (seg == 0 ? n0 : steps - n0);
        bool dense_at_end = (seg == 0 && tc >= 0.0) ? dense1 : dense2;
        pa_march_seg(h0, d, t0, t1, n, (seg == 0 && tc >= 0.0) ? PA_AP_POWER_NEAR : PA_AP_POWER_FAR, dense_at_end, lt, Lin, T);
    }
}

void atmosphereAP(vec3 d, float dist_km, float h_obs_km, int steps, out vec3 Lin, out vec3 T)
{
    atmosphereAP(d, dist_km, h_obs_km, steps, pa_lights, Lin, T);
}

// ---------------------------------------------------------------------------------------------------
// the reference integrals the LUT passes are held to (single pass; AT2-1 splits them)
// ---------------------------------------------------------------------------------------------------

// the multiple-scattering texel (h, mu_s) per unit irradiance: L2 / (1 - min(f_ms, pa_f_ms_max)) over n x n uniform directions of
// the sphere, n_steps quadratic steps each, the ground at pa_ground_albedo. f_ms_out: the largest channel of f_ms.
vec3 pa_ms_texel(float h_km, float mu_s, int n_dirs, int n_steps, out float f_ms_out)
{
    float h0 = clamp(h_km, 0.0, pa_top_height());
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s);
    lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0);
    lt.E[1] = vec3(0.0);
    lt.disc[0] = pa_lights.disc[0];
    lt.disc[1] = pa_lights.disc[1];
    lt.count = 1;
    const float pu = 1.0 / (4.0 * PA_PI);
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
            float tg = pa_ground_distance(h0, d);
            float tmax = tg >= 0.0 ? tg : max(pa_ray_alt(h0, d, pa_top_height()), 0.0);
            vec3 T = vec3(1.0);
            vec3 L = vec3(0.0);
            vec3 Fi = vec3(0.0);
            // the steps, then the ground point as one more iteration (one call site of the light's transmittance)
            float prev = 0.0;
            for (int k = 0; k <= n_steps; ++k)
            {
                bool ground = k == n_steps;
                if (ground && !(tg >= 0.0))
                {
                    break;
                }
                float a = float(k + 1) / float(n_steps);
                float am = (float(k) + 0.5) / float(n_steps);
                float dt = (a * a - prev) * tmax;
                prev = a * a;
                vec3 x;
                float h = pa_path_point(h0, d, ground ? tg : am * am * tmax, x);
                h = ground ? 0.0 : h;
                vec3 lT = pa_light_T(x, h, lt, 0);
                if (ground)
                {
                    vec3 n = x / length(x);
                    L += T * lT * (max(dot(n, lt.dir[0]), 0.0) * pa_ground_albedo / PA_PI);
                    break;
                }
                vec3 s_r;
                float s_m;
                vec3 ext;
                pa_medium(h, s_r, s_m, ext);
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
    float h0 = clamp(h_km, 0.0, pa_top_height());
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
            pa_march_seg(h0, d, 0.0, max(pa_ray_alt(h0, d, pa_top_height()), 0.0), steps, 2.0, false, lt, Lin, T);
            sum += Lin;
        }
    }
    return sum * (PA_PI / float(n_dirs * n_dirs));
}

// the sky along d from an observer at h_obs_km: `steps` uniform midpoint steps to the ground or the top (the sky-view LUT's texel),
// plus the ground at pa_ground_albedo where the ray meets it, lit by the lights' T and the sky irradiance (exact texel here)
vec3 pa_sky_radiance(vec3 d, float h_obs_km, int steps)
{
    float h0 = clamp(h_obs_km, 0.0, pa_top_height());
    float tg = pa_ground_distance(h0, d);
    float tmax = pa_ray_length(h0, d);
    vec3 Lin = vec3(0.0);
    vec3 T = vec3(1.0);
    pa_march_seg(h0, d, 0.0, tmax, steps, 1.0, false, pa_lights, Lin, T);
    if (tg >= 0.0)
    {
        vec3 p;
        pa_path_point(h0, d, tg, p);
        vec3 n = p / length(p);
        vec3 E = vec3(0.0);
        for (int l = 0; l < min(max(pa_lights.count, 0), 2); ++l)
        {
            float mu = dot(n, pa_lights.dir[l]);
            E += pa_lights.E[l] * (pa_light_T(p, 0.0, pa_lights, l) * max(mu, 0.0) + pa_sky_irradiance_texel(0.0, mu, 8, 16));
        }
        Lin += T * E * (pa_ground_albedo / PA_PI);
    }
    return Lin;
}
