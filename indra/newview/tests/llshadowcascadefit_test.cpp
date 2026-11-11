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
        for (float texel : { 0.001f, 0.0058f, 0.02f, 0.0344f, 0.0344f, 0.116f, 0.275f, 0.5f, 1.f })
        {
            const float offset = SHADOW_NORMAL_OFFSET_TEXELS * texel;
            const float margin = casterCullMargin(texel, offset);
            check(margin >= SHADOW_FOOTPRINT_M + texel + offset, "margin covers the footprint, a texel and the normal offset", margin, SHADOW_FOOTPRINT_M + texel + offset);
            check(margin >= 16.f * texel + offset, "margin covers 16 texels and the normal offset", margin, 16.f * texel + offset);
            check(margin >= prev, "margin grows with the texel size", margin, prev);
            // exactly one of the two terms decides it
            const float want = std::max(16.f * texel, SHADOW_FOOTPRINT_M + texel) + offset;
            check(std::fabs(margin - want) < 1e-6f, "margin is the larger term plus the offset", margin, want);
            prev = margin;
        }
        // the finest cascade (0.58 cm texels): the footprint decides, 26 cm instead of Legacy's 9 cm
        const float fine = casterCullMargin(0.0058f, 0.0058f);
        check(fine > 0.26f && fine < 0.27f, "finest cascade margin is about 26 cm", fine, 0.26f);
        // the coarsest (27.5 cm texels): 16 texels decide
        check(casterCullMargin(0.275f, 0.275f) == 16.f * 0.275f + 0.275f, "coarse cascade margin is 16 texels plus the offset");
    }

    // Stage S2: the receiver plane's signed depth gradient that shadowUtil.glsl's shadowPlaneGradient
    // computes (np = (r0.n / r0.r0, r1.n / r1.r1, r2.n / r2.r2), g = -np.xy / np.z) against the plane
    // measured by two tangent steps through the matrix (pcssDepthGradient), for an orthographic
    // cascade matrix built from the real cascade maths: rows of the light view, scaled by the box and the depth range.
    void test_plane_gradient()
    {
        std::mt19937 rng(20261111);
        std::uniform_real_distribution<double> u(-1.0, 1.0);
        double worst = 0.0;
        int tested = 0;
        for (int trial = 0; trial < 400; ++trial)
        {
            // light direction away from the pole, cascade box and depth range of a random cascade
            D3 light = norm({ u(rng), 0.2 + 0.8 * std::fabs(u(rng)), u(rng) });
            const auto up_f = LLShadowCascadeFit::lightUp({ (float)light.x, (float)light.y, (float)light.z });
            const D3 up = norm({ up_f.x, up_f.y, up_f.z });
            const D3 right = norm(cross(up, light));
            const D3 fwd = cross(right, up); // = light, re-orthogonalised
            const double radius = 2.0 + 60.0 * std::fabs(u(rng));
            const auto box = LLShadowCascadeFit::snappedSquare(0.f, 0.f, (float)radius, 1456.f, 1456.f, 0.f, 0.f);
            const auto dr = LLShadowCascadeFit::depthRange(0.f, (float)radius);
            // texture space: u = 0.5 + 0.5 * (2 x - (max + min)) / (max - min), depth likewise, z axis towards the light
            const double sx = 1.0 / (box.max_x - box.min_x), sy = 1.0 / (box.max_y - box.min_y), sz = 1.0 / (dr.z_max - dr.z_min);
            const D3 r0 = mul(right, sx), r1 = mul(up, sy), r2 = mul(fwd, -sz);
            const D3 point = { 10.0 * u(rng), 10.0 * u(rng), 10.0 * u(rng) };
            auto project = [&](D3 p) { return D3{ dot(r0, p), dot(r1, p), dot(r2, p) }; };
            D3 n = norm({ u(rng), u(rng), u(rng) });
            if (std::fabs(dot(n, fwd)) < 0.05) // edge-on to the light: the gradient is unbounded, the shader clamps it
            {
                continue;
            }
            const D3 t1 = norm(cross(n, std::fabs(n.x) < 0.9 ? D3{ 1, 0, 0 } : D3{ 0, 1, 0 }));
            const D3 t2 = cross(n, t1);
            const D3 c = project(point);
            const D3 d1 = sub(project(add(point, mul(t1, 0.05))), c);
            const D3 d2 = sub(project(add(point, mul(t2, 0.05))), c);
            const double det = d1.x * d2.y - d1.y * d2.x;
            const double gx = (d1.z * d2.y - d1.y * d2.z) / det;
            const double gy = (d1.x * d2.z - d1.z * d2.x) / det;
            const D3 np = { dot(r0, n) / dot(r0, r0), dot(r1, n) / dot(r1, r1), dot(r2, n) / dot(r2, r2) };
            const double hx = -np.x / np.z, hy = -np.y / np.z;
            const double scale = std::max({ 1.0, std::fabs(gx), std::fabs(gy) });
            worst = std::max(worst, std::max(std::fabs(gx - hx), std::fabs(gy - hy)) / scale);
            ++tested;
        }
        check(tested > 300, "plane gradient: enough non-degenerate cases", (double)tested, 300.0);
        check(worst < 1e-9, "plane gradient from the matrix rows equals the two tangent steps", worst, 1e-9);
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
