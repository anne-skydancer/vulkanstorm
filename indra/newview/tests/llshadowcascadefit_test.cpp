// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowcascadefit.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowcascadefit_test.cpp
// The invariants that make the sun cascades stable (doc/shadows/shadow-model-rework.md §9.1):
//   - the split distances depend only on near, far and the exponent;
//   - each slice's sphere holds all eight corners of the slice, whatever the camera's
//     orientation, and its size does not depend on the orientation;
//   - the quantised radius only grows, by less than 1/16 of an octave;
//   - the snapped box holds the sphere's square, spans exactly res texels, and its texel grid
//     stays at the same world phase while the camera moves;
//   - the light view's up axis is a unit vector orthogonal to the light.

#include "llshadowcascadefit.h"

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
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("llshadowcascadefit: all checks passed\n");
    return EXIT_SUCCESS;
}
