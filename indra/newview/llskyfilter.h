// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llskyfilter.h
 * @brief Stage AT2-1: the EEP filter's tables, renderer-free. The C++ twin of skyFilterF.glsl and the test oracle.
 *
 * Each EEP sky is a filter on the physical sky (doc/light/atmosphere-study.md section 8, decided 2026-11-17): the sky you see is today's
 * look exactly; the light the sky casts is physical in level and takes the creator's tint. skyV.glsl's haze colour is linear in its glow
 * term (colour = A + B haze_glow), so two tables hold today's look (variant c of doc/light/at2-qualification-phase1.md: 749 of 759
 * library skies within dE 5 of the reference, 735 within dE 2): A, the colour at haze_glow 0, and B, its slope; a draw evaluates the
 * glow exactly and takes skyF.glsl's x2 and clamp 5 after the lookup. The cast-light level k is the base's irradiance over the filtered
 * sky's, on the sky-view grid.
 *
 * A and B depend on the direction of skyV's rel_pos only through its elevation (rel_pos is scaled along its own direction, and lightnorm
 * enters through rel_pos_norm.y; the azimuth reaches the colour through the glow alone, which the draw evaluates). So they are one
 * column, 1 x 108: row j at the elevation of the sky-view grid's row j, Hillaire's latitude (rows crowd at the horizon), the rows the
 * phase-1 qualification interpolated between. AT2-2 reads them with normalize(rel_pos).y. The grid, in the dome's frame (y up, the
 * frame of lightnorm and sun_glow_norm): the base's sky view (atmosphereTablesF.glsl), 192 x 108, azimuth over [-pi, pi] from +x
 * towards +z.
 *
 * The arithmetic is skyV.glsl's (the Vulkanstorm L3 version: the sun's light hands over to the moon's with sun_moon_blend, the sun's glow
 * stays at the sun and fades with the band), in double. scripts/light/gen_sky_filter_vectors.py runs skyV.glsl itself and
 * skyFilterF.glsl on the GPU; indra/newview/tests/llskyfilter_test.cpp holds this twin to both.
 */

#ifndef LL_LLSKYFILTER_H
#define LL_LLSKYFILTER_H

#include "llatmosphericlight.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>

namespace LLSkyFilter
{
using LLAtmosphericLight::Vec3;

constexpr int TABLE_W = 192;   // the sky-view grid (atmosphereTablesF.glsl, skyFilterF.glsl)
constexpr int TABLE_H = 108;
constexpr double PI = 3.14159265358979323846;

// skyV.glsl's uniforms, as the sky's shader group gets them (LLSettingsVOSky::applyToUniforms, applySpecial), and the lighting pass's
// decode of a sky pixel (softenLightF.glsl). Directions in the dome's frame (y up).
struct Inputs
{
    Vec3   lightnorm;                  // getClampedLightNorm(): z clamped at -0.1 in the sky's frame, not renormalised; A and B read its y only
    Vec3   sunlight_color;             // raw psky->getSunlightColor() (up to 3): SG_SKY's value overrides SG_ANY's auto-adjusted one
    Vec3   moonlight_color;
    Vec3   ambient_color;
    Vec3   blue_horizon;
    Vec3   blue_density;
    double haze_horizon = 0.0;
    double haze_density = 0.0;
    double cloud_shadow = 0.0;
    double density_multiplier = 0.0;
    double max_y = 0.0;
    Vec3   glow;                       // glow.x and glow.z are used
    double sun_moon_blend = 0.0;       // the L3 band weight
    Vec3   sun_glow_norm;              // getClampedSunNorm(), not renormalised
    double sky_hdr_scale = 1.0;
    int    classic_mode = 0;
};

// The viewer's: the values the dome's program gets, read from LLEnvironment's uniform lists in the order the program receives them
// (llskyfilter.cpp; not in the standalone tests). False while a value is missing (no sky yet). They are the main view's only outside a
// reflection probe's irradiance pass, so LLAtmosphereLUTs reads them nowhere else.
bool current_inputs(Inputs& in);

// the dome's frame (x, y up, z) to LLSettingsSky's (z up): the inverse of LLEnvironment::toLightNorm
inline Vec3 dome_to_sky(const Vec3& d)
{
    return { d.z, d.x, d.y };
}

inline Vec3 sky_to_dome(const Vec3& s)
{
    return { s.y, s.z, s.x };
}

// the latitude of row j (fractional rows allowed): v = 2 j / (h - 1) - 1, l = sign(v) v^2 pi / 2
inline double latitude(double j)
{
    const double v = j / double(TABLE_H - 1) * 2.0 - 1.0;
    return (v < 0.0 ? -1.0 : (v > 0.0 ? 1.0 : 0.0)) * v * v * 0.5 * PI;
}

inline double azimuth(double i)
{
    return (i / double(TABLE_W - 1) * 2.0 - 1.0) * PI;
}

// texel (i, j)'s direction in the dome's frame
inline Vec3 sky_view_dir(int i, int j)
{
    const double el = latitude(j), az = azimuth(i);
    return { std::cos(el) * std::cos(az), std::sin(el), std::cos(el) * std::sin(az) };
}

// the direction A and B's row j is evaluated along (skyFilterF.glsl pass 1): the row's elevation, towards +x (any azimuth gives the same)
inline Vec3 row_dir(int j)
{
    const double el = latitude(j);
    return { std::cos(el), std::sin(el), 0.0 };
}

// a direction (dome's frame, any length) to the table's (u, v) in [0, 1] on texel centres (the lookup's coordinates before the half-texel
// shift of pa_lut_uv): u = (atan2(z, x) + pi) / 2 pi, v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi / 2))
inline void sky_view_uv(const Vec3& d, double& u, double& v)
{
    const double len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
    const double y = len > 0.0 ? std::min(std::max(d.y / len, -1.0), 1.0) : 1.0;
    const double el = std::asin(y);
    u = (std::atan2(d.z, d.x) + PI) / (2.0 * PI);
    v = 0.5 + 0.5 * (el < 0.0 ? -1.0 : 1.0) * std::sqrt(std::fabs(el) / (0.5 * PI));
}

namespace detail
{
inline Vec3 vmul(const Vec3& a, const Vec3& b) { return { a.x * b.x, a.y * b.y, a.z * b.z }; }
inline Vec3 vadd(const Vec3& a, const Vec3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vec3 vsub(const Vec3& a, const Vec3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vec3 vscale(const Vec3& a, double s) { return { a.x * s, a.y * s, a.z * s }; }
inline bool finite3(const Vec3& a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }

// skyV.glsl up to the haze colour, for rel_pos along d: the attenuated sunlight, the weights and the haze transparency
struct Terms
{
    Vec3 sunlight;      // after the off-axis attenuation
    Vec3 kb, kh;        // blue_horizon x blue_weight, haze_horizon x haze_weight
    Vec3 ch;            // combined_haze = exp(-combined x density_dist)
    Vec3 rel_pos_norm;
};

inline Terms terms(const Inputs& in, const Vec3& d)
{
    Terms t;
    Vec3 rel_pos = d;
    if (rel_pos.y > 0.0)
    {
        rel_pos = vscale(rel_pos, in.max_y / rel_pos.y);
    }
    if (rel_pos.y < 0.0)
    {
        rel_pos = vscale(rel_pos, -32000.0 / rel_pos.y);
    }
    const double len = std::sqrt(rel_pos.x * rel_pos.x + rel_pos.y * rel_pos.y + rel_pos.z * rel_pos.z);
    t.rel_pos_norm = vscale(rel_pos, 1.0 / len); // NaN for a collapsed rel_pos (max_y 0), as normalize(0) in the shader
    const double sb = in.sun_moon_blend;
    Vec3 sunlight = vadd(vscale(in.sunlight_color, 1.0 - sb), vscale(in.moonlight_color, 0.7 * sb)); // mix(sun, moon x 0.7, blend)
    const double bd[3] = { in.blue_density.x, in.blue_density.y, in.blue_density.z };
    double atten[3], comb[3], bw[3], hw[3];
    for (int c = 0; c < 3; ++c)
    {
        atten[c] = (bd[c] + in.haze_density * 0.25) * (in.density_multiplier * in.max_y);
        comb[c] = std::max(std::fabs(bd[c]) + std::fabs(in.haze_density), 1e-6);
        bw[c] = bd[c] / comb[c];
        hw[c] = in.haze_density / comb[c];
    }
    const double off_axis = 1.0 / std::max(1e-6, std::max(0.0, t.rel_pos_norm.y) + in.lightnorm.y);
    t.sunlight = { sunlight.x * std::exp(-atten[0] * off_axis), sunlight.y * std::exp(-atten[1] * off_axis), sunlight.z * std::exp(-atten[2] * off_axis) };
    const double density_dist = len * in.density_multiplier;
    t.ch = { std::exp(-comb[0] * density_dist), std::exp(-comb[1] * density_dist), std::exp(-comb[2] * density_dist) };
    t.kb = { in.blue_horizon.x * bw[0], in.blue_horizon.y * bw[1], in.blue_horizon.z * bw[2] };
    t.kh = { in.haze_horizon * hw[0], in.haze_horizon * hw[1], in.haze_horizon * hw[2] };
    return t;
}
} // namespace detail

// skyV.glsl's haze colour (vary_HazeColor) for the direction d of rel_pos, with its glow term haze_glow given: the reference the
// factored form is held to
inline Vec3 haze_color(const Inputs& in, const Vec3& d, double haze_glow)
{
    using namespace detail;
    const Terms t = terms(in, d);
    const Vec3 one = { 1.0, 1.0, 1.0 };
    Vec3 color = vadd(vmul(t.kb, vadd(t.sunlight, in.ambient_color)), vmul(t.kh, vadd(vscale(t.sunlight, haze_glow), in.ambient_color)));
    color = vmul(color, vsub(one, t.ch));
    const Vec3 ambient = { in.ambient_color.x + std::max(0.0, 1.0 - in.ambient_color.x) * in.cloud_shadow * 0.5,
                           in.ambient_color.y + std::max(0.0, 1.0 - in.ambient_color.y) * in.cloud_shadow * 0.5,
                           in.ambient_color.z + std::max(0.0, 1.0 - in.ambient_color.z) * in.cloud_shadow * 0.5 };
    const Vec3 sun_below = vscale(t.sunlight, std::max(0.0, 1.0 - in.cloud_shadow));
    const Vec3 below = vadd(vmul(t.kb, vadd(sun_below, ambient)), vmul(t.kh, vadd(vscale(sun_below, haze_glow), ambient)));
    const Vec3 ch2 = { std::sqrt(t.ch.x), std::sqrt(t.ch.y), std::sqrt(t.ch.z) };
    return vadd(color, vmul(vsub(below, color), Vec3{ 1.0 - std::sqrt(ch2.x), 1.0 - std::sqrt(ch2.y), 1.0 - std::sqrt(ch2.z) }));
}

// A and B for the direction d (skyFilterF.glsl's sf_haze_ab): colour = A + B haze_glow; both 0 where the colour is not finite (max_y 0).
// They depend on d through d.y only (the tables' row j is haze_ab along row_dir(j)).
inline void haze_ab(const Inputs& in, const Vec3& d, Vec3& A, Vec3& B)
{
    using namespace detail;
    const Terms t = terms(in, d);
    const Vec3 one = { 1.0, 1.0, 1.0 };
    const Vec3 transp = vsub(one, t.ch);
    const Vec3 above_a = vmul(vadd(vmul(t.kb, vadd(t.sunlight, in.ambient_color)), vmul(t.kh, in.ambient_color)), transp);
    const Vec3 above_b = vmul(vmul(t.kh, t.sunlight), transp);
    const Vec3 ambient = { in.ambient_color.x + std::max(0.0, 1.0 - in.ambient_color.x) * in.cloud_shadow * 0.5,
                           in.ambient_color.y + std::max(0.0, 1.0 - in.ambient_color.y) * in.cloud_shadow * 0.5,
                           in.ambient_color.z + std::max(0.0, 1.0 - in.ambient_color.z) * in.cloud_shadow * 0.5 };
    const Vec3 sun_below = vscale(t.sunlight, std::max(0.0, 1.0 - in.cloud_shadow));
    const Vec3 below_a = vadd(vmul(t.kb, vadd(sun_below, ambient)), vmul(t.kh, ambient));
    const Vec3 below_b = vmul(t.kh, sun_below);
    const Vec3 f = { 1.0 - std::sqrt(std::sqrt(t.ch.x)), 1.0 - std::sqrt(std::sqrt(t.ch.y)), 1.0 - std::sqrt(std::sqrt(t.ch.z)) };
    A = vadd(above_a, vmul(vsub(below_a, above_a), f));
    B = vadd(above_b, vmul(vsub(below_b, above_b), f));
    if (!finite3(A) || !finite3(B))
    {
        A = {};
        B = {};
    }
}

// skyV.glsl's haze_glow for the direction d: the sun's glow at the sun, fading with the band
inline double haze_glow(const Inputs& in, const Vec3& d)
{
    const double c = d.x * in.sun_glow_norm.x + d.y * in.sun_glow_norm.y + d.z * in.sun_glow_norm.z;
    const double g = std::max(1.0 - c, 0.001) * in.glow.x;
    return (1.0 - in.sun_moon_blend) * (std::pow(g, in.glow.z) + 0.25);
}

// srgbF.glsl's srgb_to_linear (no clamp: values above 1 follow the power law)
inline double srgb_to_linear_unclamped(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

// the filtered sky W in the lighting pass's linear units: skyF's x2 and clamp 5, softenLightF's decode, the classic scale
inline Vec3 look(const Inputs& in, const Vec3& A, const Vec3& B, double hg)
{
    const double scale = in.sky_hdr_scale * (in.classic_mode > 0 ? 1.1 : 1.0);
    auto ch = [&](double a, double b) { return srgb_to_linear_unclamped(std::min(std::max((a + b * hg) * 2.0, 0.0), 5.0)) * scale; };
    return { ch(A.x, B.x), ch(A.y, B.y), ch(A.z, B.z) };
}

inline double lum(const Vec3& c)
{
    return c.x * 0.2126 + c.y * 0.7152 + c.z * 0.0722;
}

// The cast-light level: the cosine-weighted luminance of the physical sky P over that of the filtered sky W on the grid's upper
// hemisphere, both on the same weights (skyFilterF.glsl passes 2 and 3). P(i, j) is the base's sky view at texel (i, j); W at texel
// (i, j) is row j's A and B with the glow of the texel's direction.
//
// k is defined (the light the sky casts is k x W, AT2-2) only where both skies cast light: W's irradiance above IRR_MIN and P's above
// K_MIN of it, both numbers. Elsewhere the decided rule applies, today's cast light where physics has none (study section 8): a
// creator's sky that is black, and with the clear-day base a night with the moon down. The base's twilight falls as a real one does (to
// about 5e-5 of its -2 degree level by -10 degrees) and on below a real night sky's own light, which it lacks (2e-7 by -15, 3e-8 by -18;
// llatmospherebase_test.cpp), so under a lit night sky k leaves K_MIN late in twilight. AT2-2 sets how the cast light hands over from
// k x W to today's as k falls; the pass gives it irr_p, irr_w and k for that.
struct CastLight
{
    double irr_p = 0.0;
    double irr_w = 0.0;
    double k = 0.0;
    bool defined = false;
};

constexpr double IRR_MIN = 1e-6;   // W casts light (the lighting pass's linear units, cosine-weighted luminance)
constexpr double K_MIN = 1e-6;     // P casts light: more than a millionth of W's

inline bool cast_light_defined(double irr_p, double irr_w)
{
    return !std::isnan(irr_p) && !std::isnan(irr_w) && irr_w > IRR_MIN && irr_p > K_MIN * irr_w;
}

inline double row_weight(int j)
{
    const double el = latitude(j), lo = latitude(j - 1), hi = j + 1 < TABLE_H ? latitude(j + 1) : el;
    return std::sin(std::max(el, 0.0)) * std::cos(el) * 0.5 * (hi - lo) * (2.0 * PI / double(TABLE_W - 1));
}

inline CastLight cast_light_level(const Inputs& in, const std::function<Vec3(int, int)>& P)
{
    CastLight out;
    for (int j = TABLE_H / 2; j < TABLE_H; ++j)
    {
        const double w = row_weight(j);
        Vec3 A, B;
        haze_ab(in, row_dir(j), A, B);
        double sp = 0.0, sw = 0.0;
        for (int i = 0; i < TABLE_W - 1; ++i)
        {
            sp += lum(P(i, j));
            sw += lum(look(in, A, B, haze_glow(in, sky_view_dir(i, j))));
        }
        out.irr_p += sp * w;
        out.irr_w += sw * w;
    }
    out.defined = cast_light_defined(out.irr_p, out.irr_w);
    out.k = out.defined ? out.irr_p / out.irr_w : 0.0;
    return out;
}

// --- the change tests (64-bit FNV-1a over the inputs).
//   table_key: the inputs A and B read, exactly (their bits), so the sky's own tables are always the current sky's. A and B read the
//     light through off_axis = 1 / (max(0, rel_pos_norm.y) + lightnorm.y), whose slope 1 / (rel_pos_norm.y + lightnorm.y)^2 has no
//     useful bound towards the horizon: quantised at 1e-4, the light's y still moved a thin, bright sky near the sun by 11 display steps
//     at sunset ([TOR] DUSK - Fairytale glow, extinction 0.008 against Default's 0.18). They are one 1 x 108 pass, so they follow their
//     inputs as they are; the glow, the sun's glow direction and the lighting pass's decode are not theirs (the draw evaluates them).
//   inputs_key: every input quantised (colours and scalars to 1e-3 of their size, 1e-4 below that size; lightnorm.y, the only part of it
//     anything reads, and sun_glow_norm to 1e-3): the cast-light level k, a hemisphere integral, follows it.
inline int64_t quantise(double x, double step)
{
    if (!std::isfinite(x))
    {
        return std::isnan(x) ? INT64_MIN : (x > 0.0 ? INT64_MAX : INT64_MIN + 1);
    }
    const double q = std::round(x / step);
    return q > 9e18 ? INT64_MAX : (q < -9e18 ? INT64_MIN + 2 : static_cast<int64_t>(q));
}

class KeyHasher
{
public:
    void add(int64_t v)
    {
        for (int i = 0; i < 8; ++i)
        {
            mHash ^= static_cast<uint64_t>(v >> (8 * i)) & 0xffu;
            mHash *= 1099511628211ull;
        }
    }
    // relative steps of 1e-3 (on a log scale) above 1e-4 in size, absolute steps of 1e-4 below
    void scalar(double x)
    {
        if (!(std::fabs(x) > 1e-4))
        {
            add(0);
            add(quantise(x, 1e-4));
            return;
        }
        add(x < 0.0 ? -1 : 1);
        add(quantise(std::log(std::fabs(x)), 1e-3));
    }
    void colour(const Vec3& c) { scalar(c.x); scalar(c.y); scalar(c.z); }
    void direction(const Vec3& d) { add(quantise(d.x, 1e-3)); add(quantise(d.y, 1e-3)); add(quantise(d.z, 1e-3)); }
    void exact(double x)
    {
        int64_t bits;
        std::memcpy(&bits, &x, sizeof bits);
        add(bits);
    }
    void exact(const Vec3& v) { exact(v.x); exact(v.y); exact(v.z); }
    uint64_t value() const { return mHash; }

private:
    uint64_t mHash = 1469598103934665603ull;
};

inline uint64_t table_key(const Inputs& in)
{
    KeyHasher h;
    h.exact(in.lightnorm.y);
    h.exact(in.sunlight_color);
    h.exact(in.moonlight_color);
    h.exact(in.ambient_color);
    h.exact(in.blue_horizon);
    h.exact(in.blue_density);
    h.exact(in.haze_horizon);
    h.exact(in.haze_density);
    h.exact(in.cloud_shadow);
    h.exact(in.density_multiplier);
    h.exact(in.max_y);
    h.exact(in.sun_moon_blend);
    return h.value();
}

inline uint64_t inputs_key(const Inputs& in)
{
    KeyHasher h;
    h.add(quantise(in.lightnorm.y, 1e-3));
    h.colour(in.sunlight_color);
    h.colour(in.moonlight_color);
    h.colour(in.ambient_color);
    h.colour(in.blue_horizon);
    h.colour(in.blue_density);
    h.scalar(in.haze_horizon);
    h.scalar(in.haze_density);
    h.scalar(in.cloud_shadow);
    h.scalar(in.density_multiplier);
    h.scalar(in.max_y);
    h.colour(in.glow);
    h.scalar(in.sun_moon_blend);
    h.direction(in.sun_glow_norm);
    h.scalar(in.sky_hdr_scale);
    h.add(in.classic_mode);
    return h.value();
}
} // namespace LLSkyFilter

#endif // LL_LLSKYFILTER_H
