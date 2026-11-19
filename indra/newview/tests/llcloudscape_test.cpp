// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llcloudscape.h (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llcloudscape_test.cpp
// Stage L7 of the light study (doc/light/volumetric-clouds.md), the cloud kind and variability layer:
//   - the Aurora Viewer's ladder as ported (coverage, type, layer heights, tile), against clouds.rs's own values;
//   - the extinction per genus from OPAC (Hess, Koepke and Schult 1998) and the fill factor that keeps the Aurora
//     cumulus at 110 km^-1;
//   - the cloud types at the EEP slider ranges: the default sky is fair-weather cumulus alone; coverage walks the
//     ladder to stratus; Density D grows congestus, cumulonimbus with an anvil, nimbostratus; a small cloud_scale
//     lifts the layer to altocumulus/altostratus and cirrocumulus/cirrostratus; Detail D adds altocumulus,
//     cloud_variance cirrus;
//   - over a grid of every slider: bands sorted, inside their WMO etage, sane parameters, the wrap period a multiple
//     of every period, the heights continuous in cloud_scale (no jump at the thresholds);
//   - the variability: the modulation keeps the mean cover exactly and stays in [0, 1]; the patch field has mean 0.5
//     over the map; the growth field's mean follows the noise slices as the texture unit filters them, and its clock
//     wraps; the creator's pattern is periodic over the map (16 tiles with cloud_variance);
//   - the clouds' light: the flat layer's terms (night: no sun; the clamp; the ambient lift);
//   - the weather map's key follows every input it is drawn from.

#include "llcloudscape.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int failures = 0;

void check(bool cond, const char* what, double a = 0.0, double b = 0.0)
{
    if (!cond)
    {
        std::printf("FAIL %s (%.17g, %.17g)\n", what, a, b);
        ++failures;
    }
}

bool near(double a, double b, double tol)
{
    return std::fabs(a - b) <= tol;
}

using namespace LLCloudscape;

const Tuning TUNING;

Scape scape(double coverage, double scale, double variance, double density, double detail)
{
    Sliders s;
    s.coverage = coverage;
    s.scale = scale;
    s.variance = variance;
    s.density = density;
    s.detail = detail;
    return make(s, TUNING);
}

const Band* find(const Scape& sc, int channel)
{
    for (int i = 0; i < sc.count; ++i)
    {
        if (sc.bands[i].channel == channel)
        {
            return &sc.bands[i];
        }
    }
    return nullptr;
}

void test_aurora_ladder()
{
    // clouds.rs: the cover ladder at the demo's cloud_shadow values, the type's ends, the layer heights, the tile
    check(coverage(0.1) == 0.0 && coverage(0.66) == 1.0, "coverage ends");
    check(cloud_type(0.38) == 1.0 && cloud_type(0.86) == 0.0, "type ends");
    double base = 0.0, top = 0.0;
    layer_km(0.0, base, top);
    check(near(base, 0.4, 1e-12) && near(top, 0.9, 1e-12), "stratus heights", base, top);
    layer_km(0.5, base, top);
    check(near(base, 0.6, 1e-12) && near(top, 1.25, 1e-12), "stratocumulus heights", base, top);
    layer_km(1.0, base, top);
    check(near(base, 1.2, 1e-12) && near(top, 2.0, 1e-12), "cumulus heights", base, top);
    // the flat layer's repeat, 2 R cloud_scale (llcloudshadow.h), within [0.5, 2] of the default scale
    check(near(tile_km(0.42), 12.6, 1e-12), "default tile", tile_km(0.42));
    check(near(tile_km(0.3), 9.0, 1e-12) && near(tile_km(0.01), 6.3, 1e-12) && near(tile_km(3.0), 25.2, 1e-12), "tile clamp");
    check(extinction_scale(3.0) == 1.5 && extinction_scale(0.5) == 0.5, "extinction scale");
    check(!layer_drawn(0.0005, true, 1.0) && !layer_drawn(0.42, false, 1.0) && !layer_drawn(0.42, true, 0.0) && layer_drawn(0.42, true, 1.0),
          "the flat layer's own off conditions");
}

void test_extinction()
{
    // OPAC Table 1a/1b through beta = 3 L / (2 rho r_eff)
    check(near(beta_stratus(), 57.298772169167804, 1e-9), "stratus beta", beta_stratus());
    check(near(beta_cumulus(), 67.590987868284232, 1e-9), "cumulus beta", beta_cumulus());
    check(near(beta_stratocumulus(), 34.5, 1e-12), "stratocumulus beta", beta_stratocumulus());
    check(near(beta_cirrus(), 1500.0 * 0.026 / (0.917 * 91.7), 1e-12) && near(beta_cirrus(), 0.4638, 1e-4), "cirrus beta", beta_cirrus());
    check(near(sigma_of(CUMULUS), AURORA_SIGMA, 1e-9), "cumulus keeps the Aurora SIGMA", sigma_of(CUMULUS));
    // physical ranges at density 1 (the plan's: water clouds 20-130, ice 0.05-1 km^-1, as fill-scaled)
    for (Genus g : { STRATUS, STRATOCUMULUS, CUMULUS, CUMULONIMBUS, NIMBOSTRATUS, ALTOCUMULUS })
    {
        check(sigma_of(g) > 30.0 && sigma_of(g) < 130.0, "water cloud extinction", (double) g, sigma_of(g));
    }
    for (Genus g : { CIRRUS, CIRROSTRATUS })
    {
        check(sigma_of(g) > 0.05 && sigma_of(g) < 1.5, "ice cloud extinction", (double) g, sigma_of(g));
    }
    check(sigma_of(ALTOSTRATUS) > sigma_of(CIRROCUMULUS) && sigma_of(CIRROCUMULUS) > sigma_of(CIRRUS), "As > Cc > Ci");
    check(sigma_of(NONE) == 0.0, "no genus, no extinction");
}

void test_types()
{
    // the default sky (LLSettingsSky defaults): fair-weather cumulus, one band, the Aurora heights
    {
        const Scape sc = scape(0.27, 0.42, 0.0, 1.0, 0.12);
        check(sc.count == 1, "default: one band", sc.count);
        const Band& b = sc.bands[0];
        check(b.genus == CUMULUS && b.channel == 0, "default: cumulus from the creator's pattern", b.genus);
        check(near(b.base_km, 1.2, 1e-12) && near(b.top_km, 2.0, 1e-12), "default: 1.2-2 km", b.base_km, b.top_km);
        check(near(b.sigma, 110.0, 1e-9), "default: Aurora's extinction", b.sigma);
        check(b.sheet == 0.0 && b.anvil == 0.0, "default: heaps, no anvil");
        check(sc.tiles_per_map == 1 && sc.map_size == 512 && near(sc.map_km, 12.6, 1e-12), "default: one tile, Aurora's map size");
    }
    // coverage walks the ladder
    check(scape(0.45, 0.42, 0.0, 1.0, 0.12).bands[0].genus == STRATOCUMULUS, "0.45: stratocumulus");
    check(scape(0.62, 0.42, 0.0, 1.0, 0.12).bands[0].genus == STRATUS, "0.62: stratus");
    check(scape(0.62, 0.42, 0.0, 1.0, 0.12).bands[0].sheet > 0.95, "overcast: a closed sheet", scape(0.62, 0.42, 0.0, 1.0, 0.12).bands[0].sheet);
    check(scape(0.66, 0.42, 0.0, 1.0, 0.12).bands[0].sheet == 1.0 && scape(0.27, 0.42, 0.0, 1.0, 0.12).bands[0].sheet == 0.0, "the sheet's ends");
    // Density D: heaps grow, sheets thicken
    {
        const Band b = scape(0.27, 0.42, 0.0, 1.6, 0.12).bands[0];
        check(b.genus == CUMULUS_CONGESTUS && b.top_km > 4.0 && b.top_km < 6.0, "D 1.6: congestus", b.genus, b.top_km);
    }
    {
        const Band b = scape(0.27, 0.42, 0.0, 2.6, 0.12).bands[0];
        check(b.genus == CUMULONIMBUS && near(b.top_km, 11.0, 1e-12) && b.anvil == 1.0, "D 2.6: cumulonimbus with an anvil", b.genus, b.top_km);
        check(near(b.sigma, 110.0 * 1.5, 1e-9), "D 2.6: the extinction scale caps at 1.5", b.sigma);
    }
    {
        const Band b = scape(0.62, 0.42, 0.0, 2.0, 0.12).bands[0];
        check(b.genus == NIMBOSTRATUS && near(b.top_km, 3.5, 1e-12) && b.anvil == 0.0, "overcast, D 2: nimbostratus", b.genus, b.top_km);
    }
    check(scape(0.62, 0.42, 0.0, 2.6, 0.12).bands[0].genus != CUMULONIMBUS, "no cumulonimbus from a sheet");
    // a small cloud_scale lifts the creator's layer (WMO element sizes)
    {
        const Scape sc = scape(0.27, 0.12, 0.0, 1.0, 0.12);
        check(near(sc.altitude, 1.0, 1e-12) && sc.bands[0].genus == ALTOCUMULUS, "scale 0.12: altocumulus", sc.altitude, sc.bands[0].genus);
        check(sc.bands[0].base_km >= 2.0 && sc.bands[0].top_km <= 7.0, "altocumulus in the middle etage", sc.bands[0].base_km, sc.bands[0].top_km);
        check(sc.bands[0].noise_scale == 0.5, "altocumulus: smaller elements");
    }
    check(scape(0.62, 0.12, 0.0, 1.0, 0.12).bands[0].genus == ALTOSTRATUS, "scale 0.12, overcast: altostratus");
    {
        const Scape sc = scape(0.27, 0.03, 0.0, 1.0, 0.12);
        check(near(sc.altitude, 2.0, 1e-12) && sc.bands[0].genus == CIRROCUMULUS, "scale 0.03: cirrocumulus", sc.altitude, sc.bands[0].genus);
        check(sc.bands[0].base_km >= 5.0 && sc.bands[0].top_km <= 13.0, "cirrocumulus in the high etage");
    }
    check(scape(0.62, 0.03, 0.0, 1.0, 0.12).bands[0].genus == CIRROSTRATUS, "scale 0.03, overcast: cirrostratus");
    // Detail D adds altocumulus cells over a low layer; not over a layer that is already aloft
    {
        const Scape sc = scape(0.27, 0.42, 0.0, 1.0, 0.8);
        const Band* ac = find(sc, 1);
        check(sc.count == 2 && ac && ac->genus == ALTOCUMULUS, "detail 0.8: altocumulus band", sc.count);
        check(ac && near(ac->cover, (0.8 - 0.25) / 0.75 * 0.6, 1e-12), "detail 0.8: its cover", ac ? ac->cover : -1.0);
        check(ac && ac->drift > 1.0, "the middle band drifts faster", ac ? ac->drift : 0.0);
        check(find(scape(0.27, 0.12, 0.0, 1.0, 0.8), 1) == nullptr, "no altocumulus band over an altocumulus layer");
    }
    check(find(scape(0.27, 0.42, 0.0, 1.0, 0.12), 1) == nullptr, "default detail: no altocumulus band");
    // cloud_variance adds cirrus, a veil as it thickens
    {
        const Scape sc = scape(0.27, 0.42, 0.5, 1.0, 0.12);
        const Band* ci = find(sc, 2);
        check(ci && ci->genus == CIRRUS && near(ci->cover, 0.4, 1e-12), "variance 0.5: cirrus", ci ? ci->genus : -1);
        check(ci && ci->stretch > 1.0 && ci->steps <= 16, "cirrus: fibres, at most 16 steps");
        check(ci && ci->drift > 1.5, "cirrus drifts faster still", ci ? ci->drift : 0.0);
        check(sc.tiles_per_map == 16 && sc.map_size == 2048, "variance: 16 tiles, the flat layer's disturbance period");
        const Band* ci2 = find(scape(0.27, 0.42, 1.0, 1.0, 0.12), 2);
        check(ci2 && ci2->genus == CIRROSTRATUS, "variance 1: cirrostratus", ci2 ? ci2->genus : -1);
        check(find(scape(0.27, 0.03, 1.0, 1.0, 0.12), 2) == nullptr, "no cirrus band over a cirrus layer");
    }
    // nothing without a cloud texture or a scale or a density
    {
        Sliders s;
        check(make(s, TUNING, false).count == 0, "no texture, no bands");
        s.scale = 0.0;
        check(make(s, TUNING).count == 0, "no scale, no bands");
        s.scale = 0.42;
        s.density = 0.0;
        check(make(s, TUNING).count == 0, "no density, no bands");
    }
}

void test_slider_ranges()
{
    // continuity in cloud_scale (a day cycle blends it): the creator's band's heights move by little for a small step,
    // also across the etage thresholds (they blend over a quarter of their value)
    {
        double prev_base = -1.0, prev_top = -1.0, worst = 0.0;
        for (double scale = 0.01; scale <= 0.4; scale += 0.0001)
        {
            const Scape c = scape(0.27, scale, 0.0, 1.0, 0.12);
            if (prev_base >= 0.0)
            {
                worst = std::max(worst, std::max(std::fabs(c.bands[0].base_km - prev_base), std::fabs(c.bands[0].top_km - prev_top)));
            }
            prev_base = c.bands[0].base_km;
            prev_top = c.bands[0].top_km;
        }
        check(worst < 0.05, "the creator's band rises continuously with cloud_scale", worst);
    }

    int scapes = 0;
    for (double scale = 0.01; scale <= 3.0001; scale += 0.01)
    {
        for (double cov = -0.2; cov <= 1.0001; cov += 0.1)
        {
            for (double var = 0.0; var <= 1.0001; var += 0.25)
            {
                for (double dens = 0.01; dens <= 3.0001; dens += 0.25)
                {
                    for (double det = 0.0; det <= 1.0001; det += 0.25)
                    {
                        const Scape sc = scape(cov, scale, var, dens, det);
                        ++scapes;
                        check(sc.count >= 1 && sc.count <= MAX_BANDS, "band count", sc.count);
                        check(sc.patch >= 0.0 && sc.growth >= 0.0 && sc.patch + sc.growth <= 1.0 + 1e-12, "modulation bounded", sc.patch, sc.growth);
                        check(near(sc.map_km, sc.tile_km * sc.tiles_per_map, 1e-12), "the map holds whole tiles", sc.map_km);
                        for (int i = 0; i < sc.count; ++i)
                        {
                            const Band& b = sc.bands[i];
                            check(b.present(), "present bands first", i);
                            check(b.base_km > 0.0 && b.top_km > b.base_km + 0.2 && b.top_km <= 12.0, "heights", b.base_km, b.top_km);
                            check(b.sigma > 0.0 && b.sigma <= 1.5 * AURORA_SIGMA + 1e-9, "extinction", b.sigma);
                            check(b.cover > 0.0 && b.cover <= 1.0, "cover", b.cover);
                            check(b.shape >= 0.0 && b.shape <= 1.0 && b.sheet >= 0.0 && b.sheet <= 1.0 && b.anvil >= 0.0 && b.anvil <= 1.0, "shape");
                            check(b.erosion > 0.0 && b.erosion < 1.0, "erosion", b.erosion);
                            check(b.noise_scale == 1.0 || b.noise_scale == 0.5 || b.noise_scale == 0.25, "noise scale", b.noise_scale);
                            const double base_tiles = WRAP_KM / (BASE_KM * b.noise_scale);
                            check(near(base_tiles, std::round(base_tiles), 1e-9), "the wrap holds whole noise tiles", b.noise_scale);
                            check(b.steps >= 16 && b.steps <= 64 && b.light_steps >= 1 && b.light_steps <= 6, "steps", b.steps);
                            check(b.step_cap >= b.steps && b.step_cap <= 96 && (b.step_cap == b.steps || b.base_km < 2.5), "step cap", b.step_cap);
                            check(b.drift >= 1.0 - 1e-12, "higher bands drift no slower", b.drift);
                            if (i > 0)
                            {
                                check(b.base_km >= sc.bands[i - 1].base_km, "sorted by height", sc.bands[i - 1].base_km, b.base_km);
                            }
                            // the etages (WMO, temperate): low bases under 2 km, middle 2-7 km, high 5-13 km
                            if (b.genus == STRATUS || b.genus == STRATOCUMULUS || b.genus == CUMULUS || b.genus == CUMULUS_CONGESTUS
                                || b.genus == CUMULONIMBUS || b.genus == NIMBOSTRATUS)
                            {
                                check(b.base_km < 2.0, "low etage base", b.genus, b.base_km);
                            }
                            if (b.genus == ALTOCUMULUS || b.genus == ALTOSTRATUS)
                            {
                                check(b.base_km >= 2.0 && b.base_km <= 7.0, "middle etage base", b.genus, b.base_km);
                            }
                            if (b.genus == CIRRUS || b.genus == CIRROSTRATUS || b.genus == CIRROCUMULUS)
                            {
                                check(b.base_km >= 5.0 && b.base_km <= 13.0, "high etage base", b.genus, b.base_km);
                            }
                        }
                        for (int i = sc.count; i < MAX_BANDS; ++i)
                        {
                            check(!sc.bands[i].present(), "absent bands last", i);
                        }
                    }
                }
            }
        }
    }
    check(scapes > 100000, "slider grid size", scapes);
}

void test_variability()
{
    // modulate(): exact mean over independent c and M (M of mean 0), bounded
    {
        std::vector<double> cs, ms;
        for (int i = 0; i <= 100; ++i)
        {
            cs.push_back(std::pow(i / 100.0, 1.7)); // a skewed cover distribution
        }
        // a zero-mean M: the centred patch field over a whole period
        const int N = 64;
        double field_mean = 0.0;
        for (int y = 0; y < N; ++y)
        {
            for (int x = 0; x < N; ++x)
            {
                field_mean += patch_field((x + 0.5) / N, (y + 0.5) / N, 0.3);
            }
        }
        field_mean /= N * N;
        check(near(field_mean, 0.5, 1e-12), "the patch field's mean is 0.5", field_mean);
        for (int y = 0; y < N; ++y)
        {
            for (int x = 0; x < N; ++x)
            {
                const double v = patch_field((x + 0.5) / N, (y + 0.5) / N, 0.3);
                check(v >= 0.0 && v <= 1.0, "patch field in [0, 1]", v);
                ms.push_back(centred(v, 0.5));
            }
        }
        for (double amplitude : { 0.3, 0.6, 1.0 })
        {
            double before = 0.0, after = 0.0;
            for (double c : cs)
            {
                for (double m : ms)
                {
                    const double cm = modulate(c, amplitude * m);
                    check(cm >= 0.0 && cm <= 1.0, "modulated cover in [0, 1]", c, cm);
                    before += c;
                    after += cm;
                }
            }
            check(near(after / before, 1.0, 1e-12), "the modulation keeps the mean cover", amplitude, after / before);
        }
        // the extremes
        check(modulate(0.0, 1.0) == 0.0 && modulate(1.0, -1.0) == 1.0 && modulate(1.0, 1.0) == 1.0, "clear stays clear, full stays full");

        // the growth field's mean: the slices' means, linear in z between slice centres, wrapped (as the texture unit)
        double means[NOISE_SLICES];
        for (int i = 0; i < NOISE_SLICES; ++i)
        {
            means[i] = 0.3 + 0.4 * i / (NOISE_SLICES - 1.0);
        }
        check(near(slice_mean_at((5 + 0.5) / NOISE_SLICES, means), means[5], 1e-12), "slice mean at a slice centre");
        check(near(slice_mean_at((5 + 0.75) / NOISE_SLICES, means), 0.75 * means[5] + 0.25 * means[6], 1e-12), "slice mean between slices");
        check(near(slice_mean_at(0.0, means), 0.5 * (means[NOISE_SLICES - 1] + means[0]), 1e-12), "slice mean wraps below the first centre");
        check(near(slice_mean_at(1.0 + (5 + 0.5) / NOISE_SLICES, means), means[5], 1e-12), "slice mean wraps above 1");
        // a cell (a quarter of the noise's period along z) lives one lifetime; the bands a third of the period apart
        const double rate = 1.0 / 1200.0;
        check(near(growth_z(1200.0, rate, 0), 0.25, 1e-12) && near(growth_z(0.0, rate, 1), 1.0 / 3.0, 1e-12), "growth z");
        for (double t = 0.0; t < 1e6; t += 977.0)
        {
            const double z = growth_z(t, rate, 2);
            check(z >= 0.0 && z < 1.0, "growth z wraps into [0, 1)", t, z);
        }
        check(centred(0.5, 0.5) == 0.0 && centred(1.0, 0.5) == 1.0 && centred(0.0, 0.5) == -1.0, "centred() ends");
    }

    // the creator's pattern is periodic over the map: a synthetic cloud texture with period 1
    auto tex = [](double x, double y)
    {
        constexpr double TAU = 6.283185307179586;
        return 0.5 + 0.25 * std::sin(TAU * x) * std::cos(TAU * 2.0 * y) + 0.2 * std::cos(TAU * (3.0 * x + y));
    };
    const double d1[3] = { 0.5, 0.526, 1.0 };
    const double d2[3] = { 0.5, 0.526, 0.12 };
    double worst16 = 0.0, worst8 = 0.0, worst1 = 0.0;
    for (int i = 0; i < 200; ++i)
    {
        const double ux = 0.137 * i, uy = 0.071 * i + 0.3;
        worst16 = std::max(worst16, std::fabs(primary_cover(tex, ux, uy, 0.4, 0.42, 0.7, d1, d2) - primary_cover(tex, ux + 16.0, uy + 16.0, 0.4, 0.42, 0.7, d1, d2)));
        worst8 = std::max(worst8, std::fabs(primary_cover(tex, ux, uy, 0.4, 0.42, 0.7, d1, d2) - primary_cover(tex, ux + 8.0, uy, 0.4, 0.42, 0.7, d1, d2)));
        worst1 = std::max(worst1, std::fabs(primary_cover(tex, ux, uy, 0.4, 0.42, 0.0, d1, d2) - primary_cover(tex, ux + 1.0, uy + 1.0, 0.4, 0.42, 0.0, d1, d2)));
    }
    check(worst16 < 1e-9, "with cloud_variance the pattern repeats every 16 tiles", worst16);
    check(worst8 > 1e-4, "but not every 8 (17 u / 16): why the map holds 16", worst8);
    check(worst1 < 1e-9, "without cloud_variance it repeats every tile", worst1);
    // the cover is a cover
    for (int i = 0; i < 400; ++i)
    {
        const double c = primary_cover(tex, 0.0113 * i, 0.0071 * i, -0.2 + 0.003 * i, 0.42, 0.3, d1, d2);
        check(c >= 0.0 && c <= 1.0, "primary cover in [0, 1]", c);
    }
    // more coverage, more cover (at no variance)
    double prev = -1.0;
    for (double cs = 0.0; cs <= 0.4001; cs += 0.05)
    {
        double mean = 0.0;
        for (int i = 0; i < 400; ++i)
        {
            mean += primary_cover(tex, 0.0113 * i, 0.0071 * i, cs, 0.42, 0.0, d1, d2);
        }
        check(mean >= prev - 1e-12, "the cover grows with the coverage slider", cs, mean);
        prev = mean;
    }
}

void test_light()
{
    SkyLight s;
    s.sunlight = { 0.7, 0.6, 0.5 };
    s.ambient = { 0.2, 0.25, 0.3 };
    s.cloud_color = { 0.4, 0.4, 0.4 };
    s.cloud_T = { 1.0, 1.0, 1.0 };
    s.cloud_shadow = 0.27;
    s.moon_blend = 0.0;
    s.hdr_scale = 1.0;
    CloudLight l = cloud_light(s);
    // the flat layer's sun term at T = 1: 2 * sunlight * cloud_color, made linear
    check(near(l.sun.r, srgb_to_linear(2.0 * 0.7 * 0.4), 1e-12), "sun term", l.sun.r);
    check(near(l.ambient.g, srgb_to_linear(2.0 * (0.25 + 0.75 * 0.27 * 0.5) * 0.4), 1e-12), "ambient term with the cloud lift", l.ambient.g);
    // night: the clouds' sun hands over to nothing (cloudsV: the glow fades out with cloud_moon_blend)
    s.moon_blend = 1.0;
    l = cloud_light(s);
    check(l.sun.r == 0.0 && l.sun.g == 0.0 && l.sun.b == 0.0, "night: no sun on the clouds");
    check(l.ambient.b > 0.0, "night: the ambient stays");
    // the clamp at 2 before linearising, and the HDR scale
    s.moon_blend = 0.0;
    s.sunlight = { 3.0, 3.0, 3.0 };
    s.cloud_color = { 1.0, 1.0, 1.0 };
    s.hdr_scale = 2.0;
    l = cloud_light(s);
    check(near(l.sun.r, srgb_to_linear(2.0) * 2.0, 1e-12), "the flat layer's clamp", l.sun.r);
    // the transmittance to the cloud layer dims the sun in linear light
    s.sunlight = { 0.5, 0.5, 0.5 };
    s.cloud_T = { 0.25, 0.5, 1.0 };
    s.hdr_scale = 1.0;
    l = cloud_light(s);
    check(l.sun.r < l.sun.g && l.sun.g < l.sun.b, "a reddened sun at the cloud layer is reddish on the clouds");
    check(near(l.sun.r, srgb_to_linear(2.0 * linear_to_srgb(srgb_to_linear(0.5) * 0.25)), 1e-12), "the transmittance in linear light", l.sun.r);
}

void test_key()
{
    Sliders s;
    const double d1[3] = { 0.5, 0.526, 1.0 };
    const double d2[3] = { 0.5, 0.526, 0.12 };
    const Scape sc = make(s, TUNING);
    const uint64_t k0 = weather_key(s, sc, d1, d2);
    check(k0 == weather_key(s, make(s, TUNING), d1, d2), "the key is stable");
    auto changed = [&](Sliders t, const double* a, const double* b) { return weather_key(t, make(t, TUNING), a, b) != k0; };
    Sliders t = s;
    t.coverage += 0.01;
    check(changed(t, d1, d2), "coverage changes the key");
    t = s;
    t.scale += 0.01;
    check(changed(t, d1, d2), "scale changes the key");
    t = s;
    t.variance = 0.1;
    check(changed(t, d1, d2), "variance changes the key");
    t = s;
    t.detail = 0.5;
    check(changed(t, d1, d2), "detail changes the key");
    const double d1b[3] = { 0.51, 0.526, 1.0 };
    check(changed(s, d1b, d2), "the creator's offset changes the key");
    const double d2b[3] = { 0.5, 0.526, 0.2 };
    check(changed(s, d1, d2b), "the detail weight changes the key");
    check(wrap_km(-1.0) == WRAP_KM - 1.0 && wrap_km(WRAP_KM + 2.5) == 2.5, "wrap_km");
    check(near(wrap_km(-1.0, 12.6), 11.6, 1e-12) && near(wrap_km(25.3, 12.6), 0.1, 1e-12), "wrap_km in the map's period");
    // the noises' periods divide the wrap
    for (double p : { BASE_KM, BASE_KM * 0.5, BASE_KM * 0.25, 0.5, 3.0 })
    {
        check(near(WRAP_KM / p, std::round(WRAP_KM / p), 1e-9), "WRAP_KM holds whole noise periods", p);
    }
}
} // namespace

int main()
{
    test_aurora_ladder();
    test_extinction();
    test_types();
    test_slider_ranges();
    test_variability();
    test_light();
    test_key();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("llcloudscape: all checks passed\n");
    return 0;
}
