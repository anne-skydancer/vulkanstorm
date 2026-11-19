// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llatmospherebase.h, the physical base sky of stage AT2-1 (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llatmospherebase_test.cpp
//
//   - the clear high-pressure day: the aerosol column is 0.03 at 550 nm, the ground visibility about 101 km, the columns of Rayleigh and
//     ozone the published ones, the noon transmittance exp(-column) per channel, no slab, no excess, nothing clamped;
//   - its cut altitudes (what the GLSL integrates the transmittance between);
//   - its sky: blue at the zenith, brighter towards the horizon, under the white card outside 10 degrees of the sun at every elevation,
//     the moon's sky a quarter of the sun's at most; the sky irradiance a reported number;
//   - the lights: E = 3 x scale x lin(source), the moon's gain bounded, the bits set;
//   - the `Atmosphere` block of the base carries its values, its lights and the camera height.

#include "llatmospherebase.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace LLAtmosphericLight;

namespace
{
int failures = 0;

void check(bool cond, const char* what, double a = 0.0, double b = 0.0)
{
    if (!cond)
    {
        std::printf("FAIL %s (%g, %g)\n", what, a, b);
        ++failures;
    }
}

bool near(double a, double b, double rel, double abs_tol = 0.0)
{
    return std::fabs(a - b) <= rel * std::fmax(std::fabs(a), std::fabs(b)) + abs_tol;
}

constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;

Vec3 dir_of(double el_deg, double az_deg = 0.0)
{
    const double e = el_deg * DEG, a = az_deg * DEG;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

struct Tables
{
    TransmittanceLUT t;
    MsLUT ms;
    SkyIrradianceLUT e;
};

// the tables of AT2-1, built as the passes build them: T, then Psi from T, then E from T and Psi
void bind_tables(LLScatter& s, Tables& tb)
{
    s.t_lut = nullptr;
    s.ms_lut = nullptr;
    s.e_lut = nullptr;
    s.build_t_lut(tb.t);
    s.t_lut = &tb.t;
    s.build_ms_lut(tb.ms);
    s.ms_lut = &tb.ms;
    s.build_e_lut(tb.e);
    s.e_lut = &tb.e;
}
} // namespace

int main()
{
    // 1. the extinction of the clear day
    const LLAtmosphere a = clear_day_atmosphere();
    {
        const double top = a.top_radius_km - a.planet_radius_km;
        const double aod = a.aerosol_ext * detail::profile_column_km(a.mie, top);
        check(near(aod, 0.03, 1e-12), "the aerosol optical depth is 0.03", aod);
        const double vis = ground_visibility_km(a);
        std::printf("clear day: aerosol optical depth %.4f, ground visibility %.1f km (Koschmieder, 550 nm)\n", aod, vis);
        check(vis > 95.0 && vis < 110.0, "the ground visibility is about 100 km", vis);
        // Rayleigh's column: beta x 8 km (1 - e^-7.5); ozone's: 15 km at density 1 (Bruneton's tent)
        const double he_r = detail::profile_column_km(a.rayleigh, top), he_o = detail::profile_column_km(a.absorption, top);
        check(near(he_r, 8.0 * (1.0 - std::exp(-60.0 / 8.0)), 1e-9), "Rayleigh's column is its 8 km scale height", he_r);
        check(near(he_o, 15.0, 1e-9), "ozone's column is 15 km at density 1", he_o);
        check(a.clamped == 0 && a.excess.x == 0.0 && a.excess.y == 0.0 && a.excess.z == 0.0 && a.slab_ext == 0.0, "nothing clamped, no excess, no slab");
        // the noon transmittance per channel is exp(-column)
        const Vec3 t = a.physical_transmittance(0.0, 1.0, 0.0);
        check(near(t.x, std::exp(-a.column.x), 1e-9) && near(t.y, std::exp(-a.column.y), 1e-9) && near(t.z, std::exp(-a.column.z), 1e-9),
              "the noon transmittance is exp(-column)", t.y, std::exp(-a.column.y));
        std::printf("clear day: noon transmittance %.4f %.4f %.4f (columns %.4f %.4f %.4f)\n", t.x, t.y, t.z, a.column.x, a.column.y, a.column.z);
        check(t.z < t.y && t.y < t.x, "the noon sun is reddened by Rayleigh scattering (blue lost most)");
    }

    // 2. the cut altitudes: the ground, Rayleigh's 4 scale heights, the aerosol's 4, 10 and 20, ozone's 10, 25 and 40 km
    {
        double cuts[detail::MAX_ALTITUDES];
        const int n = a.cut_altitudes_km(cuts, detail::MAX_ALTITUDES);
        std::sort(cuts, cuts + n);
        const double expect[] = { 0.0, 4.8, 10.0, 12.0, 24.0, 25.0, 32.0, 40.0 };
        check(n == 8 && a.cut_count == 8, "eight cut altitudes", n, a.cut_count);
        for (int i = 0; i < std::min(n, 8); ++i)
        {
            check(near(cuts[i], expect[i], 1e-9, 1e-9), "a cut altitude of the clear day", cuts[i], expect[i]);
        }
    }

    // 3. the sky of the clear day
    {
        LLScatter s = clear_day_scatter();
        check(s.anisotropy == ClearDay::AEROSOL_ASYMMETRY && s.glow_norm == 0.0 && s.bounds.ground_albedo == ClearDay::GROUND_ALBEDO, "the base's phase and ground");
        check(near(s.aerosol_scat, 0.95 * s.atm.aerosol_ext, 1e-12) && near(s.rayleigh_scat.y, s.atm.rayleigh_ext.y, 1e-12), "the base's albedos");
        double worst = 0.0;
        for (double sun_el : { 5.0, 30.0, 70.0 })
        {
            const Vec3 sun = dir_of(sun_el, 0.0);
            check(set_clear_day_lights(s, sun, { -sun.x, -sun.y, -sun.z }, { 1.0, 1.0, 1.0 }, 1.5, 0.0, 0.0046, 0.0046) == 0, "white sun lights clamp nothing");
            s.lights.count = 1;
            Tables tb;
            bind_tables(s, tb);
            const double card = luminance(s.E_sun()) / PI;
            for (double el : { 0.0, 2.0, 10.0, 30.0, 60.0, 90.0 })
            {
                for (double az : { 0.0, 60.0, 120.0, 180.0 })
                {
                    const Vec3 d = dir_of(el, az);
                    if (std::acos(std::min(1.0, detail::dot(d, s.lights.dir[0]))) < 10.0 * DEG)
                    {
                        continue;
                    }
                    worst = std::max(worst, luminance(s.sky_radiance(d, 0.0)) / card);
                }
            }
            if (sun_el == 30.0)
            {
                const Vec3 z = s.sky_radiance(dir_of(90.0), 0.0), h = s.sky_radiance(dir_of(2.0, 180.0), 0.0);
                std::printf("clear day, sun 30 degrees: zenith %.4f %.4f %.4f, horizon opposite the sun %.4f %.4f %.4f (E_sun 4.5)\n", z.x, z.y, z.z, h.x, h.y, h.z);
                check(z.z > z.y && z.y > z.x, "the clear sky is blue at the zenith", z.x, z.z);
                check(luminance(h) > luminance(z), "the clear sky is brighter towards the horizon", luminance(h), luminance(z));
                // the sky irradiance on a horizontal surface, per unit sun, and the capture irradiance it gives (reported)
                const Vec3 e = tb.e.at(0.0, std::sin(30.0 * DEG));
                std::printf("clear day, sun 30 degrees: sky irradiance %.4f %.4f %.4f per unit sun; lum(E_sky E_sun) / pi = %.4f\n", e.x, e.y, e.z,
                            luminance(e * s.E_sun()) / PI);
                check(e.y > 0.02 && e.y < 0.5, "the sky irradiance per unit sun is of the order of a tenth", e.y);
            }
        }
        std::printf("clear day: largest sky luminance / white card outside 10 degrees of the sun: %.3f\n", worst);
        check(worst > 0.01 && worst <= 1.0, "the clear sky is under the white card", worst);
    }

    // 4. the lights: E = 3 x scale x lin(source); the moon at most a quarter of the sun, its bit set
    {
        LLScatter s = clear_day_scatter();
        const unsigned c = set_clear_day_lights(s, dir_of(40.0), dir_of(-40.0, 180.0), { 1.0, 0.5, 0.25 }, 1.5, 0.7, 0.0046, 0.0046);
        check(near(s.E_sun().x, 4.5, 1e-12) && near(s.E_sun().y, 4.5 * srgb_to_linear(0.5), 1e-12), "E_sun = 3 x scale x lin(source)", s.E_sun().y);
        check((c & SCLAMP_MOON_GAIN) && near(s.E_moon().x, 0.25 * s.E_sun().x, 1e-12), "the moon's gain is bounded at a quarter", s.E_moon().x);
        check(near(detail::len(s.lights.dir[0]), 1.0, 1e-12) && s.lights.dir[0].z > 0.0, "the sun's direction is set");
    }

    // 5. the block of the base: its values, its lights, the camera height
    {
        LLScatter s = clear_day_scatter();
        set_clear_day_lights(s, dir_of(25.0, 10.0), dir_of(-25.0, 190.0), { 0.9, 0.8, 0.7 }, 1.5, 0.1, body_disc_sin(1.0), body_disc_sin(2.0));
        const AtmosphereBlock b = fill_atmosphere_block(s, 0.123);
        check(b.cut_count == 8.0f && b.cuts[0] == 0.0f && b.ground_albedo == float(ClearDay::GROUND_ALBEDO) && b.aniso == float(ClearDay::AEROSOL_ASYMMETRY),
              "the block carries the base", b.cut_count);
        check(b.glow_phase[2] == 0.0f && b.ms_scale[1] == 1.0f && b.f_ms_max == 0.6f && b.slab_ext == 0.0f && b.slab_top_km == 0.0f, "the base's scattering in the block");
        check(b.light_count == 2 && b.light_E[0][0] == float(s.E_sun().x) && b.light_dir[1][2] == float(s.lights.dir[1].z), "the block carries the lights");
        check(b.light_disc[0][0] == float(body_disc_sin(1.0)) && b.light_disc[1][0] == float(body_disc_sin(2.0)), "the block carries the discs");
        check(b.camera_km == 0.123f && b.planet_radius_km == 6360.0f && b.top_radius_km == 6420.0f, "the block carries the radii and the camera height");
        check(b.mie.layer0[2] == float(-1.0 / ClearDay::AEROSOL_SCALE_HEIGHT_M) && b.absorption.constants[2] == 2.0f && b.rayleigh.constants[2] == 1.0f,
              "the block carries the profiles");
    }

    // 6. the froxel step count over the froxel domain (AT2-1's gate; the froxels march the base, AT2-3): the aerial perspective at 8, 12
    //    and 16 steps against 1024, from the ground to 4 km, every elevation from -10 to 90 degrees, out to the draw distance (32 m to
    //    1 km) and beyond it to 10 km, the sun at 5 and 30 degrees with the moon opposite. Relative to the larger of the value and 1e-3 of
    //    its brightest channel. 12 steps (the study's choice, RenderAtmosphereFroxelSteps in AT2-3) must be within 1 %.
    {
        LLScatter s = clear_day_scatter();
        double worst[3] = {}, w_d = 0.0, w_h = 0.0, w_el = 0.0;
        int n = 0;
        for (double sun_el : { 5.0, 30.0 })
        {
            const Vec3 sun = dir_of(sun_el, 0.0);
            set_clear_day_lights(s, sun, { -sun.x, -sun.y, -sun.z }, { 1.0, 1.0, 1.0 }, 1.5, 2.5e-6, 0.0046, 0.0046);
            Tables tb;
            bind_tables(s, tb);
            for (double h : { 0.0, 0.002, 0.03, 0.1, 0.5, 2.0, 4.0 })
            {
                for (double el : { -10.0, -3.0, -1.0, 0.0, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0, 90.0 })
                {
                    for (double az : { 0.0, 90.0, 180.0 })
                    {
                        const Vec3 d = dir_of(el, az);
                        const double tg = s.ground_distance(h, d);
                        for (double dist : { 0.032, 0.064, 0.128, 0.256, 0.512, 1.024, 2.0, 10.0 })
                        {
                            if (tg >= 0.0 && dist > tg)
                            {
                                continue;
                            }
                            Vec3 Lr, Tr;
                            s.atmosphere_ap(d, dist, h, 1024, Lr, Tr);
                            const double mx = std::max(detail::vmax(Lr), 1e-15);
                            const int steps[3] = { 8, 12, 16 };
                            for (int q = 0; q < 3; ++q)
                            {
                                Vec3 L, T;
                                s.atmosphere_ap(d, dist, h, steps[q], L, T);
                                const double a[3] = { L.x, L.y, L.z }, b[3] = { Lr.x, Lr.y, Lr.z };
                                for (int c = 0; c < 3; ++c)
                                {
                                    const double e = std::fabs(a[c] - b[c]) / std::max(std::fabs(b[c]), 1e-3 * mx);
                                    if (e > worst[q])
                                    {
                                        worst[q] = e;
                                        if (q == 1)
                                        {
                                            w_d = dist;
                                            w_h = h;
                                            w_el = el;
                                        }
                                    }
                                }
                            }
                            ++n;
                        }
                    }
                }
            }
        }
        std::printf("froxel domain of the base, %d rays: worst in-scatter error at 8 / 12 / 16 steps %.2f / %.2f / %.2f %% (12 steps: at %g km, h %g km, el %g)\n", n,
                    100.0 * worst[0], 100.0 * worst[1], 100.0 * worst[2], w_d, w_h, w_el);
        check(worst[1] <= 0.01, "12 froxel steps are within 1 % of a converged march over the froxel domain", worst[1]);
    }

    if (failures)
    {
        std::printf("llatmospherebase_test: %d FAILED\n", failures);
        return 1;
    }
    std::printf("llatmospherebase_test: OK\n");
    return 0;
}
