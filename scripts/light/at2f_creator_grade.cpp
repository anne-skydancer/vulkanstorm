// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Stage AT2-F of the atmosphere redesign R (decided 2026-11-17, doc/light/atmosphere-study.md and doc/light/at2f-creator-grade.md):
// "physics sets the light, the creator sets the look": each Windlight sky is a filter on the physical sky. The visible sky (the dome and
// the haze on distant objects) is exactly today's look W; the light the sky casts (probes, shade) is physical in level, k x W, with the
// creator's tint. For every sky this measures, offline and on the CPU:
//
//   1. W, today's look: a C++ twin of class1/deferred/skyV.glsl with skyF.glsl's x2 and clamp 5, then the lighting pass of the sky's own
//      DRAW PATH (a sky draws classic, PBR, or PBR with a zero probe ambiance; see Path), with the uniforms as scripts/light/
//      bench_atmosphere.py sets them (set_today_uniforms) and the sun/moon band weight of llatmosphericlight.h (sun_moon_blend). The twin
//      is anchored to the GPU bench (--anchor; scripts/light/at2f_anchor_gpu.py makes the reference values);
//   2. the physical sky P: LLScatter::fromLegacy (llatmosphericlight.h, the viewer's AT2-0 model) with the slab-split march of
//      decision D1 (atmosphere_ap to the end of the ray, 64 steps);
//   3. the energy factor k: the scalar that brings W to P's irradiance on a horizontal surface (luminance of the cosine-weighted upper
//      hemisphere). The cast light is k x W; the visible sky is W;
//   4. how far physics alone gets (pure rebasing, no filter): the mean CIE76 dE between W and P after the best luminance gain, and after
//      the best gain and per-channel tint, on the display of the sky's own path;
//   5. the filter table's error, three structures, against W at the cell centres (dE, same display):
//        product: the table stores P x G = W per texel, then bilinear (what the sky-view table holds);
//        ratio, linear and ratio, log: the table stores G = W / P, interpolated, then multiplied by P at the sample;
//   6. the filter's size (the range of G = W / P in luminance, and its largest per-channel tint over the channels that carry light);
//   7. the white-card rule (2026-11-11), on W (the visible sky, "creator intent wins") and on k x W (the cast light): outside 20 degrees
//      of the sun at most 1x lum(E_sun) / pi; from 10 to 20 degrees at most 2x; inside 10 degrees excluded;
//   8. what the cast light does to vertical surfaces (study decision D4's trace): a wall facing the sun and one facing away, k x W / P.
//
// Sun below the horizon (night and twilight files) is out of scope here: skyV's moon branch and AT1's moon gain need their own pass.
// A sky with max_y 0 is undefined (the dome collapses to a point; W has no direction) and is reported, not measured.
//
//   g++ -std=c++20 -O2 -I indra/newview scripts/light/at2f_creator_grade.cpp -o at2f_creator_grade
//   at2f_creator_grade skies.txt            (one line per sky, written by scripts/light/at2f_creator_grade.py)
//   at2f_creator_grade --anchor skies.txt   (prints the twin's zenith, horizon and near-sun W and P, and P's irradiance, for the GPU anchor)

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
constexpr double kCamKm = 0.020;       // the GPU bench's eye height (CAM_H 20 m); the viewer's camera is at its own height
constexpr int kMarch = 64;             // decision D1: slab-split march, 64 steps
constexpr double kExcludeDeg = 10.0;   // the white-card rule: inside 10 degrees of the light, excluded
constexpr double kAureoleDeg = 20.0;   // from 10 to 20 degrees at most 2x the card; outside 20 degrees at most 1x
constexpr double kSwingDeg = 0.3;      // the viewer's default sun/moon swing (PrimaryLightParams::swing)
constexpr double kFloor = 1e-6;        // linear luminance below which a value counts as "no light"
constexpr double kChannelFloor = 1e-3; // a channel below this share of its sample's luminance carries no light (starved)

// The draw path of a sky in the viewer (llsettingsvo.cpp:845-865, softenLightF.glsl, tonemapUtilF.glsl). The path can flip mid-blend of
// two skies (llsettingssky.cpp:609 copies mCanAutoAdjust at the blend's half-way), so a filter must use each frame's own path.
enum Path
{
    kClassic = 0,  // no reflection_probe_ambiance key (the library; cached Midday (Legacy), Midnight): sky_hdr_scale 1, final_scale 1.1, tonemap mix 0
    kPbr = 1,      // reflection_probe_ambiance != 0: sky_hdr_scale 2 sqrt(gamma), tonemap on (the GPU bench's path)
    kPbrZero = 2,  // the key is present and 0 (the code default): sky_hdr_scale 1, not classic (final_scale 1); the display is the classic one (NO_POST + legacyGamma)
};

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
    int path;
};

double smoothstep3(double e0, double e1, double x)
{
    const double t = std::min(std::max((x - e0) / (e1 - e0), 0.0), 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// the viewer's sun_moon_blend: band_weight of primary_light (llatmosphericlight.h) = smoothstep(swing, -swing, elevation + dip)
double band_weight(double sun_el_deg)
{
    return smoothstep3(kSwingDeg, -kSwingDeg, sun_el_deg + horizon_dip_degrees(6360.0e3, kCamKm * 1e3));
}

// ---- 1. the creator's look: skyV.glsl (Vulkanstorm L3 version), with the bench's uniforms ------------------------------------------
// y up; the sun's azimuth is 0 (along +x). The moon's light is the sun's colour (the bench: moonlight_color = sunlight_color); the
// handover to it is sun_moon_blend (the band weight), which is 0 for sun elevations above about 0.16 degrees.
V3 windlight_sky(const SkyRow& s, double sun_el_deg, double el_deg, double az_deg, int path, double blend)
{
    // lightnorm: light_vec of the bench (y clamped at -0.1), normalised
    const double se = sun_el_deg * DEG;
    double lx = std::cos(se), ly = std::max(std::sin(se), -0.1), lz = 0.0;
    const double nl = std::sqrt(lx * lx + ly * ly + lz * lz);
    lx /= nl; ly /= nl; lz /= nl;
    // position on the 15000 m dome, camPosLocal 0 (max_y > 0 here: the caller reports max_y 0 as undefined)
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

    double light_atten[3], combined[3], blue_w[3], haze_w[3], sunlight[3];
    const double off_axis = 1.0 / std::max(1e-6, std::max(0.0, ny) + ly);
    for (int c = 0; c < 3; ++c)
    {
        light_atten[c] = (s.bd[c] + s.hd * 0.25) * (s.dm * s.my);
        combined[c] = std::max(std::fabs(s.bd[c]) + std::fabs(s.hd), 1e-6);
        blue_w[c] = s.bd[c] / combined[c];
        haze_w[c] = s.hd / combined[c];
        const double sl = s.sun[c] * (1.0 - blend) + s.sun[c] * 0.7 * blend; // mix(sunlight_color, moonlight_color * 0.7, sun_moon_blend)
        sunlight[c] = sl * std::exp(-light_atten[c] * off_axis);
    }
    const double density_dist = rlen * s.dm;
    double haze_glow = std::max(1.0 - dot_l, 0.001);
    haze_glow *= s.glow[0];
    haze_glow = std::pow(haze_glow, s.glow[2]);
    haze_glow = (1.0 - blend) * (haze_glow + 0.25);

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
    // skyF.glsl: x2, clamp [0, 5]; the lighting pass of the sky's path: srgb_to_linear x sky_hdr_scale (x final_scale on the classic path)
    double hdr = 1.0;
    if (path == kPbr)
    {
        hdr = 2.0 * std::sqrt(std::max(s.gamma, 0.0));
    }
    else if (path == kClassic)
    {
        hdr = 1.1; // sky_hdr_scale 1 (2 only with RenderSkyAutoAdjustLegacy, default off) x final_scale 1.1 (classic_mode)
    }
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

// decision D1: the slab-split march (atmosphere_ap to the end of the ray). Every direction here is above the horizon and the camera is
// above the ground, so the ray never meets the ground and sky_radiance's ground term does not apply.
V3 physical_sky(const Physical& ph, double el_deg, double az_deg)
{
    const Vec3 d = dir_of(el_deg, az_deg);
    const double h0 = kCamKm;
    const double tmax = ph.sc.ray_length(h0, d);
    Vec3 Lin, T;
    ph.sc.atmosphere_ap(d, tmax, h0, kMarch, Lin, T);
    return { Lin.x, Lin.y, Lin.z };
}

// ---- display: the GPU bench's DISPLAY_FS (toneMapACES_Hill, linear_to_srgb, 8 bit) and to_lab -----------------------------------
// The classic path has tonemap_mix 0 (llsettingssky.cpp:2066): no tone curve, only the clamp. Exposure is 1 on both.
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

// The display chain of upstream's pipeline.cpp (7885-7915) and postDeferredTonemap.glsl. A sky with reflection_probe_ambiance 0 (every
// classic sky, as llsettingssky.cpp:1177-1181 sets it, and the code default) takes the NO_POST path plus legacyGamma: clamp 0..1,
// linear_to_srgb, then c = 1 - (1 - c)^gamma with the sky's gamma uniform. A sky with a nonzero ambiance (kPbr) takes the tone map (ACES Hill,
// RenderTonemapType 1, mix 1, exposure 1) and plain sRGB. The result is the 8-bit code of the RGBA8 readback.
void display_codes(V3 lin, int path, double gamma, unsigned char* out)
{
    V3 c = lin;
    if (path == kPbr)
    {
        c = mat3_mul(kAcesIn, lin);
        c = { rrt(c.r), rrt(c.g), rrt(c.b) };
        c = mat3_mul(kAcesOut, c);
    }
    double ch[3] = { c.r, c.g, c.b };
    for (int i = 0; i < 3; ++i)
    {
        const double x = std::isnan(ch[i]) ? 0.0 : std::min(std::max(ch[i], 0.0), 1.0);
        double s = x < 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 0.41666) - 0.055; // srgbF.glsl's exponent
        if (path != kPbr)
        {
            s = 1.0 - std::pow(1.0 - std::min(std::max(s, 0.0), 1.0), gamma); // legacyGamma
        }
        out[i] = static_cast<unsigned char>(std::round(std::min(std::max(s, 0.0), 1.0) * 255.0)); // RGBA8 readback
    }
}

Lab display_lab(V3 lin, int path, double gamma = 1.0)
{
    unsigned char b8[3];
    display_codes(lin, path, gamma, b8);
    double q[3];
    for (int i = 0; i < 3; ++i)
    {
        q[i] = srgb_to_linear(b8[i] / 255.0);
    }
    auto f = [](double t) { return t > 0.008856 ? std::cbrt(t) : 7.787 * t + 16.0 / 116.0; };
    const double X = (0.4124 * q[0] + 0.3576 * q[1] + 0.1805 * q[2]) / 0.95047;
    const double Y = 0.2126 * q[0] + 0.7152 * q[1] + 0.0722 * q[2];
    const double Z = (0.0193 * q[0] + 0.1192 * q[1] + 0.9505 * q[2]) / 1.08883;
    return { 116.0 * f(Y) - 16.0, 500.0 * (f(X) - f(Y)), 200.0 * (f(Y) - f(Z)) };
}

double de76(const Lab& x, const Lab& y)
{
    return std::sqrt((x.L - y.L) * (x.L - y.L) + (x.a - y.a) * (x.a - y.a) + (x.b - y.b) * (x.b - y.b));
}

// ---- the grids -----------------------------------------------------------------------------------------------------------------
// the filter's nodes: the sky-view table's own grid (Hillaire 2020, 192 x 108, latitude mapped as v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi/2)),
// so the rows crowd at the horizon), its upper half (54 rows) and azimuth from the sun 0..180 in its 360 / 192 degree columns (both
// skies are symmetric about the sun's vertical plane). Rows are 90 t^2 spaced (about 2.7 degrees at 60 degrees, 3.3 near the zenith);
// the 1.875 degrees is the azimuth step only.
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
    Lab labW;               // today's look through the display, once
};

double sep_deg(double sun_el, double el, double az)
{
    const Vec3 s = dir_of(sun_el, 0.0), d = dir_of(el, az);
    return std::acos(std::min(1.0, std::max(-1.0, s.x * d.x + s.y * d.y + s.z * d.z))) / DEG;
}

struct TableErr
{
    double mean = 0.0, max = 0.0, el = 0.0, az = 0.0; // dE against W: the solid-angle weighted mean; the worst cell and where it is
};

struct GradeResult
{
    double sun_el = 0.0, blend = 0.0;
    int path = 0;
    double EW = 0.0, EP = 0.0;   // horizontal irradiance of W and of P, in luminance
    double k = 0.0;              // energy factor (cast light = k x W); 0 when either irradiance is 0
    double gain = 0.0;           // the best luminance gain physical -> creator (log-mean)
    double de_gain = 0.0;        // physics alone, best luminance gain
    double de_tint = 0.0;        // physics alone, best gain and per-channel tint
    TableErr prod, rlin, rlog;   // the filter table: product, ratio interpolated linearly, ratio interpolated in log
    TableErr phys;               // the sky view's own table error (P through the same bilinear filter, at the creator's exposure)
    double starved = 0.0;        // share of the sky (solid angle) where P's weakest channel is below 1e-3 of its strongest (P not all 0)
    double starved_zero = 0.0;   // share where P is exactly 0 in all channels
    double ap_remainder[3] = {}; // haze on objects: the share of W's ground light that a ratio bounded at 8x, 32x, 128x the median W / P
                                 // cannot carry (W / P without k: the bound is relative, and k scales W and the median alike)
    double grade_lo = 0.0, grade_hi = 0.0; // range of lum(W / P) over the samples where both are above kFloor
    double grade_undef = 0.0;    // share of the sky where W is above kFloor and P is not (no ratio exists)
    double tint_max = 0.0;       // largest per-channel deviation of W / P from its luminance, over channels that carry light in both
    double cardW = 0.0, cardW_aur = 0.0; // the white card on W (the visible sky): outside the aureole; from 10 to 20 degrees
    double cardC = 0.0, cardC_aur = 0.0; // the white card on k x W (the cast light)
    double wall_sun = 0.0, wall_away = 0.0; // k x W / P irradiance on a vertical wall facing the sun / away
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
    r.sun_el = sun_el;
    r.path = s.path;
    r.blend = band_weight(sun_el);
    const int path = s.path;
    const double blend = r.blend;

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
            q.W = windlight_sky(s, sun_el, el, az, path, blend);
            q.P = physical_sky(ph, el, az);
            q.labW = display_lab(q.W, path);
            smp.push_back(q);
        }
    }

    // 3. energy: horizontal irradiance in luminance
    for (const Sample& q : smp)
    {
        r.EW += lumv(q.W) * q.w_horiz;
        r.EP += lumv(q.P) * q.w_horiz;
    }
    r.k = (r.EW > 0.0 && r.EP > 0.0) ? r.EP / r.EW : 0.0;

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
        sg += w * de76(q.labW, display_lab(q.P * gain, path));
        st += w * de76(q.labW, display_lab(mul(q.P, tint), path));
        sw += w;
    }
    r.de_gain = sg / sw;
    r.de_tint = st / sw;

    // 5. the filter table at the nodes: W, P and the ratio G = W / P. Three structures are measured against W at the cell centres:
    //    product    the table holds P x G = W per texel; bilinear in linear (what the sky-view table stores);
    //    ratio, lin the table holds G; interpolated linearly, then multiplied by P at the sample;
    //    ratio, log the same, interpolated in log.
    //    The physical sky's own table error (P through the same filter, at the creator's exposure) is the baseline.
    std::vector<V3> Wn(kNEl * kNAz), Pn(kNEl * kNAz), Gn(kNEl * kNAz);
    std::vector<double> lgrades;
    for (int i = 0; i < kNEl; ++i)
    {
        for (int j = 0; j < kNAz; ++j)
        {
            const int x = i * kNAz + j;
            Wn[x] = windlight_sky(s, sun_el, grade_el(i), grade_az(j), path, blend);
            Pn[x] = physical_sky(ph, grade_el(i), grade_az(j));
            Gn[x] = { std::max(Wn[x].r, 1e-12) / std::max(Pn[x].r, 1e-12), std::max(Wn[x].g, 1e-12) / std::max(Pn[x].g, 1e-12),
                      std::max(Wn[x].b, 1e-12) / std::max(Pn[x].b, 1e-12) };
            const double lw = lumv(Wn[x]), lp = lumv(Pn[x]);
            if (lw > kFloor && lp > kFloor)
            {
                lgrades.push_back(lw / lp);
            }
        }
    }
    {
        double s_prod = 0.0, s_rlin = 0.0, s_rlog = 0.0, s_phys = 0.0, s_w = 0.0;
        for (const Sample& q : smp)
        {
            const double w = std::cos(q.el * DEG);
            const double e_prod = de76(q.labW, display_lab(bilinear(Wn, q.el, q.az, false), path));
            const double e_rlin = de76(q.labW, display_lab(mul(q.P, bilinear(Gn, q.el, q.az, false)), path));
            const double e_rlog = de76(q.labW, display_lab(mul(q.P, bilinear(Gn, q.el, q.az, true)), path));
            const double e_phys = de76(display_lab(q.P * gain, path), display_lab(bilinear(Pn, q.el, q.az, false) * gain, path));
            auto upd = [&](TableErr& t, double e) {
                if (e > t.max)
                {
                    t.max = e;
                    t.el = q.el;
                    t.az = q.az;
                }
            };
            upd(r.prod, e_prod);
            upd(r.rlin, e_rlin);
            upd(r.rlog, e_rlog);
            upd(r.phys, e_phys);
            s_prod += w * e_prod;
            s_rlin += w * e_rlin;
            s_rlog += w * e_rlog;
            s_phys += w * e_phys;
            s_w += w;
        }
        r.prod.mean = s_prod / s_w;
        r.rlin.mean = s_rlin / s_w;
        r.rlog.mean = s_rlog / s_w;
        r.phys.mean = s_phys / s_w;
    }
    {
        // haze on objects reads the filter as a ratio (it multiplies the aerial perspective's in-scatter). What a ratio bounded at B times
        // the sky's median lum(W / P) cannot carry of W's ground light is the share that needs an additive term.
        double med = 0.0;
        if (!lgrades.empty())
        {
            std::nth_element(lgrades.begin(), lgrades.begin() + lgrades.size() / 2, lgrades.end());
            med = lgrades[lgrades.size() / 2];
        }
        const double bounds[3] = { 8.0, 32.0, 128.0 };
        for (int b = 0; b < 3; ++b)
        {
            const double ghi = med * bounds[b];
            double add_e = 0.0, tot_e = 0.0;
            for (const Sample& q : smp)
            {
                // per channel: what P x min(W / P, ghi) leaves of W; a sky with no usable ratio at all carries nothing
                auto rem = [&](double w, double p) { return med > 0.0 ? w - p * std::min(w / std::max(p, 1e-12), ghi) : w; };
                add_e += lumv({ rem(q.W.r, q.P.r), rem(q.W.g, q.P.g), rem(q.W.b, q.P.b) }) * q.w_horiz;
                tot_e += lumv(q.W) * q.w_horiz;
            }
            r.ap_remainder[b] = tot_e > 0.0 ? add_e / tot_e : 0.0;
        }
    }

    // colour starvation: where the physical sky has (almost) nothing in a channel, a multiplicative filter cannot make that colour
    {
        double st2 = 0.0, sz = 0.0, sa = 0.0;
        for (const Sample& q : smp)
        {
            const double lo = std::min({ q.P.r, q.P.g, q.P.b }), hi = std::max({ q.P.r, q.P.g, q.P.b });
            const double w = std::cos(q.el * DEG);
            sa += w;
            if (hi <= 0.0)
            {
                sz += w;
            }
            else if (lo < 1e-3 * hi)
            {
                st2 += w;
            }
        }
        r.starved = sa > 0.0 ? st2 / sa : 0.0;
        r.starved_zero = sa > 0.0 ? sz / sa : 0.0;
    }

    // 6. the filter's size: G = W / P, over the samples where both W and P carry light (above kFloor); a channel below 1e-3 of its
    //    sample's luminance in W or in P is a starved one (counted above) and is left out of the tint
    r.grade_lo = 1e30;
    r.grade_hi = 0.0;
    double undef = 0.0, tot = 0.0;
    for (const Sample& q : smp)
    {
        const double w = std::cos(q.el * DEG);
        const double lw = lumv(q.W), lp = lumv(q.P);
        tot += w;
        if (lw <= kFloor)
        {
            continue;
        }
        if (lp <= kFloor)
        {
            undef += w;
            continue;
        }
        const double gl = lw / lp;
        r.grade_lo = std::min(r.grade_lo, gl);
        r.grade_hi = std::max(r.grade_hi, gl);
        const double wc[3] = { q.W.r, q.W.g, q.W.b }, pc[3] = { q.P.r, q.P.g, q.P.b };
        for (int c = 0; c < 3; ++c)
        {
            if (pc[c] < kChannelFloor * lp || wc[c] < kChannelFloor * lw)
            {
                continue;
            }
            const double t = (wc[c] / pc[c]) / gl;
            r.tint_max = std::max(r.tint_max, std::max(t, 1.0 / t));
        }
    }
    if (r.grade_hi == 0.0)
    {
        r.grade_lo = 0.0;
    }
    r.grade_undef = tot > 0.0 ? undef / tot : 0.0;

    // 7. the white card, on W (the visible sky) and on k x W (the cast light)
    const Vec3 E = ph.sc.lights.E[0];
    const double card = luminance(E) / PI;
    for (const Sample& q : smp)
    {
        const double sep = sep_deg(sun_el, q.el, q.az);
        if (sep < kExcludeDeg || card <= 0.0)
        {
            continue;
        }
        const double rw = lumv(q.W) / card, rc = r.k * lumv(q.W) / card;
        if (sep < kAureoleDeg)
        {
            r.cardW_aur = std::max(r.cardW_aur, rw);
            r.cardC_aur = std::max(r.cardC_aur, rc);
        }
        else
        {
            r.cardW = std::max(r.cardW, rw);
            r.cardC = std::max(r.cardC, rc);
        }
    }

    // 8. vertical walls: normal along +x (facing the sun's azimuth) and -x. The samples cover azimuth 0..180 (the sky is symmetric about
    //    the sun's plane), so each cell weighs its solid angle twice
    double ws_g = 0.0, ws_p = 0.0, wa_g = 0.0, wa_p = 0.0;
    for (const Sample& q : smp)
    {
        const double dw = 2.0 * std::cos(q.el * DEG) * dth * dph;
        const double cx = std::cos(q.el * DEG) * std::cos(q.az * DEG);
        const double gW = r.k * lumv(q.W), gP = lumv(q.P);
        if (cx > 0.0)
        {
            ws_g += gW * cx * dw;
            ws_p += gP * cx * dw;
        }
        else
        {
            wa_g += gW * -cx * dw;
            wa_p += gP * -cx * dw;
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
    r.sun_angle = v[22]; // the sun's elevation, radians
    // v[23]: east_angle (unused)
    r.path = static_cast<int>(v[24]);
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
        // the GPU bench's anchor (bench-2026-11-16.txt and scripts/light/at2f_anchor_gpu.py): its 720 x 220 panorama (elevation -12 .. 90)
        // read at the pixel centres: "zenith" (top row, column W/2), "horizon (2 deg, 90 deg from the light)" (row int(14/102 x 220) = 30,
        // column W/4) and near the sun (column 389 = 14.75 degrees of azimuth, the row nearest the sun's elevation). The bench draws the
        // PBR path whatever the sky, so the twin is read on it too. Camera: the twin's Windlight dome is at 0, the physical sky at 20 m.
        constexpr double W = 720.0, H = 220.0;
        auto el_of = [&](int j) { return -12.0 + 102.0 * (j + 0.5) / H; };
        auto az_of = [&](int i) { return ((i + 0.5) / W * 2.0 - 1.0) * 180.0; };
        for (const SkyRow& s : skies)
        {
            const double sun_el = std::asin(std::sin(s.sun_angle)) / DEG;
            const int j_sun = std::min(std::max(static_cast<int>((sun_el + 12.0) / 102.0 * H), 0), static_cast<int>(H) - 1);
            const int js[3] = { static_cast<int>(H) - 1, 30, j_sun }, is[3] = { static_cast<int>(W) / 2, static_cast<int>(W) / 4, 389 };
            Physical ph;
            build_physical(ph, s, sun_el);
            std::printf("ANCHOR\t%s\tsun %.3f", s.name.c_str(), sun_el);
            for (int a = 0; a < 3; ++a)
            {
                const double el = el_of(js[a]), az = az_of(is[a]);
                std::printf("\t%.4f %.4f", lumv(windlight_sky(s, sun_el, el, az, kPbr, 0.0)), lumv(physical_sky(ph, el, az)));
            }
            double EP = 0.0; // the bench's irradiance: the integral of L cos(zenith) over the upper hemisphere (both halves of the azimuth)
            for (int i = 0; i < kMEl; ++i)
            {
                for (int j = 0; j < kMAz; ++j)
                {
                    const double el = i + 0.5;
                    EP += lumv(physical_sky(ph, el, 5.0 * j + 2.5)) * 2.0 * std::cos(el * DEG) * std::sin(el * DEG) * DEG * 5.0 * DEG;
                }
            }
            std::printf("\t%.4f\n", EP);
        }
        return 0;
    }

    std::printf("ROW\tname\tsun_el\tpath\tblend\tEW\tEP\tk\tgain\tdE_gain\tdE_tint\tdE_prod\tdE_prod_max\tprod_el\tprod_az\tprod_sep\t"
                "dE_rlin\tdE_rlin_max\trlin_sep\tdE_rlog\tdE_rlog_max\trlog_sep\tdE_phys\tdE_phys_max\tstarved\tstarved_zero\tap8\tap32\t"
                "ap128\tgrade_lo\tgrade_hi\tgrade_undef\ttint_max\tcardW\tcardW_aur\tcardC\tcardC_aur\twall_sun\twall_away\n");
    for (const SkyRow& s : skies)
    {
        const double sun_el = std::asin(std::sin(s.sun_angle)) / DEG;
        if (sun_el < 0.0)
        {
            std::printf("NIGHT\t%s\t%.2f\n", s.name.c_str(), sun_el);
            continue;
        }
        if (!(s.my > 0.0))
        {
            std::printf("UNDEF\t%s\t%.2f\tmax_y %g\n", s.name.c_str(), sun_el, s.my);
            continue;
        }
        const GradeResult r = measure(s, sun_el);
        auto sep = [&](const TableErr& t) { return sep_deg(sun_el, t.el, t.az); };
        std::printf("ROW\t%s\t%.2f\t%d\t%.4f\t%.5g\t%.5g\t%.5g\t%.5g\t%.2f\t%.2f\t%.3f\t%.2f\t%.1f\t%.1f\t%.1f\t%.3f\t%.2f\t%.1f\t%.3f\t%.2f\t%.1f\t"
                    "%.3f\t%.2f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4g\t%.4g\t%.4f\t%.3g\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\t%.3f\n",
                    s.name.c_str(), sun_el, r.path, r.blend, r.EW, r.EP, r.k, r.gain, r.de_gain, r.de_tint, r.prod.mean, r.prod.max,
                    r.prod.el, r.prod.az, sep(r.prod), r.rlin.mean, r.rlin.max, sep(r.rlin), r.rlog.mean, r.rlog.max, sep(r.rlog),
                    r.phys.mean, r.phys.max, r.starved, r.starved_zero, r.ap_remainder[0], r.ap_remainder[1], r.ap_remainder[2],
                    r.grade_lo, r.grade_hi, r.grade_undef, r.tint_max, r.cardW, r.cardW_aur, r.cardC, r.cardC_aur, r.wall_sun, r.wall_away);
        std::fflush(stdout);
    }
    return 0;
}
