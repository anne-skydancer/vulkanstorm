// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// AT2 qualification, phase 1 (offline, CPU only): upstream Firestorm's sky pass against the filtered physical sky.
//
// Owner decision (2026-11-17): "today's look would be the sky under PR #543 rules, after AT1 and AT2 land and EEP applied as filters on
// top of the physical sky". The reference is therefore UPSTREAM's sky pass (what creators tuned against), not our drifted skyV/skyF:
//
//   reference U   a C++ twin of upstream/master class1/deferred/skyV.glsl (position pass-through) and skyF.glsl (the whole sky, per pixel),
//                 then the lighting pass of upstream's softenLightF.glsl for a sky pixel (GBUFFER_FLAG_SKIP_ATMOS: srgb_to_linear, x sky_hdr_scale,
//                 x final_scale 1.1 in classic mode, clampHDRRange 11.2) with llsettingsvo.cpp's choice of path (classic, PBR, PBR with
//                 reflection_probe_ambiance 0), and its day / night branch (sun_up_factor, sun_moon_glow_factor, the moon 0.7, no glow for the moon);
//   ours O        the filtered physical sky as AT2-F / #543 plan it: the table stores the PRODUCT (physical x filter = reference) per texel in
//                 the sky-view table's own grid (Hillaire 2020: 192 x 108, latitude v = 0.5 + 0.5 sign(l) sqrt(|l| / (pi/2)); the 54 upper rows
//                 and the 97 columns from the light's azimuth to the opposite one), sampled bilinearly. Because the table holds the product,
//                 the additive part where the physical sky is 0 is inside it; the physical sky P matters here only to say WHY a sky is hard
//                 (starved channels, P = 0), and is computed with LLScatter::fromLegacy and the slab-split march (64 steps) on a coarse grid;
//   near-sun      option (AT2-2): within N degrees of the light the sky pass evaluates upstream's glow analytically (the same expression,
//                 one pow) on top of two extra tables A and B (the glow-free part and the part that scales with haze_glow: skyF's colour is
//                 linear in haze_glow before the x2 clamp), blended to the product table between N and 1.5 N degrees (smoothstep);
//   known         OUR current shaders (skyV/skyF with the L3 sun/moon handover band) against upstream, so the band is never counted as a
//                 filter failure. The night sky (stars, Milky Way), the moon's halo and the per-vertex evaluation of our skyV are not
//                 numerically measured here (listed in the report).
//
// Per sky the display is the path's own (classic: clamp only; PBR and PBR with ambiance 0: ACES Hill), sRGB 8 bit, then CIE76 dE,
// solid-angle weighted over 0.5 x 2.5 degree cells (180 x 72 above the horizon, azimuth 0..180 by symmetry).
//
//   g++ -std=c++20 -O2 -pthread -Wno-unused-function -I indra/newview scripts/light/at2_qual.cpp -o at2_qual
//   at2_qual skies.txt                        (rows written by scripts/light/at2_qual.py)
//   at2_qual --anchor skies.txt               (upstream's pre-lighting colour at five directions per sky, for the GPU anchor)
//   at2_qual --pano DIR N skies.txt           (writes DIR/<row>.rgb: three 512 x 128 RGB strips: upstream | ours (near-sun N) | dE heat map)

#define main at2f_unused_main // the AT2-F tool's own main (shares its twin of our skyV, the physical sky and the display, not duplicated)
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

Geo geo_of(const SkyRow& s, double el_deg, double az_deg)
{
    // skyF.glsl: rel_pos = pos - camPosLocal + (0, 50, 0), clamped to max_y above and to the plane at -32000 below
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
    return { px / rlen, py / rlen, pz / rlen, rlen };
}

struct Dir3
{
    double x, y, z;
};

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
// direction. hg_fixed, when not null, replaces haze_glow (the colour is linear in it).
V3 sky_pre(const SkyRow& s, const Geo& g, const Dir3& L, const Dir3& Gl, double blend, double gscale, const double* hg_fixed)
{
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

// the haze_glow of a direction (the value the near-sun treatment evaluates analytically)
double glow_of(const SkyRow& s, const Geo& g, const Dir3& Gl, double gscale)
{
    double hg = std::max(1.0 - (g.nx * Gl.x + g.ny * Gl.y + g.nz * Gl.z), 0.001);
    hg *= s.glow[0];
    hg = std::pow(hg, s.glow[2]);
    return gscale * (hg + 0.25);
}

// skyF.glsl's end (x2, clamp 0..5) then the lighting pass of the sky's path (softenLightF.glsl, SKIP_ATMOS branch): srgb_to_linear x
// sky_hdr_scale, x final_scale (1.1 when classic), clampHDRRange (0..11.2). rainbow and halo22 are 0 (moisture and ice levels 0).
V3 sky_final(V3 pre, const SkyRow& s, int path)
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
    const double in[3] = { pre.r, pre.g, pre.b };
    for (int c = 0; c < 3; ++c)
    {
        const double v = std::min(std::max(in[c] * 2.0, 0.0), 5.0);
        *o[c] = std::min(std::max(srgb_to_linear(v) * hdr, 0.0), kClamp11);
    }
    return r;
}

// ---- the reference: upstream's sky pass -------------------------------------------------------------------------------------------
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

// ---- ours currently (L3: the sun/moon handover band): the existing AT2-F twin's formulas, extended to the night -------------------
V3 current_sky(const QSky& q, double blend, double el, double az)
{
    const Geo g = geo_of(q.s, el, az);
    const Dir3 L = light_vec(q.light_el);
    const Dir3 Gl = light_vec(q.sun_el); // sun_glow_norm: the clamped sun norm
    return sky_final(sky_pre(q.s, g, L, Gl, blend, 1.0 - blend, nullptr), q.s, q.s.path);
}

// ---- ours: the table -----------------------------------------------------------------------------------------------------------
struct Tables
{
    std::vector<V3> W, A, B; // product (final linear), glow-free pre-colour, per-unit-haze_glow pre-colour
};

void build_tables(const QSky& q, const Up& u, Tables& t)
{
    t.W.resize(kNEl * kNAz);
    t.A.resize(kNEl * kNAz);
    t.B.resize(kNEl * kNAz);
    const double zero = 0.0, one = 1.0;
    for (int i = 0; i < kNEl; ++i)
    {
        for (int j = 0; j < kNAz; ++j)
        {
            const Geo g = geo_of(q.s, grade_el(i), grade_az(j));
            const int x = i * kNAz + j;
            t.W[x] = sky_final(upstream_pre(q, u, g), q.s, q.s.path);
            const V3 a = upstream_pre(q, u, g, &zero), b1 = upstream_pre(q, u, g, &one);
            t.A[x] = a;
            t.B[x] = { b1.r - a.r, b1.g - a.g, b1.b - a.b };
        }
    }
}

double smooth_step(double e0, double e1, double x)
{
    const double t = std::min(std::max((x - e0) / (e1 - e0), 0.0), 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// the sky pass: the product table, and (n_deg > 0) the analytic glow within n_deg of the light, blended out by 1.5 n_deg
V3 ours_sky(const QSky& q, const Up& u, const Tables& t, double el, double az, double n_deg)
{
    const V3 prod = bilinear(t.W, el, az, false);
    if (n_deg <= 0.0)
    {
        return prod;
    }
    const double sep = sep_deg(q.light_el, el, az);
    const double w = smooth_step(n_deg, 1.5 * n_deg, sep); // 1: the table
    if (w >= 1.0)
    {
        return prod;
    }
    const V3 a = bilinear(t.A, el, az, false), b = bilinear(t.B, el, az, false);
    const double hg = glow_of(q.s, geo_of(q.s, el, az), u.L, u.gscale);
    const V3 near = sky_final(a + b * hg, q.s, q.s.path);
    return near * (1.0 - w) + prod * w;
}

// ---- measuring ---------------------------------------------------------------------------------------------------------------------
constexpr int kQEl = 180, kQAz = 72; // 0.5 x 2.5 degree cells
constexpr int kVariants = 4;         // plain table, near-sun 5, 10, 20 degrees
const double kNear[kVariants] = { 0.0, 5.0, 10.0, 20.0 };

struct Stat
{
    double mean = 0.0, worst = 0.0, el = 0.0, sep = 0.0;
};

struct QResult
{
    Stat v[kVariants];
    Stat cur; // our current shaders (L3 band) against upstream
    double band = 0.0;
    double starved = -1.0, starved_zero = -1.0; // the physical sky's share with a starved channel / exactly 0 (day only; -1 not computed)
    double w_max = 0.0;                         // brightest channel of upstream's sky over the cells (linear)
    double sat = 0.0;                           // share of cells (solid angle) where upstream's skyF output clamps at 5 in some channel
};

QResult measure_q(const QSky& q)
{
    QResult r;
    const Up u = up_of(q);
    Tables t;
    build_tables(q, u, t);
    r.band = band_weight(q.sun_el);
    double sw = 0.0, mean[kVariants] = {}, cmean = 0.0;
    for (int i = 0; i < kQEl; ++i)
    {
        const double el = 0.25 + 0.5 * i;
        const double w = std::cos(el * DEG);
        for (int j = 0; j < kQAz; ++j)
        {
            const double az = 1.25 + 2.5 * j;
            const Geo g = geo_of(q.s, el, az);
            const V3 pre = upstream_pre(q, u, g);
            const V3 up = sky_final(pre, q.s, q.s.path);
            const Lab lu = display_lab(up, q.s.path);
            r.w_max = std::max({ r.w_max, up.r, up.g, up.b });
            if (pre.r * 2.0 >= 5.0 || pre.g * 2.0 >= 5.0 || pre.b * 2.0 >= 5.0)
            {
                r.sat += w;
            }
            sw += w;
            const double sep = sep_deg(q.light_el, el, az);
            for (int k = 0; k < kVariants; ++k)
            {
                const double e = de76(lu, display_lab(ours_sky(q, u, t, el, az, kNear[k]), q.s.path));
                mean[k] += w * e;
                if (e > r.v[k].worst)
                {
                    r.v[k].worst = e;
                    r.v[k].el = el;
                    r.v[k].sep = sep;
                }
            }
            const double e = de76(lu, display_lab(current_sky(q, r.band, el, az), q.s.path));
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
    }
    r.cur.mean = cmean / sw;

    if (q.sun_up)
    {
        // why a sky is hard for a physical start: the share of the sky (solid angle) where P has a starved channel or is exactly 0
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

// ---- rows and output -------------------------------------------------------------------------------------------------------------
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

void rgb8(V3 lin, int path, unsigned char* out)
{
    V3 c = lin;
    if (path != kClassic)
    {
        c = mat3_mul(kAcesIn, lin);
        c = { rrt(c.r), rrt(c.g), rrt(c.b) };
        c = mat3_mul(kAcesOut, c);
    }
    const double ch[3] = { c.r, c.g, c.b };
    for (int i = 0; i < 3; ++i)
    {
        const double x = std::min(std::max(ch[i], 0.0), 1.0);
        const double s = x < 0.0031308 ? x * 12.92 : 1.055 * std::pow(x, 0.41666) - 0.055;
        out[i] = static_cast<unsigned char>(std::round(std::min(std::max(s, 0.0), 1.0) * 255.0));
    }
}

void heat(double de, unsigned char* out)
{
    // dE 0 black, 2 blue, 5 yellow, 10 red, 20+ white (piecewise linear)
    static const double stops[5] = { 0, 2, 5, 10, 20 };
    static const double col[5][3] = { { 0, 0, 0 }, { 0, 60, 200 }, { 255, 220, 0 }, { 230, 20, 20 }, { 255, 255, 255 } };
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

void write_pano(const QSky& q, double n_deg, const std::string& file)
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
            const V3 ours = ours_sky(q, u, t, el, az, n_deg);
            const double e = de76(display_lab(up, q.s.path), display_lab(ours, q.s.path));
            rgb8(up, q.s.path, &img[3 * (j * 3 * W + i)]);
            rgb8(ours, q.s.path, &img[3 * (j * 3 * W + W + i)]);
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
    bool anchor = false;
    std::string pano_dir;
    double pano_n = 10.0;
    const char* path = nullptr;
    for (int i = 1; i < argc; ++i)
    {
        if (!std::strcmp(argv[i], "--anchor"))
        {
            anchor = true;
        }
        else if (!std::strcmp(argv[i], "--pano") && i + 2 < argc)
        {
            pano_dir = argv[++i];
            pano_n = std::atof(argv[++i]);
        }
        else
        {
            path = argv[i];
        }
    }
    if (!path)
    {
        std::printf("usage: at2_qual [--anchor | --pano DIR N] skies.txt\n");
        return 2;
    }
    std::FILE* f = std::fopen(path, "r");
    if (!f)
    {
        std::printf("cannot open %s\n", path);
        return 2;
    }
    std::vector<QSky> skies;
    char line[4096];
    while (std::fgets(line, sizeof line, f))
    {
        QSky q;
        if (parse_q(line, q))
        {
            skies.push_back(q);
        }
    }
    std::fclose(f);

    if (anchor)
    {
        for (const QSky& q : skies)
        {
            const Up u = up_of(q);
            const double samples[5][2] = { { 90.0, 0.0 }, { 2.0, 90.0 }, { 45.0, 135.0 }, { q.light_el, 14.75 }, { q.light_el + 0.4, 0.0 } };
            static const char* labels[5] = { "zenith", "horizon", "mid", "near-light", "light-core" };
            for (int a = 0; a < 5; ++a)
            {
                const V3 pre = upstream_pre(q, u, geo_of(q.s, samples[a][0], samples[a][1]));
                const double c[3] = { std::min(std::max(pre.r * 2.0, 0.0), 5.0), std::min(std::max(pre.g * 2.0, 0.0), 5.0),
                                      std::min(std::max(pre.b * 2.0, 0.0), 5.0) };
                std::printf("ANCHOR\t%s\t%s\t%.6f %.6f %.6f\n", q.s.name.c_str(), labels[a], c[0], c[1], c[2]);
            }
        }
        return 0;
    }

    if (!pano_dir.empty())
    {
        for (size_t i = 0; i < skies.size(); ++i)
        {
            write_pano(skies[i], pano_n, pano_dir + "/" + std::to_string(i) + ".rgb");
        }
        return 0;
    }

    std::printf("ROW\tname\tsun_el\tlight_el\tsun_up\tpath\tband\tw_max\tsat\tstarved\tstarved_zero");
    static const char* vn[kVariants] = { "plain", "ns5", "ns10", "ns20" };
    for (int k = 0; k < kVariants; ++k)
    {
        std::printf("\t%s_mean\t%s_worst\t%s_el\t%s_sep", vn[k], vn[k], vn[k], vn[k]);
    }
    std::printf("\tcur_mean\tcur_worst\tcur_el\tcur_sep\n");
    std::vector<QResult> res(skies.size());
    std::vector<char> done(skies.size(), 0), bad(skies.size(), 0);
    std::atomic<size_t> next { 0 };
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
                if (!(skies[i].s.my > 0.0))
                {
                    bad[i] = 1;
                    continue;
                }
                res[i] = measure_q(skies[i]);
                done[i] = 1;
            }
        });
    }
    for (std::thread& th : pool)
    {
        th.join();
    }
    for (size_t i = 0; i < skies.size(); ++i)
    {
        const QSky& q = skies[i];
        if (bad[i])
        {
            std::printf("UNDEF\t%s\t%.2f\tmax_y %g\n", q.s.name.c_str(), q.sun_el, q.s.my);
            continue;
        }
        const QResult& r = res[i];
        std::printf("ROW\t%s\t%.3f\t%.3f\t%d\t%d\t%.4f\t%.4g\t%.4f\t%.4f\t%.4f", q.s.name.c_str(), q.sun_el, q.light_el, q.sun_up, q.s.path, r.band,
                    r.w_max, r.sat, r.starved, r.starved_zero);
        for (int k = 0; k < kVariants; ++k)
        {
            std::printf("\t%.4f\t%.3f\t%.2f\t%.2f", r.v[k].mean, r.v[k].worst, r.v[k].el, r.v[k].sep);
        }
        std::printf("\t%.4f\t%.3f\t%.2f\t%.2f\n", r.cur.mean, r.cur.worst, r.cur.el, r.cur.sep);
    }
    return 0;
}
