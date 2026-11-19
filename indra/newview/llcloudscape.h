// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llcloudscape.h
 * @brief The cloud kind and variability layer of the volumetric clouds (stage L7 of the light study,
 *        doc/light/volumetric-clouds.md). Renderer-free.
 *
 * The stack (owner, 2026-11-18): the physical clear base sky, then the automatically generated volumetric clouds,
 * then this layer, then the EEP atmospheric filter. This layer decides, from the EEP cloud sliders alone, which
 * kinds of cloud (WMO genera) stand at which heights and how they vary:
 *
 * - up to three bands, sorted by height, each with a genus and its physical height range, height profile,
 *   extinction, edge erosion and noise scale;
 * - the primary band carries the creator's own cloud pattern (the EEP cloud texture, as the flat layer draws it,
 *   cloudsF.glsl's alpha1), so the volumetric clouds stand where the flat clouds stand. Its height class (etage)
 *   follows the apparent size of its elements, the WMO's criterion for Sc, Ac and Cc (International Cloud Atlas,
 *   2017: elements wider than 5 degrees Sc, 1 to 5 Ac, under 1 Cc): a small cloud_scale lifts the layer;
 * - Coverage picks the type in the band (the owner's Aurora ladder: cumulus -> stratocumulus -> stratus; aloft
 *   altocumulus -> altostratus, cirrocumulus -> cirrostratus); Density D sets how far heaps grow (humilis and
 *   mediocris, congestus, cumulonimbus with an anvil; sheets thicken to nimbostratus);
 * - two generated bands join it: altocumulus cells when Detail D is high, and cirrus (a veil, cirrostratus, as it
 *   thickens) with cloud_variance, the flat layer's streaks;
 * - variability: a mean-preserving modulation of each band's cover by a mesoscale patch field and by a slow
 *   growth and decay field (cells live about 20 minutes), and a wind shear: higher bands drift faster;
 * - the light the clouds get: the flat layer's own terms (cloudsV.glsl), the creator's look (the EEP filter on the
 *   clouds' light), linear for the volumetric trace.
 *
 * Extinction per genus (km^-1 at 550 nm, at the noise's density 1): the OPAC cloud models (Hess, Koepke and Schult
 * 1998, BAMS 79, Table 1a and 1b) through beta = 3 L / (2 rho r_eff) (geometric optics, Q_ext = 2; Stephens 1978),
 * scaled by one fill factor that keeps the owner's tuned cumulus of the Aurora Viewer (SIGMA 110 km^-1).
 *
 * Heights are above the camera's ground (the sky is fixed for the observer, #439), temperate latitudes.
 * Doubles throughout; indra/newview/tests/llcloudscape_test.cpp checks it. The GLSL side is
 * app_settings/shaders/class1/deferred/cloudsVolWeatherF.glsl and cloudsVolFuncsF.glsl.
 */

#ifndef LL_LLCLOUDSCAPE_H
#define LL_LLCLOUDSCAPE_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace LLCloudscape
{
constexpr int MAX_BANDS = 3;

// Two offsets place a band: the weather map's, wrapped in the map's own period (it is periodic), and the noises',
// wrapped in WRAP_KM, a multiple of every noise period the trace reads (the base noise 10.5 km and its halves and
// quarters, the erosion noise 0.5 km, the curl noise 3 km), so that neither wrap shows. 1680 km keeps a float's step
// under 0.1 m.
constexpr double WRAP_KM = 1680.0;
constexpr double BASE_KM = 10.5;     // the base noise's tile across (the Aurora Viewer's clouds.wgsl BASE_KM)
// LLSettingsSky::DOME_RADIUS (llcloudshadow.h): the flat layer's texture repeats every 2 R cloud_scale.
constexpr double DOME_RADIUS_KM = 15.0;

enum Genus : int
{
    NONE = 0,
    STRATUS,
    STRATOCUMULUS,
    CUMULUS,           // humilis, mediocris
    CUMULUS_CONGESTUS, // genus cumulus, species congestus: the towers
    CUMULONIMBUS,
    NIMBOSTRATUS,
    ALTOCUMULUS,
    ALTOSTRATUS,
    CIRROCUMULUS,
    CIRROSTRATUS,
    CIRRUS,
    GENUS_COUNT
};

inline const char* genus_name(Genus g)
{
    static const char* NAMES[GENUS_COUNT] = { "none", "St", "Sc", "Cu", "Cu con", "Cb", "Ns", "Ac", "As", "Cc", "Cs", "Ci" };
    return (g >= NONE && g < GENUS_COUNT) ? NAMES[g] : "?";
}

// The EEP cloud sliders (panel_settings_sky_clouds.xml ranges).
struct Sliders
{
    double coverage = 0.27; // cloud_shadow, 0..1
    double scale = 0.42;    // cloud_scale, 0.01..3
    double variance = 0.0;  // cloud_variance, 0..1
    double density = 1.0;   // cloud_pos_density1.z, "Density D", 0..3
    double detail = 0.12;   // cloud_pos_density2.z, "Detail D", 0..1
};

// The structural constants, as debug settings (RenderCloudscape*, settings.xml); these are their defaults.
struct Tuning
{
    double middle_scale = 0.15;        // below this cloud_scale the creator's layer stands in the middle etage (Ac, As)
    double high_scale = 0.05;          // below this, in the high etage (Cc, Cs)
    double congestus_density = 1.0;    // Density D from which heaps grow into congestus (sheets thicken)
    double cumulonimbus_density = 2.0; // Density D from which heaps become cumulonimbus
    double altocumulus_detail = 0.25;  // Detail D from which a middle band of altocumulus forms
    double altocumulus_cover = 0.6;    // its cover at Detail D 1
    double cirrus_base = 0.0;          // the cirrus band's cover at cloud_variance 0
    double cirrus_variance = 0.8;      // and per unit of cloud_variance
    double patch_base = 0.3;           // the patch field's amplitude at cloud_variance 0
    double patch_variance = 0.4;       // and per unit of cloud_variance
    double growth = 0.3;               // the growth and decay field's amplitude
    double lifetime_s = 1200.0;        // a cell's life (fair-weather cumulus live 10-30 min)
    double shear_height_km = 4.0;      // the wind's height scale: wind(h) ~ 1 + h / shear_height_km
};

struct Band
{
    Genus genus = NONE;
    int channel = 0;          // the weather map's channel the band reads: 0 the creator's pattern, 1 middle, 2 high
    double base_km = 0.0;     // bottom and top above the camera's ground
    double top_km = 0.0;
    double shape = 1.0;       // the height profile: 0 a sheet (slab) .. 1 a heap (dome)
    double sigma = 0.0;       // extinction at the noise's density 1, km^-1 (Density D's scale included)
    double erosion = 0.3;     // how much the detail noise eats the edges
    double anvil = 0.0;       // 0..1: the cover spreads again under the top (cumulonimbus)
    double noise_scale = 1.0; // the base noise's tile, relative to BASE_KM (1, 1/2, 1/4)
    double stretch = 1.0;     // fibres: the noise stretched along the wind (cirrus)
    double cover = 0.0;       // the band's mean cover (generated bands) or its gain (the creator's band); 0 = absent
    double sheet = 0.0;       // 0 heaps with gaps .. 1 a closed sheet (the Aurora ladder's second channel)
    double drift = 1.0;       // the band's drift against the creator's scroll (wind shear)
    double far_km = 45.0;     // how far along a ray the band is traced
    int steps = 64;           // march steps
    int step_cap = 64;        // the most steps a long (grazing) path through the band may take: 96 for a low creator's
                              // band (no step longer than 0.3 km, the Aurora Viewer's rule), the steps for the others
    int light_steps = 5;      // light march taps (plus one far tap)

    bool present() const { return genus != NONE && cover > 0.0; }
};

struct Scape
{
    Band bands[MAX_BANDS]; // sorted by base height; absent bands (cover 0) last
    int count = 0;         // bands present
    double altitude = 0.0; // the creator's band: 0 low, 1 middle, 2 high etage (continuous)
    double tile_km = 12.6;
    int tiles_per_map = 1; // 1, or 16 when cloud_variance draws the flat layer's disturbance (its taps repeat
                           // every 16 tiles: u / 8 and 17 u / 16)
    double map_km = 12.6;
    int map_size = 512;    // the weather map's side (texels)
    double patch = 0.0;    // amplitudes of the mean-preserving modulation; patch + growth <= 1
    double growth = 0.0;
    double growth_rate = 1.0 / 1200.0; // growth cycles per second
};

// ---- the Aurora Viewer's ladder (clouds.rs, the owner's tuned code), ported

inline double smoothstep(double e0, double e1, double x)
{
    const double t = std::clamp((x - e0) / (e1 - e0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The cover class of a sky's cloud_shadow (clouds.rs coverage()).
inline double coverage(double cloud_shadow)
{
    const double t = std::clamp((cloud_shadow - 0.1) / 0.56, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// The type from the cover: 0 stratus (a grey sheet), 0.5 stratocumulus, 1 cumulus (clouds.rs cloud_type()).
inline double cloud_type(double cover)
{
    const double t = std::clamp((0.86 - cover) / (0.86 - 0.38), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Bottom and top of the low layer for a type (clouds.rs layer_km(): WMO heights; St 0.4-0.9, Sc 0.6-1.25, Cu 1.2-2 km).
inline void layer_km(double type, double& base, double& top)
{
    auto pick = [type](double lo, double mid, double hi) { return type < 0.5 ? lo + (mid - lo) * type * 2.0 : mid + (hi - mid) * (type - 0.5) * 2.0; };
    base = pick(0.4, 0.6, 1.2);
    top = pick(0.9, 1.25, 2.0);
}

// Side of the creator's tile (km): the flat layer's repeat, 2 R cloud_scale (llcloudshadow.h layer_texcoord), so that
// the volumetric clouds stand where the flat layer's texture puts its clouds. Like the Aurora Viewer's tile (clouds.rs
// tile_km(), not rounded here: the noises wrap on their own offset) it stays within [0.5, 2] of the default scale 0.42;
// a smaller scale lifts the layer instead (altitude_of()), so its elements look smaller as the WMO's do.
constexpr double TILE_SCALE_MIN = 0.21, TILE_SCALE_MAX = 0.84;
inline double tile_km(double cloud_scale)
{
    return 2.0 * DOME_RADIUS_KM * std::clamp(cloud_scale, TILE_SCALE_MIN, TILE_SCALE_MAX);
}

// Extinction scale from Density D (clouds.rs extinction()).
inline double extinction_scale(double density)
{
    return std::clamp(density, 0.0, 1.5);
}

// Below this the flat layer is taken as off (a cloud scale or a density; clouds.rs LAYER_MIN).
constexpr double LAYER_MIN = 0.001;

inline bool layer_drawn(double cloud_scale, bool has_texture, double density)
{
    return cloud_scale >= LAYER_MIN && has_texture && density > LAYER_MIN;
}

// ---- extinction per genus (OPAC, see the file comment)

// beta = 3 L / (2 rho r_eff), L in g m^-3, r_eff in micrometres, rho in g cm^-3: km^-1
inline double beta_km(double lwc, double r_eff_um, double rho = 1.0)
{
    return 1500.0 * lwc / (rho * r_eff_um);
}

constexpr double RHO_ICE = 0.917;
// OPAC Table 1a: stratus (continental) L 0.28, r_eff 7.33; cumulus (continental, clean) L 0.26, r_eff 5.77;
// stratocumulus: the polluted cumulus distribution (r_eff 4.00) with N 400 and L 0.092 (OPAC section 3a).
// Table 1b: cirrus 1 (-25 C) IWC 0.0260, r_eff 91.7.
inline double beta_stratus() { return beta_km(0.28, 7.33); }
inline double beta_cumulus() { return beta_km(0.26, 5.77); }
inline double beta_stratocumulus() { return beta_km(0.092, 4.00); }
inline double beta_cirrus() { return beta_km(0.0260, 91.7, RHO_ICE); }
// No OPAC model: altostratus from its optical depth (5 to 20 over 1 to 3 km of a layer half filled): about 6.5 km^-1;
// cirrocumulus small cells (optical depth 0.5 to 2 over 0.4 km half filled): about 2.5 km^-1.
constexpr double BETA_ALTOSTRATUS = 6.5;
constexpr double BETA_CIRROCUMULUS = 2.5;

// The fill factor: the noise's density is rarely 1 inside a cloud, so the extinction at density 1 is a little above
// the bulk value. One factor for every genus, set so that cumulus keeps the Aurora Viewer's SIGMA (110 km^-1).
constexpr double AURORA_SIGMA = 110.0;
inline double fill() { return AURORA_SIGMA / beta_cumulus(); }

inline double sigma_of(Genus g)
{
    switch (g)
    {
    case STRATUS:
    case NIMBOSTRATUS: return fill() * beta_stratus();
    case STRATOCUMULUS:
    case ALTOCUMULUS: return fill() * beta_stratocumulus();
    case CUMULUS:
    case CUMULUS_CONGESTUS:
    case CUMULONIMBUS: return fill() * beta_cumulus();
    case ALTOSTRATUS: return fill() * BETA_ALTOSTRATUS;
    case CIRROCUMULUS: return fill() * BETA_CIRROCUMULUS;
    case CIRROSTRATUS:
    case CIRRUS: return fill() * beta_cirrus();
    default: return 0.0;
    }
}

// ---- the creator's band

// The etage of the creator's layer from its element size: 0 low, 1 middle, 2 high, continuous so that a day cycle that
// blends cloud_scale across a threshold lifts the layer smoothly.
inline double altitude_of(double cloud_scale, const Tuning& t)
{
    return smoothstep(t.middle_scale * 1.25, t.middle_scale * 0.8, cloud_scale) + smoothstep(t.high_scale * 1.25, t.high_scale * 0.8, cloud_scale);
}

namespace detail
{
inline double lerp(double a, double b, double x) { return a + (b - a) * x; }

// a piecewise ladder over the type: 0 sheet, 0.5 middle, 1 heap
inline double ladder(double type, double sheet_value, double middle_value, double heap_value)
{
    return type < 0.5 ? lerp(sheet_value, middle_value, type * 2.0) : lerp(middle_value, heap_value, (type - 0.5) * 2.0);
}

struct Etage
{
    Genus genus;
    double base, top, shape, sigma, erosion, anvil, noise_scale;
};

// the low etage: the Aurora ladder, grown by Density D
inline Etage low_etage(double type, double density, const Tuning& t)
{
    Etage e{};
    layer_km(type, e.base, e.top);
    e.shape = type;
    e.sigma = ladder(type, sigma_of(STRATUS), sigma_of(STRATOCUMULUS), sigma_of(CUMULUS));
    e.erosion = lerp(0.3, 0.45, type);
    e.noise_scale = 1.0;
    const double heap = smoothstep(0.5, 0.75, type); // how much of a heap the type is
    // heaps grow: congestus (tops to 6 km, WMO), then cumulonimbus (tops at the tropopause, about 11 km) with an anvil
    const double congestus = smoothstep(t.congestus_density, t.cumulonimbus_density, density) * heap;
    const double cumulonimbus = smoothstep(t.cumulonimbus_density, t.cumulonimbus_density + 0.5, density) * heap;
    e.top = lerp(e.top, 6.0, congestus);
    e.top = lerp(e.top, 11.0, cumulonimbus);
    e.anvil = cumulonimbus;
    e.erosion = lerp(e.erosion, 0.35, cumulonimbus);
    // sheets thicken into nimbostratus (2 to 4 km deep)
    const double nimbo = smoothstep(t.congestus_density, t.cumulonimbus_density, density) * (1.0 - smoothstep(0.25, 0.5, type));
    e.top = lerp(e.top, 3.5, nimbo);
    if (cumulonimbus > 0.5)
    {
        e.genus = CUMULONIMBUS;
    }
    else if (congestus > 0.5)
    {
        e.genus = CUMULUS_CONGESTUS;
    }
    else if (nimbo > 0.5)
    {
        e.genus = NIMBOSTRATUS;
    }
    else
    {
        e.genus = type >= 0.75 ? CUMULUS : (type >= 0.25 ? STRATOCUMULUS : STRATUS);
    }
    return e;
}

// the middle etage: altocumulus cells, altostratus as the cover closes (WMO: 2 to 7 km; Ac 0.2-1 km deep, As 1-3 km)
inline Etage middle_etage(double type)
{
    Etage e{};
    e.base = 3.0;
    e.top = lerp(4.5, 3.6, type);
    e.shape = lerp(0.0, 0.75, type);
    e.sigma = lerp(sigma_of(ALTOSTRATUS), sigma_of(ALTOCUMULUS), type);
    e.erosion = lerp(0.3, 0.4, type);
    e.noise_scale = 0.5;
    e.genus = type >= 0.5 ? ALTOCUMULUS : ALTOSTRATUS;
    return e;
}

// the high etage: cirrocumulus cells, cirrostratus as the cover closes (WMO: 5 to 13 km)
inline Etage high_etage(double type)
{
    Etage e{};
    e.base = 7.0;
    e.top = lerp(8.2, 7.4, type);
    e.shape = lerp(0.0, 0.6, type);
    e.sigma = lerp(sigma_of(CIRROSTRATUS), sigma_of(CIRROCUMULUS), type);
    e.erosion = 0.5;
    e.noise_scale = 0.25;
    e.genus = type >= 0.5 ? CIRROCUMULUS : CIRROSTRATUS;
    return e;
}

// Between two etages the layer rises continuously; its genus is the one of the etage its base stands in (WMO: the
// middle etage from 2 km, the high one from 5 km, temperate latitudes).
inline Etage blend(const Etage& a, const Etage& b, double x)
{
    const double base = lerp(a.base, b.base, x);
    const double boundary = b.base >= 5.0 ? 5.0 : 2.0;
    Etage e = base < boundary ? a : b;
    e.base = base;
    e.top = lerp(a.top, b.top, x);
    e.shape = lerp(a.shape, b.shape, x);
    e.sigma = lerp(a.sigma, b.sigma, x);
    e.erosion = lerp(a.erosion, b.erosion, x);
    e.anvil = lerp(a.anvil, b.anvil, x);
    // the noise scale stays one of 1, 1/2, 1/4 (the wrap period holds whole tiles of each)
    e.noise_scale = base < boundary ? a.noise_scale : b.noise_scale;
    return e;
}
} // namespace detail

// The wind at height h against the wind at h0 (a simple profile growing with height, mid-latitude westerlies).
inline double wind_ratio(double h_km, double h0_km, double shear_height_km)
{
    const double s = std::max(shear_height_km, 0.1);
    return (1.0 + std::max(h_km, 0.0) / s) / (1.0 + std::max(h0_km, 0.0) / s);
}

// The sky's bands from its cloud sliders. `has_texture` is whether the sky has a cloud texture; without one, or below
// LAYER_MIN, the flat layer draws nothing and neither do these.
inline Scape make(const Sliders& s, const Tuning& t, bool has_texture = true)
{
    Scape sc;
    sc.tile_km = tile_km(s.scale);
    sc.tiles_per_map = s.variance > 0.0 ? 16 : 1;
    sc.map_km = sc.tile_km * sc.tiles_per_map;
    sc.map_size = sc.tiles_per_map == 1 ? 512 : 2048;
    sc.altitude = altitude_of(s.scale, t);

    if (!layer_drawn(s.scale, has_texture, s.density))
    {
        return sc;
    }

    const double variance = std::clamp(s.variance, 0.0, 1.0);
    const double cover_class = coverage(s.coverage);
    const double type = cloud_type(cover_class);
    // the Aurora ladder's sheet: as the cover passes one half a closed sheet takes over (env.wgsl fs_cloud_weather)
    const double sheet = std::clamp((smoothstep(0.0, 1.0, std::clamp((s.coverage - 0.1) / 0.56, 0.0, 1.0)) - 0.5) * 2.0, 0.0, 1.0);
    const double ext = extinction_scale(s.density);

    Band bands[MAX_BANDS];
    int n = 0;

    // the creator's band
    {
        const detail::Etage low = detail::low_etage(type, s.density, t);
        const detail::Etage mid = detail::middle_etage(type);
        const detail::Etage high = detail::high_etage(type);
        const detail::Etage e = sc.altitude <= 1.0 ? detail::blend(low, mid, sc.altitude) : detail::blend(mid, high, sc.altitude - 1.0);
        Band& b = bands[n++];
        b.genus = e.genus;
        b.channel = 0;
        b.base_km = e.base;
        b.top_km = e.top;
        b.shape = e.shape;
        b.sigma = e.sigma * ext;
        b.erosion = e.erosion;
        b.anvil = e.anvil;
        b.noise_scale = e.noise_scale;
        b.cover = 1.0;
        b.sheet = sheet;
        b.drift = 1.0;
        b.far_km = e.base < 2.5 ? 45.0 : 120.0;
        b.steps = 64;
        b.step_cap = e.base < 2.5 ? 96 : 64;
        b.light_steps = 5;
    }

    // altocumulus cells with a high Detail D, under a creator's band that is low
    const double mid_weight = 1.0 - std::clamp(sc.altitude * 2.0, 0.0, 1.0);
    const double ac = std::clamp((s.detail - t.altocumulus_detail) / std::max(1.0 - t.altocumulus_detail, 1e-6), 0.0, 1.0) * t.altocumulus_cover * mid_weight;
    if (ac > 0.0)
    {
        const detail::Etage e = detail::middle_etage(1.0);
        Band& b = bands[n++];
        b.genus = ALTOCUMULUS;
        b.channel = 1;
        b.base_km = e.base;
        b.top_km = e.top;
        b.shape = e.shape;
        b.sigma = e.sigma;
        b.erosion = e.erosion;
        b.noise_scale = e.noise_scale;
        b.cover = ac;
        b.far_km = 120.0;
        b.steps = 32;
        b.step_cap = 32;
        b.light_steps = 3;
    }

    // cirrus with cloud_variance (the flat layer's streaks), above a creator's band that is not high itself
    const double high_weight = 1.0 - std::clamp(sc.altitude - 1.0, 0.0, 1.0);
    const double ci = std::clamp(t.cirrus_base + t.cirrus_variance * variance, 0.0, 1.0) * high_weight;
    if (ci > 0.0)
    {
        // fibres (Ci) while thin, a veil (Cs) as the cover closes
        const double veil = smoothstep(0.4, 0.7, ci);
        Band& b = bands[n++];
        b.genus = veil > 0.5 ? CIRROSTRATUS : CIRRUS;
        b.channel = 2;
        b.base_km = 8.5;
        b.top_km = detail::lerp(9.5, 9.7, veil);
        b.shape = detail::lerp(0.2, 0.0, veil);
        b.sigma = sigma_of(CIRRUS);
        b.erosion = 0.6;
        b.noise_scale = 1.0;
        b.stretch = detail::lerp(4.0, 2.0, veil);
        b.cover = ci;
        b.sheet = veil * 0.5;
        b.far_km = 160.0;
        b.steps = 16;
        b.step_cap = 16;
        b.light_steps = 2;
    }

    // wind shear against the creator's band, taken at its lower part (a tower's scroll is its lower body's)
    const double h0 = std::min(0.5 * (bands[0].base_km + bands[0].top_km), bands[0].base_km + 1.0);
    for (int i = 1; i < n; ++i)
    {
        bands[i].drift = wind_ratio(0.5 * (bands[i].base_km + bands[i].top_km), h0, t.shear_height_km);
    }

    // sorted by base height (front to back for a camera below the clouds)
    std::sort(bands, bands + n, [](const Band& a, const Band& b) { return a.base_km < b.base_km; });
    for (int i = 0; i < n; ++i)
    {
        sc.bands[i] = bands[i];
    }
    sc.count = n;

    // the modulation: patch + growth <= 1 keeps every cover in [0, 1]
    double patch = std::max(t.patch_base + t.patch_variance * variance, 0.0);
    double growth = std::max(t.growth, 0.0);
    const double sum = patch + growth;
    if (sum > 1.0)
    {
        patch /= sum;
        growth /= sum;
    }
    sc.patch = patch;
    sc.growth = growth;
    sc.growth_rate = 1.0 / std::max(t.lifetime_s, 1.0);
    return sc;
}

// ---- variability (the weather pass's twin)

// The mean-preserving modulation of a cover c in [0, 1] by a field M in [-1, 1] with mean 0, independent of c:
// c' = c (1 + (1 - c) M). Then E[c'] = E[c] + E[c (1 - c)] E[M] = E[c], and 0 <= c' <= 1 for |M| <= 1.
inline double modulate(double c, double m)
{
    return c * (1.0 + (1.0 - c) * m);
}

// The base noise's depth (LLCloudNoise::BASE_SIZE): its slices along z are the growth field's time axis.
constexpr int NOISE_SLICES = 128;
// A cell of the growth field (the Worley FBM's lowest octave: 4 cells a period) lives one lifetime: z moves a quarter
// of the noise's period per lifetime. The bands read the field at thirds of the period apart.
constexpr double GROWTH_CELLS_PER_PERIOD = 4.0;

inline double growth_z(double time_s, double growth_rate, int band)
{
    const double z = time_s * growth_rate / GROWTH_CELLS_PER_PERIOD + band / 3.0;
    return z - std::floor(z);
}

// The mean over the map of the noise sampled at z (in noise periods, wrapped) with linear filtering in z, from the
// slices' own means: what the texture unit returns, averaged over whole periods of the slice (the map samples it at
// a whole number of map texels per noise texel).
inline double slice_mean_at(double z, const double* means, int slices = NOISE_SLICES)
{
    const double t = (z - std::floor(z)) * slices - 0.5;
    const double f = t - std::floor(t);
    const int i0 = (((int) std::floor(t)) % slices + slices) % slices;
    const int i1 = (i0 + 1) % slices;
    return means[i0] + (means[i1] - means[i0]) * f;
}

// A field value v in [0, 1] with mean `mean` as a zero-mean field in [-1, 1].
inline double centred(double v, double mean)
{
    const double span = std::max(std::max(mean, 1.0 - mean), 1e-6);
    return std::clamp((v - mean) / span, -1.0, 1.0);
}

// The patch field: a few low plane waves over the weather map, periodic in it, mean exactly 0.5 over whole periods,
// in [0, 1]. (x, y) in map units (0..1); `phase` seeds the band.
inline double patch_field(double x, double y, double phase)
{
    constexpr double TAU = 6.283185307179586;
    const double a = std::cos(TAU * (x + phase * 0.37));
    const double b = std::cos(TAU * (y + phase * 0.61));
    const double c = std::cos(TAU * (x + y + phase * 0.13));
    const double d = std::cos(TAU * (2.0 * x - y + phase * 0.79));
    return 0.5 + 0.5 * (0.3 * a + 0.3 * b + 0.2 * c + 0.2 * d);
}

// The flat layer's taps for the creator's band (cloudsF.glsl alpha1, at no scroll; the self-shadow taps' light
// shift, at most 0.0125 of a tile, is left out so that the map does not follow the sun), cut as the Aurora Viewer's
// weather map (env.wgsl fs_cloud_weather: heaps with a knee, a sheet over one half of cover). n(uv) is the cloud
// texture; u is the texel's position in tiles. Returns the cover before the modulation.
template <typename Tex>
inline double primary_cover(const Tex& n, double ux, double uy, double cloud_shadow, double cloud_scale, double variance,
                            const double density1[3], const double density2[3])
{
    constexpr double HEAP_KNEE = 0.4, HEAP_SLOPE = 0.2;
    double uv1x = ux, uv1y = uy;
    const double uv3x = ux * 16.0, uv3y = uy * 16.0;
    const double k = variance * (1.0 - cloud_scale * 0.25);
    const double dx = n(uv1x / 8.0, uv1y / 8.0) * k, dy = n((uv3x + uv1x) / 16.0, (uv3y + uv1y) / 16.0) * k;
    const double d2x = n((uv1x + uv3x) / 4.0, (uv1y + uv3y) / 4.0) * k, d2y = n((uv3x + uv1x) / 8.0, (uv3y + uv1y) / 8.0) * k;
    uv1x += density1[0] + dx * 0.2;
    uv1y += density1[1] + dy * 0.2;
    const double density_variance = std::min(1.0, (dx * 2.0 + dy * 2.0 + d2x + d2y) * 4.0);
    const double cloud_density = 2.0 * (cloud_shadow - 0.25) * (1.0 - density_variance * density_variance);
    const double n1 = n(uv1x, uv1y) - 0.5;
    const double detail_tap = n(uv3x + density2[0], uv3y + density2[1]);
    const double fine = n1 + (detail_tap - 0.5) * density2[2];
    const double wc0 = smoothstep(-0.05, 0.45, fine + cloud_density);
    const double gc = smoothstep(0.0, 1.0, std::clamp((cloud_shadow - 0.1) / 0.56, 0.0, 1.0));
    const double wc1 = 0.55 + 0.45 * smoothstep(-0.25, 0.25, fine);
    const double sheet = std::clamp((gc - 0.5) * 2.0, 0.0, 1.0);
    const double compressed = std::min(wc0, HEAP_KNEE) + std::max(wc0 - HEAP_KNEE, 0.0) * HEAP_SLOPE;
    const double heaps = compressed + (wc0 - compressed) * sheet;
    return heaps + (wc1 - heaps) * sheet;
}

// ---- the light the clouds get (the EEP filter on the clouds: the flat layer's terms, cloudsV.glsl)

struct Vec3
{
    double r = 0.0, g = 0.0, b = 0.0;
};

inline double srgb_to_linear(double c)
{
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

inline double linear_to_srgb(double c)
{
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}

struct SkyLight
{
    Vec3 sunlight;       // sunlight_color (the sky's, as the clouds' shader gets it)
    Vec3 ambient;        // ambient_color
    Vec3 cloud_color;
    Vec3 cloud_T;        // cloud_light_transmittance (AT1: the sun's transmittance at the cloud layer, linear)
    double cloud_shadow = 0.27;
    double moon_blend = 0.0; // cloud_moon_blend (L3: 0 the sun, 1 the moon or night)
    double hdr_scale = 1.0;  // sky_hdr_scale
};

struct CloudLight
{
    Vec3 sun;     // linear radiance of the light on a cloud (before the phase function), sky-scaled
    Vec3 ambient; // linear radiance of the sky light on a cloud
};

// cloudsV.glsl's vary_CloudColorSun without the glow's angular shape (the trace's phase function replaces it) and
// vary_CloudColorAmbient without the haze (the trace adds the haze for the cloud's own distance); both doubled,
// clamped and made linear as softenLightF.glsl makes the flat clouds (srgb_to_linear(min(2 c, 2)) * sky_hdr_scale).
inline CloudLight cloud_light(const SkyLight& s)
{
    auto channel = [&s](double sun, double amb, double cloud, double T, double& sun_out, double& amb_out)
    {
        const double sun_cloud = linear_to_srgb(srgb_to_linear(std::max(sun, 0.0)) * T); // cloudsV: the sun at the cloud layer
        const double sun_srgb = sun_cloud * (1.0 - s.moon_blend) * cloud;                 // the glow's handover, the cloud's tint
        const double tmp_ambient = amb + (1.0 - amb) * s.cloud_shadow * 0.5;              // more clouds, more ambient
        const double amb_srgb = tmp_ambient * cloud;
        sun_out = srgb_to_linear(std::clamp(sun_srgb * 2.0, 0.0, 2.0)) * s.hdr_scale;
        amb_out = srgb_to_linear(std::clamp(amb_srgb * 2.0, 0.0, 2.0)) * s.hdr_scale;
    };
    CloudLight l;
    channel(s.sunlight.r, s.ambient.r, s.cloud_color.r, s.cloud_T.r, l.sun.r, l.ambient.r);
    channel(s.sunlight.g, s.ambient.g, s.cloud_color.g, s.cloud_T.g, l.sun.g, l.ambient.g);
    channel(s.sunlight.b, s.ambient.b, s.cloud_color.b, s.cloud_T.b, l.sun.b, l.ambient.b);
    return l;
}

// ---- the weather map's key: what it is drawn from, quantised (a change redraws it)

inline uint64_t hash_mix(uint64_t h, uint64_t v)
{
    return (h ^ v) * 0x100000001b3ull;
}

inline uint64_t quantise(double v, double step)
{
    return (uint64_t) (int64_t) std::llround(v / step);
}

inline uint64_t weather_key(const Sliders& s, const Scape& sc, const double density1[3], const double density2[3])
{
    uint64_t h = 0xcbf29ce484222325ull;
    const double values[] = { s.coverage, s.scale, s.variance, s.density, s.detail, density1[0], density1[1], density2[0], density2[1], density2[2] };
    for (double v : values)
    {
        h = hash_mix(h, quantise(v, 1.0 / 4096.0));
    }
    h = hash_mix(h, (uint64_t) sc.tiles_per_map);
    h = hash_mix(h, (uint64_t) sc.map_size);
    for (int i = 0; i < sc.count; ++i)
    {
        h = hash_mix(h, (uint64_t) sc.bands[i].channel);
        h = hash_mix(h, quantise(sc.bands[i].cover, 1.0 / 4096.0));
    }
    h = hash_mix(h, quantise(sc.patch, 1.0 / 4096.0));
    h = hash_mix(h, quantise(sc.growth, 1.0 / 4096.0));
    return h;
}

// A position (km) wrapped into [0, period): WRAP_KM for the noises' offset, the map's period for the weather's.
inline double wrap_km(double v, double period = WRAP_KM)
{
    const double r = std::fmod(v, period);
    return r < 0.0 ? r + period : r;
}
} // namespace LLCloudscape

#endif // LL_LLCLOUDSCAPE_H
