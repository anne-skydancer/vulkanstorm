// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowcascadefit.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowcascadefit_test.cpp
// The invariants that make the sun cascades stable:
//   - the split distances depend only on near, far and the exponent;
//   - each slice's sphere holds all eight corners of the slice, whatever the camera's
//     orientation, and its size does not depend on the orientation;
//   - the quantised radius only grows, by less than 1/16 of an octave;
//   - the snapped box holds the sphere's square, spans exactly res texels, and its texel grid
//     stays at the same world phase while the camera moves;
//   - the box corner's world-grid index k (returned by snappedSquare) matches the box and keeps a world point's
//     map column + k constant while the camera walks (the Stochastic alpha dither origin, finding N1);
//   - the light view's up axis is a unit vector orthogonal to the light.

#include "llshadowcascadefit.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>

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

    struct D3
    {
        double x, y, z;
    };
    D3 add(D3 a, D3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    D3 sub(D3 a, D3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
    D3 mul(D3 a, double s) { return { a.x * s, a.y * s, a.z * s }; }
    double dot(D3 a, D3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
    D3 cross(D3 a, D3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
    D3 norm(D3 a) { return mul(a, 1.0 / std::sqrt(dot(a, a))); }

    void test_splits()
    {
        const auto a = LLShadowCascadeFit::splitDistances(0.25f, 128.f, 3.f);
        const auto b = LLShadowCascadeFit::splitDistances(0.25f, 128.f, 3.f);
        for (int i = 0; i < 5; ++i)
        {
            check(a[i] == b[i], "splits are a pure function", a[i], b[i]);
        }
        check(a[0] == 0.25f && a[4] == 128.f, "splits span near..far", a[0], a[4]);
        for (int i = 0; i < 4; ++i)
        {
            check(a[i] < a[i + 1], "splits increase", a[i], a[i + 1]);
        }
        for (float e : { 0.5f, 1.f, 2.f, 3.f, 6.f })
        {
            const auto s = LLShadowCascadeFit::splitDistances(0.1f, 64.f, e);
            for (int i = 0; i < 4; ++i)
            {
                check(s[i] <= s[i + 1], "splits never decrease", s[i], s[i + 1]);
            }
        }
    }

    // the eight corners of a slice n..f of a camera at eye looking along at with up axis up
    void slice_corners(D3 eye, D3 at, D3 up, double tan_y, double aspect, double n, double f, D3 out[8])
    {
        const D3 left = norm(cross(up, at));
        const D3 u = cross(at, left);
        int k = 0;
        for (double d : { n, f })
        {
            for (int sx : { -1, 1 })
            {
                for (int sy : { -1, 1 })
                {
                    out[k++] = add(add(add(eye, mul(at, d)), mul(left, sx * d * tan_y * aspect)), mul(u, sy * d * tan_y));
                }
            }
        }
    }

    void test_sphere()
    {
        std::mt19937 rng(653);
        std::uniform_real_distribution<double> uni(-1.0, 1.0);
        for (int t = 0; t < 2000; ++t)
        {
            const double fov_y = 0.3 + 1.6 * (uni(rng) * 0.5 + 0.5);
            const double aspect = 0.75 + 2.0 * (uni(rng) * 0.5 + 0.5);
            const double n = 0.1 + 20.0 * (uni(rng) * 0.5 + 0.5);
            const double f = n + 0.5 + 300.0 * (uni(rng) * 0.5 + 0.5);
            const double tan_y = std::tan(fov_y / 2);
            const double k = tan_y * std::sqrt(1.0 + aspect * aspect);
            const auto s = LLShadowCascadeFit::sliceSphere((float)k, (float)n, (float)f);
            // any orientation: the corners stay inside, and the sphere does not change
            D3 at = norm({ uni(rng), uni(rng), uni(rng) });
            D3 up = norm(cross(at, norm({ uni(rng), uni(rng), uni(rng) })));
            const D3 eye = { 1000.0 * uni(rng), 1000.0 * uni(rng), 2000.0 + 2000.0 * uni(rng) };
            D3 c[8];
            slice_corners(eye, at, up, tan_y, aspect, n, f, c);
            const D3 centre = add(eye, mul(at, s.distance));
            double worst = 0.0;
            for (const D3& p : c)
            {
                const D3 d = sub(p, centre);
                worst = std::fmax(worst, std::sqrt(dot(d, d)));
            }
            check(worst <= s.radius * (1.0 + 1e-5) + 1e-4, "sphere holds the slice", worst, s.radius);
            // and is tight: some corner lies on it
            check(worst >= s.radius * (1.0 - 1e-4) - 1e-4, "sphere is tight", worst, s.radius);
        }
    }

    void test_quantize()
    {
        for (double r = 0.01; r < 5000.0; r *= 1.013)
        {
            const float q = LLShadowCascadeFit::quantizeRadius((float)r);
            check(q >= (float)r, "quantised radius never shrinks", q, r);
            check(q <= (float)r * (1.f + 1.f / 16.f) * 1.0001f, "quantised radius grows by less than 1/16", q, r);
            check(LLShadowCascadeFit::quantizeRadius(q) == q, "quantising is idempotent", q, LLShadowCascadeFit::quantizeRadius(q));
        }
    }

    void test_snap()
    {
        // a light-view basis fixed in the world; the view is anchored at the moving camera, as
        // pipeline.cpp look(camera origin, ...) is: light-view coordinates are relative to the eye
        const LLShadowCascadeFit::Vec3 lv = { 0.6f, -0.3f, -0.74f };
        const auto upf = LLShadowCascadeFit::lightUp(lv);
        const D3 dir = norm({ lv.x, lv.y, lv.z });
        const D3 upv = { upf.x, upf.y, upf.z };
        const D3 lft = norm(cross(dir, upv));
        const D3 upn = norm(cross(lft, dir));
        const float r = LLShadowCascadeFit::quantizeRadius(6.83f);
        const float res = 1456.f;
        const double texel = 2.0 * r / (res - 1.0);
        const D3 world_point = { 131.25, 87.5, 3000.4 }; // a fixed point in a skybox
        double phase0 = -1.0;
        for (int step = 0; step < 400; ++step)
        {
            const D3 eye = { 128.0 + 0.0137 * step, 90.0 - 0.0071 * step, 3001.7 + 0.002 * step };
            const D3 at = norm({ std::cos(0.05 * step), std::sin(0.05 * step), -0.2 });
            const D3 centre_w = add(eye, mul(at, 5.0));
            // light-view coordinates relative to the eye (float, as glm computes them)
            const float cx = (float)dot(sub(centre_w, eye), lft), cy = (float)dot(sub(centre_w, eye), upn);
            const float ax = (float)dot(sub({ 0, 0, 0 }, eye), lft), ay = (float)dot(sub({ 0, 0, 0 }, eye), upn);
            const auto box = LLShadowCascadeFit::snappedSquare(cx, cy, r, res, res, ax, ay);
            check(box.min_x <= cx - r + 1e-3f && box.max_x >= cx + r - 1e-3f, "box holds the sphere (x)", box.min_x, cx - r);
            check(box.min_y <= cy - r + 1e-3f && box.max_y >= cy + r - 1e-3f, "box holds the sphere (y)", box.min_y, cy - r);
            const double tx = (box.max_x - box.min_x) / res;
            check(std::fabs(tx - texel) < texel * 1e-3, "box spans res texels of the fixed size", tx, texel);
            // the fixed world point's position in texels from the box corner: its fractional part
            // (the grid's phase at that point) must not change while the camera moves and turns
            const double px = dot(sub(world_point, eye), lft);
            const double u = (px - box.min_x) / texel;
            double phase = u - std::floor(u);
            if (phase0 < 0.0)
            {
                phase0 = phase;
            }
            double dphase = std::fabs(phase - phase0);
            dphase = std::fmin(dphase, 1.0 - dphase);
            check(dphase < 0.05, "texel grid phase is fixed in the world", phase, phase0);
        }
    }

    // The shader's hash (shadowAlphaMaskF.glsl), in float, of the fragment (fx, fy) minus the origin
    float dither_hash(float fx, float fy, float origin_x, float origin_y)
    {
        const float a = 0.06711056f * (fx - origin_x) + 0.00583715f * (fy - origin_y);
        const float b = a - std::floor(a);
        const float c = 52.9829189f * b;
        return c - std::floor(c);
    }

    // Finding N1: a camera walks (x, y and diagonal) past a fixed world point at the centre of a world texel. For the
    // map pixel that holds the point, the hash of (gl_FragCoord - origin) with the origin from alphaDitherOrigin must
    // be the same at every step: it depends only on the world texel (N) and on the 2^16 wrap epoch of k, which is
    // the one place it may change (all points together). Eye positions are light-view coordinates of the eye; the
    // world origin is at 0 there, so k = (eye - r) / texel for a sphere centred on the eye.
    void walk_case(const char* name, double ex0, double ey0, double dirx, double diry, double stride, int steps,
        float res_x, float res_y, bool expect_wrap)
    {
        const float r = LLShadowCascadeFit::quantizeRadius(6.83f);
        const float tx = 2.f * r / (res_x - 1.f), ty = 2.f * r / (res_y - 1.f);
        // the world point: the centre of the world texel under the middle of the walk
        const double midx = ex0 + dirx * stride * steps / 2.0, midy = ey0 + diry * stride * steps / 2.0;
        const long long nx = (long long)std::floor(midx / tx), ny = (long long)std::floor(midy / ty);
        const double wx = ((double)nx + 0.5) * tx, wy = ((double)ny + 0.5) * ty;
        long long kx_prev = 0, ky_prev = 0, epoch0 = 0, changes_x = 0, changes_y = 0, epoch_changes = 0, epoch_prev = 0;
        float hash_ref = -1.f;
        for (int step = 0; step <= steps; ++step)
        {
            const double ex = ex0 + dirx * stride * step, ey = ey0 + diry * stride * step;
            const float ax = (float)(-ex), ay = (float)(-ey); // the world origin, eye relative
            const auto box = LLShadowCascadeFit::snappedSquare(0.f, 0.f, r, res_x, res_y, ax, ay);
            // the box corner is the formula it had before k was returned, bit for bit
            check(box.min_x == ax + std::floor((0.f - r - ax) / tx) * tx && box.min_y == ay + std::floor((0.f - r - ay) / ty) * ty,
                  "box corner is unchanged by k", box.min_x, box.min_y);
            // k is the integer in (min - anchor) / texel
            const double qx = ((double)box.min_x - (double)ax) / tx, qy = ((double)box.min_y - (double)ay) / ty;
            check(std::llround(qx) == box.k_x && std::llround(qy) == box.k_y && std::fabs(qx - (double)box.k_x) < 0.05
                  && std::fabs(qy - (double)box.k_y) < 0.05, name, qx, (double)box.k_x);
            // the map pixel holding the world point (eye-relative position minus the corner)
            const long long mx = (long long)std::floor((wx - ex - (double)box.min_x) / tx);
            const long long my = (long long)std::floor((wy - ey - (double)box.min_y) / ty);
            check(mx >= 0 && mx < (long long)res_x && my >= 0 && my < (long long)res_y, "the world point stays in the map", (double)mx, (double)my);
            const auto origin = LLShadowCascadeFit::alphaDitherOrigin(box);
            const float h = dither_hash((float)mx + 0.5f, (float)my + 0.5f, origin.x, origin.y);
            // what the hash must be: the world texel's column minus the wrap epoch of k
            const long long epoch_x = (long long)std::floor((double)box.k_x / 65536.0);
            const long long epoch_y = (long long)std::floor((double)box.k_y / 65536.0);
            const float want = dither_hash((float)(nx - 65536 * epoch_x) + 0.5f, (float)(ny - 65536 * epoch_y) + 0.5f, 0.f, 0.f);
            check(h == want, name, h, want);
            check(std::fabs(origin.x) < 65536.f && std::fabs(origin.y) < 65536.f, "the origin stays below 2^16", origin.x, origin.y);
            const long long epoch = epoch_x * 1000003 + epoch_y;
            if (step == 0)
            {
                hash_ref = h;
                epoch0 = epoch;
            }
            else
            {
                changes_x += box.k_x != kx_prev;
                changes_y += box.k_y != ky_prev;
                if (epoch == epoch0)
                {
                    check(h == hash_ref, "the hash does not re-roll inside a wrap epoch", h, hash_ref);
                }
                epoch_changes += epoch != epoch_prev;
            }
            kx_prev = box.k_x;
            ky_prev = box.k_y;
            epoch_prev = epoch;
        }
        // k changes at most once per texel walked (per axis), the true bound: floor(distance / texel) + 1
        const double walked_x = std::fabs(dirx) * stride * steps / tx, walked_y = std::fabs(diry) * stride * steps / ty;
        check((double)changes_x <= std::floor(walked_x) + 1.0 && (double)changes_y <= std::floor(walked_y) + 1.0,
              "k changes at most once per texel walked", (double)changes_x, walked_x);
        if (expect_wrap)
        {
            check(epoch_changes >= 1, "the walk crosses a multiple of 2^16 in k", (double)epoch_changes);
        }
        else
        {
            check(epoch_changes == 0, "no wrap epoch change in this walk", (double)epoch_changes);
        }
    }

    void test_world_anchor()
    {
        const float r = LLShadowCascadeFit::quantizeRadius(6.83f);
        const double t = 2.0 * r / (1456.0 - 1.0);
        const double r_d = r;
        for (double stride : { 0.1 * t, 0.5 * t, 1.0 * t, 3.7 * t })
        {
            const int steps = 120;
            walk_case("walk x", 60.0, 40.0, 1.0, 0.0, stride, steps, 1456.f, 1456.f, false);
            walk_case("walk y", 60.0, 40.0, 0.0, 1.0, stride, steps, 1456.f, 1456.f, false);
            walk_case("walk diagonal", 60.0, 40.0, 0.70710678, 0.70710678, stride, steps, 1456.f, 1456.f, false);
            walk_case("non-square map", 60.0, 40.0, 0.70710678, -0.70710678, stride, steps, 1456.f, 728.f, false);
            walk_case("negative k", -300.0, -200.0, 0.6, 0.8, stride, steps, 1456.f, 1456.f, false);
            // k crosses zero mid-walk on both axes (the eye passes r): the epoch changes there, as at any multiple of 2^16
            walk_case("k crosses zero", r_d - 0.5 * stride * steps, r_d - 0.5 * stride * steps, 1.0, 1.0, stride, steps,
                      1456.f, 1456.f, true);
            // far from the origin, the walk crosses k = 6 * 2^16: the grain re-rolls once, there only
            const double wrap_eye = 6.0 * 65536.0 * t + r_d - 0.5 * stride * steps;
            walk_case("large offset wraps", wrap_eye, wrap_eye * 0.5, 1.0, 0.0, stride, steps, 1456.f, 1456.f, true);
        }
        // a large offset without the wrap
        walk_case("large offset, no wrap", 6.0 * 65536.0 * t + 100.0, 3000.0, 0.6, 0.8, 0.5 * t, 100, 1456.f, 1456.f, false);
        // non-finite input gives k = 0 (no undefined cast)
        const auto bad = LLShadowCascadeFit::snappedSquare(std::nanf(""), 0.f, r, 1456.f, 1456.f, 0.f, 0.f);
        check(bad.k_x == 0, "NaN input gives k = 0");
        const auto inf = LLShadowCascadeFit::snappedSquare(0.f, INFINITY, r, 1456.f, 1456.f, 0.f, 0.f);
        check(inf.k_y == 0, "inf input gives k = 0");
        // the wrap is a non-negative modulo
        check(LLShadowCascadeFit::wrapDitherK(-1) == -65535.f && LLShadowCascadeFit::wrapDitherK(65536) == 0.f
              && LLShadowCascadeFit::wrapDitherK(-65536) == 0.f && LLShadowCascadeFit::wrapDitherK(5) == -5.f, "wrapDitherK is a non-negative modulo");
    }

    void test_up()
    {
        std::mt19937 rng(7);
        std::uniform_real_distribution<float> uni(-1.f, 1.f);
        for (int t = 0; t < 5000; ++t)
        {
            LLShadowCascadeFit::Vec3 d = { uni(rng), uni(rng), uni(rng) };
            if (d.x * d.x + d.y * d.y + d.z * d.z < 1e-4f)
            {
                continue;
            }
            const auto u = LLShadowCascadeFit::lightUp(d);
            const float len = std::sqrt(u.x * u.x + u.y * u.y + u.z * u.z);
            const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            check(std::fabs(len - 1.f) < 1e-4f, "up is a unit vector", len);
            check(std::fabs((u.x * d.x + u.y * d.y + u.z * d.z) / dl) < 1e-4f, "up is orthogonal to the light");
        }
        // the same light gives the same up, whatever the camera (it takes no camera input at all)
        const auto a = LLShadowCascadeFit::lightUp({ 0.3f, 0.2f, -0.9f });
        const auto b = LLShadowCascadeFit::lightUp({ 0.3f, 0.2f, -0.9f });
        check(a.x == b.x && a.y == b.y && a.z == b.z, "up depends on the light only");
        check(LLShadowCascadeFit::LIGHT_UP_SWITCH >= 0.9985f, "the up switch sits next to the north-south axis",
              LLShadowCascadeFit::LIGHT_UP_SWITCH);

        // A light moving slowly towards the north horizon (straight north, and slightly off
        // it) turns the up continuously until it is right at the north-south axis: no step
        // between two lights 0.01 degrees apart turns it more than 2 degrees while both stay
        // outside the switch. The old switch at |y| > 0.9 (about 26 degrees) failed this.
        for (float off : { 0.f, 0.02f, -0.05f })
        {
            double worst = 0.0;
            LLShadowCascadeFit::Vec3 prev = {};
            bool have_prev = false;
            for (int i = 0; i <= 6000; ++i)
            {
                const double e = (60.0 - i * 0.01) * 3.14159265358979 / 180.0; // elevation 60..0 degrees
                LLShadowCascadeFit::Vec3 d = { off, (float)std::cos(e), (float)-std::sin(e) };
                const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
                if (std::fabs(d.y / dl) > 0.9985f)
                {
                    have_prev = false; // within about 3 degrees of the axis one turn is allowed
                    continue;
                }
                const auto u = LLShadowCascadeFit::lightUp(d);
                if (have_prev)
                {
                    const double dot = (double)u.x * prev.x + (double)u.y * prev.y + (double)u.z * prev.z;
                    const double ang = std::acos(std::min(1.0, std::max(-1.0, dot))) * 180.0 / 3.14159265358979;
                    worst = std::max(worst, ang);
                }
                prev = u;
                have_prev = true;
            }
            check(worst < 2.0, "up turns continuously as the light nears the north horizon", worst, off);
        }
    }

    void test_distance_steps()
    {
        const float cases[][2] = { { 0.f, 32.f }, { -5.f, 32.f }, { 31.f, 32.f }, { 47.9f, 32.f }, { 48.1f, 64.f },
                                   { 100.f, 96.f }, { 128.f, 128.f }, { 175.f, 160.f }, { 177.f, 192.f },
                                   { 256.f, 256.f }, { 300.f, 256.f }, { 512.f, 256.f } };
        for (const auto& c : cases)
        {
            const float got = LLShadowCascadeFit::snapShadowDistance(c[0]);
            check(got == c[1], "shadow distance snaps to the 32 m steps", got, c[1]);
        }
        check(LLShadowCascadeFit::snapShadowDistance(std::nanf("")) == 32.f, "NaN distance snaps to 32");
    }


    // Stage S2: the cull guard covers the filter's reach (SHADOW_FOOTPRINT_M plus a bilinear texel, or
    // 16 texels) and the sample's normal offset, at every texel size a cascade can have
    void test_cull_margin()
    {
        using namespace LLShadowCascadeFit;
        check(SHADOW_FOOTPRINT_M == 0.25f, "the footprint is one radius of 25 cm", SHADOW_FOOTPRINT_M, 0.25f);
        float prev = 0.f;
        for (float texel : { 0.001f, 0.0058f, 0.02f, 0.0344f, 0.116f, 0.275f, 0.5f, 1.f })
        {
            const float offset = SHADOW_NORMAL_OFFSET_TEXELS * texel;
            const float margin = casterCullMargin(texel, offset);
            check(margin >= SHADOW_FOOTPRINT_M + texel + offset, "margin covers the footprint, a texel and the normal offset", margin, SHADOW_FOOTPRINT_M + texel + offset);
            check(margin >= 16.f * texel + offset, "margin covers 16 texels and the normal offset", margin, 16.f * texel + offset);
            check(margin >= prev, "margin grows with the texel size", margin, prev);
            // exactly one of the two terms decides it
            const float want = std::max(16.f * texel, SHADOW_FOOTPRINT_M + texel) + offset;
            check(std::fabs(margin - want) < 1e-6f, "margin is the larger term plus the offset", margin, want);
            // the farthest a filter reaches from the sample (shadowUtil.glsl: the search and the filter extent are
            // clamped to max(footprint, 2 texels), and the bilinear compare adds a texel), moved by the normal offset
            const float reach = std::max(SHADOW_FOOTPRINT_M, 2.f * texel) + texel + offset;
            check(reach <= margin, "the filter reach stays inside the cull margin", reach, margin);
            prev = margin;
        }
        // the finest cascade (0.58 cm texels): the footprint decides, 26 cm instead of 14 cm (16 texels plus 5 cm)
        const float fine = casterCullMargin(0.0058f, 0.0058f);
        check(fine > 0.26f && fine < 0.27f, "finest cascade margin is about 26 cm", fine, 0.26f);
        // the coarse cascades (11.6 cm texels and up) are texel limited: 16 texels decide
        check(casterCullMargin(0.116f, 0.116f) == 16.f * 0.116f + 0.116f, "texel limited cascade margin is 16 texels plus the offset");
        check(casterCullMargin(0.275f, 0.275f) == 16.f * 0.275f + 0.275f, "coarse cascade margin is 16 texels plus the offset");
    }

    // a 4x4 matrix in doubles, row major, for the cascade matrix as pipeline.cpp builds it
    struct M4
    {
        double m[4][4];
    };
    M4 mmul(const M4& a, const M4& b)
    {
        M4 r{};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += a.m[i][k] * b.m[k][j];
        return r;
    }

    // Stage S2: the receiver plane's signed depth gradient that shadowUtil.glsl's shadowPlaneGradient computes
    // (np = (r0.n / r0.r0, r1.n / r1.r1, r2.n / r2.r2), g = -np.xy / np.z, clamped to SHADOW_GRAD_MAX) against
    // the plane measured by two tangent steps through the matrix. The matrix is the one
    // generateSunCascadesStable builds: bias(0.5, 0.5) * ortho(snappedSquare box, depthRange) * light view.
    void test_plane_gradient()
    {
        std::mt19937 rng(20261111);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        double worst = 0.0;
        int tested = 0;
        int clamped = 0;
        for (int trial = 0; trial < 600; ++trial)
        {
            // light direction away from the pole, cascade box and depth range of a random cascade
            D3 light = norm({ u(rng), 0.2 + 0.8 * std::fabs(u(rng)), u(rng) });
            const auto up_f = LLShadowCascadeFit::lightUp({ (float)light.x, (float)light.y, (float)light.z });
            const D3 up = norm({ up_f.x, up_f.y, up_f.z });
            const D3 right = norm(cross(up, light));
            const D3 back = cross(right, up); // the view z axis (towards the light), re-orthogonalised
            const double radius = 2.0 + 60.0 * std::fabs(u(rng));
            const auto box = LLShadowCascadeFit::snappedSquare(0.f, 0.f, (float)radius, 1456.f, 1456.f, 0.f, 0.f);
            const auto dr = LLShadowCascadeFit::depthRange(0.f, (float)radius);
            const M4 view = { { { right.x, right.y, right.z, 0 }, { up.x, up.y, up.z, 0 }, { back.x, back.y, back.z, 0 }, { 0, 0, 0, 1 } } };
            // glm::ortho(l, r, b, t, n, f) with n = -z_max and f = -z_min, as pipeline.cpp calls it
            const double l = box.min_x, r = box.max_x, b = box.min_y, tp = box.max_y, n = -dr.z_max, f = -dr.z_min;
            const M4 ortho = { { { 2 / (r - l), 0, 0, -(r + l) / (r - l) }, { 0, 2 / (tp - b), 0, -(tp + b) / (tp - b) },
                                 { 0, 0, -2 / (f - n), -(f + n) / (f - n) }, { 0, 0, 0, 1 } } };
            const M4 bias = { { { 0.5, 0, 0, 0.5 }, { 0, 0.5, 0, 0.5 }, { 0, 0, 0.5, 0.5 }, { 0, 0, 0, 1 } } };
            const M4 mat = mmul(bias, mmul(ortho, view));
            const D3 r0 = { mat.m[0][0], mat.m[0][1], mat.m[0][2] };
            const D3 r1 = { mat.m[1][0], mat.m[1][1], mat.m[1][2] };
            const D3 r2 = { mat.m[2][0], mat.m[2][1], mat.m[2][2] };
            const D3 point = { 10.0 * u(rng), 10.0 * u(rng), 10.0 * u(rng) };
            auto project = [&](D3 p) { return D3{ dot(r0, p), dot(r1, p), dot(r2, p) }; };
            // every fourth case is nearly edge-on to the light
            D3 nrm = norm({ u(rng), u(rng), u(rng) });
            if (trial % 4 == 0)
            {
                nrm = norm(add(nrm, mul(back, -dot(nrm, back) * (1.0 - 1e-9))));
            }
            const D3 t1 = norm(cross(nrm, std::fabs(nrm.x) < 0.9 ? D3{ 1, 0, 0 } : D3{ 0, 1, 0 }));
            const D3 t2 = cross(nrm, t1);
            const D3 c = project(point);
            const D3 d1 = sub(project(add(point, mul(t1, 0.05))), c);
            const D3 d2 = sub(project(add(point, mul(t2, 0.05))), c);
            const double det = d1.x * d2.y - d1.y * d2.x;
            const double gx = (d1.z * d2.y - d1.y * d2.z) / det;
            const double gy = (d1.x * d2.z - d1.z * d2.x) / det;
            // shadowPlaneGradient, in doubles
            const D3 np = { dot(r0, nrm) / dot(r0, r0), dot(r1, nrm) / dot(r1, r1), dot(r2, nrm) / dot(r2, r2) };
            const double nz = std::fabs(np.z) < 1e-7 ? (np.z < 0 ? -1e-7 : 1e-7) : np.z;
            const double gmax = LLShadowCascadeFit::SHADOW_GRAD_MAX;
            const double hx = std::clamp(-np.x / nz, -gmax, gmax), hy = std::clamp(-np.y / nz, -gmax, gmax);
            check(std::fabs(hx) <= gmax && std::fabs(hy) <= gmax, "the shader gradient never exceeds SHADOW_GRAD_MAX", hx, hy);
            if (!(std::fabs(gx) < gmax && std::fabs(gy) < gmax)) // also catches an infinite or NaN measured slope
            {
                // edge-on beyond the limit: the shader value is the clamp, not the measured slope
                ++clamped;
                check(std::fabs(hx) == gmax || std::fabs(hy) == gmax, "an edge-on plane is clamped to SHADOW_GRAD_MAX", hx, hy);
                continue;
            }
            const double scale = std::max({ 1.0, std::fabs(gx), std::fabs(gy) });
            worst = std::max(worst, std::max(std::fabs(gx - hx), std::fabs(gy - hy)) / scale);
            ++tested;
        }
        check(tested > 300, "plane gradient: enough non-degenerate cases", (double)tested, 300.0);
        check(clamped > 20, "plane gradient: edge-on cases reach the clamp", (double)clamped, 20.0);
        check(worst < 1e-6, "plane gradient from the matrix rows equals the two tangent steps", worst, 1e-6);
    }

    void test_depth()
    {
        const auto z = LLShadowCascadeFit::depthRange(-3.f, 5.f);
        check(z.z_min == -8.f && z.z_max == 7.f, "depth range: sphere plus one radius towards the light", z.z_min, z.z_max);
    }
}

int main()
{
    test_splits();
    test_sphere();
    test_quantize();
    test_snap();
    test_world_anchor();
    test_up();
    test_depth();
    test_cull_margin();
    test_plane_gradient();
    test_distance_steps();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("llshadowcascadefit: all checks passed\n");
    return EXIT_SUCCESS;
}
