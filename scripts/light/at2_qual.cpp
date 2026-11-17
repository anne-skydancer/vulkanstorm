// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// AT2 qualification, phase 1 (offline, CPU only): can a table hold upstream Firestorm's sky pass?
//
// Owner decision (2026-11-17): "today's look would be the sky under PR #543 rules, after AT1 and AT2 land and EEP applied as filters on
// top of the physical sky." The reference is UPSTREAM's sky pass (what creators tuned against), not our drifted skyV/skyF:
//
//   reference U   a C++ twin of upstream/master class1/deferred/skyF.glsl (skyV only passes the position on), then the lighting pass of
//                 upstream's softenLightF.glsl for a sky pixel (srgb_to_linear, x sky_hdr_scale, x final_scale 1.1 in classic mode,
//                 clampHDRRange 11.2), with llsettingsvo.cpp's choice of path and its INSTANT day / night switch (sun_up_factor,
//                 sun_moon_glow_factor, moonlight x 0.7, no glow for the moon). The display is upstream's own chain (see display_codes in
//                 at2f_creator_grade.cpp). The reference follows upstream; ours keeps the L3 blend, and the band differences are accepted.
//   variants      what a table in the sky pass could store (the physical sky P never enters: a table that holds the product holds the
//                 additive part too; P is computed only on request, to label why a sky is hard):
//     a  the PRODUCT table in final linear (the AT2-F / #543 structure), sampled bilinearly;
//     b  a CODED-domain table: min(2 pre, 5), the coded sRGB value skyF writes, interpolated, then srgb_to_linear x hdr per pixel;
//     c  WHOLE-SKY A / B: two tables A (pre colour at haze_glow 0) and B (its slope in haze_glow; skyF's colour is exactly linear in
//        haze_glow), then pre = A + B haze_glow per pixel with the exact glow (one pow), x2, clamp 5, srgb_to_linear, no zone, no blend;
//     ns5, ns10, ns20  variant a plus an analytic glow zone of N degrees (A / B inside, blended to a out to 1.5 N); for comparison only.
//   known         OUR current shaders (the L3 handover band) against upstream, so the band is never counted as a failure.
//
// Modes: default (all variants per sky), --anchor, --pano DIR VARIANT, --blend T, --dome (viewer geometry and per-vertex evaluation).
//
//   g++ -std=c++20 -O2 -pthread -Wno-unused-function -I indra/newview scripts/light/at2_qual.cpp -o at2_qual
//   at2_qual [--physical] skies.txt         one row per sky (scripts/light/at2_qual.py writes them)
//
// COUPLING: this file #includes at2f_creator_grade.cpp (the AT2-F tool) with its main renamed, to share its twin of our skyV, the physical
// sky and the display, in one translation unit (they live in its anonymous namespace). Change those helpers there, and rerun both tools.

#define main at2f_unused_main
#include "at2f_creator_grade.cpp"
#undef main

#include <atomic>
#include <thread>

namespace
{
constexpr double kClamp11 = 11.2; // clampHDRRange (deferredUtil.glsl)

struct QSky
{
    SkyRow s;
    double moon = 0.5;   // moon_brightness (the library carries none: the default 0.5)
    double light_el = 0; // elevation of the up light body (the sun by day, the moon by night), degrees
    int sun_up = 1;
    double sun_el = 0;   // the sun's own elevation, degrees (the L3 band weight comes from it)
};

struct Geo
{
    double nx, ny, nz, rlen;
};
struct Dir3
{
    double x, y, z;
};

// skyF.glsl: rel_pos clamped to max_y above the horizon and to the plane at -32000 below; the output depends on its direction only
Geo geo_from_rel(const SkyRow& s, double px, double py, double pz)
{
    if (py > 0.0)
    {
        const double f = s.my / py;
        px *= f; py *= f; pz *= f;
    }
    if (py < 0.0)
    {
        const double f = -32000.0 / py;
        px *= f; py *= f; pz *= f;
    }
    const double rlen = std::sqrt(px * px + py * py + pz * pz);
    return { px / rlen, py / rlen, pz / rlen, rlen };
}

// the bench's geometry: the direction x 15000 (+ 50 m in y, camPosLocal 0)
Geo geo_of(const SkyRow& s, double el_deg, double az_deg)
{
    const double e = el_deg * DEG, a = az_deg * DEG;
    const double R = 15000.0;
    return geo_from_rel(s, std::cos(e) * std::cos(a) * R, std::sin(e) * R + 50.0, std::cos(e) * std::sin(a) * R);
}

// the viewer's geometry: the dome is a pi/8 cap of radius R = 15000 seen from y = 0.96 R (lldrawpoolwlsky.cpp:303-308, DOME_OFFSET), the
// vertex at distance rho from the camera in the view direction; skyF adds 50 m in y to rel_pos
constexpr double kDomeR = 15000.0, kDomeH = 0.96 * 15000.0;
double dome_rho(double sin_el)
{
    return -kDomeH * sin_el + std::sqrt(kDomeH * kDomeH * sin_el * sin_el - kDomeH * kDomeH + kDomeR * kDomeR);
}
Geo geo_viewer(const SkyRow& s, double el_deg, double az_deg)
{
    const double e = el_deg * DEG, a = az_deg * DEG;
    const double rho = dome_rho(std::sin(e));
    return geo_from_rel(s, std::cos(e) * std::cos(a) * rho, std::sin(e) * rho + 50.0, std::cos(e) * std::sin(a) * rho);
}

// upstream's lightnorm: LLEnvironment::getClampedLightNorm, light direction with its z (up) clamped at -0.1; the bench normalises it
Dir3 light_vec(double light_el_deg)
{
    const double e = light_el_deg * DEG;
    double x = std::cos(e), y = std::max(std::sin(e), -0.1);
    const double n = std::sqrt(x * x + y * y);
    return { x / n, y / n, 0.0 };
}

// skyF.glsl's colour before the x2: the haze colour above and below the clouds, as a function of the colour blend (0: sunlight_color,
// 1: moonlight x 0.7), the glow multiplier gscale (haze_glow = gscale x (glow + 0.25)), the light direction (for off_axis) and the glow's
// direction. hg_fixed, when not null, replaces haze_glow (the colour is linear in it). max_y 0: rel_pos collapses to a point, normalize(0)
// is NaN in the shader and clampHDRRange (softenLightF) maps NaN to 0: the sky is black (checked on the GPU), returned as 0 here.
V3 sky_pre(const SkyRow& s, const Geo& g, const Dir3& L, const Dir3& Gl, double blend, double gscale, const double* hg_fixed)
{
    if (!(s.my > 0.0))
    {
        return {};
    }
    const double dot_g = g.nx * Gl.x + g.ny * Gl.y + g.nz * Gl.z;
    double light_atten[3], combined[3], blue_w[3], haze_w[3], sunlight[3];
    const double off_axis = 1.0 / std::max(1e-6, std::max(0.0, g.ny) + L.y);
    for (int c = 0; c < 3; ++c)
    {
        light_atten[c] = (s.bd[c] + s.hd * 0.25) * (s.dm * s.my);
        combined[c] = std::max(std::fabs(s.bd[c]) + std::fabs(s.hd), 1e-6);
        blue_w[c] = s.bd[c] / combined[c];
        haze_w[c] = s.hd / combined[c];
        const double sl = s.sun[c] * (1.0 - blend) + s.sun[c] * 0.7 * blend; // the moon's light colour is the sun's (getMoonlightColor)
        sunlight[c] = sl * std::exp(-light_atten[c] * off_axis);
    }
    const double density_dist = g.rlen * s.dm;
    double hg;
    if (hg_fixed)
    {
        hg = *hg_fixed;
    }
    else
    {
        hg = std::max(1.0 - dot_g, 0.001);
        hg *= s.glow[0];
        hg = std::pow(hg, s.glow[2]);
        hg = gscale * (hg + 0.25);
    }
    V3 out;
    double* o[3] = { &out.r, &out.g, &out.b };
    for (int c = 0; c < 3; ++c)
    {
        const double ch = std::exp(-combined[c] * density_dist);
        double color = s.bh[c] * blue_w[c] * (sunlight[c] + s.amb[c]) + (s.hh * haze_w[c]) * (sunlight[c] * hg + s.amb[c]);
        color *= (1.0 - ch);
        const double ambient = s.amb[c] + std::max(0.0, 1.0 - s.amb[c]) * s.cs * 0.5;
        const double sun_c = sunlight[c] * std::max(0.0, 1.0 - s.cs);
        const double below = s.bh[c] * blue_w[c] * (sun_c + ambient) + (s.hh * haze_w[c]) * (sun_c * hg + ambient);
        const double ch2 = std::sqrt(ch);
        color += (below - color) * (1.0 - std::sqrt(ch2));
        *o[c] = color;
    }
    return out;
}

// the haze_glow of a direction (what variant c and the near-sun zones evaluate analytically per pixel)
double glow_of(const SkyRow& s, const Geo& g, const Dir3& Gl, double gscale)
{
    double hg = std::max(1.0 - (g.nx * Gl.x + g.ny * Gl.y + g.nz * Gl.z), 0.001);
    hg *= s.glow[0];
    hg = std::pow(hg, s.glow[2]);
    return gscale * (hg + 0.25);
}

// skyF.glsl's end: x2, clamp 0..5 (the CODED value, still sRGB-encoded)
V3 coded_of(V3 pre)
{
    return { std::min(std::max(pre.r * 2.0, 0.0), 5.0), std::min(std::max(pre.g * 2.0, 0.0), 5.0), std::min(std::max(pre.b * 2.0, 0.0), 5.0) };
}

// the lighting pass of the sky's path (softenLightF.glsl, SKIP_ATMOS branch) from the coded value: srgb_to_linear x sky_hdr_scale
// (x final_scale, 1.1 when classic), clampHDRRange (0..11.2). rainbow and halo22 are 0 (moisture and ice levels 0).
V3 final_from_coded(V3 coded, const SkyRow& s, int path)
{
    double hdr = 1.0;
    if (path == kPbr)
    {
        hdr = 2.0 * std::sqrt(std::max(s.gamma, 0.0)); // sqrtf(g) * 2
    }
    else if (path == kClassic)
    {
        hdr = 1.1; // sky_hdr_scale 1 (the auto-adjust is off by default) x final_scale 1.1
    }
    V3 r;
    double* o[3] = { &r.r, &r.g, &r.b };
    const double in[3] = { coded.r, coded.g, coded.b };
    for (int c = 0; c < 3; ++c)
    {
        *o[c] = std::min(std::max(srgb_to_linear(in[c]) * hdr, 0.0), kClamp11);
    }
    return r;
}
V3 sky_final(V3 pre, const SkyRow& s, int path) { return final_from_coded(coded_of(pre), s, path); }

// ---- the reference: upstream's sky pass ---------------------------------------------------------------------------------------------
struct Up
{
    Dir3 L;
    double blend, gscale;
};

Up up_of(const QSky& q)
{
    Up u;
    u.L = light_vec(q.light_el);
    u.blend = q.sun_up ? 0.0 : 1.0;
    // getSunMoonGlowFactor: 1 by day; by night the moon's brightness x 0.25; skyF: below 1 no glow at all, else factor x (glow + 0.25)
    const double gf = q.sun_up ? 1.0 : q.moon * 0.25;
    u.gscale = gf < 1.0 ? 0.0 : gf;
    return u;
}

V3 upstream_pre(const QSky& q, const Up& u, const Geo& g, const double* hg_fixed = nullptr)
{
    return sky_pre(q.s, g, u.L, u.L, u.blend, u.gscale, hg_fixed);
}
V3 upstream_sky(const QSky& q, const Up& u, double el, double az)
{
    return sky_final(upstream_pre(q, u, geo_of(q.s, el, az)), q.s, q.s.path);
}

// ---- ours currently (L3: the sun/moon handover band): the AT2-F twin's formulas, extended to the night -------------------------------
V3 current_pre(const QSky& q, double blend, const Geo& g)
{
    const Dir3 L = light_vec(q.light_el);
    const Dir3 Gl = light_vec(q.sun_el); // sun_glow_norm: the clamped sun norm
    return sky_pre(q.s, g, L, Gl, blend, 1.0 - blend, nullptr);
}
V3 current_sky(const QSky& q, double blend, double el, double az)
{
    return sky_final(current_pre(q, blend, geo_of(q.s, el, az)), q.s, q.s.path);
}

// ---- ours: the tables ----------------------------------------------------------------------------------------------------------------
enum Variant { kVA, kVB, kVC, kVN5, kVN10, kVN20, kVariants };
const char* const kVName[kVariants] = { "a", "b", "c", "ns5", "ns10", "ns20" };
const double kZone[kVariants] = { 0, 0, 0, 5.0, 10.0, 20.0 };

struct Tables
{
    std::vector<V3> W, C, A, B; // a: final linear; b: coded min(2 pre, 5); c: pre at haze_glow 0 and its slope in haze_glow
    // the A / B self-check at the nodes: max |A + B haze_glow - pre| / max(1, |pre|, |A|, |B haze_glow|). B = pre(1) - pre(0) is a finite
    // difference, so a sky with a huge haze_glow (Das fog) loses about 1e-8 to rounding: the limit is 1e-6
    double ab_err = 0.0;
};

void build_tables(const QSky& q, const Up& u, Tables& t)
{
    const size_t n = static_cast<size_t>(kNEl) * kNAz;
    t.W.resize(n); t.C.resize(n); t.A.resize(n); t.B.resize(n);
    const double zero = 0.0, one = 1.0;
    for (int i = 0; i < kNEl; ++i)
    {
        for (int j = 0; j < kNAz; ++j)
        {
            const Geo g = geo_of(q.s, grade_el(i), grade_az(j));
            const int x = i * kNAz + j;
            const V3 pre = upstream_pre(q, u, g);
            t.W[x] = sky_final(pre, q.s, q.s.path);
            t.C[x] = coded_of(pre);
            const V3 a = upstream_pre(q, u, g, &zero), b1 = upstream_pre(q, u, g, &one);
            t.A[x] = a;
            t.B[x] = { b1.r - a.r, b1.g - a.g, b1.b - a.b };
            const double hg = glow_of(q.s, g, u.L, u.gscale);
            const double p[3] = { pre.r, pre.g, pre.b }, av[3] = { a.r, a.g, a.b }, bh[3] = { t.B[x].r * hg, t.B[x].g * hg, t.B[x].b * hg };
            for (int c = 0; c < 3; ++c)
            {
                t.ab_err = std::max(t.ab_err, std::fabs(av[c] + bh[c] - p[c]) / std::max({ 1.0, std::fabs(p[c]), std::fabs(av[c]), std::fabs(bh[c]) }));
            }
        }
    }
}

double smooth_step(double e0, double e1, double x)
{
    const double t = std::min(std::max((x - e0) / (e1 - e0), 0.0), 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// variant c's pixel: the tables' A and B, the exact glow
V3 ab_sky(const QSky& q, const Up& u, const Tables& t, double el, double az, const Geo& g)
{
    const V3 a = bilinear(t.A, el, az, false), b = bilinear(t.B, el, az, false);
    return sky_final(a + b * glow_of(q.s, g, u.L, u.gscale), q.s, q.s.path);
}

V3 ours_sky(const QSky& q, const Up& u, const Tables& t, double el, double az, int v)
{
    switch (v)
    {
    case kVA:
        return bilinear(t.W, el, az, false);
    case kVB:
        return final_from_coded(bilinear(t.C, el, az, false), q.s, q.s.path);
    case kVC:
        return ab_sky(q, u, t, el, az, geo_of(q.s, el, az));
    default:
        break;
    }
    // the N-degree zones (for comparison): the product table, with variant c's glow within N degrees, blended out by 1.5 N
    const V3 prod = bilinear(t.W, el, az, false);
    const double n_deg = kZone[v];
    const double w = smooth_step(n_deg, 1.5 * n_deg, sep_deg(q.light_el, el, az)); // 1: the table
    if (w >= 1.0)
    {
        return prod;
    }
    return ab_sky(q, u, t, el, az, geo_of(q.s, el, az)) * (1.0 - w) + prod * w;
}

// ---- measuring -----------------------------------------------------------------------------------------------------------------------
constexpr int kQEl = 180, kQAz = 72; // 0.5 x 2.5 degree cells over the visible hemisphere (azimuth 0..180 by symmetry)
constexpr double kViewEl = 40.0;     // the view-weighted mean: elevation 0..40 degrees, where the eye looks

struct Stat
{
    double mean = 0.0, vmean = 0.0, worst = 0.0, el = 0.0, sep = 0.0;
};

struct QResult
{
    Stat v[kVariants];
    Stat cur; // our current shaders (L3 band) against upstream
    double band = 0.0;
    double starved = -1.0, starved_zero = -1.0; // the physical sky's share with a starved channel / exactly 0 (--physical, day only; -1 not computed)
    double w_max = 0.0;                         // brightest channel of upstream's sky over the cells (linear)
    double sat = 0.0;                           // share of cells (solid angle) where upstream's skyF output clamps at 5 in some channel
    double ab_err = 0.0;                        // variant c's self-check: A + B haze_glow against the sky's colour, at the nodes and at sample cells
};

QResult measure_q(const QSky& q, bool physical)
{
    QResult r;
    const Up u = up_of(q);
    Tables t;
    build_tables(q, u, t);
    r.ab_err = t.ab_err;
    r.band = band_weight(q.sun_el);
    const int gm = q.s.path;
    const double gamma = q.s.gamma;
    double sw = 0.0, vw = 0.0, mean[kVariants] = {}, vmean[kVariants] = {}, cmean = 0.0;
    const double zero = 0.0, one = 1.0;
    int cell = 0;
    for (int i = 0; i < kQEl; ++i)
    {
        const double el = 0.25 + 0.5 * i;
        const double w = std::cos(el * DEG);
        for (int j = 0; j < kQAz; ++j, ++cell)
        {
            const double az = 1.25 + 2.5 * j;
            const Geo g = geo_of(q.s, el, az);
            const V3 pre = upstream_pre(q, u, g);
            const V3 up = sky_final(pre, q.s, gm);
            const Lab lu = display_lab(up, gm, gamma);
            r.w_max = std::max({ r.w_max, up.r, up.g, up.b });
            if (pre.r * 2.0 >= 5.0 || pre.g * 2.0 >= 5.0 || pre.b * 2.0 >= 5.0)
            {
                r.sat += w;
            }
            if (cell % 11 == 0)
            {
                // the A / B self-check at a sample cell: exact A and B here, A + B haze_glow against the sky's colour
                const V3 a = upstream_pre(q, u, g, &zero), b1 = upstream_pre(q, u, g, &one);
                const double hg = glow_of(q.s, g, u.L, u.gscale);
                const double p[3] = { pre.r, pre.g, pre.b }, av[3] = { a.r, a.g, a.b }, bh[3] = { (b1.r - a.r) * hg, (b1.g - a.g) * hg, (b1.b - a.b) * hg };
                for (int c = 0; c < 3; ++c)
                {
                    r.ab_err = std::max(r.ab_err, std::fabs(av[c] + bh[c] - p[c]) / std::max({ 1.0, std::fabs(p[c]), std::fabs(av[c]), std::fabs(bh[c]) }));
                }
            }
            sw += w;
            if (el <= kViewEl)
            {
                vw += w;
            }
            const double sep = sep_deg(q.light_el, el, az);
            for (int k = 0; k < kVariants; ++k)
            {
                const double e = de76(lu, display_lab(ours_sky(q, u, t, el, az, k), gm, gamma));
                mean[k] += w * e;
                if (el <= kViewEl)
                {
                    vmean[k] += w * e;
                }
                if (e > r.v[k].worst)
                {
                    r.v[k].worst = e;
                    r.v[k].el = el;
                    r.v[k].sep = sep;
                }
            }
            const double e = de76(lu, display_lab(current_sky(q, r.band, el, az), gm, gamma));
            cmean += w * e;
            if (e > r.cur.worst)
            {
                r.cur.worst = e;
                r.cur.el = el;
                r.cur.sep = sep;
            }
        }
    }
    r.sat /= sw;
    for (int k = 0; k < kVariants; ++k)
    {
        r.v[k].mean = mean[k] / sw;
        r.v[k].vmean = vmean[k] / vw;
    }
    r.cur.mean = cmean / sw;

    if (physical && q.sun_up && q.s.my > 0.0)
    {
        // why a sky is hard for a physical start: the share of the sky (solid angle) where P has a starved channel or is exactly 0.
        // P never enters the comparison above; this only labels.
        Physical ph;
        build_physical(ph, q.s, q.sun_el);
        double st = 0.0, sz = 0.0, sa = 0.0;
        for (int i = 0; i < 30; ++i)
        {
            const double el = 1.5 + 3.0 * i;
            for (int j = 0; j < 18; ++j)
            {
                const V3 P = physical_sky(ph, el, 5.0 + 10.0 * j);
                const double lo = std::min({ P.r, P.g, P.b }), hi = std::max({ P.r, P.g, P.b });
                const double w = std::cos(el * DEG);
                sa += w;
                if (hi <= 0.0)
                {
                    sz += w;
                }
                else if (lo < 1e-3 * hi)
                {
                    st += w;
                }
            }
        }
        r.starved = st / sa;
        r.starved_zero = sz / sa;
    }
    return r;
}

// ---- viewer geometry and per-vertex evaluation (--dome) ----------------------------------------------------------------------------
// The viewer's dome (llvowlsky.cpp, WLSkyDetail 64: 64 stacks x 128 slices, calcPhi's phi, NEW_TESS undefined): rings i = 1.. of the
// sphere of radius 15000, and per-vertex haze (our skyV, the Firestorm 7.2.5 inheritance on release and devel; upstream moved to per-pixel
// in b9727dd841, in canary and Firestorm 7.3.0 beta). The rings above the horizon are regular; the last ones (the rim below it) are not used.
constexpr int kDomeStacks = 64, kDomeSlices = 128, kDomeRows = 61; // rows r = 0..60 are the rings i = r + 1 (the horizon is at about i = 53)

double calc_phi(int i)
{
    double t = static_cast<double>(i) / kDomeStacks;
    t *= t;
    t *= t;
    t = 1.0 - t;
    t = t * t;
    t = 1.0 - t;
    return PI / 8.0 * t;
}

struct DomeStat
{
    double mean = 0.0, worst = 0.0;
};

// psi: the sun's azimuth within a slice (0: on a slice's vertex, half: between two)
void dome_measure(const QSky& q, double psi_slices, DomeStat& geo, DomeStat& vert)
{
    const Up u = up_of(q);
    const double band = band_weight(q.sun_el);
    const double dth = 2.0 * PI / kDomeSlices, psi = psi_slices * dth;
    std::vector<V3> pre(static_cast<size_t>(kDomeRows) * kDomeSlices);
    std::vector<double> vx(pre.size()), vy(pre.size()), vz(pre.size());
    for (int r = 0; r < kDomeRows; ++r)
    {
        const double phi = calc_phi(r + 1);
        for (int j = 0; j < kDomeSlices; ++j)
        {
            const double th = dth * j - psi; // the sun's frame: the sun at azimuth 0
            const double x = kDomeR * std::sin(phi) * std::cos(th), y = kDomeR * std::cos(phi), z = kDomeR * std::sin(phi) * std::sin(th);
            const size_t k = static_cast<size_t>(r) * kDomeSlices + j;
            vx[k] = x; vy[k] = y; vz[k] = z;
            // our skyV: rel_pos = position - camPosLocal + (0, 50, 0), camPosLocal = (0, 0.96 R, 0)
            pre[k] = current_pre(q, band, geo_from_rel(q.s, x, y - kDomeH + 50.0, z));
        }
    }
    double sw = 0.0, sg = 0.0, sv = 0.0;
    const int gm = q.s.path;
    for (int i = 0; i < kQEl; ++i)
    {
        const double el = 0.25 + 0.5 * i, e = el * DEG, w = std::cos(e);
        for (int j = 0; j < 144; ++j)
        {
            const double az = 1.25 + 2.5 * j, a = az * DEG;
            const double dx = std::cos(e) * std::cos(a), dy = std::sin(e), dz = std::cos(e) * std::sin(a);
            // the reference: upstream's per-pixel sky at the viewer's geometry
            const V3 ref = sky_final(upstream_pre(q, u, geo_viewer(q.s, el, az)), q.s, gm);
            const Lab lr = display_lab(ref, gm, q.s.gamma);
            const double eg = de76(lr, display_lab(upstream_sky(q, u, el, az), gm, q.s.gamma)); // the bench's geometry at the same view direction
            // the ray from the camera (0, h, 0) to the dome: the sphere's hit, its ring and slice, the triangle (the strip's diagonal runs
            // from the lower ring's column c to the upper ring's column c + 1), the vertex colours interpolated across it
            const double rho = dome_rho(dy);
            const double hx = rho * dx, hy = kDomeH + rho * dy, hz = rho * dz;
            const double phi = std::acos(std::min(1.0, std::max(-1.0, hy / kDomeR)));
            int r = 0;
            while (r < kDomeRows - 2 && calc_phi(r + 2) <= phi)
            {
                ++r;
            }
            double thd = std::atan2(hz, hx) + psi;
            thd -= 2.0 * PI * std::floor(thd / (2.0 * PI));
            const int c = std::min(static_cast<int>(thd / dth), kDomeSlices - 1), c1 = (c + 1) % kDomeSlices;
            const size_t U0 = static_cast<size_t>(r) * kDomeSlices + c, U1 = static_cast<size_t>(r) * kDomeSlices + c1;
            const size_t L0 = static_cast<size_t>(r + 1) * kDomeSlices + c, L1 = static_cast<size_t>(r + 1) * kDomeSlices + c1;
            const size_t tri[2][3] = { { U0, L0, U1 }, { L0, U1, L1 } };
            double best_min = -1e30, bw[3] = { 0, 0, 0 };
            size_t bt = 0;
            for (int k = 0; k < 2; ++k)
            {
                // barycentric weights of the ray's hit on the triangle's plane (Moller-Trumbore, origin (0, h, 0))
                const size_t a0 = tri[k][0], a1 = tri[k][1], a2 = tri[k][2];
                const double e1x = vx[a1] - vx[a0], e1y = vy[a1] - vy[a0], e1z = vz[a1] - vz[a0];
                const double e2x = vx[a2] - vx[a0], e2y = vy[a2] - vy[a0], e2z = vz[a2] - vz[a0];
                const double px = dy * e2z - dz * e2y, py = dz * e2x - dx * e2z, pz = dx * e2y - dy * e2x;
                const double det = e1x * px + e1y * py + e1z * pz;
                if (std::fabs(det) < 1e-30)
                {
                    continue;
                }
                const double tx = -vx[a0], ty = kDomeH - vy[a0], tz = -vz[a0];
                const double uu = (tx * px + ty * py + tz * pz) / det;
                const double qx = ty * e1z - tz * e1y, qy = tz * e1x - tx * e1z, qz = tx * e1y - ty * e1x;
                const double vv = (dx * qx + dy * qy + dz * qz) / det;
                const double ww[3] = { 1.0 - uu - vv, uu, vv };
                const double mn = std::min({ ww[0], ww[1], ww[2] });
                if (mn > best_min)
                {
                    best_min = mn;
                    bt = static_cast<size_t>(k);
                    bw[0] = ww[0]; bw[1] = ww[1]; bw[2] = ww[2];
                }
            }
            const V3 p = pre[tri[bt][0]] * bw[0] + pre[tri[bt][1]] * bw[1] + pre[tri[bt][2]] * bw[2];
            const double ev = de76(lr, display_lab(sky_final(p, q.s, gm), gm, q.s.gamma));
            sw += w; sg += w * eg; sv += w * ev;
            geo.worst = std::max(geo.worst, eg);
            vert.worst = std::max(vert.worst, ev);
        }
    }
    geo.mean = sg / sw;
    vert.mean = sv / sw;
}

// ---- day-cycle blending (--blend T): rows come in triples A, B, M (M: the parameters lerped at T) -------------------------------------
struct BlendStat
{
    double mean[3] = {}, worst[3] = {}; // lerp of the a tables; lerp of the c tables (A, B) with M's exact glow; M's own c tables (rebuilt)
};

BlendStat blend_measure(const QSky& A, const QSky& B, const QSky& M, double tt)
{
    BlendStat s;
    const Up ua = up_of(A), ub = up_of(B), um = up_of(M);
    Tables ta, tb, tm;
    build_tables(A, ua, ta);
    build_tables(B, ub, tb);
    build_tables(M, um, tm);
    const size_t n = ta.W.size();
    std::vector<V3> LW(n), LA(n), LB(n);
    for (size_t i = 0; i < n; ++i)
    {
        LW[i] = ta.W[i] * (1.0 - tt) + tb.W[i] * tt;
        LA[i] = ta.A[i] * (1.0 - tt) + tb.A[i] * tt;
        LB[i] = ta.B[i] * (1.0 - tt) + tb.B[i] * tt;
    }
    double sw = 0.0, m[3] = {};
    const int gm = M.s.path;
    for (int i = 0; i < kQEl; ++i)
    {
        const double el = 0.25 + 0.5 * i, w = std::cos(el * DEG);
        for (int j = 0; j < kQAz; ++j)
        {
            const double az = 1.25 + 2.5 * j;
            const Geo g = geo_of(M.s, el, az);
            const Lab lu = display_lab(upstream_sky(M, um, el, az), gm, M.s.gamma);
            const V3 va = bilinear(LW, el, az, false);
            const V3 vc = sky_final(bilinear(LA, el, az, false) + bilinear(LB, el, az, false) * glow_of(M.s, g, um.L, um.gscale), M.s, gm);
            const V3 vr = ab_sky(M, um, tm, el, az, g);
            const V3 vs[3] = { va, vc, vr };
            for (int k = 0; k < 3; ++k)
            {
                const double e = de76(lu, display_lab(vs[k], gm, M.s.gamma));
                m[k] += w * e;
                s.worst[k] = std::max(s.worst[k], e);
            }
            sw += w;
        }
    }
    for (int k = 0; k < 3; ++k)
    {
        s.mean[k] = m[k] / sw;
    }
    return s;
}

// ---- rows and output -----------------------------------------------------------------------------------------------------------------
constexpr int kRowLen = 16384;

bool parse_q(char* line, QSky& q)
{
    char* tab = std::strchr(line, '\t');
    if (!tab)
    {
        return false;
    }
    *tab = 0;
    q.s.name = line;
    double v[28];
    char* p = tab + 1;
    for (int n = 0; n < 28; ++n)
    {
        char* end = nullptr;
        v[n] = std::strtod(p, &end);
        if (end == p)
        {
            return false;
        }
        p = end;
    }
    SkyRow& r = q.s;
    for (int c = 0; c < 3; ++c)
    {
        r.sun[c] = v[c];
        r.amb[c] = v[3 + c];
        r.bd[c] = v[6 + c];
        r.bh[c] = v[9 + c];
        r.glow[c] = v[18 + c];
    }
    r.hd = v[12];
    r.hh = v[13];
    r.cs = v[14];
    r.dm = v[15];
    r.dist = v[16];
    r.my = v[17];
    r.gamma = v[21];
    r.sun_angle = v[22];
    r.path = static_cast<int>(v[24]);
    q.moon = v[25];
    q.light_el = v[26];
    q.sun_up = static_cast<int>(v[27]);
    q.sun_el = std::asin(std::sin(r.sun_angle)) / DEG;
    return true;
}

void rgb8(V3 lin, int path, double gamma, unsigned char* out) { display_codes(lin, path, gamma, out); }

void heat(double de, unsigned char* out)
{
    // dE 0 black, 2 blue, 5 yellow, 10 red, 20+ white (piecewise linear)
    static const double stops[5] = { 0, 2, 5, 10, 20 };
    static const double col[5][3] = { { 0, 0, 0 }, { 0, 60, 200 }, { 255, 220, 0 }, { 230, 20, 20 }, { 255, 255, 255 } };
    if (!std::isfinite(de))
    {
        de = 20.0;
    }
    int k = 0;
    while (k < 3 && de > stops[k + 1])
    {
        ++k;
    }
    const double t = std::min(std::max((de - stops[k]) / (stops[k + 1] - stops[k]), 0.0), 1.0);
    for (int i = 0; i < 3; ++i)
    {
        out[i] = static_cast<unsigned char>(std::round(col[k][i] + (col[k + 1][i] - col[k][i]) * t));
    }
}

void write_pano(const QSky& q, int variant, const std::string& file)
{
    constexpr int W = 512, H = 128;
    const Up u = up_of(q);
    Tables t;
    build_tables(q, u, t);
    std::vector<unsigned char> img(3 * W * H * 3);
    for (int j = 0; j < H; ++j)
    {
        const double el = 90.0 - (j + 0.5) / H * 90.0;
        for (int i = 0; i < W; ++i)
        {
            const double az = std::fabs((i + 0.5) / W * 360.0 - 180.0);
            const V3 up = upstream_sky(q, u, el, az);
            const V3 ours = ours_sky(q, u, t, el, az, variant);
            const double e = de76(display_lab(up, q.s.path, q.s.gamma), display_lab(ours, q.s.path, q.s.gamma));
            rgb8(up, q.s.path, q.s.gamma, &img[3 * (j * 3 * W + i)]);
            rgb8(ours, q.s.path, q.s.gamma, &img[3 * (j * 3 * W + W + i)]);
            heat(e, &img[3 * (j * 3 * W + 2 * W + i)]);
        }
    }
    std::FILE* f = std::fopen(file.c_str(), "wb");
    if (f)
    {
        std::fwrite(img.data(), 1, img.size(), f);
        std::fclose(f);
    }
}
} // namespace

int main(int argc, char** argv)
{
    bool anchor = false, physical = false, dome = false;
    std::string pano_dir;
    int pano_variant = kVC;
    double blend_t = -1.0;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--anchor"))
        {
            anchor = true;
        }
        else if (!std::strcmp(argv[i], "--physical"))
        {
            physical = true;
        }
        else if (!std::strcmp(argv[i], "--dome"))
        {
            dome = true;
        }
        else if (!std::strcmp(argv[i], "--blend") && i + 1 < argc)
        {
            blend_t = std::atof(argv[++i]);
        }
        else if (!std::strcmp(argv[i], "--pano") && i + 2 < argc)
        {
            pano_dir = argv[++i];
            const char* vn = argv[++i];
            pano_variant = -1;
            for (int k = 0; k < kVariants; ++k)
            {
                if (!std::strcmp(vn, kVName[k]))
                {
                    pano_variant = k;
                }
            }
            if (pano_variant < 0)
            {
                std::printf("unknown variant %s\n", vn);
                return 2;
            }
        }
        else
        {
            path = argv[i];
        }
    }
    if (!path)
    {
        std::printf("usage: at2_qual [--physical] [--anchor | --pano DIR VARIANT | --blend T | --dome] skies.txt\n");
        return 2;
    }
    std::FILE* f = std::fopen(path, "r");
    if (!f)
    {
        std::printf("cannot open %s\n", path);
        return 2;
    }
    std::vector<QSky> skies;
    static char line[kRowLen];
    while (std::fgets(line, sizeof line, f))
    {
        if (!std::strchr(line, '\n') && !std::feof(f))
        {
            std::printf("a row is longer than %d characters\n", kRowLen - 1);
            return 2;
        }
        QSky q;
        if (!parse_q(line, q))
        {
            std::printf("a row could not be read: %.60s\n", line);
            return 2;
        }
        skies.push_back(q);
    }
    std::fclose(f);
    std::printf("ROWS\t%zu\n", skies.size()); // the driver checks it against the rows it wrote

    if (anchor)
    {
        for (const QSky& q : skies)
        {
            const Up u = up_of(q);
            const double samples[7][2] = { { 90.0, 0.0 }, { 2.0, 90.0 }, { 0.5, 45.0 }, { 45.0, 135.0 }, { q.light_el, 14.75 },
                                           { q.light_el + 10.0, 0.0 }, { q.light_el + 0.4, 0.0 } };
            static const char* labels[7] = { "zenith", "horizon", "low", "mid", "near-light", "light+10", "light-core" };
            for (int a = 0; a < 7; ++a)
            {
                const V3 c = coded_of(upstream_pre(q, u, geo_of(q.s, samples[a][0], samples[a][1])));
                std::printf("ANCHOR\t%s\t%s\t%.6f %.6f %.6f\n", q.s.name.c_str(), labels[a], c.r, c.g, c.b);
            }
        }
        return 0;
    }

    if (!pano_dir.empty())
    {
        for (size_t i = 0; i < skies.size(); ++i)
        {
            write_pano(skies[i], pano_variant, pano_dir + "/" + std::to_string(i) + ".rgb");
        }
        return 0;
    }

    if (blend_t >= 0.0)
    {
        for (size_t i = 0; i + 2 < skies.size(); i += 3)
        {
            const BlendStat b = blend_measure(skies[i], skies[i + 1], skies[i + 2], blend_t);
            std::printf("BLEND\t%s\t%.2f\t%.3f\t%.2f\t%.3f\t%.2f\t%.3f\t%.2f\t%.2f\t%.2f\n", skies[i + 2].s.name.c_str(), blend_t, b.mean[0],
                        b.worst[0], b.mean[1], b.worst[1], b.mean[2], b.worst[2], skies[i + 2].sun_el, skies[i + 2].light_el);
        }
        return 0;
    }

    std::vector<DomeStat> dg(skies.size() * 2), dv(skies.size() * 2);
    std::vector<QResult> res(skies.size());
    std::atomic<size_t> next { 0 };
    std::atomic<bool> ab_bad { false };
    const unsigned nt = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
    std::vector<std::thread> pool;
    for (unsigned w = 0; w < nt; ++w)
    {
        pool.emplace_back([&]() {
            for (;;)
            {
                const size_t i = next.fetch_add(1);
                if (i >= skies.size())
                {
                    return;
                }
                if (dome)
                {
                    dome_measure(skies[i], 0.0, dg[2 * i], dv[2 * i]);
                    dome_measure(skies[i], 0.5, dg[2 * i + 1], dv[2 * i + 1]);
                }
                else
                {
                    res[i] = measure_q(skies[i], physical);
                    if (!(res[i].ab_err <= 1e-6))
                    {
                        ab_bad = true;
                    }
                }
            }
        });
    }
    for (std::thread& th : pool)
    {
        th.join();
    }
    if (dome)
    {
        std::printf("DOME\tname\tsun_el\tband\tgeo_mean\tgeo_worst\tvert_aligned_mean\tvert_aligned_worst\tvert_half_mean\tvert_half_worst\n");
        for (size_t i = 0; i < skies.size(); ++i)
        {
            std::printf("DOME\t%s\t%.3f\t%.4f\t%.4f\t%.3f\t%.4f\t%.3f\t%.4f\t%.3f\n", skies[i].s.name.c_str(), skies[i].sun_el,
                        band_weight(skies[i].sun_el), dg[2 * i].mean, std::max(dg[2 * i].worst, dg[2 * i + 1].worst), dv[2 * i].mean,
                        dv[2 * i].worst, dv[2 * i + 1].mean, dv[2 * i + 1].worst);
        }
        return 0;
    }
    if (ab_bad)
    {
        for (size_t i = 0; i < skies.size(); ++i)
        {
            if (!(res[i].ab_err <= 1e-6))
            {
                std::fprintf(stderr, "A / B self-check failed for %s: relative error %.3g\n", skies[i].s.name.c_str(), res[i].ab_err);
            }
        }
        return 3;
    }

    std::printf("ROW\tname\tsun_el\tlight_el\tsun_up\tpath\tband\tw_max\tsat\tstarved\tstarved_zero\tab_err");
    for (int k = 0; k < kVariants; ++k)
    {
        std::printf("\t%s_mean\t%s_vmean\t%s_worst\t%s_el\t%s_sep", kVName[k], kVName[k], kVName[k], kVName[k], kVName[k]);
    }
    std::printf("\tcur_mean\tcur_worst\tcur_el\tcur_sep\n");
    for (size_t i = 0; i < skies.size(); ++i)
    {
        const QSky& q = skies[i];
        const QResult& r = res[i];
        std::printf("ROW\t%s\t%.4f\t%.4f\t%d\t%d\t%.4f\t%.4g\t%.4f\t%.4f\t%.4f\t%.2e", q.s.name.c_str(), q.sun_el, q.light_el, q.sun_up, q.s.path,
                    r.band, r.w_max, r.sat, r.starved, r.starved_zero, r.ab_err);
        for (int k = 0; k < kVariants; ++k)
        {
            std::printf("\t%.4f\t%.4f\t%.3f\t%.2f\t%.2f", r.v[k].mean, r.v[k].vmean, r.v[k].worst, r.v[k].el, r.v[k].sep);
        }
        std::printf("\t%.4f\t%.3f\t%.2f\t%.2f\n", r.cur.mean, r.cur.worst, r.cur.el, r.cur.sep);
    }
    return 0;
}
