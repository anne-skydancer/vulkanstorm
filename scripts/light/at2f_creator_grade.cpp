// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Stage AT2-F of the atmosphere redesign R (decided 2026-11-17, doc/light/atmosphere-study.md and doc/light/at2f-creator-grade.md):
// "physics sets the light, the creator sets the look". For every shipped Windlight sky this measures, offline and on the CPU:
//
//   1. the creator's look: today's drawn sky, a C++ twin of class1/deferred/skyV.glsl with skyF.glsl's x2 and clamp 5 and the PBR
//      path's decode x sky_hdr_scale (2 sqrt(gamma)), with the uniforms exactly as scripts/light/bench_atmosphere.py sets them
//      (set_today_uniforms): the twin is anchored to that GPU bench's Default figures (zenith 0.279, horizon 1.711);
//   2. the physical sky: LLScatter::fromLegacy (llatmosphericlight.h, the viewer's AT2-0 model) with the slab-split march of
//      decision D1 (atmosphere_ap to the end of the ray, 64 steps, plus sky_radiance's ground term);
//   3. the energy factor k: the scalar that brings the creator's sky to the physical sky's irradiance on a horizontal surface
//      (luminance of the cosine-weighted upper hemisphere). The graded sky is k x the creator's look on the grade grid;
//   4. how far physics alone gets (pure rebasing, no grade): the mean CIE76 dE between the creator's sky and the physical sky after the
//      best luminance gain, and after the best gain and per-channel tint, on the display transform of the GPU bench
//      (ACES Hill, sRGB, 8 bit);
//   5. the grade grid's interpolation error: the grade sampled on its nodes and bilinearly interpolated at the cell centres, against
//      the creator's sky there (dE, same display);
//   6. the grade's size (the range of k x creator / physical in luminance, and its largest per-channel tint);
//   7. the white-card rule (2026-11-11) on the graded sky: outside 10 degrees of the sun no radiance above lum(E_sun) / pi, within 10 to
//      20 degrees at most 2x;
//   8. what the grade does to vertical surfaces (study decision D4's trace): a wall facing the sun and one facing away, graded / physical.
//
// Sun below the horizon (night and twilight files) is out of scope here: skyV's moon branch and AT1's moon gain need their own pass.
//
//   g++ -std=c++20 -O2 -I indra/newview scripts/light/at2f_creator_grade.cpp -o at2f_creator_grade
//   at2f_creator_grade skies.txt            (one line per sky, written by scripts/light/at2f_creator_grade.py)
//   at2f_creator_grade --anchor skies.txt   (prints the twin's Default zenith and horizon, for the GPU anchor)

#include "llatmosphericlight.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace LLAtmosphericLight;

namespace
{
constexpr double PI = 3.14159265358979323846;
constexpr double DEG = PI / 180.0;
constexpr double kCamKm = 0.020;      // the GPU bench's eye height (CAM_H 20 m)
constexpr int kMarch = 64;            // decision D1: slab-split march, 64 steps
constexpr double kExcludeDeg = 10.0;  // the white-card rule's exclusion around the light
constexpr double kAureoleDeg = 20.0;
constexpr double kAureoleMax = 2.0;

struct V3
{
    double r = 0.0, g = 0.0, b = 0.0;
};
V3 operator+(V3 a, V3 b) { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
V3 operator*(V3 a, double s) { return { a.r * s, a.g * s, a.b * s }; }
V3 mul(V3 a, V3 b) { return { a.r * b.r, a.g * b.g, a.b * b.b }; }
double lumv(V3 c) { return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b; }

struct SkyRow
{
    std::string name;
    double sun[3], amb[3], bd[3], bh[3];
    double hd, hh, cs, dm, dist, my;
    double glow[3];
    double gamma, sun_angle;
};

// ---- 1. the creator's look: skyV.glsl (Vulkanstorm L3 version), by day, with the bench's uniforms --------------------------------
// y up; the sun's azimuth is 0 (along +x). sun_moon_blend is 0 (the bench leaves it unset, so 0): the day branch.
V3 windlight_sky(const SkyRow& s, double sun_el_deg, double el_deg, double az_deg)
{
    // lightnorm: light_vec of the bench (y clamped at -0.1), normalised
    const double se = sun_el_deg * DEG;
    double lx = std::cos(se), ly = std::max(std::sin(se), -0.1), lz = 0.0;
    const double nl = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= nl; ly /= nl; lz /= nl;
    // position on the 15000 m dome, camPosLocal 0
    const double e = el_deg * DEG, a = az_deg * DEG;
    const double R = 15000.0;
    double px = std::cos(e) * std::cos(a) * R, py = std::sin(e) * R + 50.0, pz = std::cos(e) * std::sin(a) * R;
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
    const double nx = px / rlen, ny = py / rlen, nz = pz / rlen;
    const double dot_l = nx * lx + ny * ly + nz * lz; // sun_glow_norm = lightnorm by day (the bench)

    const double sl[3] = { s.sun[0], s.sun[1], s.sun[2] }; // mix(sunlight_color, moonlight * 0.7, 0)
    double light_atten[3], combined[3], blue_w[3], haze_w[3], sunlight[3];
    const double off_axis = 1.0 / std::max(1e-6, std::max(0.0, ny) + ly);
    for (int c = 0; c < 3; ++c)
    {
        light_atten[c] = (s.bd[c] + s.hd * 0.25) * (s.dm * s.my);
        combined[c] = std::max(std::fabs(s.bd[c]) + std::fabs(s.hd), 1e-6);
        blue_w[c] = s.bd[c] / combined[c];
        haze_w[c] = s.hd / combined[c];
        sunlight[c] = sl[c] * std::exp(-light_atten[c] * off_axis);
    }
    const double density_dist = rlen * s.dm;
    double haze_glow = std::max(1.0 - dot_l, 0.001);
    haze_glow *= s.glow[0];
    haze_glow = std::pow(haze_glow, s.glow[2]);
    haze_glow = 1.0 * (haze_glow + 0.25); // (1 - sun_moon_blend)

    double out[3];
    for (int c = 0; c < 3; ++c)
    {
        const double ch = std::exp(-combined[c] * density_dist);
        double color = s.bh[c] * blue_w[c] * (sunlight[c] + s.amb[c]) + (s.hh * haze_w[c]) * (sunlight[c] * haze_glow + s.amb[c]);
        color *= (1.0 - ch);
        const double ambient = s.amb[c] + std::max(0.0, 1.0 - s.amb[c]) * s.cs * 0.5;
        const double sun_c = sunlight[c] * std::max(0.0, 1.0 - s.cs);
        const double below = s.bh[c] * blue_w[c] * (sun_c + ambient) + (s.hh * haze_w[c]) * (sun_c * haze_glow + ambient);
        const double ch2 = std::sqrt(ch);
        color += (below - color) * (1.0 - std::sqrt(ch2));
        out[c] = color;
    }
    // skyF.glsl: x2, clamp [0, 5]; the lighting pass: srgb_to_linear x sky_hdr_scale (2 sqrt(gamma), the PBR path)
    const double hdr = 2.0 * std::sqrt(std::max(s.gamma, 0.0));
    V3 r;
    double* o[3] = { &r.r, &r.g, &r.b };
    for (int c = 0; c < 3; ++c)
    {
        const double v = std::min(std::max(out[c] * 2.0, 0.0), 5.0);
        *o[c] = srgb_to_linear(v) * hdr;
    }
    return r;
}

// ---- 2. the physical sky -----------------------------------------------------------------------------------------------------
Vec3 dir_of(double el_deg, double az_deg)
{
    const double e = el_deg * DEG, a = az_deg * DEG;
    return { std::cos(e) * std::cos(a), std::cos(e) * std::sin(a), std::sin(e) };
}

struct Physical
{
    LLScatter sc;
    TransmittanceLUT tl;
    MsLUT ms;
    SkyIrradianceLUT el;
};

void build_physical(Physical& ph, const SkyRow& s, double sun_el_deg)
{
    LegacySky sky;
    const double m = std::fmax(s.sun[0], std::fmax(s.sun[1], s.sun[2]));
    const double k = m > 1.0 ? 1.0 / m : 1.0;
    sky.source = { std::min(std::max(s.sun[0] * k, 0.0), 1.0), std::min(std::max(s.sun[1] * k, 0.0), 1.0),
                   std::min(std::max(s.sun[2] * k, 0.0), 1.0) };
    sky.blue_density = { s.bd[0], s.bd[1], s.bd[2] };
    sky.haze_density = s.hd;
    sky.density_multiplier = s.dm;
    sky.max_y = s.my;
    sky.distance_multiplier = s.dist;
    sky.absorption.count = 2; // the validated stored ozone (as atmosphere_library_check.cpp)
    sky.absorption.layer[0].width_m = 25000.0;
    sky.absorption.layer[1].constant = 1.0;
    ScatterFields f;
    f.blue_horizon = { s.bh[0], s.bh[1], s.bh[2] };
    f.haze_horizon = s.hh;
    f.ambient = { s.amb[0], s.amb[1], s.amb[2] };
    f.glow_x = s.glow[0];
    f.glow_z = s.glow[2];
    ph.sc = LLScatter::fromLegacy(sky, f, 0.0);
    const Vec3 sun = dir_of(sun_el_deg, 0.0);
    ph.sc.set_light_dirs(sun, { -sun.x, -sun.y, -sun.z });
    ph.sc.lights.count = 1;
    ph.sc.build_t_lut(ph.tl);
    ph.sc.t_lut = &ph.tl;
    ph.sc.build_ms_lut(ph.ms);
    ph.sc.ms_lut = &ph.ms;
    ph.sc.build_e_lut(ph.el);
    ph.sc.e_lut = &ph.el;
}

// decision D1: the slab-split march (atmosphere_ap to the end of the ray) plus sky_radiance's ground term (as check_skyview_steps.cpp)
V3 physical_sky(const Physical& ph, double el_deg, double az_deg)
{
    const Vec3 d = dir_of(el_deg, az_deg);
    const double h0 = kCamKm;
    const double tg = ph.sc.ground_distance(h0, d);
    const double tmax = ph.sc.ray_length(h0, d);
    Vec3 Lin, T;
    ph.sc.atmosphere_ap(d, tmax, h0, kMarch, Lin, T);
    if (tg >= 0.0)
    {
        const Vec3 full = ph.sc.sky_radiance(d, h0, 1024);
        Vec3 Lu, Tu;
        ph.sc.atmosphere_ap(d, tmax, h0, 1024, Lu, Tu);
        Lin += full - Lu;
    }
    return { Lin.x, Lin.y, Lin.z };
}

// ---- display: the GPU bench's DISPLAY_FS (toneMapACES_Hill, linear_to_srgb, 8 bit) and to_lab -----------------------------------
// GLSL mat3 is column-major: out_r = m[0] x + m[3] y + m[6] z
const double kAcesIn[9] = { 0.59719, 0.07600, 0.02840, 0.35458, 0.90834, 0.13383, 0.04823, 0.01566, 0.83777 };
const double kAcesOut[9] = { 1.60475, -0.10208, -0.00327, -0.53108, 1.10813, -0.07276, -0.07367, -0.00605, 1.07602 };

V3 mat3_mul(const double* m, V3 v)
{
    return { m[0] * v.r + m[3] * v.g + m[6] * v.b, m[1] * v.r + m[4] * v.g + m[7] * v.b, m[2] * v.r + m[5] * v.g + m[8] * v.b };
}
double rrt(double c) { return (c * (c + 0.0245786) - 0.000090537) / (c * (0.983729 * c + 0.4329510) + 0.238081); }

struct Lab
{
    double L, a, b;
};

Lab display_lab(V3 lin)
{
    V3 c = mat3_mul(kAcesIn, lin);
    c = { rrt(c.r), rrt(c.g), rrt(c.b) };
    c = mat3_mul(kAcesOut, c);
    double ch[3] = { c.r, c.g, c.b };
    double q[3];
    for (int i = 0; i < 3; ++i)
    {
        const double x = std::min(std::max(ch[i], 0.0), 1.0);
        const double s = x < 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 0.41666) - 0.055; // srgbF.glsl's exponent
        const double b8 = std::round(std::min(std::max(s, 0.0), 1.0) * 255.0);           // RGBA8 readback
        q[i] = srgb_to_linear(b8 / 255.0);
    }
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double X = (0.4124 * q[0] + 0.3576 * q[1] + 0.1805 * q[2]) / 0.95047;
    const double Y = 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2];
    const double Z = (0.0193 * q[0] + 0.1192 * q[1] + 0.9505 * q[2]) / 1.08883;
    return { 116.0 * f(Y) - 16.0, 500.0 * (f(X) - f(Y)), 200.0 * (f(Y) - f(Z)) };
}

double de76(V3 a, V3 b)
{
    const Lab x = display_lab(a), y = display_lab(b);
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b));
}

// ---- the grids -----------------------------------------------------------------------------------------------------------------
// the grade's nodes: the sky-view table's own grid (Hillaire 2020, 192 x 108, latitude mapped as v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi/2)),
// so the rows crowd at the horizon), its upper half (54 rows) and azimuth from the sun 0..180 in its 360 / 192 degree columns (both
// skies are symmetric about the sun's vertical plane). The grade is a table of that size, multiplied into the sky view.
constexpr int kNEl = 54;
constexpr int kNAz = 97; // 0 .. 180 in 1.875 degree steps
double grade_el(int i) { const double t = (i + 0.5) / kNEl; return 90.0 * t * t; }
double grade_az(int j) { return 360.0 / 192.0 * j; }

// the measuring grid (irradiance, dE): 1 degree in elevation (cell centres 0.5 .. 89.5), 5 degrees in azimuth (centres 2.5 .. 177.5)
constexpr int kMEl = 90, kMAz = 36;

struct Sample
{
    double el, az, w_horiz; // solid angle x cos(zenith) weight (both halves of the azimuth: x 2)
    V3 W, P;
};

double sep_deg(double sun_el, double el, double az)
{
    const Vec3 s = dir_of(sun_el, 0.0), d = dir_of(el, az);
    return std::acos(std::min(1.0, std::max(-1.0, s.x * d.x + s.y * d.y + s.z * d.z))) / DEG;
}

struct GradeResult
{
    double k = 0.0;          // energy factor (graded = k x creator)
    double de_gain = 0.0;    // physics alone, best luminance gain
    double de_tint = 0.0;    // physics alone, best gain and per-channel tint
    double de_lut = 0.0, de_lut_max = 0.0;   // the dome and probes: the graded sky-view table (k W per texel), bilinear, against W
    double lut_el = 0.0, lut_az = 0.0;         // where its worst cell is
    double de_lut_phys = 0.0, de_lut_phys_max = 0.0; // the same table error for the physical sky alone (the sky view's own baseline)
    double starved = 0.0;    // fraction of the sky (solid angle) where the physical sky's weakest channel is below 1e-3 of its strongest
    double ap_remainder = 0.0; // haze on objects: the share of the graded sky's ground light a ratio bounded at 32x the median grade
                               // cannot carry (it needs an additive term)
    double grade_lo = 0.0, grade_hi = 0.0; // range of lum(k W / P) over the measuring grid
    double tint_max = 0.0;   // largest per-channel deviation of (k W / P) from its luminance, as a factor
    double card = 0.0;       // largest graded radiance / white card outside the exclusion, outside the aureole
    double card_aureole = 0.0;
    double wall_sun = 0.0, wall_away = 0.0; // graded / physical irradiance on a vertical wall facing the sun / away
    double gain = 0.0;       // the best luminance gain physical -> creator (log-mean)
};

V3 bilinear(const std::vector<V3>& G, double el, double az, bool in_log)
{
    // the row: invert the latitude mapping (clamped to the first and last rows: constant beyond them)
    const double u = std::sqrt(std::max(el, 0.0) / 90.0) * kNEl - 0.5;
    const int i = std::min(std::max(static_cast<int>(std::floor(u)), 0), kNEl - 2);
    const double te = std::min(std::max(u - i, 0.0), 1.0);
    const double ua = az / (360.0 / 192.0);
    const int j = std::min(std::max(static_cast<int>(std::floor(ua)), 0), kNAz - 2);
    const double ta = std::min(std::max(ua - j, 0.0), 1.0);
    auto at = [&](int a, int b) { return G[a * kNAz + b]; };
    // a ratio is interpolated in log space, a radiance linearly
    auto lg = [in_log](V3 v) {
        return in_log ? V3 { std::log(std::max(v.r, 1e-12)), std::log(std::max(v.g, 1e-12)), std::log(std::max(v.b, 1e-12)) } : v;
    };
    const V3 v = lg(at(i, j)) * ((1 - te) * (1 - ta)) + lg(at(i + 1, j)) * (te * (1 - ta)) + lg(at(i, j + 1)) * ((1 - te) * ta)
                 + lg(at(i + 1, j + 1)) * (te * ta);
    return in_log ? V3 { std::exp(v.r), std::exp(v.g), std::exp(v.b) } : v;
}

GradeResult measure(const SkyRow& s, double sun_el)
{
    Physical ph;
    build_physical(ph, s, sun_el);
    GradeResult r;

    // the measuring grid
    std::vector<Sample> smp;
    smp.reserve(kMEl * kMAz);
    const double dth = DEG, dph = 5.0 * DEG;
    for (int i = 0; i < kMEl; ++i)
    {
        const double el = i + 0.5;
        for (int j = 0; j < kMAz; ++j)
        {
            const double az = 5.0 * j + 2.5;
            Sample q;
            q.el = el;
            q.az = az;
            q.w_horiz = 2.0 * std::cos(el * DEG) * std::sin(el * DEG) * dth * dph; // dw = cos(el) del daz; x sin(el); both halves
            q.W = windlight_sky(s, sun_el, el, az);
            q.P = physical_sky(ph, el, az);
            smp.push_back(q);
        }
    }

    // 3. energy: horizontal irradiance in luminance
    double EW = 0.0, EP = 0.0;
    for (const Sample& q : smp)
    {
        EW += lumv(q.W) * q.w_horiz;
        EP += lumv(q.P) * q.w_horiz;
    }
    r.k = EW > 0.0 ? EP / EW : 0.0;

    // 4. physics alone: the best gain (log-mean of luminance) and the best gain + per-channel tint, from P to the creator's W
    double lg = 0.0, lc[3] = {};
    int n = 0;
    for (const Sample& q : smp)
    {
        lg += std::log(lumv(q.W) + 1e-6) - std::log(lumv(q.P) + 1e-6);
        lc[0] += std::log(q.W.r + 1e-6) - std::log(q.P.r + 1e-6);
        lc[1] += std::log(q.W.g + 1e-6) - std::log(q.P.g + 1e-6);
        lc[2] += std::log(q.W.b + 1e-6) - std::log(q.P.b + 1e-6);
        ++n;
    }
    const double gain = std::exp(lg / n);
    const V3 tint = { std::exp(lc[0] / n), std::exp(lc[1] / n), std::exp(lc[2] / n) };
    r.gain = gain;
    double sg = 0.0, st = 0.0, sw = 0.0;
    for (const Sample& q : smp)
    {
        const double w = std::cos(q.el * DEG); // solid-angle weight (the bench's dE is per panorama pixel; this is per solid angle)
        sg += w * de76(q.W, q.P * gain);
        st += w * de76(q.W, mul(q.P, tint));
        sw += w;
    }
    r.de_gain = sg / sw;
    r.de_tint = st / sw;

    // 5. the grade in the sky view (the drawn dome and the probes' capture): the table holds P x G = W (x k) at each texel, so what is
    //    drawn is the creator's sky through the table's bilinear filter. Measured against W at the cell centres, with the physical
    //    sky's own table error (P through the same filter) as the baseline. The haze on objects reads the grade as a ratio instead
    //    (it multiplies the aerial perspective's in-scatter); where the physical sky has almost no light in a channel the ratio
    //    explodes, so the share that a ratio bounded at 32x the sky's median grade cannot carry is reported (ap_remainder).
    std::vector<V3> Wn(kNEl * kNAz), Pn(kNEl * kNAz);
    std::vector<double> lgrades;
    for (int i = 0; i < kNEl; ++i)
    {
        for (int j = 0; j < kNAz; ++j)
        {
            Wn[i * kNAz + j] = windlight_sky(s, sun_el, grade_el(i), grade_az(j));
            Pn[i * kNAz + j] = physical_sky(ph, grade_el(i), grade_az(j));
            lgrades.push_back(lumv(Wn[i * kNAz + j]) / std::max(lumv(Pn[i * kNAz + j]), 1e-12));
        }
    }
    {
        double sl = 0.0, sp = 0.0, sw = 0.0;
        for (const Sample& q : smp)
        {
            const double e = de76(q.W, bilinear(Wn, q.el, q.az, false));
            const double gp = r.gain; // show the physical sky at the creator's exposure, so its dE is on the same scale
            const double ep = de76(q.P * gp, bilinear(Pn, q.el, q.az, false) * gp);
            const double w = std::cos(q.el * DEG);
            sl += w * e;
            sp += w * ep;
            sw += w;
            if (e > r.de_lut_max)
            {
                r.de_lut_max = e;
                r.lut_el = q.el;
                r.lut_az = q.az;
            }
            r.de_lut_phys_max = std::max(r.de_lut_phys_max, ep);
        }
        r.de_lut = sl / sw;
        r.de_lut_phys = sp / sw;
    }
    {
        std::nth_element(lgrades.begin(), lgrades.begin() + lgrades.size() / 2, lgrades.end());
        const double ghi = lgrades[lgrades.size() / 2] * 32.0;
        double add_e = 0.0, tot_e = 0.0;
        for (const Sample& q : smp)
        {
            // per channel: what P x min(W / P, ghi) leaves of W
            const double rem[3] = { q.W.r - q.P.r * std::min(q.W.r / std::max(q.P.r, 1e-12), ghi),
                                    q.W.g - q.P.g * std::min(q.W.g / std::max(q.P.g, 1e-12), ghi),
                                    q.W.b - q.P.b * std::min(q.W.b / std::max(q.P.b, 1e-12), ghi) };
            add_e += lumv({ rem[0], rem[1], rem[2] }) * q.w_horiz;
            tot_e += lumv(q.W) * q.w_horiz;
        }
        r.ap_remainder = tot_e > 0.0 ? add_e / tot_e : 0.0;
    }

    // colour starvation: where the physical sky has (almost) nothing in a channel, a multiplicative grade cannot make that colour
    {
        double st = 0.0, sa = 0.0;
        for (const Sample& q : smp)
        {
            const double lo = std::min({ q.P.r, q.P.g, q.P.b }), hi = std::max({ q.P.r, q.P.g, q.P.b });
            const double w = std::cos(q.el * DEG);
            sa += w;
            if (lo < 1e-3 * hi)
            {
                st += w;
            }
        }
        r.starved = sa > 0.0 ? st / sa : 0.0;
    }

    // 6. the grade's size: k W / P
    r.grade_lo = 1e30;
    r.grade_hi = 0.0;
    for (const Sample& q : smp)
    {
        const double gl = r.k * lumv(q.W) / std::max(lumv(q.P), 1e-12);
        r.grade_lo = std::min(r.grade_lo, gl);
        r.grade_hi = std::max(r.grade_hi, gl);
        const double ch[3] = { r.k * q.W.r / std::max(q.P.r, 1e-12), r.k * q.W.g / std::max(q.P.g, 1e-12), r.k * q.W.b / std::max(q.P.b, 1e-12) };
        for (double c : ch)
        {
            const double t = c / std::max(gl, 1e-12);
            r.tint_max = std::max(r.tint_max, std::max(t, 1.0 / std::max(t, 1e-12)));
        }
    }

    // 7. the white card on the graded sky
    const Vec3 E = ph.sc.lights.E[0];
    const double card = luminance(E) / PI;
    for (const Sample& q : smp)
    {
        const double sep = sep_deg(sun_el, q.el, q.az);
        if (sep < kExcludeDeg || card <= 0.0)
        {
            continue;
        }
        const double ratio = r.k * lumv(q.W) / card;
        if (sep < kAureoleDeg)
        {
            r.card_aureole = std::max(r.card_aureole, ratio);
        }
        else
        {
            r.card = std::max(r.card, ratio);
        }
    }

    // 8. vertical walls: normal along +x (facing the sun's azimuth) and -x
    double ws_g = 0.0, ws_p = 0.0, wa_g = 0.0, wa_p = 0.0;
    for (const Sample& q : smp)
    {
        const double dw = 2.0 * std::cos(q.el * DEG) * dth * dph * 0.5; // per half (the wall is not symmetric in az; use one half x 2)
        const double cx = std::cos(q.el * DEG) * std::cos(q.az * DEG);
        const double gW = r.k * lumv(q.W), gP = lumv(q.P);
        if (cx > 0.0)
        {
            ws_g += gW * cx * dw * 2.0;
            ws_p += gP * cx * dw * 2.0;
        }
        else
        {
            wa_g += gW * -cx * dw * 2.0;
            wa_p += gP * -cx * dw * 2.0;
        }
    }
    r.wall_sun = ws_p > 0.0 ? ws_g / ws_p : 0.0;
    r.wall_away = wa_p > 0.0 ? wa_g / wa_p : 0.0;
    return r;
}

bool parse(char* line, SkyRow& r)
{
    char* tab = std::strchr(line, '\t');
    if (!tab)
    {
        return false;
    }
    *tab = 0;
    r.name = line;
    double v[25];
    char* p = tab + 1;
    for (int n = 0; n < 25; ++n)
    {
        char* end = nullptr;
        v[n] = std::strtod(p, &end);
        if (end == p)
        {
            return false;
        }
        p = end;
    }
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
    // v[23], v[24]: reserved (east_angle, star brightness), unused
    return true;
}
} // namespace

int main(int argc, char** argv)
{
    bool anchor = false;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--anchor"))
        {
            anchor = true;
        }
        else
        {
            path = argv[i];
        }
    }
    if (!path)
    {
        std::printf("usage: at2f_creator_grade [--anchor] skies.txt\n");
        return 2;
    }
    std::FILE* f = std::fopen(path, "r");
    if (!f)
    {
        std::printf("cannot open %s\n", path);
        return 2;
    }
    std::vector<SkyRow> skies;
    char line[4096];
    while (std::fgets(line, sizeof line, f))
    {
        SkyRow r;
        if (parse(line, r))
        {
            skies.push_back(r);
        }
    }
    std::fclose(f);

    if (anchor)
    {
        // the GPU bench's anchor (bench-2026-11-16.txt): its 720 x 220 panorama (elevation -12 .. 90) read at the pixel centres it
        // prints as "zenith" (top row, column W/2) and "horizon (2 deg, 90 deg from the light)" (row int(14/102 x 220) = 30, column W/4)
        constexpr double W = 720.0, H = 220.0;
        const double zen_el = -12.0 + 102.0 * (H - 0.5) / H, zen_az = ((W / 2 + 0.5) / W * 2.0 - 1.0) * 180.0;
        const double hor_el = -12.0 + 102.0 * (30 + 0.5) / H, hor_az = ((W / 4 + 0.5) / W * 2.0 - 1.0) * 180.0;
        for (const SkyRow& s : skies)
        {
            const double sun_el = std::asin(std::sin(s.sun_angle)) / DEG;
            const V3 z = windlight_sky(s, sun_el, zen_el, zen_az);
            const V3 h = windlight_sky(s, sun_el, hor_el, hor_az);
            std::printf("ANCHOR\t%s\tsun %.2f\tzenith %.4f\thorizon %.4f\n", s.name.c_str(), sun_el, lumv(z), lumv(h));
        }
        return 0;
    }

    std::printf("ROW\tname\tsun_el\tk\tgain\tdE_gain\tdE_tint\tdE_lut\tdE_lut_max\tlut_el\tlut_az\tlut_sep\tdE_lut_phys\tdE_lut_phys_max\tstarved\tap_remainder\tgrade_lo\tgrade_hi\ttint_max\tcard\tcard_aureole\twall_sun\twall_away\n");
    int skipped_night = 0;
    for (const SkyRow& s : skies)
    {
        const double sun_el = std::asin(std::sin(s.sun_angle)) / DEG;
        if (sun_el < 0.0)
        {
            ++skipped_night;
            std::printf("NIGHT\t%s\t%.2f\n", s.name.c_str(), sun_el);
            continue;
        }
        const GradeResult r = measure(s, sun_el);
        std::printf("ROW\t%s\t%.2f\t%.5g\t%.5g\t%.2f\t%.2f\t%.3f\t%.2f\t%.1f\t%.1f\t%.1f\t%.3f\t%.2f\t%.4f\t%.4f\t%.4g\t%.4g\t%.3g\t%.3f\t%.3f\t%.3f\t%.3f\n",
                    s.name.c_str(), sun_el, r.k, r.gain, r.de_gain, r.de_tint, r.de_lut, r.de_lut_max, r.lut_el, r.lut_az,
                    sep_deg(sun_el, r.lut_el, r.lut_az), r.de_lut_phys, r.de_lut_phys_max, r.starved, r.ap_remainder, r.grade_lo, r.grade_hi,
                    r.tint_max, r.card, r.card_aureole, r.wall_sun, r.wall_away);
        std::fflush(stdout);
    }
    std::printf("NIGHT_COUNT\t%d\n", skipped_night);
    return 0;
}
