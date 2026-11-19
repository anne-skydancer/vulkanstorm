/**
 * @file class1\windlight\atmosphereCommonF.glsl
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
 *
 * The physical atmosphere: the GLSL twin of llatmosphericlight.h's LLAtmosphere (the extinction and the transmittance, stage AT0)
 * and LLScatter (the scattering core, stage AT2-0). Functions only, one compilation unit: stage AT2-1 attaches it to the table passes
 * (atmosphereTablesF.glsl; LLAtmosphereLUTs, llatmosphereluts.cpp), and everything it reads comes from the `Atmosphere` uniform block
 * (std140; its C++ mirror is LLAtmosphericLight::AtmosphereBlock in llatmospherebase.h, filled once per frame with one
 * glBufferSubData and bound at LLGLSLShader::UB_ATMOSPHERE). The block replaced the pa_* uniforms of AT0 and AT2-0, which would have
 * been re-applied to every program on its first bind of every frame (doc/light/atmosphere-study.md, R10). The extinction and the
 * scattering are in one file because the viewer compiles each file as its own unit and the block, and the structs it holds, can be
 * declared once only.
 *
 * Held equal to the C++ by the GPU vectors of scripts/light/gen_transmittance_vectors.py (pa_transmittance) and
 * scripts/light/gen_atmosphere_scatter_vectors.py (the rest), and the table passes by scripts/light/check_atmosphere_tables.py;
 * indra/newview/tests/llatmosphericscatter_test.cpp checks this file's SHA-256 against the one the vectors were made with.
 *
 * pa_transmittance(h_km, mu, disc_sin, path_factor): the transmittance, per channel, from altitude h_km above the ground towards a
 * body at zenith cosine mu, to the top of the atmosphere (the ground does not occlude), times the share of the body's disc above the
 * horizon, times the excess factor exp(-pa_atm.excess x path_factor / max(mu - mu_h, 1e-6)) of the columns that were capped (a light
 * rig asking for almost no sun; 0 for every other sky). Segmented 8-point Gauss-Legendre along the path, cut at the altitudes the C++
 * computes once per atmosphere (LLAtmosphere::finalize: every profile kink, the clamp crossings, the slab's 0.75 H and H, 4, 10 and 20
 * scale heights of every exponential layer, the ground), uploaded in pa_cuts, plus the perigee of a downward ray and its ends.
 *
 * ALTITUDES. A position at the planet's radius (6360 km) has 0.5 m of float resolution, and the haze slab is 250 m thick, so no
 * function here forms r - Rg: the observer's height h0 is carried, and the altitude of the point at path length t is
 * q / (r + Rg) with q = h0 (2 Rg + h0) + 2 b t + t^2 = |x|^2 - Rg^2 and b = (Rg + h0) d.z; the sphere intersections are quadratics
 * in altitudes solved without cancellation. The transmittance table is indexed by the altitude h and mu. Units km, z up, the observer
 * at (0, 0, Rg + h0); linear colours.
 *
 * Switches for the test harnesses: PA_LIGHT_T_EXACT takes the lights' transmittance from pa_transmittance instead of the table;
 * PA_E_EXACT takes the ground's sky irradiance in pa_sky_radiance from the exact texel instead of the table; PA_MS_OVERRIDE lets the
 * harness define vec3 pa_ms_at(float h_km, float mu_s) (after this file) instead of the table lookup.
 *
 * pa_march_seg is the one march of the sky view, the froxels, the far table and the vectors; atmosphereAP is the aerial perspective
 * the sky view, the froxel pass and the cloud trace share: the march is split where the ray leaves the 0.25 km haze slab (a uniform
 * march skips the slab). pa_ms_dir and pa_sky_irradiance_dir are one direction of the multiple-scattering and sky irradiance texels:
 * the split passes run one fragment each and a reduction sums them; pa_ms_texel and pa_sky_irradiance_texel are the single-pass sums.
 */

const float PA_PI = 3.14159265358979;
const float PA_AP_POWER_NEAR = 1.2;          // llatmosphericlight.h AP_POWER_NEAR, AP_POWER_FAR, AP_WEIGHT_FLOOR, AP_DENSE_RATIO, AP_DENSE_TAU
const float PA_AP_POWER_FAR = 1.1;
const float PA_AP_WEIGHT_FLOOR = 0.2;
const float PA_AP_DENSE_RATIO = 2.0;
const float PA_AP_DENSE_TAU = 2.0;

// A density profile (Bruneton's form; EEP's): clamp(exp_term e^(exp_scale h) + linear h + constant, 0, 1), layer 0 below its width.
struct PADensityProfile
{
    vec4 layer0;                             // width (m), exp_term, exp_scale (1/m), linear (1/m)
    vec4 layer1;                             // the same for layer 1 (its width is not used)
    vec4 constants;                          // x layer 0's constant, y layer 1's, z the number of layers (1 or 2)
};

struct PAAtmosphere
{
    vec3  rayleigh_ext;                      // 1/km at density 1
    float aerosol_ext;
    vec3  ozone_ext;
    float slab_ext;                          // ground haze slab, 1/km
    vec3  excess;                            // what the capped columns lack (tau per channel); 0 unless a light rig asks for almost no sun
    float slab_top_km;
    float planet_radius_km;
    float top_radius_km;
    float cut_count;                         // the number of altitudes in pa_cuts
    float unused0;
    PADensityProfile rayleigh;
    PADensityProfile mie;
    PADensityProfile absorption;
};

struct PALights
{
    vec3  dir[2];                            // unit, z up: 0 the sun, 1 the moon
    vec3  E[2];                              // irradiance above the atmosphere (linear)
    float disc[2];                           // sin of the angular radius
    int   count;
};

#define PA_MAX_ALT 32                        // llatmosphericlight.h detail::MAX_ALTITUDES
#define PA_MAX_CUTS 67                       // 2 roots per altitude, the perigee and the two ends

layout(std140) uniform Atmosphere
{
    PAAtmosphere pa_atm;
    PALights     pa_lights;
    vec3  pa_rayleigh_scat;                  // 1/km at density 1 = albedo x rayleigh_ext
    float pa_aerosol_scat;
    vec3  pa_ms_scale;
    float pa_slab_scat;
    vec3  pa_glow_phase;                     // glow.x, glow.z, 1 / its integral over the sphere; z = 0: Cornette-Shanks
    float pa_aniso;                          // the Mie anisotropy, for Cornette-Shanks
    float pa_max_phase;
    float pa_f_ms_max;                       // 0.6 (ScatterBounds::max_f_ms)
    float pa_ground_albedo;
    float pa_camera_km;                      // the camera's height above the sea, km (the aerial perspective's observer, AT2-3)
    vec4  pa_cuts[PA_MAX_ALT / 4];           // the cut altitudes of pa_transmittance (km), LLAtmosphere::finalize
};

uniform sampler2D pa_trans_lut;              // 256 x 64
uniform sampler2D pa_ms_lut;                 // 32 x 32 (the table's size is read from the texture)
uniform sampler2D pa_e_lut;                  // 64 x 16

vec3 pa_ms_at(float h_km, float mu_s);

// ---------------------------------------------------------------------------------------------------
// the extinction: density profiles and the transmittance (LLAtmosphere)
// ---------------------------------------------------------------------------------------------------

// a layer's expression before the clamp (h in metres): layer = (width, exp_term, exp_scale, linear), c its constant
float pa_layer_value(vec4 layer, float c, float h)
{
    // no exp when exp_term is 0 (0 * inf is NaN); the exponent is bounded so e^x stays finite
    float e = layer.y != 0.0 ? layer.y * exp(clamp(layer.z * h, -80.0, 80.0)) : 0.0;
    return e + layer.w * h + c;
}

// clamp(exp_term e^(exp_scale h) + linear h + constant, 0, 1); layer 0 below its width
float pa_density(PADensityProfile p, float h_km)
{
    float h = h_km * 1000.0;
    bool upper = p.constants.z > 1.5 && h >= p.layer0.x;
    return clamp(upper ? pa_layer_value(p.layer1, p.constants.y, h) : pa_layer_value(p.layer0, p.constants.x, h), 0.0, 1.0);
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

vec3 pa_transmittance(float h_km, float mu, float disc_sin, float path_factor)
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
    // plane-parallel on the excess, in the angle above the observer's horizon: 0 at and below it, continuously; 1 without excess
    float pf = (isnan(path_factor) || isinf(path_factor)) ? 1.0 : clamp(path_factor, 0.0, 1e6);
    vec3 ex = exp(-pa_atm.excess * (pf / max(mu - mu_h, 1e-6)));

    float b = r0 * mu;
    float unused;
    float path;
    pa_path_roots(b, h0, rg, top, unused, path);
    if (!(path > 0.0))
    {
        return vec3(vis) * ex;
    }

    float cut[PA_MAX_CUTS];
    int n = 0;
    cut[n++] = 0.0;
    cut[n++] = path;
    int na = clamp(int(pa_atm.cut_count + 0.5), 0, PA_MAX_ALT);
    for (int i = 0; i < na; ++i)
    {
        float s_lo;
        float s_hi;
        pa_path_roots(b, h0, rg, pa_cuts[i / 4][i % 4], s_lo, s_hi);
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

    const float GL_X[8] = float[8](-0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                                    0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363);
    const float GL_W[8] = float[8](0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                                   0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763);
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
            float s = mid + hw * GL_X[k];
            float r = sqrt(max(r0 * r0 + 2.0 * b * s + s * s, 0.0));
            float h = max((q0 + 2.0 * b * s + s * s) / (r + rg), 0.0);  // altitude, without the cancellation
            float dr = pa_density(pa_atm.rayleigh, h);
            float dm = pa_density(pa_atm.mie, h);
            float dz = pa_density(pa_atm.absorption, h);
            float ds = pa_atm.slab_top_km > 0.0 ? 1.0 - smoothstep(slab_lo, pa_atm.slab_top_km, h) : 0.0;
            vec3 grey = vec3(pa_atm.aerosol_ext * dm + pa_atm.slab_ext * ds);
            tau += hw * GL_W[k] * (pa_atm.rayleigh_ext * dr + grey + pa_atm.ozone_ext * dz);
        }
    }
    return exp(-tau) * vis * ex;
}

vec3 pa_transmittance(float h_km, float mu, float disc_sin)
{
    return pa_transmittance(h_km, mu, disc_sin, 1.0);
}

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

// the sky irradiance table E(h, mu_s) per unit irradiance of the sun: x = mu_s, y = sqrt(h / (Rt - Rg))
vec3 pa_E_lut(float h_km, float mu_s)
{
    vec2 uv = vec2(mu_s * 0.5 + 0.5, sqrt(clamp(h_km / pa_top_height(), 0.0, 1.0)));
    return texture(pa_e_lut, pa_lut_uv(uv, vec2(textureSize(pa_e_lut, 0)))).rgb;
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
    // march (a shader compiler inlines each, and each inlines the light's transmittance).
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
// the reference integrals the table passes are held to: one direction each (the split passes), and their sums
// ---------------------------------------------------------------------------------------------------

// a light of unit irradiance at zenith cosine mu_s (the table texels are per unit irradiance)
PALights pa_unit_light(float mu_s)
{
    PALights lt;
    lt.dir[0] = vec3(sqrt(max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s);
    lt.dir[1] = vec3(0.0);
    lt.E[0] = vec3(1.0);
    lt.E[1] = vec3(0.0);
    lt.disc[0] = pa_lights.disc[0];
    lt.disc[1] = pa_lights.disc[1];
    lt.count = 1;
    return lt;
}

// One direction (i, j) of the n x n uniform directions of the sphere of the multiple-scattering texel (h, mu_s): its second-order
// radiance L (per unit irradiance, x 1/(4 pi)) and its transfer Fi (x 1/(4 pi)); n_steps quadratic steps, the ground at
// pa_ground_albedo where the ray meets it (LLScatter::ms_texel's inner loop).
void pa_ms_dir(float h_km, float mu_s, ivec2 ij, int n_dirs, int n_steps, out vec3 L, out vec3 Fi)
{
    float h0 = clamp(h_km, 0.0, pa_top_height());
    mu_s = clamp(mu_s, -1.0, 1.0);
    PALights lt = pa_unit_light(mu_s);
    const float pu = 1.0 / (4.0 * PA_PI);
    float ct = 1.0 - 2.0 * (float(ij.y) + 0.5) / float(n_dirs);
    float st = sqrt(1.0 - ct * ct);
    float ph = 2.0 * PA_PI * (float(ij.x) + 0.5) / float(n_dirs);
    vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
    float tg = pa_ground_distance(h0, d);
    float tmax = tg >= 0.0 ? tg : max(pa_ray_alt(h0, d, pa_top_height()), 0.0);
    vec3 T = vec3(1.0);
    L = vec3(0.0);
    Fi = vec3(0.0);
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
    Fi *= pu;
}

// the sum of the n x n directions: L2 / (1 - min(f_ms, pa_f_ms_max)), f_ms = 4 pi x the mean transfer (the reduction pass does the same)
vec3 pa_ms_resolve(vec3 L_sum, vec3 Fi_sum, int n_dirs, out float f_ms_out)
{
    float inv = 1.0 / float(n_dirs * n_dirs);
    vec3 L2 = L_sum * inv;
    vec3 fms = Fi_sum * (4.0 * PA_PI * inv);
    f_ms_out = max(fms.x, max(fms.y, fms.z));
    return L2 / (vec3(1.0) - min(fms, vec3(pa_f_ms_max)));
}

// the multiple-scattering texel (h, mu_s) per unit irradiance in one pass (the reference of the split pass and its reduction)
vec3 pa_ms_texel(float h_km, float mu_s, int n_dirs, int n_steps, out float f_ms_out)
{
    vec3 L_sum = vec3(0.0);
    vec3 Fi_sum = vec3(0.0);
    for (int i = 0; i < n_dirs; ++i)
    {
        for (int j = 0; j < n_dirs; ++j)
        {
            vec3 L;
            vec3 Fi;
            pa_ms_dir(h_km, mu_s, ivec2(i, j), n_dirs, n_steps, L, Fi);
            L_sum += L;
            Fi_sum += Fi;
        }
    }
    return pa_ms_resolve(L_sum, Fi_sum, n_dirs, f_ms_out);
}

// One direction (i, j) of the n x n cosine-weighted directions of the upper hemisphere of the sky irradiance texel (h, mu_s): the
// in-scattered radiance along it per unit irradiance of the sun, `steps` quadratic steps to the top of the atmosphere
vec3 pa_sky_irradiance_dir(float h_km, float mu_s, ivec2 ij, int n_dirs, int steps)
{
    float h0 = clamp(h_km, 0.0, pa_top_height());
    mu_s = clamp(mu_s, -1.0, 1.0);
    PALights lt = pa_unit_light(mu_s);
    float ct = sqrt(1.0 - (float(ij.y) + 0.5) / float(n_dirs));
    float st = sqrt((float(ij.y) + 0.5) / float(n_dirs));
    float ph = 2.0 * PA_PI * (float(ij.x) + 0.5) / float(n_dirs);
    vec3 d = vec3(st * cos(ph), st * sin(ph), ct);
    vec3 Lin = vec3(0.0);
    vec3 T = vec3(1.0);
    pa_march_seg(h0, d, 0.0, max(pa_ray_alt(h0, d, pa_top_height()), 0.0), steps, 2.0, false, lt, Lin, T);
    return Lin;
}

// the sky irradiance texel E(h, mu_s) per unit irradiance of the sun on a horizontal surface at altitude h_km: pi x the mean radiance
// over the n x n directions (the reference of the split pass and its reduction)
vec3 pa_sky_irradiance_texel(float h_km, float mu_s, int n_dirs, int steps)
{
    vec3 sum = vec3(0.0);
    for (int i = 0; i < n_dirs; ++i)
    {
        for (int j = 0; j < n_dirs; ++j)
        {
            sum += pa_sky_irradiance_dir(h_km, mu_s, ivec2(i, j), n_dirs, steps);
        }
    }
    return sum * (PA_PI / float(n_dirs * n_dirs));
}

// The sky along d from an observer at h_obs_km (the sky-view table's texel): the aerial perspective to the end of the ray, the ground
// or the top (the slab-split march, decision D1 of doc/light/atmosphere-study.md: a uniform march skips the 0.25 km haze slab), in
// `steps` steps, plus the ground at pa_ground_albedo where the ray meets it, lit by the lights' T and the sky irradiance of the
// E table (PA_E_EXACT: the exact texel, 8 x 8 directions and 16 steps)
vec3 pa_sky_radiance(vec3 d, float h_obs_km, int steps)
{
    float h0 = clamp(h_obs_km, 0.0, pa_top_height());
    float tg = pa_ground_distance(h0, d);
    float tmax = pa_ray_length(h0, d);
    vec3 Lin;
    vec3 T;
    atmosphereAP(d, tmax, h0, steps, pa_lights, Lin, T);
    if (tg >= 0.0)
    {
        vec3 p;
        pa_path_point(h0, d, tg, p);
        vec3 n = p / length(p);
        vec3 E = vec3(0.0);
        for (int l = 0; l < min(max(pa_lights.count, 0), 2); ++l)
        {
            float mu = dot(n, pa_lights.dir[l]);
#ifdef PA_E_EXACT
            vec3 sky_e = pa_sky_irradiance_texel(0.0, mu, 8, 16);
#else
            vec3 sky_e = pa_E_lut(0.0, mu);
#endif
            E += pa_lights.E[l] * (pa_light_T(p, 0.0, pa_lights, l) * max(mu, 0.0) + sky_e);
        }
        Lin += T * E * (pa_ground_albedo / PA_PI);
    }
    return Lin;
}
