// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llatmospherebase.h
 * @brief The physical base sky of stage AT2-1: one clear high-pressure day under every EEP sky, and the std140 mirror of the
 *        `Atmosphere` uniform block the table passes read. Renderer-free.
 *
 * The owner's model (2026-11-18), the clear-day base: the physical render first, then post-production filters. The base sky is a
 * clear, dry high-pressure-dome day (clean air, minimal aerosol, long visibility); every EEP sky, the built-in ones included, is a filter
 * on it (doc/light/atmosphere-study.md section 8: the sky you see is today's look exactly, the light the sky casts is physical in level
 * with the creator's tint). So the physics is one atmosphere, not a translation per EEP: its transmittance, multiple-scattering and sky
 * irradiance tables are built once, and only the sky view follows the lights. The sun's light on the ground stays AT1's
 * (LLEnvironment::updatePrimaryLight: the EEP's noon sunlight per channel with a physical airmass), the sun's part of the filter; it
 * goes through the EEP's own atmosphere (LLAtmosphere::fromLegacy), not through this one, so in a hazy sky the ground sun is dimmer than
 * the base's (an open point for the owner, section 8 of the study).
 *
 * The base (constants, no settings; sources in the comments):
 *   - Rayleigh scattering at 680 / 550 / 440 nm: 5.802, 13.558, 33.1 x 1e-3 /km at sea level, scale height 8 km (E. Bruneton, "A
 *     Qualitative and Quantitative Evaluation of 8 Clear Sky Models", IEEE TVCG 2017, and its reference implementation; S. Hillaire,
 *     "A Scalable and Production Ready Sky and Atmosphere Rendering Technique", EGSR 2020);
 *   - ozone absorption 0.650, 1.881, 0.085 x 1e-3 /km at the layer's peak, Bruneton's tent from 10 to 40 km, peak at 25 km (same);
 *   - aerosol: a clean continental background, optical depth 0.03 at 550 nm, scale height 1.2 km, single-scattering albedo 0.95,
 *     Cornette-Shanks phase with g = 0.75 (typical clean-background ranges: optical depth 0.03 to 0.08). The AT2-0 core's aerosol is
 *     grey, one extinction for the three channels: a clean-continental Angstrom exponent of 1.3 would move the 440 and 680 nm optical
 *     depths by about +0.01 and -0.007, below the accuracy of what reads the base in AT2-1 (the cast-light level, a luminance);
 *   - no ground haze slab: the subsidence inversion caps a dry, clean boundary layer;
 *   - the visibility that follows, Koschmieder's 3.912 / (beta_Rayleigh + beta_aerosol) at 550 nm on the ground: about 101 km;
 *   - ground albedo 0.1 (Bruneton); the multiple scattering as Hillaire's (scale 1, series bound 0.6);
 *   - Earth's radii, 6360 and 6420 km;
 *   - the sun's and the moon's discs physical, 0.265 degrees in angular radius (AT1's disc at scale 1; Bruneton's precomputed model
 *     likewise fixes the sun's angular radius in its atmosphere): the EEP's sun and moon scales size the discs you see, the filter's, and
 *     the base's tables never follow them.
 */

#ifndef LL_LLATMOSPHEREBASE_H
#define LL_LLATMOSPHEREBASE_H

#include "llatmosphericlight.h"

#include <cstddef>
#include <cstring>

namespace LLAtmosphericLight
{
namespace ClearDay
{
constexpr double RAYLEIGH_EXT[3] = { 5.802e-3, 13.558e-3, 33.1e-3 };   // 1/km at sea level, 680 / 550 / 440 nm
constexpr double OZONE_EXT[3] = { 0.650e-3, 1.881e-3, 0.085e-3 };      // 1/km at the layer's peak
constexpr double AEROSOL_OPTICAL_DEPTH = 0.03;                          // the vertical column at 550 nm
constexpr double AEROSOL_SCALE_HEIGHT_M = 1200.0;
constexpr double AEROSOL_ALBEDO = 0.95;
constexpr double AEROSOL_ASYMMETRY = 0.75;
constexpr double GROUND_ALBEDO = 0.1;
constexpr double PLANET_RADIUS_KM = 6360.0;
constexpr double TOP_RADIUS_KM = 6420.0;
} // namespace ClearDay

// The base's extinction: Rayleigh, the clean aerosol and ozone on the stock profiles, no slab, finalized (its cut altitudes computed).
inline LLAtmosphere clear_day_atmosphere()
{
    LLAtmosphere a;
    a.planet_radius_km = ClearDay::PLANET_RADIUS_KM;
    a.top_radius_km = ClearDay::TOP_RADIUS_KM;
    a.rayleigh = rayleigh_profile();
    a.mie = mie_profile();
    a.mie.layer[0].exp_scale = -1.0 / ClearDay::AEROSOL_SCALE_HEIGHT_M;
    a.absorption = ozone_profile();
    const double top = a.top_radius_km - a.planet_radius_km;
    a.rayleigh_ext = { ClearDay::RAYLEIGH_EXT[0], ClearDay::RAYLEIGH_EXT[1], ClearDay::RAYLEIGH_EXT[2] };
    a.ozone_ext = { ClearDay::OZONE_EXT[0], ClearDay::OZONE_EXT[1], ClearDay::OZONE_EXT[2] };
    // the extinction at density 1 that gives the aerosol column its optical depth over the profile's own column
    a.aerosol_ext = ClearDay::AEROSOL_OPTICAL_DEPTH / detail::profile_column_km(a.mie, top);
    a.slab_ext = 0.0;
    a.slab_top_km = 0.0;
    a.excess = {};
    const double he_r = detail::profile_column_km(a.rayleigh, top), he_o = detail::profile_column_km(a.absorption, top);
    a.column = { a.rayleigh_ext.x * he_r + ClearDay::AEROSOL_OPTICAL_DEPTH + a.ozone_ext.x * he_o,
                 a.rayleigh_ext.y * he_r + ClearDay::AEROSOL_OPTICAL_DEPTH + a.ozone_ext.y * he_o,
                 a.rayleigh_ext.z * he_r + ClearDay::AEROSOL_OPTICAL_DEPTH + a.ozone_ext.z * he_o };
    a.clamped = 0;
    a.finalize();
    return a;
}

// The base's scattering. The lights are the caller's (set_clear_day_lights): the sky's own sun colour and sunlight scale, as the ground
// light has them, through this clear atmosphere (AT1's ground light goes through the EEP's own; see the header).
inline LLScatter clear_day_scatter()
{
    LLScatter s;
    s.atm = clear_day_atmosphere();
    s.bounds.ground_albedo = ClearDay::GROUND_ALBEDO;
    s.rayleigh_albedo = { 1.0, 1.0, 1.0 };
    s.rayleigh_scat = s.atm.rayleigh_ext;
    s.aerosol_albedo = ClearDay::AEROSOL_ALBEDO;
    s.aerosol_scat = ClearDay::AEROSOL_ALBEDO * s.atm.aerosol_ext;
    s.slab_scat = 0.0;
    s.ms_scale = { 1.0, 1.0, 1.0 };
    s.glow_x = 0.0;
    s.glow_z = 0.0;
    s.glow_norm = 0.0; // Cornette-Shanks with the anisotropy below, not a creator's glow lobe
    s.anisotropy = ClearDay::AEROSOL_ASYMMETRY;
    s.max_phase = s.bounds.max_phase;
    s.scatter_clamped = 0;
    return s;
}

// The lights of the base: the sky's own sun (`source`, sunlight_color as the deferred pass receives it: normalised to a maximum of 1),
// E = 3 x scale x lin(source) as LLScatter::fromLegacy; the moon E_sun x AT1's gain (moon_brightness x the moon's compensation), not
// marched when that is 0; both discs physical (body_disc_sin(1), 0.265 degrees: the tables built once hold for every EEP); directions
// unit, z up (LLSettingsSky's frame). Returns the bounds that applied (ScatterClamp bits).
inline unsigned set_clear_day_lights(LLScatter& s, const Vec3& sun_dir, const Vec3& moon_dir, const Vec3& source, double sunlight_scale,
                                     double moon_gain)
{
    unsigned clamped = 0;
    const double disc = body_disc_sin(1.0);
    s.lights = LLScatter::lights_of(source, sunlight_scale, moon_gain, disc, disc, s.bounds, clamped);
    s.lights.count = detail::vmax(s.lights.E[1]) > 0.0 ? 2 : 1;
    s.set_light_dirs(sun_dir, moon_dir);
    return clamped;
}

// Koschmieder's visibility on the ground at 550 nm, km: 3.912 / extinction (the 2 % contrast threshold)
inline double ground_visibility_km(const LLAtmosphere& a)
{
    const double ext = a.rayleigh_ext.y * a.rayleigh.density(0.0) + a.aerosol_ext * a.mie.density(0.0) + a.slab_ext + a.ozone_ext.y * a.absorption.density(0.0);
    return ext > 0.0 ? 3.912 / ext : 1e30;
}

// ---------------------------------------------------------------------------------------------------
// The `Atmosphere` uniform block (std140), the mirror of atmosphereCommonF.glsl's declaration. Floats only, every member at its std140
// offset (static_asserts below); scripts/light/gen_atmosphere_scatter_vectors.py reads the offsets from the driver and the test checks
// them against these. Filled by fill_atmosphere_block; uploaded once per frame when it changed (LLAtmosphereLUTs).
// ---------------------------------------------------------------------------------------------------
struct AtmosphereBlock
{
    struct Profile               // PADensityProfile: 3 x vec4
    {
        float layer0[4];         // width (m), exp_term, exp_scale (1/m), linear (1/m)
        float layer1[4];
        float constants[4];      // layer 0's constant, layer 1's, the number of layers, 0
    };
    // PAAtmosphere pa_atm
    float rayleigh_ext[3];
    float aerosol_ext;
    float ozone_ext[3];
    float slab_ext;
    float excess[3];
    float slab_top_km;
    float planet_radius_km;
    float top_radius_km;
    float cut_count;
    float unused0;
    Profile rayleigh;
    Profile mie;
    Profile absorption;
    // PALights pa_lights: std140 rounds every array element up to a vec4
    float light_dir[2][4];
    float light_E[2][4];
    float light_disc[2][4];
    int   light_count;
    int   unused1[3];
    float rayleigh_scat[3];
    float aerosol_scat;
    float ms_scale[3];
    float slab_scat;
    float glow_phase[3];
    float aniso;
    float max_phase;
    float f_ms_max;
    float ground_albedo;
    float unused2;               // std140 pads the cuts' vec4 array to 384 (AT2-3 gives the froxels their observer per view)
    float cuts[detail::MAX_ALTITUDES];
};

static_assert(sizeof(float) == 4 && sizeof(int) == 4, "std140 mirror");
static_assert(offsetof(AtmosphereBlock, aerosol_ext) == 12 && offsetof(AtmosphereBlock, ozone_ext) == 16 && offsetof(AtmosphereBlock, slab_ext) == 28, "std140: pa_atm");
static_assert(offsetof(AtmosphereBlock, excess) == 32 && offsetof(AtmosphereBlock, slab_top_km) == 44, "std140: pa_atm");
static_assert(offsetof(AtmosphereBlock, planet_radius_km) == 48 && offsetof(AtmosphereBlock, cut_count) == 56, "std140: pa_atm");
static_assert(offsetof(AtmosphereBlock, rayleigh) == 64 && offsetof(AtmosphereBlock, mie) == 112 && offsetof(AtmosphereBlock, absorption) == 160, "std140: profiles");
static_assert(offsetof(AtmosphereBlock, light_dir) == 208 && offsetof(AtmosphereBlock, light_E) == 240 && offsetof(AtmosphereBlock, light_disc) == 272, "std140: pa_lights");
static_assert(offsetof(AtmosphereBlock, light_count) == 304 && offsetof(AtmosphereBlock, rayleigh_scat) == 320, "std140: pa_lights");
static_assert(offsetof(AtmosphereBlock, aerosol_scat) == 332 && offsetof(AtmosphereBlock, ms_scale) == 336 && offsetof(AtmosphereBlock, slab_scat) == 348, "std140: scattering");
static_assert(offsetof(AtmosphereBlock, glow_phase) == 352 && offsetof(AtmosphereBlock, aniso) == 364 && offsetof(AtmosphereBlock, max_phase) == 368, "std140: scattering");
static_assert(offsetof(AtmosphereBlock, unused2) == 380 && offsetof(AtmosphereBlock, cuts) == 384 && sizeof(AtmosphereBlock) == 512, "std140: cuts");

namespace detail
{
inline void put_profile(AtmosphereBlock::Profile& out, const DensityProfile& p)
{
    std::memset(&out, 0, sizeof(out));
    const DensityLayer& a = p.layer[0];
    const DensityLayer& b = p.layer[p.count > 1 ? 1 : 0];
    out.layer0[0] = float(a.width_m);
    out.layer0[1] = float(a.exp_term);
    out.layer0[2] = float(a.exp_scale);
    out.layer0[3] = float(a.linear);
    out.layer1[0] = float(b.width_m);
    out.layer1[1] = float(b.exp_term);
    out.layer1[2] = float(b.exp_scale);
    out.layer1[3] = float(b.linear);
    out.constants[0] = float(a.constant);
    out.constants[1] = float(b.constant);
    out.constants[2] = float(p.count > 1 ? 2 : 1);
}

inline void put3(float* out, const Vec3& v)
{
    out[0] = float(v.x);
    out[1] = float(v.y);
    out[2] = float(v.z);
}
} // namespace detail

// The block for a finalized atmosphere's scattering `s`: nothing in it depends on the camera or the view. The cut altitudes are the C++'s
// (LLAtmosphere::finalize), not searched per texel: an atmosphere that was not finalized has them computed here.
inline AtmosphereBlock fill_atmosphere_block(const LLScatter& s)
{
    AtmosphereBlock b;
    std::memset(&b, 0, sizeof(b));
    const LLAtmosphere& a = s.atm;
    detail::put3(b.rayleigh_ext, a.rayleigh_ext);
    b.aerosol_ext = float(a.aerosol_ext);
    detail::put3(b.ozone_ext, a.ozone_ext);
    b.slab_ext = float(a.slab_ext);
    detail::put3(b.excess, a.excess);
    b.slab_top_km = float(a.slab_top_km);
    b.planet_radius_km = float(a.planet_radius_km);
    b.top_radius_km = float(a.top_radius_km);
    double cuts[detail::MAX_ALTITUDES];
    int n = a.cut_count;
    const double* src = a.cuts_km;
    if (n < 0)
    {
        n = a.cut_altitudes_km(cuts, detail::MAX_ALTITUDES);
        src = cuts;
    }
    n = std::min(std::max(n, 0), detail::MAX_ALTITUDES);
    for (int i = 0; i < n; ++i)
    {
        b.cuts[i] = float(src[i]);
    }
    b.cut_count = float(n);
    detail::put_profile(b.rayleigh, a.rayleigh);
    detail::put_profile(b.mie, a.mie);
    detail::put_profile(b.absorption, a.absorption);
    for (int i = 0; i < 2; ++i)
    {
        detail::put3(b.light_dir[i], s.lights.dir[i]);
        detail::put3(b.light_E[i], s.lights.E[i]);
        b.light_disc[i][0] = float(s.lights.disc_sin[i]);
    }
    b.light_count = std::min(std::max(s.lights.count, 0), 2);
    detail::put3(b.rayleigh_scat, s.rayleigh_scat);
    b.aerosol_scat = float(s.aerosol_scat);
    detail::put3(b.ms_scale, s.ms_scale);
    b.slab_scat = float(s.slab_scat);
    b.glow_phase[0] = float(s.glow_x);
    b.glow_phase[1] = float(s.glow_z);
    b.glow_phase[2] = float(s.glow_norm);
    b.aniso = float(s.anisotropy);
    b.max_phase = float(s.max_phase);
    b.f_ms_max = float(s.bounds.max_f_ms);
    b.ground_albedo = float(s.bounds.ground_albedo);
    return b;
}
} // namespace LLAtmosphericLight

#endif // LL_LLATMOSPHEREBASE_H
