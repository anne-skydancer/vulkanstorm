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
    double swing = 0.30;   // half-width of the direction/colour swing, degrees of sun elevation: all sun at +swing, all moon at -swing (RenderLightHandoverSwing, 0.05 .. 5)
    double fade  = 0.30;   // width of each shadow fade outside the swing, degrees: the sun's from swing + fade to swing, the moon's
                           // from -swing to -swing - fade                                   (RenderLightHandoverFade, 0 .. 5)
    double horizon_dip_deg = 0.0; // the swing's centre is at -this degrees of sun elevation instead of 0 (the cloud call sets it to the
                                  // cloud layer's horizon dip; the shadow fades are not shifted). 0 for the ground.
    double dark  = 1e-4;   // the moon is available when its light (before the compensation) reaches dark x luminance(source); smooth from 0
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

// the dip of the horizon seen from height h above a sphere of radius R, degrees: acos(R / (R + h)); 0 for a bad or non-positive input
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

    // The swing and the shadow fades follow the sun's elevation (a moon's share of the light would go 0 -> 1 within +-0.1 degree, because
    // the sun's light collapses at the horizon): the band weight is a smoothstep from all sun at +swing to all moon at -swing. The moon only takes over as
    // far as it is available (up and bright enough): with no usable moon the light keeps the sun's direction (nothing swings to a moon that is
    // not there) and fades to dark with the sun's transmittance, while the dome and the clouds blend as at night (band_weight).
    constexpr double RAD2DEG = 180.0 / 3.14159265358979323846;
    const double elev = std::asin(std::min(std::max(sun_z, -1.0), 1.0)) * RAD2DEG;
    const double swing = std::min(std::max(finite_or(p.swing, 0.3), 0.05), 5.0);
    const double fade = std::min(std::max(finite_or(p.fade, 0.3), 0.0), 5.0);
    // the moon's availability: its light before the compensation against the dark threshold (the disc's visibility is in moon_T, so a
    // moon rising or setting fades smoothly), and its gain against 1 % of the physical moon (a sky with moon_brightness 0 has no
    // moon; 0.01 is the smallest moon_brightness that still counts)
    const double e_m_raw = luminance({ src.x * moon_T.x, src.y * moon_T.y, src.z * moon_T.z });
    const double avail = (p.dark * y_src > 0.0 ? smoothstep(0.0, p.dark * y_src, e_m_raw) : 0.0)
                       * smoothstep(0.0, 0.01 * MOON_PHYSICAL, gain);
    const double dip = std::min(std::max(finite_or(p.horizon_dip_deg, 0.0), 0.0), 20.0);
    const double band = smoothstep(swing, -swing, elev + dip); // the band is centred on -dip
    const double t = band * avail;                             // direction and colour
    // the sun's shadow 1 -> 0 from swing + fade to swing; the moon's 0 -> 1 from -swing to -swing - fade, scaled by its availability
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
    PrimaryLight light;        // direction, ground transmittance (moon gain included), shadow strength, moon weight, band weight, moon availability
    Vec3   cloud_T;            // the sun's transmittance at the cloud layer (the clouds' sun term; the moon's half is not used)
    Vec3   cloud_dir;          // the clouds' own light direction (the glow near the light), unit, z up
    double cloud_moon_weight = 0.0; // the clouds' own band weight: primary_light() on the cloud-layer transmittances, the band centred on -dip
    Vec3   color;              // the ground light in sunlight_color's encoding: sRGB(lin(source) x light.T), not clamped
    double gain = 0.0;         // moon_brightness x moon_compensation
    double light_scale = 1.0;  // mix(sun_scale, moon_scale, moon weight t)
    double haze_glow_factor = 1.0;
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

    Vec3 sun_T, moon_T, sun_Tc, moon_Tc;
    if (s.physical)
    {
        const LLAtmosphere atm = LLAtmosphere::fromLegacy(s.sky, s.bounds);
        const double h_cloud = std::min(std::max(finite_or(s.cloud_height_km, 1.5), 0.0), 20.0);
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
    // the cloud layer's horizon dip, acos(R / (R + h)): the clouds' band is centred on -dip instead of 0 (the ground's shadow fades stay)
    PrimaryLightParams cparams = s.params;
    {
        const double R = std::min(std::max(finite_or(s.sky.planet_radius_km, 6360.0), 1000.0), 100000.0); // as the atmosphere clamps it
        cparams.horizon_dip_deg = horizon_dip_degrees(R, std::min(std::max(finite_or(s.cloud_height_km, 1.5), 0.0), 20.0));
    }
    const PrimaryLight cl = primary_light(cin, cparams);
    rig.cloud_moon_weight = cl.band_weight; // the clouds' sun term ends with the band (as at night before), the glow direction follows cl.dir
    rig.cloud_dir = cl.dir;
    rig.cloud_T = { std::min(std::max(finite_or(sun_Tc.x, 0.0), 0.0), 1.0), std::min(std::max(finite_or(sun_Tc.y, 0.0), 0.0), 1.0),
                    std::min(std::max(finite_or(sun_Tc.z, 0.0), 0.0), 1.0) };
    rig.light_scale = (1.0 - t) * std::min(std::max(finite_or(s.sun_scale, 1.0), 0.0), 100.0)
                    + t * std::min(std::max(finite_or(s.moon_scale, 1.0), 0.0), 100.0);

    // the dome's glow: the sun's, handing over to the moon's with the band (a quarter of its brightness, once the moon is up): 1 above the
    // band exactly, 0 below it when there is no moon

    rig.haze_glow_factor = (1.0 - tb) + tb * mb * 0.25 * smoothstep(-moon_disc, moon_disc, moon_z);

    const Vec3 ground = { in.source.x * rig.light.T.x, in.source.y * rig.light.T.y, in.source.z * rig.light.T.z };
    rig.color = linear_to_srgb(ground);
    rig.shadow_visible = rig.light.shadow_strength > 1e-3 && luminance(ground) > 1e-4 * luminance(in.source);
    return rig;
}
} // namespace LLAtmosphericLight

#endif // LL_LLATMOSPHERICLIGHT_H
