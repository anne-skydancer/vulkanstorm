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
 *
 * Stage AT0 of the item-4 plan adds the physical atmosphere (LLAtmosphere: the legacy EEP sky
 * fields mapped onto Rayleigh, aerosol and ozone densities, and the transmittance of the sun's and
 * the moon's light through them) and the stateless sun/moon handover helpers (primary_light and its
 * parts). Stage AT1/L3 puts them to use: LLEnvironment::updatePrimaryLight calls compute_primary_light()
 * once per frame, and the ground light (sunlit) is that result, not the GLSL's old sun-or-moon pick and
 * airmass. class1/windlight/
 * physicalAtmosphereF.glsl is the GLSL twin of LLAtmosphere::transmittance (the physical part
 * and the excess factor of the capped columns), held equal by the vectors of
 * scripts/light/gen_transmittance_vectors.py.
 *
 * Stage AT2-0 (the end of this file) adds the scattering core on top of LLAtmosphere's extinction: LLScatter (albedos, phase
 * functions, multiple-scattering scale, the lights' irradiance, all bounded), the transmittance / multiple-scattering / sky
 * irradiance tables' parameterisations and texels, and the march (atmosphere_ap) the aerial perspective and the cloud trace share.
 * class1/windlight/atmosphereCommonF.glsl is its GLSL twin, held equal by scripts/light/gen_atmosphere_scatter_vectors.py;
 * indra/newview/tests/llatmosphericscatter_test.cpp is the test; doc/light/atmosphere-scattering-core.md the description.
 *
 * Ozone: the EEP default absorption layer (llsettingssky.cpp absorptionConfigDefault) has a rising
 * zone of slope -1/25000, which is 0 everywhere, and the sky validator clamps layer slopes and
 * constants in place (llsettingssky.cpp:211-232, llsettingsbase.cpp verifyFloatRange), so the
 * skies in use hold a flat layer. No stored default changes: fromLegacy() recognises these
 * forms (is_stored_ozone_default) and uses Bruneton's ozone_profile() for them. The ozone
 * coefficients are Bruneton & Neyret's (Precomputed Atmospheric Scattering, 2008).
 */

#ifndef LL_LLATMOSPHERICLIGHT_H
#define LL_LLATMOSPHERICLIGHT_H

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

// the precondition check of LLScatter: the viewer's llassert when it is there, else assert
#if defined(llassert)
#define LL_ATMOS_REQUIRE(cond) llassert(cond)
#else
#include <cassert>
#define LL_ATMOS_REQUIRE(cond) assert(cond)
#endif

namespace LLAtmosphericLight
{
struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
};

// The uniforms atmosphericsFuncs.glsl reads for these two terms. Colours are the viewer's
// uniform values. Stage L3: the ground light is `primary_light_color` (sunlight_color's encoding), the
// sun's or the moon's colour with its path through the atmosphere already taken
// (LLEnvironment::updatePrimaryLight); the GLSL no longer picks the body or applies an airmass.
struct Inputs
{
    Vec3 primary_light_color;
    Vec3 ambient_color;
    double cloud_shadow = 0.0;
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
    const Vec3& light = in.primary_light_color;
    const double lc[3] = { light.x, light.y, light.z };
    const double amb[3] = { in.ambient_color.x, in.ambient_color.y, in.ambient_color.z };
    double s[3], a[3];
    for (int i = 0; i < 3; ++i)
    {
        s[i] = lc[i];
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

// ---------------------------------------------------------------------------------------------------
// Stage AT0: the physical atmosphere and the sun/moon handover (stage AT1/L3 below puts them to use)
// ---------------------------------------------------------------------------------------------------

// --- colour helpers ---
inline Vec3 srgb_to_linear(const Vec3& c)
{
    return { srgb_to_linear(c.x), srgb_to_linear(c.y), srgb_to_linear(c.z) };
}

// srgb_to_linear's inverse without linear_to_srgb's clamp to 1 (as skyF.glsl's night_sky_encode):
// values above 1 are kept, negative values count as 0.
inline double linear_to_srgb(double c)
{
    c = std::max(c, 0.0);
    return c < 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
inline Vec3 linear_to_srgb(const Vec3& c)
{
    return { linear_to_srgb(c.x), linear_to_srgb(c.y), linear_to_srgb(c.z) };
}

inline double luminance(const Vec3& c) // Rec. 709 weights on linear values
{
    return c.x * 0.2126 + c.y * 0.7152 + c.z * 0.0722;
}

// GLSL's smoothstep; the edges may be given in either order. Equal edges are a step.
inline double smoothstep(double edge0, double edge1, double x)
{
    if (edge0 == edge1)
    {
        return x < edge0 ? 0.0 : 1.0;
    }
    const double t = std::min(std::max((x - edge0) / (edge1 - edge0), 0.0), 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// --- EEP density profiles (Bruneton's form; metres) ---
struct DensityLayer
{
    double width_m = 0.0;       // 0: the rest of the atmosphere
    double exp_term = 0.0;
    double exp_scale = 0.0;     // 1/m
    double linear = 0.0;        // 1/m
    double constant = 0.0;
};

struct DensityProfile
{
    DensityLayer layer[2];
    int count = 1;

    // clamp(exp_term e^(exp_scale h) + linear h + constant, 0, 1); layer[0] below its width
    double density(double h_m) const
    {
        const DensityLayer& l = (count > 1 && h_m < layer[0].width_m) ? layer[0] : layer[count > 1 ? 1 : 0];
        const double d = layer_value(l, h_m);
        return !(d >= 0.0) ? 0.0 : (d > 1.0 ? 1.0 : d); // NaN gives 0
    }

    // a layer's expression before the clamp
    static double layer_value(const DensityLayer& l, double h_m)
    {
        // no exp when exp_term is 0 (0 * inf is NaN); the exponent is bounded so e^x stays finite
        const double e = l.exp_term != 0.0 ? l.exp_term * std::exp(std::min(std::max(l.exp_scale * h_m, -80.0), 80.0)) : 0.0;
        return e + l.linear * h_m + l.constant;
    }

    // The h in (lo, hi) (metres) where a layer's unclamped expression crosses density 0 or 1, for a layer with an
    // exponential term. f = exp_term e^(exp_scale h) + linear h + constant has f'' of the sign of exp_term, so at most one
    // stationary point (linear = -exp_term exp_scale e^(exp_scale h)); split there, each part is monotone, so a sign change
    // of f - level across it holds exactly one crossing, found by 32 bisections (physicalAtmosphereF.glsl does the same).
    static int layer_crossings(const DensityLayer& l, double lo, double hi, double* out, int max_count)
    {
        double br[3];
        int nb = 0;
        br[nb++] = lo;
        if (l.exp_scale != 0.0 && l.linear != 0.0)
        {
            const double q = -l.linear / (l.exp_term * l.exp_scale);
            if (q > 0.0 && std::isfinite(q))
            {
                const double x = std::log(q) / l.exp_scale;
                if (x > lo && x < hi)
                {
                    br[nb++] = x;
                }
            }
        }
        br[nb++] = hi;
        int n = 0;
        for (int j = 0; j + 1 < nb; ++j)
        {
            for (double level : { 0.0, 1.0 })
            {
                double a = br[j], b = br[j + 1];
                double ga = layer_value(l, a) - level;
                const double gb = layer_value(l, b) - level;
                if (!((ga < 0.0 && gb > 0.0) || (ga > 0.0 && gb < 0.0)))
                {
                    continue;
                }
                for (int it = 0; it < 32; ++it)
                {
                    const double m = 0.5 * (a + b);
                    const double gm = layer_value(l, m) - level;
                    if ((gm < 0.0) == (ga < 0.0))
                    {
                        a = m;
                        ga = gm;
                    }
                    else
                    {
                        b = m;
                    }
                }
                if (n < max_count)
                {
                    out[n++] = 0.5 * (a + b);
                }
            }
        }
        return n;
    }

    // The altitudes (metres, in no particular order) where density() is not smooth: the layer boundary and the clamps
    // (h where the unclamped expression is 0 or 1, inside the layer and below top_m). A linear layer has them in closed
    // form; a layer with an exponential term by layer_crossings, except the stock forms (exp_term in (0, 1], decaying, no
    // linear or constant term: always within [0, 1]), which have none. Returns the number written, at most max_count.
    int kinks(double* out_m, int max_count, double top_m) const
    {
        int n = 0;
        auto add = [&](double h) {
            if (!(h > 0.0) || !std::isfinite(h) || n >= max_count)
            {
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                if (std::fabs(out_m[i] - h) <= 1e-9 * h)
                {
                    return;
                }
            }
            out_m[n++] = h;
        };
        const int layers = count > 1 ? 2 : 1;
        if (layers == 2)
        {
            add(layer[0].width_m);
        }
        for (int i = 0; i < layers; ++i)
        {
            const DensityLayer& l = layer[i];
            const double lo = (i == 0) ? 0.0 : layer[0].width_m;
            const double hi = (layers == 2 && i == 0) ? layer[0].width_m : top_m;
            if (l.exp_term == 0.0)
            {
                if (l.linear == 0.0)
                {
                    continue;
                }
                for (double level : { 0.0, 1.0 })
                {
                    const double h = (level - l.constant) / l.linear;
                    if (h > lo && h < hi)
                    {
                        add(h);
                    }
                }
            }
            else if (!(l.linear == 0.0 && l.constant == 0.0 && l.exp_scale <= 0.0 && l.exp_term > 0.0 && l.exp_term <= 1.0))
            {
                double c[4];
                const int k = layer_crossings(l, lo, hi, c, 4);
                for (int j = 0; j < k; ++j)
                {
                    add(c[j]);
                }
            }
        }
        return n;
    }
};

inline DensityProfile rayleigh_profile() // llsettingssky.cpp rayleighConfigDefault()
{
    DensityProfile p;
    p.layer[0].exp_term = 1.0;
    p.layer[0].exp_scale = -1.0 / 8000.0;
    return p;
}

inline DensityProfile mie_profile() // llsettingssky.cpp mieConfigDefault()
{
    DensityProfile p;
    p.layer[0].exp_term = 1.0;
    p.layer[0].exp_scale = -1.0 / 1200.0;
    return p;
}

// Bruneton's: 0 below 10 km, ramps to 1 at 25 km, back to 0 at 40 km (15 km of column at density 1)
inline DensityProfile ozone_profile()
{
    DensityProfile p;
    p.count = 2;
    p.layer[0].width_m = 25000.0;
    p.layer[0].linear = 1.0 / 15000.0;
    p.layer[0].constant = -2.0 / 3.0;
    p.layer[1].linear = -1.0 / 15000.0;
    p.layer[1].constant = 8.0 / 3.0;
    return p;
}

// The layer pairs of llsettingssky.cpp absorptionConfigDefault() (-1/25000 rising slope: 0 everywhere) that
// fromLegacy replaces by ozone_profile(), to 1e-6 relative:
//   1. as the function builds it:   a = (25000, 0, 0, -1/25000, -2/3), b = (0, 0, 0, -1/15000, 8/3)
//   2. as a sky holds it after the validator clamped linear to [0,2] and constant to [0,1]
//      (llsettingssky.cpp:211-232, llsettingsbase.cpp:709-720; every construction path validates):
//                                   a = (25000, 0, 0, 0, 0),          b = (0, 0, 0, 0, 1)
//   3. the same for a rising slope of +1/15000 (a sky saved by a build that changed the default):
//                                   a = (25000, 0, 0, 1/15000, 0),    b = (0, 0, 0, 0, 1)
inline bool is_stored_ozone_default(const DensityProfile& p)
{
    if (p.count != 2)
    {
        return false;
    }
    auto same = [](double x, double y) { return std::fabs(x - y) <= 1e-6 * std::fmax(std::fabs(x), std::fabs(y)) + 1e-12; };
    const DensityLayer& a = p.layer[0];
    const DensityLayer& b = p.layer[1];
    if (!(same(a.width_m, 25000.0) && same(a.exp_term, 0.0) && same(a.exp_scale, 0.0) && same(b.width_m, 0.0)
          && same(b.exp_term, 0.0) && same(b.exp_scale, 0.0)))
    {
        return false;
    }
    const bool raw = same(a.linear, -1.0 / 25000.0) && same(a.constant, -2.0 / 3.0) && same(b.linear, -1.0 / 15000.0) && same(b.constant, 8.0 / 3.0);
    const bool validated = (same(a.linear, 0.0) || same(a.linear, 1.0 / 15000.0)) && same(a.constant, 0.0) && same(b.linear, 0.0) && same(b.constant, 1.0);
    return raw || validated;
}

// --- the legacy fields, as the deferred pass receives them ---
struct LegacySky
{
    Vec3 source;                 // sunlight_color as bindDeferredShader sends it: normalised to max 1, clamped [0,1],
                                 // x RenderSkyAutoAdjustSunColorScale when auto-adjusting (pipeline.cpp:6427-6442, 9981-9988)
    Vec3 blue_density;
    double haze_density = 0.0;
    double density_multiplier = 0.0;
    double max_y = 0.0;          // m
    double distance_multiplier = 0.0;
    DensityProfile rayleigh = rayleigh_profile();
    DensityProfile mie = mie_profile();
    DensityProfile absorption = ozone_profile();
    double planet_radius_km = 6360.0;
    double top_radius_km = 6420.0;
};

struct AtmosphereBounds
{
    double haze_slab_km = 0.25;      // RenderAtmosphereHazeSlab / 1000
    double max_column = 8.0;         // tau per channel
    double max_ground_haze = 50.0;   // 1/km
};

namespace detail
{
// 8-point Gauss-Legendre
constexpr double GL_X[8] = { -0.9602898564975363, -0.7966664774136267, -0.5255324099163290, -0.1834346424956498,
                              0.1834346424956498,  0.5255324099163290,  0.7966664774136267,  0.9602898564975363 };
constexpr double GL_W[8] = { 0.1012285362903763, 0.2223810344533745, 0.3137066458778873, 0.3626837833783620,
                             0.3626837833783620, 0.3137066458778873, 0.2223810344533745, 0.1012285362903763 };

constexpr int MAX_ALTITUDES = 32;                // cut altitudes of one atmosphere
constexpr int MAX_CUTS = 2 * MAX_ALTITUDES + 3;  // two path roots per altitude, the perigee and the two ends
// an exponential layer is cut at these multiples of its scale height: the segments between them hold 8-point
// Gauss-Legendre to about 1e-11 of the layer's column (measured for the stock profiles, not guaranteed for
// others), and nothing is left beyond the last (e^-20)
constexpr double EXP_CUT_SCALES[3] = { 4.0, 10.0, 20.0 };

inline double finite_or(double x, double fallback)
{
    return std::isfinite(x) ? x : fallback;
}

inline double dot(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

// The altitudes (km, inside (0, top_km)) where a profile needs a cut: its kinks (layer boundary, clamp crossings) and, for an
// exponential layer, the EXP_CUT_SCALES multiples of its scale height (so no segment spans many e-foldings).
inline int profile_cuts_km(const DensityProfile& p, double top_km, double* out, int max_count)
{
    double kinks_m[16]; // metres
    int n = 0;
    const int k = p.kinks(kinks_m, 16, top_km * 1e3);
    for (int i = 0; i < k && n < max_count; ++i)
    {
        const double h = kinks_m[i] * 1e-3;
        if (h > 0.0 && h < top_km)
        {
            out[n++] = h;
        }
    }
    for (int i = 0; i < (p.count > 1 ? 2 : 1); ++i)
    {
        const DensityLayer& l = p.layer[i];
        if (l.exp_term != 0.0 && l.exp_scale < 0.0)
        {
            for (double scales : EXP_CUT_SCALES)
            {
                const double h = scales / -l.exp_scale * 1e-3;
                if (h > 0.0 && h < top_km && n < max_count)
                {
                    out[n++] = h;
                }
            }
        }
    }
    return n;
}

// the vertical column of a profile from the ground to top_km (km at density 1)
inline double profile_column_km(const DensityProfile& p, double top_km)
{
    double cut[MAX_ALTITUDES + 2];
    int n = 0;
    cut[n++] = 0.0;
    n += profile_cuts_km(p, top_km, cut + n, MAX_ALTITUDES);
    cut[n++] = top_km;
    std::sort(cut, cut + n);
    double sum = 0.0;
    for (int i = 0; i + 1 < n; ++i)
    {
        const double hw = 0.5 * (cut[i + 1] - cut[i]), mid = 0.5 * (cut[i + 1] + cut[i]);
        for (int k = 0; k < 8; ++k)
        {
            sum += hw * GL_W[k] * p.density((mid + hw * GL_X[k]) * 1e3);
        }
    }
    return sum;
}
} // namespace detail

// today's airmass, as a transmittance (the A/B debug setting and the legacy cloud light):
// per channel lin(source e^(-(bd + hd/4) dm max_y path_scale / max(1e-6, mu))) / lin(source)
// path_scale 1: the ground light (atmosphericsFuncs.glsl before L3); 0.5: the clouds' sun (cloudsV.glsl before L3)
inline Vec3 legacy_transmittance(const LegacySky& sky, double mu, double path_scale)
{
    using detail::finite_or;
    const double src[3] = { std::max(finite_or(sky.source.x, 0.0), 0.0), std::max(finite_or(sky.source.y, 0.0), 0.0),
                            std::max(finite_or(sky.source.z, 0.0), 0.0) };
    const double bd[3] = { sky.blue_density.x, sky.blue_density.y, sky.blue_density.z };
    const double air = 1.0 / std::max(1e-6, finite_or(mu, 0.0));
    double t[3];
    for (int k = 0; k < 3; ++k)
    {
        double x = (finite_or(bd[k], 0.0) + finite_or(sky.haze_density, 0.0) * 0.25) * finite_or(sky.density_multiplier, 0.0)
                 * finite_or(sky.max_y, 0.0) * finite_or(path_scale, 1.0) * air;
        x = std::max(x, 0.0);
        const double lin = srgb_to_linear(src[k]);
        t[k] = lin > 1e-9 ? srgb_to_linear(src[k] * std::exp(-x)) / lin : std::exp(-2.4 * x);
    }
    return { t[0], t[1], t[2] };
}

// --- the physical atmosphere mapped from the legacy fields ---
struct LLAtmosphere
{
    enum Clamp : unsigned // which bounds applied (diagnostics, tests)
    {
        CLAMP_COLUMN = 1u << 0,        // a channel's column hit max_column (diagnostic only: `excess` carries the rest, nothing switches on it)
        CLAMP_GROUND_HAZE = 1u << 1,   // the apparent ground haze hit max_ground_haze
        CLAMP_PLANET_RADIUS = 1u << 2,
        CLAMP_ATMOSPHERE_HEIGHT = 1u << 3,
        CLAMP_NON_FINITE = 1u << 4,    // a non-finite input was replaced
        CLAMP_OZONE = 1u << 5,         // ozone capped at a channel's column
        CLAMP_SLAB = 1u << 6,          // the ground haze slab capped at the aerosol column
        CLAMP_PROFILE = 1u << 7,       // an empty density profile was replaced by the default
    };

    // The legacy fields mapped onto Rayleigh, aerosol, ground haze slab and ozone. At noon (the sun at the
    // zenith, observer on the ground) T(0, 1) equals today's lin(source e^-tau) / lin(source) in every
    // channel, including the light rigs that ask for almost no sun: a channel's column is capped at max_column for the
    // physical model, and what is above the cap is kept as `excess` (see transmittance()).
    static LLAtmosphere fromLegacy(const LegacySky& sky, const AtmosphereBounds& bounds = {})
    {
        LLAtmosphere a;
        unsigned clamped = 0;
        auto fin = [&](double x, double fallback = 0.0) {
            if (!std::isfinite(x))
            {
                clamped |= CLAMP_NON_FINITE;
                return fallback;
            }
            return x;
        };

        double rg = fin(sky.planet_radius_km, 6360.0);
        double height = fin(sky.top_radius_km, 6420.0) - rg;
        if (rg < 1000.0 || rg > 100000.0)
        {
            rg = std::min(std::max(rg, 1000.0), 100000.0);
            clamped |= CLAMP_PLANET_RADIUS;
        }
        if (height < 10.0 || height > 1000.0)
        {
            height = std::min(std::max(height, 10.0), 1000.0);
            clamped |= CLAMP_ATMOSPHERE_HEIGHT;
        }
        a.planet_radius_km = rg;
        a.top_radius_km = rg + height;

        auto usable = [&](const DensityProfile& p, const DensityProfile& fallback) {
            if (detail::profile_column_km(p, height) > 1e-9)
            {
                return p;
            }
            clamped |= CLAMP_PROFILE;
            return fallback;
        };
        a.rayleigh = usable(sky.rayleigh, rayleigh_profile());
        a.mie = usable(sky.mie, mie_profile());
        a.absorption = usable(is_stored_ozone_default(sky.absorption) ? ozone_profile() : sky.absorption, ozone_profile());
        const double he_r = detail::profile_column_km(a.rayleigh, height);
        const double he_m = detail::profile_column_km(a.mie, height);
        const double he_o = detail::profile_column_km(a.absorption, height);

        const double src[3] = { std::max(fin(sky.source.x), 0.0), std::max(fin(sky.source.y), 0.0), std::max(fin(sky.source.z), 0.0) };
        const double bd[3] = { std::max(fin(sky.blue_density.x), 0.0), std::max(fin(sky.blue_density.y), 0.0),
                               std::max(fin(sky.blue_density.z), 0.0) };
        const double hd = std::max(fin(sky.haze_density), 0.0);
        const double dm = std::max(fin(sky.density_multiplier), 0.0);
        const double my = std::max(fin(sky.max_y), 0.0);
        const double dist = std::max(fin(sky.distance_multiplier), 0.0);
        const AtmosphereBounds defaults;
        const double max_column = (bounds.max_column >= 0.0 && std::isfinite(bounds.max_column)) ? bounds.max_column : defaults.max_column;
        const double max_ground_haze = (bounds.max_ground_haze >= 0.0 && std::isfinite(bounds.max_ground_haze)) ? bounds.max_ground_haze : defaults.max_ground_haze;

        // 1. the column, per channel, as today's sunlight reaches the ground at noon
        double column[3], true_column[3], over[3];
        for (int k = 0; k < 3; ++k)
        {
            const double tau = (bd[k] + hd * 0.25) * dm * my;
            const double lin = srgb_to_linear(src[k]);
            double c = lin > 1e-9 ? -std::log(srgb_to_linear(src[k] * std::exp(-tau)) / lin) : 2.4 * tau;
            if (!(c > 0.0))
            {
                c = 0.0;
            }
            c = std::min(c, 1e9); // a sun e^-tau that underflows has an infinite column; the excess stays finite
            true_column[k] = c;
            over[k] = 0.0;
            if (c > max_column)
            {
                over[k] = c - max_column; // the sun that goes missing above the cap (a light rig asking for almost no sun)
                c = max_column;
                clamped |= CLAMP_COLUMN;
            }
            column[k] = c;
        }

        // 2. ozone (Earth's values, never more than the column), aerosol (grey), Rayleigh (the rest)
        const double earth_ozone[3] = { 0.000650, 0.001881, 0.000085 }; // Bruneton and Neyret, /km at density 1
        double tau_o[3], tau_r[3], share_sum = 0.0, tau_m_max = 1e30, tau_m_true_max = 1e30;
        bool any_free = false;
        for (int k = 0; k < 3; ++k)
        {
            tau_o[k] = earth_ozone[k] * he_o;
            if (tau_o[k] > column[k])
            {
                tau_o[k] = column[k];
                clamped |= CLAMP_OZONE;
            }
            const double total = bd[k] + hd * 0.25;
            // the haze share of the true column (the cap is not a property of the air)
            share_sum += true_column[k] * (total > 0.0 ? hd * 0.25 / total : 0.0);
            tau_m_max = std::min(tau_m_max, column[k] - tau_o[k]);
            tau_m_true_max = std::min(tau_m_true_max, true_column[k] - std::min(earth_ozone[k] * he_o, true_column[k]));
            any_free = any_free || over[k] == 0.0;
        }
        // The aerosol is bounded by the true columns less ozone when some channel is under the cap, so a channel under the cap has
        // exactly the aerosol/Rayleigh split of the uncapped sky, whatever the cap does to the other channels (a capped channel
        // keeps its cap through the excess, below). That bound is at most the cap: some channel's column is under it. When every
        // channel is capped (huge densities), the capped columns bound it.
        if (any_free)
        {
            tau_m_max = tau_m_true_max;
        }
        const double tau_m = std::min(std::max(share_sum / 3.0, 0.0), std::max(tau_m_max, 0.0));
        for (int k = 0; k < 3; ++k)
        {
            tau_r[k] = std::max(column[k] - tau_m - tau_o[k], 0.0);
            const double model = tau_m + tau_o[k] + tau_r[k];
            if (model > column[k])
            {
                // a capped channel whose aerosol alone reaches the cap: the model column is above the cap, so the excess is what is left
                over[k] = std::max(true_column[k] - model, 0.0);
                column[k] = model;
            }
        }
        a.rayleigh_ext = { tau_r[0] / he_r, tau_r[1] / he_r, tau_r[2] / he_r };
        // a profile with no column (a layer above the atmosphere, or the 10 km height clamp over ozone_profile: 2e-31 of round-off, or 0) has no ozone; tau_o / he_o would be 0/0
        a.ozone_ext = he_o > 1e-9 ? Vec3{ tau_o[0] / he_o, tau_o[1] / he_o, tau_o[2] / he_o } : Vec3{};

        // 3. the ground haze slab: what the legacy distance haze has beyond the exponential profiles, taken
        //    from the aerosol column (never added to it)
        double k_ap = (bd[0] + hd) * dm * dist * 1000.0;
        if (k_ap > max_ground_haze)
        {
            k_ap = max_ground_haze;
            clamped |= CLAMP_GROUND_HAZE;
        }
        const double slab_h = std::min(std::max(detail::finite_or(bounds.haze_slab_km, 0.0), 0.0), height);
        double slab_ext = 0.0;
        if (slab_h > 0.0)
        {
            slab_ext = std::max(k_ap - (tau_r[0] / he_r + tau_m / he_m), 0.0);
            const double cap = tau_m / (0.875 * slab_h);
            if (slab_ext > cap)
            {
                slab_ext = cap;
                clamped |= CLAMP_SLAB;
            }
        }
        a.slab_ext = slab_ext;
        a.slab_top_km = slab_h;
        a.aerosol_ext = std::max(tau_m - slab_ext * 0.875 * slab_h, 0.0) / he_m;
        a.column = { column[0], column[1], column[2] };
        a.excess = { over[0], over[1], over[2] };
        a.clamped = clamped;
        a.finalize();
        return a;
    }

    // Transmittance from altitude h_km above the ground, towards a body at zenith cosine mu, to the top of the
    // atmosphere (the ground is not an occluder here), times the body's visible part over the horizon:
    // smoothstep(mu_h - disc_sin, mu_h + disc_sin, mu), mu_h = -sqrt(1 - (Rg/(Rg+h))^2). This is what lights call
    // (physicalAtmosphereF.glsl's pa_transmittance is its twin); physical_transmittance() is the part without the excess.
    //
    //   T = physical_transmittance(h, mu, disc) x exp(-excess x path_factor / max(mu - mu_h, 1e-6))
    //
    // `excess` is what the capped columns lack (a light rig asks for almost no sun: its column is above max_column). It is
    // plane-parallel in the angle above the horizon at the observer's altitude (mu_h, as visible_share has it; 0 on the ground),
    // so it goes to 0 at and below the horizon, continuously, where visible_share does; it has no other dependence on the
    // altitude (a property of the sky); and it is 1 where there is no excess. So T is continuous in the cap max_column at every
    // altitude, and nothing switches as the sun moves.
    //
    // path_factor scales the excess only. In the physical model a light that is lower in the air than the sun's (the cloud light)
    // gets its thinner air from the altitude: T is taken AT THE CLOUD LAYER (h_km = the cloud height setting, 1500 m), not by
    // halving the path. The excess has no altitude dependence, so path_factor is there only for it: the legacy cloud light
    // (cloudsV.glsl, before L3) has half the airmass, and path_factor 0.5 gives a rig's cloud light that same half airmass. So AT1's
    // cloud light calls transmittance(h_cloud, mu, disc, 0.5); the sun and moon light on the ground use 1.
    // A non-finite argument gives 0 (a non-finite path_factor counts as 1, as in the GLSL).
    Vec3 transmittance(double h_km, double mu, double disc_sin, double path_factor = 1.0) const
    {
        const Vec3 t = physical_transmittance(h_km, mu, disc_sin);
        if (!(excess.x > 0.0 || excess.y > 0.0 || excess.z > 0.0) || !(t.x > 0.0 || t.y > 0.0 || t.z > 0.0))
        {
            return t;
        }
        const double pf = std::min(std::max(detail::finite_or(path_factor, 1.0), 0.0), 1e6);
        const double air = pf / std::max(std::min(mu, 1.0) - horizon_mu(h_km), 1e-6);
        return { t.x * std::exp(-excess.x * air), t.y * std::exp(-excess.y * air), t.z * std::exp(-excess.z * air) };
    }

    // the zenith cosine of the horizon seen from altitude h_km (negative above the ground, 0 on it)
    double horizon_mu(double h_km) const
    {
        const double rg = planet_radius_km;
        const double h0 = std::min(std::max(h_km, 0.0), top_radius_km - rg);
        return -std::sqrt(h0 * (2.0 * rg + h0)) / (rg + h0);
    }

    // the share of the body's disc above the horizon (the factor transmittance() ends with)
    double visible_share(double h_km, double mu, double disc_sin) const
    {
        mu = std::min(std::max(mu, -1.0), 1.0);
        const double d = std::min(std::max(disc_sin, 0.0), 1.0);
        const double mu_h = horizon_mu(h_km);
        return d > 0.0 ? smoothstep(mu_h - d, mu_h + d, mu) : (mu >= mu_h ? 1.0 : 0.0);
    }

    // The physical model alone, the capped columns without their excess: for tests and diagnostics only; lights call transmittance().
    // Segmented 8-point Gauss-Legendre along the path, cut at every profile kink, at the slab's 0.75H and H, at EXP_CUT_SCALES
    // scale heights of each exponential layer, where a ray crosses the ground and at the perigee of a downward ray.
    // Accuracy: about 1e-9 absolute for the stock profiles (exponential Rayleigh and aerosol, Bruneton's ozone), as measured by the
    // test; a profile with clamp crossings inside it is cut at them (the test's mixed profiles measure the error against the
    // 200000-step reference); other profiles are not guaranteed that. The cut altitudes come from finalize().
    Vec3 physical_transmittance(double h_km, double mu, double disc_sin) const
    {
        if (!std::isfinite(h_km) || !std::isfinite(mu) || !std::isfinite(disc_sin))
        {
            return {};
        }
        const double vis = visible_share(h_km, mu, disc_sin);
        if (!(vis > 0.0))
        {
            return {};
        }
        const double rg = planet_radius_km;
        const double top = top_radius_km - rg;
        const double h0 = std::min(std::max(h_km, 0.0), top);
        mu = std::min(std::max(mu, -1.0), 1.0);
        const double r0 = rg + h0;
        const double q0 = h0 * (2.0 * rg + h0); // r0^2 - Rg^2, without the cancellation

        // path length s to the altitude a: s^2 + 2 b s - k = 0, b = r0 mu, k = (a - h0)(2 Rg + a + h0)
        const double b = r0 * mu;
        auto roots = [&](double alt, double& s_lo, double& s_hi) {
            const double k = (alt - h0) * (2.0 * rg + alt + h0);
            const double disc = b * b + k;
            s_lo = s_hi = -1.0;
            if (disc < 0.0)
            {
                return;
            }
            const double sq = std::sqrt(disc);
            if (b >= 0.0)
            {
                s_hi = (b + sq) > 0.0 ? k / (b + sq) : -1.0; // -b + sq
            }
            else
            {
                s_hi = -b + sq;
                s_lo = s_hi > 0.0 ? -k / s_hi : -1.0;         // -b - sq
            }
        };
        double unused, path;
        roots(top, unused, path);
        if (!(path > 0.0))
        {
            return { vis, vis, vis };
        }

        // the cut altitudes: computed once per atmosphere by finalize(); an atmosphere built by hand and not finalized computes them here
        double local[detail::MAX_ALTITUDES];
        const double* alts = cuts_km;
        int na = cut_count;
        if (na < 0)
        {
            na = cut_altitudes_km(local, detail::MAX_ALTITUDES);
            alts = local;
        }
        double cut[detail::MAX_CUTS];
        int n = 0;
        cut[n++] = 0.0;
        cut[n++] = path;
        for (int i = 0; i < na; ++i)
        {
            double s_lo, s_hi;
            roots(alts[i], s_lo, s_hi);
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
            cut[n++] = -b; // the perigee
        }
        for (int i = 1; i < n; ++i) // insertion sort
        {
            const double v = cut[i];
            int j = i - 1;
            while (j >= 0 && cut[j] > v)
            {
                cut[j + 1] = cut[j];
                --j;
            }
            cut[j + 1] = v;
        }

        double tau[3] = { 0.0, 0.0, 0.0 };
        const double slab_lo = 0.75 * slab_top_km;
        for (int i = 0; i + 1 < n; ++i)
        {
            const double hw = 0.5 * (cut[i + 1] - cut[i]), mid = 0.5 * (cut[i + 1] + cut[i]);
            if (!(hw > 0.0))
            {
                continue;
            }
            for (int k = 0; k < 8; ++k)
            {
                const double s = mid + hw * detail::GL_X[k];
                const double r = std::sqrt(std::max(r0 * r0 + 2.0 * b * s + s * s, 0.0));
                const double h = std::max((q0 + 2.0 * b * s + s * s) / (r + rg), 0.0); // altitude, without the cancellation
                const double dr = rayleigh.density(h * 1e3), dm = mie.density(h * 1e3), dz = absorption.density(h * 1e3);
                const double ds = slab_top_km > 0.0 ? 1.0 - smoothstep(slab_lo, slab_top_km, h) : 0.0;
                const double grey = aerosol_ext * dm + slab_ext * ds;
                const double w = hw * detail::GL_W[k];
                tau[0] += w * (rayleigh_ext.x * dr + grey + ozone_ext.x * dz);
                tau[1] += w * (rayleigh_ext.y * dr + grey + ozone_ext.y * dz);
                tau[2] += w * (rayleigh_ext.z * dr + grey + ozone_ext.z * dz);
            }
        }
        return { std::exp(-tau[0]) * vis, std::exp(-tau[1]) * vis, std::exp(-tau[2]) * vis };
    }

    // the altitudes (km above the ground, inside the atmosphere) where the path integral is cut
    int cut_altitudes_km(double* out, int max_count) const
    {
        const double top = top_radius_km - planet_radius_km;
        int n = 0;
        out[n++] = 0.0; // where a ray below the horizon crosses the ground (the altitude is clamped there)
        if (slab_ext > 0.0 && slab_top_km > 0.0 && slab_top_km < top && max_count >= 3)
        {
            out[n++] = 0.75 * slab_top_km;
            out[n++] = slab_top_km;
        }
        for (const DensityProfile* p : { &rayleigh, &mie, &absorption })
        {
            double buf[detail::MAX_ALTITUDES];
            const int k = detail::profile_cuts_km(*p, top, buf, detail::MAX_ALTITUDES);
            for (int i = 0; i < k && n < max_count; ++i)
            {
                out[n++] = buf[i];
            }
        }
        return n;
    }

    // Computes the cut altitudes once for this atmosphere (the clamp crossings take bisections, so not once per call). fromLegacy() calls it;
    // an atmosphere built or changed by hand (profiles, slab, planet radius, top) must call it after its last change, or transmittance()
    // computes the cuts on every call. Cuts left stale by a later change are not detected. The rule: edit the fields, then call finalize().
    void finalize()
    {
        cut_count = cut_altitudes_km(cuts_km, detail::MAX_ALTITUDES);
    }

    Vec3   column;                      // tau per channel, linear, capped at max_column: lin(source) e^-(column + excess) = lin(source e^-tau_legacy) (noon)
    Vec3   excess;                      // per channel: what the true column has above max_column (0 unless a light rig asks for almost no sun)
    Vec3   rayleigh_ext;                // 1/km at density 1
    double aerosol_ext = 0.0;           // 1/km at density 1 (the column part above the slab)
    double slab_ext = 0.0;              // ground haze slab, 1/km: constant to 0.75 H, smooth to 0 at H
    double slab_top_km = 0.25;
    Vec3   ozone_ext;                   // Bruneton & Neyret's Earth values (0.000650, 0.001881, 0.000085) /km, capped at the column
    DensityProfile rayleigh = rayleigh_profile(), mie = mie_profile(),
                   absorption = ozone_profile(); // ozone_profile() if the stored default was the broken one
    double planet_radius_km = 6360.0, top_radius_km = 6420.0;   // clamped: Rg in [1000, 100000], Rt - Rg in [10, 1000]
    unsigned clamped = 0;               // bit mask of Clamp: the bounds that applied

    double cuts_km[detail::MAX_ALTITUDES] = {}; // the path integral's cut altitudes, from finalize()
    int cut_count = -1;                         // -1: not computed (a copy or default struct never holds indeterminate values)
};

// --- L3: the sun/moon handover (stateless) ---
constexpr double MOON_PHYSICAL = 2.5e-6;   // full moon / sun illuminance (0.25 lx / 100 klx)

struct PrimaryLightParams
{
    double swing = 0.30;   // half-width of the direction/colour swing, degrees of sun elevation: all sun at +swing, all moon at -swing
                           // (RenderLightHandoverSwing, 0.05 .. 5)
    double fade  = 0.30;   // width of each shadow fade outside the swing, degrees: the sun's from swing + fade to swing, the moon's
                           // from -swing to -swing - fade                                   (RenderLightHandoverFade, 0 .. 5)
    double horizon_dip_deg = 0.0; // the swing's centre is at -this degrees of sun elevation instead of 0 (the cloud call sets it to the
                                  // cloud layer's horizon dip; the shadow fades are not shifted). 0 for the ground.
    double dark  = 1e-4;   // the moon is available when its light (before the compensation) reaches dark x luminance(source);
                           // smooth from 0
};

struct PrimaryLightInputs
{
    Vec3 sun_dir, moon_dir;           // unit, z up (LLSettingsSky's frame)
    Vec3 sun_T, moon_T;               // transmittance to each body at the ground observer, disc visibility included
    Vec3 source;                      // linear light above the atmosphere (EEP's sunlight_color; the moon's light uses this shared
                                      // source too, so AT1 decides whether the moon needs a source of its own)
    double moon_gain = 0.0;           // moon_brightness x moon_compensation(...)
};

struct PrimaryLight
{
    Vec3   dir;              // unit: the one light the scene is lit and shadowed by
    Vec3   T;                // its transmittance: ground light = source x T (cross-faded, moon gain included)
    double shadow_strength;  // 1 full, 0 none
    double moon_weight;      // t: 0 the sun, 1 the moon, as far as the moon is available (the direction and the transmittance)
    double band_weight;      // the swing's weight alone, 0 above the band and 1 below it, whether or not a moon is there: the dome's
                             // and the clouds' blend (with no moon the night is as before: no sun colour, no sun glow)
    double moon_avail;       // 0 .. 1: how much of a moon there is to take over (up and bright enough)
};

// nlerp, bent over the zenith when the two face apart; never through zero, never below the horizon for two lights above it:
// normalize((1-t) a + t b + 4 t (1-t) max(0, -a.b) Z). A zero-length argument counts as Z.
inline Vec3 route_direction(const Vec3& from, const Vec3& to, double t)
{
    auto unit = [](const Vec3& v) {
        const double len = std::sqrt(detail::dot(v, v));
        return (len > 1e-12 && std::isfinite(len)) ? Vec3{ v.x / len, v.y / len, v.z / len } : Vec3{ 0.0, 0.0, 1.0 };
    };
    const Vec3 a = unit(from), b = unit(to);
    t = std::min(std::max(detail::finite_or(t, 0.0), 0.0), 1.0);
    const double bend = 4.0 * t * (1.0 - t) * std::max(0.0, -detail::dot(a, b));
    return unit({ (1.0 - t) * a.x + t * b.x, (1.0 - t) * a.y + t * b.y, (1.0 - t) * a.z + t * b.z + bend });
}

// the moon's compensation, adapted over twilight: MOON_PHYSICAL while the sun is up, compensation at sun_z <= -depth_sin
// MOON_PHYSICAL + (compensation - MOON_PHYSICAL) x smoothstep(0, -depth_sin, sun_z)
inline double moon_compensation(double compensation, double sun_z, double depth_sin)
{
    depth_sin = detail::finite_or(depth_sin, 1e-9);
    sun_z = detail::finite_or(sun_z, 1.0); // not finite: the sun counts as up
    return MOON_PHYSICAL + (compensation - MOON_PHYSICAL) * smoothstep(0.0, -std::max(depth_sin, 1e-9), sun_z);
}

inline Vec3 cross_fade(const Vec3& sun_T, const Vec3& moon_T, double moon_gain, double t)
{
    const double m = t * moon_gain, s = 1.0 - t;
    return { s * sun_T.x + m * moon_T.x, s * sun_T.y + m * moon_T.y, s * sun_T.z + m * moon_T.z };
}

// the dip of the horizon seen from height h above a sphere of radius R, degrees: acos(R / (R + h));
// 0 for a bad or non-positive input
inline double horizon_dip_degrees(double R, double h)
{
    if (!(std::isfinite(R) && std::isfinite(h) && R > 0.0 && h > 0.0))
    {
        return 0.0;
    }
    return std::acos(R / (R + h)) * 180.0 / 3.14159265358979323846;
}

inline PrimaryLight primary_light(const PrimaryLightInputs& in, const PrimaryLightParams& p)
{
    using detail::finite_or;
    auto clean = [](const Vec3& v) { // non-finite counts as 0, then clamped to [0,1]
        auto c = [](double x) { return std::isfinite(x) ? std::min(std::max(x, 0.0), 1.0) : 0.0; };
        return Vec3{ c(v.x), c(v.y), c(v.z) };
    };
    const Vec3 sun_T = clean(in.sun_T), moon_T = clean(in.moon_T);
    const Vec3 src = { std::max(finite_or(in.source.x, 0.0), 0.0), std::max(finite_or(in.source.y, 0.0), 0.0),
                       std::max(finite_or(in.source.z, 0.0), 0.0) };
    const double gain = std::max(finite_or(in.moon_gain, 0.0), 0.0);

    const double y_src = luminance(src);
    const double sun_len = std::sqrt(detail::dot(in.sun_dir, in.sun_dir));
    const double sun_z = (sun_len > 1e-12 && std::isfinite(sun_len)) ? in.sun_dir.z / sun_len : 1.0;

    // The swing and the shadow fades follow the sun's elevation (a moon's share of the light would go 0 -> 1 within +-0.1 degree,
    // because the sun's light collapses at the horizon): the band weight is a smoothstep from all sun at +swing to all moon at
    // -swing. The moon only takes over as far as it is available (up and bright enough): with no usable moon the light keeps the
    // sun's direction (nothing swings to a moon that is not there) and fades to dark with the sun's transmittance, while the dome
    // and the clouds blend as at night (band_weight). Neither the band nor the shadow depends on the sun's size (sun_scale).
    constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;
    const double elev = std::asin(std::min(std::max(sun_z, -1.0), 1.0)) * RAD2DEG;
    const double swing = std::min(std::max(finite_or(p.swing, 0.3), 0.05), 5.0);
    const double fade = std::min(std::max(finite_or(p.fade, 0.3), 0.0), 5.0);
    // the moon's availability: its light before the compensation against the dark threshold (the disc's visibility is in
    // moon_T, so a moon rising or setting fades smoothly), and its gain against 1 % of the physical moon (a sky with
    // moon_brightness 0 has no moon; 0.01 is the smallest moon_brightness that still counts)
    const double e_m_raw = luminance({ src.x * moon_T.x, src.y * moon_T.y, src.z * moon_T.z });
    const double avail = (p.dark * y_src > 0.0 ? smoothstep(0.0, p.dark * y_src, e_m_raw) : 0.0)
                       * smoothstep(0.0, 0.01 * MOON_PHYSICAL, gain);
    const double dip = std::min(std::max(finite_or(p.horizon_dip_deg, 0.0), 0.0), 20.0);
    const double band = smoothstep(swing, -swing, elev + dip); // the band is centred on -dip
    const double t = band * avail;                             // direction and colour
    // the sun's shadow 1 -> 0 from swing + fade to swing; the moon's 0 -> 1 from -swing to -swing - fade, scaled by its
    // availability
    const double s = std::max(smoothstep(swing, swing + fade, elev), avail * (1.0 - smoothstep(-swing - fade, -swing, elev)));

    PrimaryLight out;
    out.dir = route_direction(in.sun_dir, in.moon_dir, t);
    out.T = cross_fade(sun_T, moon_T, gain, t);
    out.shadow_strength = s;
    out.moon_weight = t;
    out.band_weight = band;
    out.moon_avail = avail;
    return out;
}

// ---------------------------------------------------------------------------------------------------
// Stage AT1/L3: the viewer's assembly of the sun/moon handover (LLEnvironment::updatePrimaryLight calls it
// once per main-thread frame). Renderer-free so that the whole chain is tested offline: the physical (or the
// old) transmittances at the ground and at the cloud layer, the moon's compensation, primary_light(), and the
// values the shaders get.
// ---------------------------------------------------------------------------------------------------

struct PrimaryLightSetup
{
    LegacySky sky;                       // the sky as the deferred pass receives it (LegacySky::source)
    AtmosphereBounds bounds;
    PrimaryLightParams params;
    Vec3   sun_dir, moon_dir;            // unit, z up
    double sun_scale = 1.0, moon_scale = 1.0;
    double moon_brightness = 0.5;        // the sky's, in [0, 1]
    double compensation_ev = -3.0;       // RenderMoonlightCompensation, limited to [-8, -2]
    double adaptation_depth_deg = 6.0;   // RenderMoonAdaptationDepth, limited to [1, 18]
    double cloud_height_km = 1.5;        // the cloud layer the cloud light is taken at
    bool   physical = true;              // RenderAtmospherePhysicalLight: the physical T, or today's airmass
    // The colour the cloud shader multiplies the cloud transmittance with: the sky's RAW sunlight_color (not normalised,
    // up to 3, sRGB; times the auto-adjust scale), which differs from sky.source for dim and bright skies. Its
    // transmittances are built from it so that lin(raw) x T is what the old exp gave. Unset: sky.source.
    Vec3   cloud_source;
    bool   has_cloud_source = false;
};

struct PrimaryLightRig
{
    PrimaryLight light;        // direction, ground transmittance (moon gain included), shadow strength, moon weight, band weight,
                               // moon availability
    Vec3   cloud_T;            // the sun's transmittance at the cloud layer (the clouds' sun term; the moon's half is not used)
    Vec3   cloud_dir;          // the clouds' glow direction: the sun's (unit, z up), through the whole handover
    double cloud_band_weight = 0.0; // the clouds' own band weight: primary_light() on the cloud-layer transmittances, the band
                                    // centred on -dip
    Vec3   color;              // the ground light in sunlight_color's encoding: sRGB(lin(source) x light.T), not clamped
    double gain = 0.0;         // moon_brightness x moon_compensation
    double light_scale = 1.0;  // mix(sun_scale, moon_scale, moon weight t)
    double haze_glow_factor = 1.0; // the dome's and the haze's glow: the sun's share (1 - band weight) plus moon_glow_factor
    double moon_glow_factor = 0.0; // the moon's share of it: band weight x moon_brightness / 4 x the moon disc's visibility
    double moon_halo_weight = 0.0; // band weight x the moon disc's visibility x (moon_brightness > 0): the weight of the halo and
                                   // rainbow around the moon
    bool   shadow_visible = true;
};

inline PrimaryLightRig compute_primary_light(const PrimaryLightSetup& s)
{
    using detail::finite_or;
    constexpr double DEG = 3.14159265358979323846 / 180.0;
    auto disc_sin = [&](double scale) {
        return std::min(std::max(std::sin(0.265 * DEG * std::min(std::max(finite_or(scale, 1.0), 0.0), 100.0)), 1e-5), 0.2);
    };
    const double sun_disc = disc_sin(s.sun_scale), moon_disc = disc_sin(s.moon_scale);
    const double sun_z = finite_or(s.sun_dir.z, 1.0), moon_z = finite_or(s.moon_dir.z, -1.0);

    LegacySky cloud_sky = s.sky; // the clouds' light is made of the colour their shader has
    if (s.has_cloud_source)
    {
        cloud_sky.source = s.cloud_source;
    }
    const Vec3 cloud_src_lin = srgb_to_linear(Vec3{ std::max(finite_or(cloud_sky.source.x, 0.0), 0.0), std::max(finite_or(cloud_sky.source.y, 0.0), 0.0),
                                                     std::max(finite_or(cloud_sky.source.z, 0.0), 0.0) });

    // the cloud layer's altitude: the cloud light's path and the horizon dip of the clouds' band
    const double h_cloud = std::min(std::max(finite_or(s.cloud_height_km, 1.5), 0.0), 20.0);

    Vec3 sun_T, moon_T, sun_Tc, moon_Tc;
    if (s.physical)
    {
        const LLAtmosphere atm = LLAtmosphere::fromLegacy(s.sky, s.bounds);
        sun_T = atm.transmittance(0.0, sun_z, sun_disc, 1.0);
        moon_T = atm.transmittance(0.0, moon_z, moon_disc, 1.0);
        // the cloud light thins through the cloud layer's altitude (and keeps half of the legacy path for a clamped rig)
        const LLAtmosphere atm_c = s.has_cloud_source ? LLAtmosphere::fromLegacy(cloud_sky, s.bounds) : atm;
        sun_Tc = atm_c.transmittance(h_cloud, sun_z, sun_disc, 0.5);
        moon_Tc = atm_c.transmittance(h_cloud, moon_z, moon_disc, 0.5);
    }
    else
    { // A/B: today's airmass
        sun_T = legacy_transmittance(s.sky, sun_z, 1.0);
        moon_T = legacy_transmittance(s.sky, moon_z, 1.0);
        sun_Tc = legacy_transmittance(cloud_sky, sun_z, 0.5);
        moon_Tc = legacy_transmittance(cloud_sky, moon_z, 0.5);
    }

    const double ev = std::min(std::max(finite_or(s.compensation_ev, -3.0), -8.0), -2.0); // never more than a quarter of a sun
    const double depth_sin = std::sin(std::min(std::max(finite_or(s.adaptation_depth_deg, 6.0), 1.0), 18.0) * DEG);
    const double mb = std::min(std::max(finite_or(s.moon_brightness, 0.5), 0.0), 1.0);

    PrimaryLightRig rig;
    rig.gain = mb * moon_compensation(std::exp2(ev), sun_z, depth_sin);

    PrimaryLightInputs in;
    in.sun_dir = s.sun_dir;
    in.moon_dir = s.moon_dir;
    in.sun_T = sun_T;
    in.moon_T = moon_T;
    in.source = { srgb_to_linear(std::max(finite_or(s.sky.source.x, 0.0), 0.0)), srgb_to_linear(std::max(finite_or(s.sky.source.y, 0.0), 0.0)),
                  srgb_to_linear(std::max(finite_or(s.sky.source.z, 0.0), 0.0)) };
    in.moon_gain = rig.gain;
    rig.light = primary_light(in, s.params);

    const double t = rig.light.moon_weight, tb = rig.light.band_weight;
    // The clouds hand over by their own band: at 1.5 km the sun is still seen after the ground's sunset (the horizon dips
    // 1.24 degrees there), so the ground's band would take the clouds' sun away while it still shines on them. The
    // shader multiplies the sun term by (1 - t_cloud) and shows no moon term, so only the sun's transmittance is passed.
    PrimaryLightInputs cin = in;
    cin.sun_T = sun_Tc;
    cin.moon_T = moon_Tc;
    cin.source = cloud_src_lin;
    // the cloud layer's horizon dip, acos(R / (R + h)): the clouds' band is centred on -dip instead of 0
    // (the ground's shadow fades stay)
    PrimaryLightParams cparams = s.params;
    {
        const double R = std::min(std::max(finite_or(s.sky.planet_radius_km, 6360.0), 1000.0), 100000.0); // as the atmosphere clamps it
        cparams.horizon_dip_deg = horizon_dip_degrees(R, h_cloud);
    }
    const PrimaryLight cl = primary_light(cin, cparams);
    // the clouds' sun term ends with the band (as at night before)
    rig.cloud_band_weight = cl.band_weight;
    // the clouds' glow is the sun's only (the moon's half was never visible on them), so it stays at the sun, not at the route
    // between the sun and the moon (a phantom glow in the empty sky between them)
    rig.cloud_dir = route_direction(s.sun_dir, s.moon_dir, 0.0);
    rig.cloud_T = { std::min(std::max(finite_or(sun_Tc.x, 0.0), 0.0), 1.0), std::min(std::max(finite_or(sun_Tc.y, 0.0), 0.0), 1.0),
                    std::min(std::max(finite_or(sun_Tc.z, 0.0), 0.0), 1.0) };
    rig.light_scale = (1.0 - t) * std::min(std::max(finite_or(s.sun_scale, 1.0), 0.0), 100.0)
                    + t * std::min(std::max(finite_or(s.moon_scale, 1.0), 0.0), 100.0);

    // the dome's glow: the sun's, handing over to the moon's with the band (a quarter of its brightness, once the moon is up):
    // 1 above the band exactly, 0 below it when there is no moon
    // Each body's glow stays at the body: the sun's at the sun, fading with (1 - tb), and the moon's at the moon, fading in with
    // tb (the shaders get both directions), not at the blended light's direction between them.
    const double moon_vis = smoothstep(-moon_disc, moon_disc, moon_z);
    rig.moon_glow_factor = tb * mb * 0.25 * moon_vis;
    rig.haze_glow_factor = (1.0 - tb) + rig.moon_glow_factor;
    rig.moon_halo_weight = tb * moon_vis * smoothstep(0.0, 0.01, mb); // no halo or rainbow around a moon of a sky with moon_brightness 0

    const Vec3 ground = { in.source.x * rig.light.T.x, in.source.y * rig.light.T.y, in.source.z * rig.light.T.z };
    rig.color = linear_to_srgb(ground);
    rig.shadow_visible = rig.light.shadow_strength > 1e-3 && luminance(ground) > 1e-4 * luminance(in.source);
    return rig;
}

// ---------------------------------------------------------------------------------------------------
// Stage AT2-0: the scattering core of the physical atmosphere (no rendering change).
//
// AT0/AT1 gave the physical EXTINCTION of every sky (LLAtmosphere). This adds what a sky needs to be drawn from it:
// the scattering (albedos), the phase functions, the multiple-scattering scale, the lights' irradiance, the bounds that
// keep every legacy sky a physical one (no whiteout, no fallback to the old model), and the double-precision reference
// integrals that AT2's GPU passes are held to: the transmittance LUT's parameterisation, the multiple-scattering texel,
// the sky irradiance texel, the sky radiance and the aerial-perspective march. class1/windlight/atmosphereCommonF.glsl
// is their GLSL twin (held equal by scripts/light/gen_atmosphere_scatter_vectors.py), indra/newview/tests/
// llatmosphericscatter_test.cpp the test. Hillaire 2020 (A Scalable and Production Ready Sky and Atmosphere Rendering
// Technique) on Bruneton and Neyret's parameterisation; the model and its measurements are doc/light/atmosphere-scattering-core.md.
//
// Units: km; z is up (LLSettingsSky's frame); the observer is at (0, 0, Rg + h). Colours are linear. All the work is
// per-channel (Vec3) except the grey aerosol and slab scattering.
//
// Altitudes are never taken as |x| - Rg (in float that is 0.5 m of resolution at 6360 km, and the haze slab is 250 m thick):
// the march carries the observer's height h0 and takes the altitude of a point at path length t as q / (r + Rg), with
// q = h0 (2 Rg + h0) + 2 b t + t^2 = |x|^2 - Rg^2 and b = (Rg + h0) d.z. The same forms are in the GLSL.
// ---------------------------------------------------------------------------------------------------

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 operator*(const Vec3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline Vec3 operator*(double s, const Vec3& a) { return a * s; }
inline Vec3 operator*(const Vec3& a, const Vec3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
inline Vec3& operator+=(Vec3& a, const Vec3& b) { a = a + b; return a; }

namespace detail
{
constexpr double SC_PI = 3.14159265358979323846;

inline Vec3 vexp(const Vec3& a) { return { std::exp(a.x), std::exp(a.y), std::exp(a.z) }; }
inline double vmax(const Vec3& a) { return std::max(a.x, std::max(a.y, a.z)); }
inline double len(const Vec3& a) { return std::sqrt(dot(a, a)); }
// NaN gives lo (a NaN would reach a static_cast<int> in the tables: undefined behaviour)
inline double clampd(double x, double lo, double hi) { return !(x >= lo) ? lo : (x > hi ? hi : x); }

// a unit vector along v; `fallback` when v is not finite or has no length
inline Vec3 sane_dir(const Vec3& v, const Vec3& fallback)
{
    const double l = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    return (std::isfinite(l) && l > 1e-12) ? Vec3{ v.x / l, v.y / l, v.z / l } : fallback;
}

// the roots t0 <= t1 of t^2 + 2 b t + c = 0, without cancellation; false if there are none
inline bool quad_roots(double b, double c, double& t0, double& t1)
{
    const double disc = b * b - c;
    if (!(disc >= 0.0))
    {
        return false;
    }
    const double s = std::sqrt(disc), q = -(b + (b >= 0.0 ? s : -s));
    const double r2 = q != 0.0 ? c / q : q;
    t0 = std::min(q, r2);
    t1 = std::max(q, r2);
    return true;
}
} // namespace detail

// The EEP fields beyond LegacySky that the scattering needs.
struct ScatterFields
{
    Vec3   blue_horizon;               // tints the Rayleigh albedo; the shader's value, RenderSkyAutoAdjustBlueHorizonScale applied
    double haze_horizon = 0.0;         // tints the aerosol albedo (the red channel, as the legacy dome)
    Vec3   ambient;                    // scales the multiple scattering
    double glow_x = 5.0, glow_z = -0.48; // the creator's glow lobe (glow.x, glow.z): (glow.x max(1 - cos, 0.001))^glow.z + 0.25
    double mie_anisotropy = 0.8;       // EEP's Mie anisotropy (the phase of an atmosphere built by hand without a glow lobe)
    double sky_sunlight_scale = 1.0;   // RenderHDRSkySunlightScale (the irradiance pbrBaseLight gives a sunlit surface is 3 x lin(source) x this)
    double sun_disc_sin = 0.0046, moon_disc_sin = 0.0046; // the lights' disc (sin of the angular radius), for the horizon cut-off
};

// The bounds (every value a sky can ask for is brought into them; none switches to the old model).
struct ScatterBounds
{
    // Default.xml is the physical reference: its blue_horizon, haze_horizon and ambient map to a Rayleigh albedo of 1, an
    // aerosol albedo of 0.9 and Hillaire's multiple scattering as it is; other skies act relative to it
    Vec3   ref_blue_horizon = { 0.4954838156700134, 0.4954838156700134, 0.6399999856948853 };
    double ref_haze_horizon = 0.1899999976158142;
    double ref_ambient = 1.0499999523162842;
    double max_rayleigh_albedo = 1.0;     // <= 1: energy conservation
    double max_aerosol_albedo = 0.95;
    double aerosol_albedo_at_ref = 0.9;   // the aerosol albedo of a sky whose haze_horizon is Default's
    // The multiple scattering is scaled by ms_scale and summed as 1 / (1 - f_ms): the product is at most max_ms_scale / (1 - max_f_ms) =
    // 4 / 0.4 = 10 (a scale of 4 with a series of 10 let a legal synthetic sky reach 1.43 x the white card across the open sky). No
    // shipped sky is on the scale bound (the largest ambient / Default's is 2.86, the library check reports it); only a very thick fog
    // reaches f_ms 0.6, where Hillaire's series is weak anyway.
    double max_ms_scale = 4.0;            // per channel
    double max_f_ms = 0.6;
    double max_phase = 1e4;               // the aerosol phase's peak, 1 / sr
    double glow_z_min = -3.0, glow_z_max = 0.5; // the glow lobes: glow.z and glow.x are clamped into these (a continuous clamp)
    double glow_x_min = 0.01, glow_x_max = 1000.0;
    double max_sunlight_scale = 4.0;
    double max_moon_gain = 0.25;          // AT1's ev clamp: the moon is never more than a quarter of a sun
    double ground_albedo = 0.2;           // the planet's surface, for the multiple scattering and the sky below the horizon
};

constexpr int MS_DIRS_DEFAULT = 8;       // 8 x 8 directions per multiple-scattering texel
constexpr int MS_STEPS_DEFAULT = 20;
// atmosphere_ap's step placement (the same numbers are in the GLSL): the spacing exponents of its first and second segment, the floor of
// the weights that share the steps between them, and the rule for which end of a segment the steps crowd to (ray_segment)
constexpr double AP_POWER_NEAR = 1.2;
constexpr double AP_POWER_FAR = 1.1;
constexpr double AP_WEIGHT_FLOOR = 0.2;
constexpr double AP_DENSE_RATIO = 2.0;
constexpr double AP_DENSE_TAU = 2.0;
constexpr int AP_MIN_STEPS = 4;          // atmosphere_ap marches at least this many steps
constexpr int AP_MAX_STEPS = 1 << 16;

struct ScatterLights
{
    Vec3   dir[2];                        // unit, z up: 0 the sun, 1 the moon
    Vec3   E[2];                          // irradiance above the atmosphere on a surface facing the light (linear): the one place E_sun and E_moon live
    double disc_sin[2] = { 0.0046, 0.0046 };
    int    count = 2;                     // lights marched (1: the moon is skipped); clamped to [0, 2]
};

struct LLScatter;

// Bruneton and Neyret's transmittance table T(h, mu), 256 x 64, built from LLAtmosphere::physical_transmittance (the twin of
// pa_transmittance, the cut at the slab and every kink: a 40-step midpoint march misses the 0.25 km slab on grazing rays and is
// off by 0.13). Holds the physical part only: the lights multiply the disc's visibility and the excess factor (light_T).
// The table is of the exact transmittance of the physical atmosphere (AT2-1's pass builds it with pa_transmittance, the cuts
// uploaded), and the multiple-scattering and sky irradiance tables are built from the table's lookups (build_ms_lut with t_lut bound).
struct TransmittanceLUT
{
    static constexpr int W = 256, H = 64;
    double rg = 0.0, rt = 0.0;
    std::vector<Vec3> texel;

    // (h, mu) -> (u, v) in [0, 1]^2: u the distance to the atmosphere's top between its minimum (Rt - r) and the horizon's, v the
    // square root of the horizon distance, sqrt(rho / H) (Bruneton and Neyret's is rho / H; the root puts the texels where the 0.25 km
    // slab is: linear in rho one texel is 14 km of horizon distance, 0.015 km of altitude, and a bilinear lookup in fog is off by 6e-3;
    // with the root 1.4e-3). A ray below the horizon has u > 1 (clamped by lookup). h is the altitude: r - Rg is never formed.
    static void uv_of(double rg, double rt, double h, double mu, double& u, double& v)
    {
        const double top = rt - rg, hh = std::sqrt(std::max(0.0, top * (rt + rg)));
        h = detail::clampd(h, 0.0, top);
        const double r = rg + h, rho = std::sqrt(h * (2.0 * rg + h));
        const double disc = (top - h) * (rt + r) + r * r * mu * mu; // Rt^2 - r^2 (1 - mu^2)
        const double d = std::max(0.0, -r * mu + std::sqrt(std::max(disc, 0.0)));
        const double dmin = top - h, dmax = rho + hh;
        u = (d - dmin) / std::max(dmax - dmin, 1e-6);
        v = hh > 0.0 ? std::sqrt(rho / hh) : 0.0;
    }
    static void rmu_of(double rg, double rt, double u, double v, double& h, double& mu)
    {
        const double top = rt - rg, h2 = std::max(0.0, top * (rt + rg)), hh = std::sqrt(h2);
        const double rho = hh * v * v, r = std::sqrt(rho * rho + rg * rg);
        h = rho * rho / (r + rg);
        const double dmin = top - h, dmax = rho + hh, d = dmin + u * (dmax - dmin);
        mu = d == 0.0 ? 1.0 : detail::clampd((h2 - rho * rho - d * d) / (2.0 * r * d), -1.0, 1.0);
    }

    // The texel's ray: mu at least a hair above the horizon's (the table covers the lit side; the cut-off is the lights')
    static void texel_ray(const LLAtmosphere& atm, int i, int j, double& h, double& mu)
    {
        rmu_of(atm.planet_radius_km, atm.top_radius_km, i / double(W - 1), j / double(H - 1), h, mu);
        mu = std::max(mu, atm.horizon_mu(h) + 1e-6);
    }

    void build(const LLAtmosphere& atm)
    {
        rg = atm.planet_radius_km;
        rt = atm.top_radius_km;
        texel.assign(W * H, Vec3{});
        for (int j = 0; j < H; ++j)
        {
            for (int i = 0; i < W; ++i)
            {
                double h, mu;
                texel_ray(atm, i, j, h, mu);
                texel[j * W + i] = atm.physical_transmittance(h, mu, 0.0);
            }
        }
    }

    Vec3 lookup(double h, double mu) const
    {
        double u, v;
        uv_of(rg, rt, h, mu, u, v);
        const double x = detail::clampd(u, 0.0, 1.0) * (W - 1), y = detail::clampd(v, 0.0, 1.0) * (H - 1);
        const int x0 = std::min(static_cast<int>(x), W - 2), y0 = std::min(static_cast<int>(y), H - 2);
        const double fx = x - x0, fy = y - y0;
        const Vec3 a = texel[y0 * W + x0] * (1.0 - fx) + texel[y0 * W + x0 + 1] * fx;
        const Vec3 b = texel[(y0 + 1) * W + x0] * (1.0 - fx) + texel[(y0 + 1) * W + x0 + 1] * fx;
        return a * (1.0 - fy) + b * fy;
    }
};

// A small 2D table with bilinear lookups on texel centres (the GLSL's half-texel convention), u and v in [0, 1].
struct Table2D
{
    int w = 0, h = 0;
    std::vector<Vec3> texel;
    void init(int w_, int h_) { w = std::max(w_, 2); h = std::max(h_, 2); texel.assign(static_cast<size_t>(w) * h, Vec3{}); }
    Vec3 lookup(double u, double v) const
    {
        if (texel.empty())
        {
            return {};
        }
        const double x = detail::clampd(u, 0.0, 1.0) * (w - 1), y = detail::clampd(v, 0.0, 1.0) * (h - 1);
        const int x0 = std::min(static_cast<int>(x), w - 2), y0 = std::min(static_cast<int>(y), h - 2);
        const double fx = x - x0, fy = y - y0;
        const Vec3 a = texel[y0 * w + x0] * (1.0 - fx) + texel[y0 * w + x0 + 1] * fx;
        const Vec3 b = texel[(y0 + 1) * w + x0] * (1.0 - fx) + texel[(y0 + 1) * w + x0 + 1] * fx;
        return a * (1.0 - fy) + b * fy;
    }
};

// The multiple-scattering table Psi(h, mu_s) (per unit irradiance): x = mu_s, y = h / (Rt - Rg)
struct MsLUT : Table2D
{
    double top_km = 0.0;
    static double mu_of(int i, int w) { return i / double(w - 1) * 2.0 - 1.0; }
    static double h_of(double top, int j, int h) { return std::max(j / double(h - 1), 1e-3) * top; }
    Vec3 at(double h_km, double mu_s) const { return lookup(mu_s * 0.5 + 0.5, h_km / top_km); }
};

// The sky irradiance table E(h, mu_s) (per unit irradiance of the sun): x = mu_s, y = sqrt(h / (Rt - Rg)): the light the sky
// (single scattering with the true phase + Psi) puts on a horizontal surface at altitude h; for the clouds, the probes' ambient.
struct SkyIrradianceLUT : Table2D
{
    double top_km = 0.0;
    static double h_of(double top, int j, int h) { const double v = j / double(h - 1); return top * v * v; }
    Vec3 at(double h_km, double mu_s) const { return lookup(mu_s * 0.5 + 0.5, std::sqrt(detail::clampd(h_km / top_km, 0.0, 1.0))); }
};

enum ScatterClamp : unsigned // which bounds applied (diagnostics, tests)
{
    SCLAMP_RAYLEIGH_ALBEDO = 1u << 0,  // blue_horizon / Default's left [0, max_rayleigh_albedo]
    SCLAMP_AEROSOL_ALBEDO  = 1u << 1,
    SCLAMP_MS_SCALE        = 1u << 2,
    SCLAMP_GLOW_LOBE       = 1u << 3,  // glow.x or glow.z was clamped into the usable lobes
    SCLAMP_PHASE_PEAK      = 1u << 4,  // the aerosol phase's peak is capped at max_phase
    SCLAMP_SUNLIGHT_SCALE  = 1u << 5,
    SCLAMP_MOON_GAIN       = 1u << 6,
    SCLAMP_SOURCE          = 1u << 7,  // the light source colour was left [0, 1]
    SCLAMP_NON_FINITE      = 1u << 8,
    SCLAMP_ANISOTROPY      = 1u << 9,
};

struct LLScatter
{
    LLAtmosphere atm;                   // the extinction (AT0), as a finalized atmosphere
    ScatterBounds bounds;
    Vec3   rayleigh_scat;               // 1/km at density 1 = albedo x rayleigh_ext
    Vec3   rayleigh_albedo;
    double aerosol_albedo = 0.0;
    double aerosol_scat = 0.0;          // 1/km: albedo x aerosol_ext
    double slab_scat = 0.0;             // 1/km: albedo x slab_ext
    Vec3   ms_scale = { 1.0, 1.0, 1.0 };
    double glow_x = 0.0, glow_z = 0.0, glow_norm = 0.0; // glow_norm 0: Cornette-Shanks (an atmosphere built by hand)
    double anisotropy = 0.8;
    double max_phase = 1e4;
    ScatterLights lights;               // E[0] is E_sun and E[1] E_moon: there is no second copy
    unsigned scatter_clamped = 0;       // bit mask of ScatterClamp

    Vec3 E_sun() const { return lights.E[0]; }
    Vec3 E_moon() const { return lights.E[1]; }

    // what the tables are read from; unset (null): the exact value (physical_transmittance, no table). For tests and the vectors.
    const TransmittanceLUT* t_lut = nullptr;
    const MsLUT* ms_lut = nullptr;
    const SkyIrradianceLUT* e_lut = nullptr;
    std::function<Vec3(double, double)> ms_override; // test hook: replaces the Psi lookup (h, mu_s); the GPU vectors use a smooth stand-in

    // The sky's scattering from the same legacy fields as the extinction. `moon_gain` is the moon's irradiance relative to the sun's
    // (AT1's rig.gain = moon_brightness x compensation).
    static LLScatter fromLegacy(const LegacySky& sky, const ScatterFields& f, double moon_gain, const AtmosphereBounds& ab = {},
                                const ScatterBounds& sb = {})
    {
        using detail::clampd;
        LLScatter s;
        s.bounds = sb;
        s.atm = LLAtmosphere::fromLegacy(sky, ab);
        unsigned c = 0;
        // NaN -> fallback; +-infinity -> the largest finite value of that sign (so the bound below it applies, not 0)
        auto fin = [&](double x, double fallback) {
            if (std::isnan(x))
            {
                c |= SCLAMP_NON_FINITE;
                return fallback;
            }
            if (std::isinf(x))
            {
                c |= SCLAMP_NON_FINITE;
                return x > 0.0 ? 1e30 : -1e30;
            }
            return x;
        };
        auto bounded = [&](double x, double lo, double hi, unsigned bit) {
            const double y = clampd(x, lo, hi);
            if (y != x)
            {
                c |= bit;
            }
            return y;
        };

        // 1. albedos: blue_horizon tints the Rayleigh albedo, haze_horizon the aerosol's; both relative to Default's, never above their bounds
        const double bh[3] = { fin(f.blue_horizon.x, 0.0), fin(f.blue_horizon.y, 0.0), fin(f.blue_horizon.z, 0.0) };
        const double rb[3] = { sb.ref_blue_horizon.x, sb.ref_blue_horizon.y, sb.ref_blue_horizon.z };
        double ar[3];
        for (int k = 0; k < 3; ++k)
        {
            ar[k] = bounded(bh[k] / rb[k], 0.0, sb.max_rayleigh_albedo, SCLAMP_RAYLEIGH_ALBEDO);
        }
        s.rayleigh_albedo = { ar[0], ar[1], ar[2] };
        s.rayleigh_scat = { ar[0] * s.atm.rayleigh_ext.x, ar[1] * s.atm.rayleigh_ext.y, ar[2] * s.atm.rayleigh_ext.z };
        s.aerosol_albedo = bounded(sb.aerosol_albedo_at_ref * fin(f.haze_horizon, 0.0) / sb.ref_haze_horizon, 0.0, sb.max_aerosol_albedo,
                                   SCLAMP_AEROSOL_ALBEDO);
        s.aerosol_scat = s.aerosol_albedo * s.atm.aerosol_ext;
        s.slab_scat = s.aerosol_albedo * s.atm.slab_ext;

        // 2. the multiple scattering's scale
        const double am[3] = { fin(f.ambient.x, 0.0), fin(f.ambient.y, 0.0), fin(f.ambient.z, 0.0) };
        s.ms_scale = { bounded(am[0] / sb.ref_ambient, 0.0, sb.max_ms_scale, SCLAMP_MS_SCALE),
                       bounded(am[1] / sb.ref_ambient, 0.0, sb.max_ms_scale, SCLAMP_MS_SCALE),
                       bounded(am[2] / sb.ref_ambient, 0.0, sb.max_ms_scale, SCLAMP_MS_SCALE) };

        // 3. the aerosol phase: the creator's glow lobe, normalised over the sphere; glow.x and glow.z are clamped into the usable lobes
        //    (a continuous, monotone clamp: the nearest usable lobe, never a switch to another phase function)
        s.max_phase = sb.max_phase;
        s.anisotropy = bounded(fin(f.mie_anisotropy, 0.8), -0.95, 0.95, SCLAMP_ANISOTROPY);
        s.glow_x = bounded(fin(f.glow_x, 0.0), sb.glow_x_min, sb.glow_x_max, SCLAMP_GLOW_LOBE);
        s.glow_z = bounded(fin(f.glow_z, 0.0), sb.glow_z_min, sb.glow_z_max, SCLAMP_GLOW_LOBE);
        s.glow_norm = 1.0 / glow_integral(s.glow_x, s.glow_z);
        if (s.phase_peak() > sb.max_phase) // the lobe peaks at cos = 1 (glow.z < 0) or at cos = -1 (glow.z > 0)
        {
            c |= SCLAMP_PHASE_PEAK;
        }

        // 4. the lights: E = 3 x scale x lin(source), the irradiance pbrBaseLight gives a sunlit surface; the moon's is the sun's x gain
        const double scale = bounded(fin(f.sky_sunlight_scale, 1.0), 0.0, sb.max_sunlight_scale, SCLAMP_SUNLIGHT_SCALE);
        const double src[3] = { bounded(fin(sky.source.x, 0.0), 0.0, 1.0, SCLAMP_SOURCE), bounded(fin(sky.source.y, 0.0), 0.0, 1.0, SCLAMP_SOURCE),
                                bounded(fin(sky.source.z, 0.0), 0.0, 1.0, SCLAMP_SOURCE) };
        s.lights.E[0] = srgb_to_linear(Vec3{ src[0], src[1], src[2] }) * (3.0 * scale);
        s.lights.E[1] = s.lights.E[0] * bounded(fin(moon_gain, 0.0), 0.0, sb.max_moon_gain, SCLAMP_MOON_GAIN);
        s.lights.disc_sin[0] = clampd(fin(f.sun_disc_sin, 0.0046), 1e-5, 0.2);
        s.lights.disc_sin[1] = clampd(fin(f.moon_disc_sin, 0.0046), 1e-5, 0.2);
        s.lights.dir[0] = { 0.0, 0.0, 1.0 };
        s.lights.dir[1] = { 0.0, 0.0, -1.0 };
        s.scatter_clamped = c;
        return s;
    }

    // the integral over the sphere of the glow lobe (max(1 - cos, 0.001) glow_x)^glow_z + 0.25, closed form
    static double glow_integral(double gx, double gz)
    {
        const double x0 = 0.001;
        double lobe = x0 * std::pow(x0 * gx, gz); // the part below x0, where the lobe is constant
        if (std::fabs(gz + 1.0) < 1e-9)
        {
            lobe += std::log(2.0 / x0) / gx; // gx^gz ln(2/x0) with gz = -1
        }
        else
        {
            lobe += std::pow(gx, gz) * (std::pow(2.0, gz + 1.0) - std::pow(x0, gz + 1.0)) / (gz + 1.0);
        }
        return 2.0 * detail::SC_PI * (lobe + 0.5);
    }

    double glow_phase_raw(double cos_angle) const
    {
        return std::pow(std::max(1.0 - cos_angle, 0.001) * glow_x, glow_z) + 0.25;
    }

    void set_light_dirs(const Vec3& sun_dir, const Vec3& moon_dir)
    {
        lights.dir[0] = detail::sane_dir(sun_dir, { 0.0, 0.0, 1.0 });
        lights.dir[1] = detail::sane_dir(moon_dir, { 0.0, 0.0, -1.0 });
    }

    // --- phase functions (1 / sr) ---
    static double phase_r(double c) { return 3.0 / (16.0 * detail::SC_PI) * (1.0 + c * c); }
    double phase_m_uncapped(double c) const
    {
        if (glow_norm > 0.0)
        {
            return glow_phase_raw(c) * glow_norm;
        }
        const double g = anisotropy, g2 = g * g;
        return 3.0 / (8.0 * detail::SC_PI) * ((1.0 - g2) * (1.0 + c * c)) / ((2.0 + g2) * std::pow(std::max(1.0 + g2 - 2.0 * g * c, 1e-4), 1.5));
    }
    double phase_m(double c) const { return std::min(phase_m_uncapped(c), max_phase); }
    double phase_peak() const { return std::max(phase_m_uncapped(1.0), phase_m_uncapped(-1.0)); }

    // --- the medium at altitude h_km: scattering (Rayleigh, aerosol + slab) and extinction, per km ---
    void medium(double h_km, Vec3& s_r, double& s_m, Vec3& ext) const
    {
        h_km = std::max(h_km, 0.0);
        const double dr = atm.rayleigh.density(h_km * 1e3), dm = atm.mie.density(h_km * 1e3), doz = atm.absorption.density(h_km * 1e3);
        const double dh = atm.slab_top_km > 0.0 ? 1.0 - smoothstep(0.75 * atm.slab_top_km, atm.slab_top_km, h_km) : 0.0;
        s_r = rayleigh_scat * dr;
        s_m = aerosol_scat * dm + slab_scat * dh;
        const double grey = atm.aerosol_ext * dm + atm.slab_ext * dh;
        ext = atm.rayleigh_ext * dr + Vec3{ grey, grey, grey } + atm.ozone_ext * doz;
    }

    // transmittance from the point x (at altitude h_km) to the top of the atmosphere towards light i, 0 behind the planet (soft over the
    // disc), the excess factor of the capped columns included. From the table when t_lut is set, else exact.
    Vec3 light_T(const Vec3& x, double h_km, const ScatterLights& lt, int i) const
    {
        const double r = detail::len(x), mu = detail::dot(x, lt.dir[i]) / r;
        if (!t_lut)
        {
            return atm.transmittance(h_km, mu, lt.disc_sin[i], 1.0);
        }
        const double mu_h = atm.horizon_mu(h_km), d = lt.disc_sin[i];
        const double vis = smoothstep(mu_h - d, mu_h + d, mu);
        if (!(vis > 0.0))
        {
            return {};
        }
        Vec3 t = t_lut->lookup(h_km, mu) * vis;
        if (atm.excess.x > 0.0 || atm.excess.y > 0.0 || atm.excess.z > 0.0)
        {
            const double air = 1.0 / std::max(std::min(mu, 1.0) - mu_h, 1e-6);
            t = t * Vec3{ std::exp(-atm.excess.x * air), std::exp(-atm.excess.y * air), std::exp(-atm.excess.z * air) };
        }
        return t;
    }

    // Psi(h, mu_s). PRECONDITION of every march (atmosphere_ap, sky_radiance, sky_irradiance_texel): the multiple-scattering table is bound
    // (ms_lut, built by build_ms_lut) or ms_override is set. fromLegacy() leaves both unset, and an unbound march would silently leave the
    // multiple scattering out, so it is enforced: an assertion (llassert in the viewer, assert elsewhere) and, where assertions are compiled
    // out, a warning once on stderr; the march then has no multiple scattering. A caller that wants single scattering only says so with
    // use_single_scattering_only(). There is no on-the-fly exact Psi (a texel is 1300 light transmittances).
    Vec3 ms_at(double h_km, double mu_s) const
    {
        if (ms_override)
        {
            return ms_override(h_km, mu_s);
        }
        if (!ms_lut)
        {
            LL_ATMOS_REQUIRE(false && "LLScatter: bind ms_lut (build_ms_lut) before a march, or call use_single_scattering_only()");
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                std::fprintf(stderr, "LLScatter: a march without a bound multiple-scattering table leaves the multiple scattering out\n");
            }
            return {};
        }
        return ms_lut->at(h_km, mu_s);
    }

    // the documented opt-out of the precondition above: marches then have the single scattering only (the reference of a test, not a sky)
    void use_single_scattering_only() { ms_override = [](double, double) { return Vec3{}; }; }

    // --- geometry from the observer's height h0 (the observer is at (0, 0, Rg + h0)), in altitudes ---

    // the point at path length t along d: its position (for the directions), its radius and its altitude
    struct PathPoint
    {
        Vec3 x;
        double h;
    };
    PathPoint path_point(double h0, const Vec3& d, double t) const
    {
        const double rg = atm.planet_radius_km;
        const double q0 = h0 * (2.0 * rg + h0), b = (rg + h0) * d.z;
        const double q = std::max(q0 + t * (2.0 * b + t), -rg * rg * 0.999999);
        const double r = std::sqrt(rg * rg + q);
        return { { d.x * t, d.y * t, rg + h0 + d.z * t }, q / (r + rg) };
    }

    // distance along d from the observer to the sphere of altitude `alt`: the first hit ahead, else the far one, -1 if none
    double ray_alt(double h0, const Vec3& d, double alt) const
    {
        const double rg = atm.planet_radius_km;
        double t0, t1;
        if (!detail::quad_roots((rg + h0) * d.z, (h0 - alt) * (2.0 * rg + h0 + alt), t0, t1))
        {
            return -1.0;
        }
        return t0 > 0.0 ? t0 : (t1 > 0.0 ? t1 : -1.0);
    }

    // The distance to the planet's ground along d from the observer, -1 if the ray does not meet it. An observer on the ground (h0 = 0)
    // looking below its horizon is AT the ground: 0, not the far side of the chord through the planet.
    double ground_distance(double h0, const Vec3& d) const
    {
        if (!(h0 > 0.0))
        {
            return d.z < 0.0 ? 0.0 : -1.0;
        }
        return ray_alt(h0, d, 0.0);
    }

    // the distance to the planet's ground along d from the observer, else to the top of the atmosphere
    double ray_length(double h0, const Vec3& d) const
    {
        const double tg = ground_distance(h0, d);
        return tg >= 0.0 ? tg : std::max(ray_alt(h0, d, atm.top_radius_km - atm.planet_radius_km), 0.0);
    }

    // distance along d to the slab's top (the sphere of altitude slab_top_km), the first crossing ahead in (0, tmax), else -1
    double slab_cross(double h0, const Vec3& d, double tmax) const
    {
        if (!(atm.slab_top_km > 0.0))
        {
            return -1.0;
        }
        const double rg = atm.planet_radius_km, H = atm.slab_top_km;
        double t0, t1;
        if (!detail::quad_roots((rg + h0) * d.z, (h0 - H) * (2.0 * rg + h0 + H), t0, t1))
        {
            return -1.0;
        }
        const double t = t0 > 1e-4 ? t0 : t1;
        return (t > 1e-4 && t < tmax) ? t : -1.0;
    }

    static double spacing(double a, double power, bool dense_at_end)
    {
        const double x = dense_at_end ? 1.0 - a : a;
        const double y = power == 1.0 ? x : (power == 2.0 ? x * x : std::pow(x, power));
        return dense_at_end ? 1.0 - y : y;
    }

    // --- the march: from the observer's height h0 along d for t in [t0, t1] in n steps (step i ends at (i / n)^power of the segment: 1
    //     uniform, 2 quadratic, dense at its start, or at its end when dense_at_end), midpoints; accumulates in-scattered radiance and the transmittance of the segment. The one
    //     march of the sky view, the froxels, the far table and the vectors.
    void march_seg(double h0, const Vec3& d, double t0, double t1, int n, double power, bool dense_at_end, const ScatterLights& lt, Vec3& Lin, Vec3& T) const
    {
        const double len = t1 - t0;
        if (n <= 0 || !(len > 0.0))
        {
            return;
        }
        const int nl = std::min(std::max(lt.count, 0), 2);
        double pr[2], pm[2];
        for (int i = 0; i < nl; ++i)
        {
            const double c = detail::dot(d, lt.dir[i]);
            pr[i] = phase_r(c);
            pm[i] = phase_m(c);
        }
        double prev = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double a = double(i + 1) / double(n), am = (double(i) + 0.5) / double(n);
            // step edges at a^power from t0 (dense at the start) or, mirrored, at 1 - (1 - a)^power (dense at the end)
            const double u = spacing(a, power, dense_at_end), um = spacing(am, power, dense_at_end);
            const double dt = (u - prev) * len, t = t0 + um * len;
            prev = u;
            const PathPoint p = path_point(h0, d, t);
            const Vec3 up = p.x * (1.0 / detail::len(p.x));
            Vec3 s_r, ext;
            double s_m;
            medium(p.h, s_r, s_m, ext);
            const Vec3 s_tot = s_r + Vec3{ s_m, s_m, s_m };
            Vec3 S;
            for (int l = 0; l < nl; ++l)
            {
                const Vec3 single = light_T(p.x, p.h, lt, l) * (s_r * pr[l] + Vec3{ s_m * pm[l], s_m * pm[l], s_m * pm[l] });
                S += lt.E[l] * (single + ms_at(p.h, detail::dot(up, lt.dir[l])) * s_tot * ms_scale);
            }
            const Vec3 Ts = detail::vexp(ext * -dt);
            const Vec3 e = { std::max(ext.x, 1e-7), std::max(ext.y, 1e-7), std::max(ext.z, 1e-7) };
            const Vec3 w = { (S.x - S.x * Ts.x) / e.x, (S.y - S.y * Ts.y) / e.y, (S.z - S.z * Ts.z) / e.z };
            Lin += T * w;
            T = T * Ts;
        }
    }

    // A part [a, b] of a ray: its optical depth (green channel, Simpson's rule on the medium's extinction) and which end the steps crowd
    // to. The steps crowd to the end b when the air there is more than AP_DENSE_RATIO times as dense as at a and the part is thin
    // (tau < AP_DENSE_TAU: the radiance comes from where the air is); in a thick part the radiance comes from where the light still is,
    // the start, and the steps crowd there.
    struct RaySegment
    {
        double tau;
        bool dense_at_end;
    };
    RaySegment ray_segment(double h0, const Vec3& d, double a, double b) const
    {
        auto ext_y = [&](double t) {
            Vec3 s_r, ext;
            double s_m;
            medium(path_point(h0, d, t).h, s_r, s_m, ext);
            return ext.y;
        };
        const double ea = ext_y(a), eb = ext_y(b);
        const double tau = (ea + 4.0 * ext_y(0.5 * (a + b)) + eb) / 6.0 * (b - a);
        return { tau, eb > AP_DENSE_RATIO * ea && tau < AP_DENSE_TAU };
    }

    // atmosphereAP (the GLSL's name): the in-scattered radiance and transmittance from an observer at h_obs_km along d (unit, z up) for
    // dist_km, in `steps` steps (at least AP_MIN_STEPS). The march is split where the ray leaves the 0.25 km haze slab (a uniform march
    // skips the slab: it is off by up to 99 % at 40 km in fog) into two segments, or is one segment when it never crosses the slab's
    // top. In each segment the steps are spaced as (i / n)^power from the start, or the end if that is the dense one (ray_segment): power
    // AP_POWER_NEAR in the first segment, AP_POWER_FAR in the second (or the only) one. The steps are shared between the two segments by
    // the radiance each can contribute, with a floor: with w1 = 1 - e^-tau1 and w2 = e^-tau1 (1 - e^-tau2) the first segment gets
    // steps (w1 + c) / (w1 + w2 + 2c), c = AP_WEIGHT_FLOOR, at least 2 and at most steps - 2.
    // 12 steps meet 1 % of a converged march over the domain doc/light/atmosphere-scattering-core.md names (observer 0 to 6 km, every
    // elevation from -2 to 90 degrees, 2 to 40 km, ending below 12 km), not beyond it.
    void atmosphere_ap(const Vec3& d_in, double dist_km, double h_obs_km, int steps, const ScatterLights& lt, Vec3& Lin, Vec3& T) const
    {
        Lin = {};
        T = { 1.0, 1.0, 1.0 };
        const Vec3 d = detail::sane_dir(d_in, { 0.0, 0.0, 1.0 });
        const double h0 = detail::clampd(h_obs_km, 0.0, atm.top_radius_km - atm.planet_radius_km);
        const double dist = detail::clampd(dist_km, 0.0, 1e4);
        steps = std::min(std::max(steps, AP_MIN_STEPS), AP_MAX_STEPS);
        const double tc = slab_cross(h0, d, dist);
        if (tc < 0.0)
        {
            march_seg(h0, d, 0.0, dist, steps, AP_POWER_FAR, ray_segment(h0, d, 0.0, dist).dense_at_end, lt, Lin, T);
            return;
        }
        const RaySegment s1 = ray_segment(h0, d, 0.0, tc), s2 = ray_segment(h0, d, tc, dist);
        const double w1 = 1.0 - std::exp(-s1.tau), w2 = std::exp(-s1.tau) * (1.0 - std::exp(-s2.tau));
        const int n0 = std::min(std::max(static_cast<int>(steps * (w1 + AP_WEIGHT_FLOOR) / (w1 + w2 + 2.0 * AP_WEIGHT_FLOOR) + 0.5), 2), steps - 2);
        march_seg(h0, d, 0.0, tc, n0, AP_POWER_NEAR, s1.dense_at_end, lt, Lin, T);
        march_seg(h0, d, tc, dist, steps - n0, AP_POWER_FAR, s2.dense_at_end, lt, Lin, T);
    }
    void atmosphere_ap(const Vec3& d, double dist_km, double h_obs_km, int steps, Vec3& Lin, Vec3& T) const
    {
        atmosphere_ap(d, dist_km, h_obs_km, steps, lights, Lin, T);
    }

    // --- the multiple-scattering texel (h, mu_s) per unit irradiance (Hillaire 2020): the second-order radiance L2 of a uniformly
    //     emitting medium over n x n directions (n_steps quadratic steps each) and its transfer f_ms, summed as L2 / (1 - min(f_ms, max_f_ms)). Rays from the
    //     ground that go below the horizon end at the ground at once (ground_distance).
    Vec3 ms_texel(double h_km, double mu_s, int n_dirs = MS_DIRS_DEFAULT, int n_steps = MS_STEPS_DEFAULT, double* f_ms_out = nullptr) const
    {
        using detail::SC_PI;
        n_dirs = std::max(n_dirs, 1);
        n_steps = std::max(n_steps, 1);
        const double h0 = detail::clampd(h_km, 0.0, atm.top_radius_km - atm.planet_radius_km);
        mu_s = detail::clampd(mu_s, -1.0, 1.0);
        ScatterLights lt;
        lt.dir[0] = { std::sqrt(std::max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s };
        lt.disc_sin[0] = lights.disc_sin[0];
        const double pu = 1.0 / (4.0 * SC_PI);
        Vec3 L2, fms;
        for (int i = 0; i < n_dirs; ++i)
        {
            for (int j = 0; j < n_dirs; ++j)
            {
                const double ct = 1.0 - 2.0 * (j + 0.5) / n_dirs, st = std::sqrt(1.0 - ct * ct), ph = 2.0 * SC_PI * (i + 0.5) / n_dirs;
                const Vec3 d = { st * std::cos(ph), st * std::sin(ph), ct };
                const double tg = ground_distance(h0, d);
                const double tmax = tg >= 0.0 ? tg : std::max(ray_alt(h0, d, atm.top_radius_km - atm.planet_radius_km), 0.0);
                Vec3 T = { 1.0, 1.0, 1.0 }, L, Fi;
                double prev = 0.0;
                for (int k = 0; k < n_steps; ++k)
                {
                    // quadratic steps: the chord to the top of a near-horizontal ray is a thousand kilometres, a uniform step of it 50
                    const double a = double(k + 1) / n_steps, am = (k + 0.5) / n_steps;
                    const double dt = (a * a - prev) * tmax;
                    prev = a * a;
                    const PathPoint p = path_point(h0, d, am * am * tmax);
                    Vec3 s_r, ext;
                    double s_m;
                    medium(p.h, s_r, s_m, ext);
                    const Vec3 s = s_r + Vec3{ s_m, s_m, s_m };
                    const Vec3 Ts = detail::vexp(ext * -dt);
                    const Vec3 w = { (1.0 - Ts.x) / std::max(ext.x, 1e-7), (1.0 - Ts.y) / std::max(ext.y, 1e-7), (1.0 - Ts.z) / std::max(ext.z, 1e-7) };
                    L += T * s * light_T(p.x, p.h, lt, 0) * w * pu;
                    Fi += T * s * w;
                    T = T * Ts;
                }
                if (tg >= 0.0)
                {
                    const PathPoint p = path_point(h0, d, tg);
                    const Vec3 n = p.x * (1.0 / detail::len(p.x));
                    L += T * light_T(p.x, 0.0, lt, 0) * (std::max(detail::dot(n, lt.dir[0]), 0.0) * bounds.ground_albedo / SC_PI);
                }
                L2 += L;
                fms += Fi * pu;
            }
        }
        const double inv = 1.0 / (double(n_dirs) * n_dirs);
        L2 = L2 * inv;
        fms = fms * (4.0 * SC_PI * inv);
        if (f_ms_out)
        {
            *f_ms_out = detail::vmax(fms);
        }
        const double m = bounds.max_f_ms;
        return { L2.x / (1.0 - std::min(fms.x, m)), L2.y / (1.0 - std::min(fms.y, m)), L2.z / (1.0 - std::min(fms.z, m)) };
    }

    // --- the sky irradiance texel E(h, mu_s) per unit irradiance of the sun on a horizontal surface at altitude h: pi x the mean
    //     radiance over n x n cosine-weighted directions of the upper hemisphere (single scattering with the true phase, plus Psi),
    //     each in `steps` quadratic steps to the top of the atmosphere.
    Vec3 sky_irradiance_texel(double h_km, double mu_s, int n_dirs = 8, int steps = 16) const
    {
        using detail::SC_PI;
        n_dirs = std::max(n_dirs, 1);
        const double h0 = detail::clampd(h_km, 0.0, atm.top_radius_km - atm.planet_radius_km);
        mu_s = detail::clampd(mu_s, -1.0, 1.0);
        ScatterLights lt;
        lt.dir[0] = { std::sqrt(std::max(0.0, 1.0 - mu_s * mu_s)), 0.0, mu_s };
        lt.E[0] = { 1.0, 1.0, 1.0 };
        lt.disc_sin[0] = lights.disc_sin[0];
        lt.count = 1;
        Vec3 sum;
        for (int i = 0; i < n_dirs; ++i)
        {
            for (int j = 0; j < n_dirs; ++j)
            {
                const double ct = std::sqrt(1.0 - (j + 0.5) / n_dirs), st = std::sqrt((j + 0.5) / n_dirs), ph = 2.0 * SC_PI * (i + 0.5) / n_dirs;
                const Vec3 d = { st * std::cos(ph), st * std::sin(ph), ct };
                Vec3 Lin, T = { 1.0, 1.0, 1.0 };
                march_seg(h0, d, 0.0, std::max(ray_alt(h0, d, atm.top_radius_km - atm.planet_radius_km), 0.0), steps, 2.0, false, lt, Lin, T);
                sum += Lin;
            }
        }
        return sum * (SC_PI / (double(n_dirs) * n_dirs));
    }

    // --- tables ---
    void build_t_lut(TransmittanceLUT& out) const { out.build(atm); }

    // the Psi table, w x h texels (the pass is 32 x 32), from the exact or the bound transmittance (AT2-1 builds it from the table)
    void build_ms_lut(MsLUT& out, int w = 32, int h = 32, int n_dirs = MS_DIRS_DEFAULT, int n_steps = MS_STEPS_DEFAULT) const
    {
        out.init(w, h);
        out.top_km = atm.top_radius_km - atm.planet_radius_km;
        for (int j = 0; j < out.h; ++j)
        {
            for (int i = 0; i < out.w; ++i)
            {
                out.texel[static_cast<size_t>(j) * out.w + i] = ms_texel(MsLUT::h_of(out.top_km, j, out.h), MsLUT::mu_of(i, out.w), n_dirs, n_steps);
            }
        }
    }

    void build_e_lut(SkyIrradianceLUT& out, int w = 64, int h = 16, int n_dirs = 8, int steps = 16) const
    {
        out.init(w, h);
        out.top_km = atm.top_radius_km - atm.planet_radius_km;
        for (int j = 0; j < out.h; ++j)
        {
            for (int i = 0; i < out.w; ++i)
            {
                out.texel[static_cast<size_t>(j) * out.w + i] = sky_irradiance_texel(SkyIrradianceLUT::h_of(out.top_km, j, out.h), MsLUT::mu_of(i, out.w), n_dirs, steps);
            }
        }
    }

    // --- the sky: radiance along d (unit, z up) from an observer at h_obs_km, `steps` uniform midpoint steps to the ground or the top
    //     (the sky-view LUT's texel), plus the ground at ground_albedo where the ray meets it, lit by the lights' T and the sky
    //     irradiance E(0, mu) of the e_lut (or the exact texel).
    Vec3 sky_radiance(const Vec3& d_in, double h_obs_km, int steps = 32) const
    {
        const Vec3 d = detail::sane_dir(d_in, { 0.0, 0.0, 1.0 });
        const double h0 = detail::clampd(h_obs_km, 0.0, atm.top_radius_km - atm.planet_radius_km);
        steps = std::min(std::max(steps, 1), AP_MAX_STEPS);
        const double tg = ground_distance(h0, d);
        const double tmax = ray_length(h0, d);
        Vec3 Lin, T = { 1.0, 1.0, 1.0 };
        march_seg(h0, d, 0.0, tmax, steps, 1.0, false, lights, Lin, T);
        if (tg >= 0.0)
        {
            const PathPoint p = path_point(h0, d, tg);
            const Vec3 n = p.x * (1.0 / detail::len(p.x));
            Vec3 E;
            const int nl = std::min(std::max(lights.count, 0), 2);
            for (int l = 0; l < nl; ++l)
            {
                const double mu = detail::dot(n, lights.dir[l]);
                const Vec3 sky_e = e_lut ? e_lut->at(0.0, mu) : sky_irradiance_texel(0.0, mu);
                E += lights.E[l] * (light_T(p.x, 0.0, lights, l) * std::max(mu, 0.0) + sky_e);
            }
            Lin += T * E * (bounds.ground_albedo / detail::SC_PI);
        }
        return Lin;
    }
};
} // namespace LLAtmosphericLight

#endif // LL_LLATMOSPHERICLIGHT_H
