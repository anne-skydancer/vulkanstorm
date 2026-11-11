// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llcloudoptics.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llcloudoptics_test.cpp
// Stage L2b of the light study (doc/light/cloud-shadows.md):
//   - clear is exactly clear: tau(0) = 0, beam 1, diffuse 0;
//   - over a 1e5 grid of opacities and several tau_v: tau is monotone, 0 <= beam <= total <= 1,
//     diffuse >= 0, beam + diffuse + reflected = 1 with reflected >= 0;
//   - the cap: tau(1) = tau_v ln 255, and an opacity past it does not go further;
//   - alpha() is the inverse of tau();
//   - the anchors of the default tau_v 7.6: the visibility threshold tau 0.03 is one 8-bit step of
//     the drawn opacity; alpha 0.5 lies in the ISCCP middle class (3.6 - 23), alpha 0.96 at its
//     thick boundary (>= 23);
//   - a table of reference values from the closed form, computed independently in Python;
//   - delta-Eddington invariance: (1 - g') tau' = (1 - g) tau for the scaled problem
//     (g' = g / (1 + g), tau' = (1 - g^2) tau), so the total is the one of the unscaled problem.

#include "llcloudoptics.h"

#include <cmath>
#include <cstdio>

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

struct Row
{
    double alpha, tau_v, tau, beam, total, diffuse;
};

// from Python's math: tau = -tau_v ln(max(1 - alpha, 1/255)), beam = exp(-(1 - 0.85^2) tau),
// total = 1 / (1 + 0.75 (1 - 0.85) tau), diffuse = max(total - beam, 0)
const Row TABLE[] = {
    { 0, 7.6, -0.0, 1.0, 1.0, 0.0 },
    { 0.004, 7.6, 0.030460962621295044, 0.9915827083262344, 0.99658484495749, 0.005002136631255594 },
    { 0.05, 7.6, 0.3898290373453844, 0.897468229741157, 0.9579867563121843, 0.0605185265710273 },
    { 0.1, 7.6, 0.8007399189994797, 0.8007509312491805, 0.917361135821055, 0.11661020457187454 },
    { 0.25, 7.6, 2.1863837506335346, 0.5451351732277068, 0.802588719772788, 0.25745354654508124 },
    { 0.3, 7.6, 2.7107295739343664, 0.4713155476354774, 0.7663087296688036, 0.2949931820333262 },
    { 0.5, 7.6, 5.267918572255584, 0.23180763660886555, 0.6278879551965233, 0.39608031858765774 },
    { 0.724, 7.6, 9.783893540813901, 0.0662029365954025, 0.4760345129255796, 0.4098315763301771 },
    { 0.9, 7.6, 17.49964670675475, 0.0077803655103980225, 0.3368466149503314, 0.3290662494399334 },
    { 0.96, 7.6, 24.463456268998318, 0.0011265345654310154, 0.26651465876866026, 0.26538812420322927 },
    { 0.99, 7.6, 34.99929341350949, 6.0534087475391195e-05, 0.202534906267994, 0.2024743721805186 },
    { 1.0, 7.6, 42.113602943204036, 8.406336285749004e-06, 0.17428342360511145, 0.17427501726882572 },
    { 0.5, 3.0, 2.0794415416798357, 0.561555297551196, 0.8104140320313077, 0.24885873448011164 },
    { 0.5, 20.0, 13.862943611198906, 0.02134437901178741, 0.3906889209409654, 0.369344541929178 },
};
}

int main()
{
    using namespace LLCloudOptics;

    // clear is exactly clear
    for (double tv : { 1.0, 3.0, 7.6, 20.0, 30.0 })
    {
        check(tau(0.0, tv) == 0.0, "tau(0) is 0", tau(0.0, tv));
        const Transmittance t = optics(0.0, tv);
        check(t.beam == 1.0, "clear beam is exactly 1", t.beam);
        check(t.diffuse == 0.0, "clear diffuse is exactly 0", t.diffuse);
    }

    // the grid
    for (double tv : { 1.0, 3.0, DEFAULT_VISUAL_DEPTH, 20.0, 30.0 })
    {
        double prev_tau = -1.0, prev_beam = 2.0;
        const int N = 100000;
        for (int i = 0; i <= N; ++i)
        {
            const double a = (double) i / N;
            const double t = tau(a, tv);
            const Transmittance o = optics(a, tv);
            const double tot = total(t);
            check(t >= prev_tau, "tau is monotone", prev_tau, t);
            check(o.beam <= prev_beam, "beam falls with opacity", prev_beam, o.beam);
            check(o.beam >= 0.0 && o.beam <= tot + 1e-15, "0 <= beam <= total", o.beam, tot);
            check(tot <= 1.0, "total <= 1", tot);
            check(o.diffuse >= 0.0, "diffuse >= 0", o.diffuse);
            check(std::fabs(o.diffuse - (tot - o.beam)) < 1e-15, "diffuse is total - beam", o.diffuse, tot - o.beam);
            check(1.0 - o.beam - o.diffuse >= -1e-15, "reflected >= 0", o.beam + o.diffuse);
            prev_tau = t;
            prev_beam = o.beam;
        }
        check(std::fabs(tau(1.0, tv) - tv * std::log(255.0)) < 1e-12, "tau(1) = tau_v ln 255", tau(1.0, tv), tv * std::log(255.0));
        check(tau(1.0 - 1e-9, tv) == tau(1.0, tv), "the cap holds past one step", tau(1.0 - 1e-9, tv), tau(1.0, tv));
        for (int i = 0; i <= 250; ++i)
        {
            const double a = i / 255.0 * 0.999; // below the cap
            check(std::fabs(alpha(tau(a, tv), tv) - a) < 1e-12, "alpha(tau(a)) = a", alpha(tau(a, tv), tv), a);
        }
        check(alpha(0.0, tv) == 0.0, "alpha(0) = 0", alpha(0.0, tv));
    }

    // diffuse >= 0 without the clamp too, for every tau: (1 - g^2) >= 3/4 (1 - g) makes beam <= total
    for (double t = 0.0; t < 200.0; t += 0.01)
    {
        check(total(t) - beam(t) >= -1e-15, "total >= beam without the clamp", total(t), beam(t));
    }

    // the anchors at tau_v 7.6
    {
        const double step = alpha(0.03, DEFAULT_VISUAL_DEPTH);
        check(std::fabs(step - ALPHA_STEP) < 1.5e-4, "tau 0.03 is about one 8-bit step of opacity", step, ALPHA_STEP);
        const double t50 = tau(0.5, DEFAULT_VISUAL_DEPTH);
        check(t50 >= 3.6 && t50 <= 23.0, "alpha 0.5 is in the ISCCP middle class", t50);
        check(tau(0.96, DEFAULT_VISUAL_DEPTH) >= 23.0, "alpha 0.96 is at the thick boundary or beyond", tau(0.96, DEFAULT_VISUAL_DEPTH));
    }

    // reference values
    for (const Row& r : TABLE)
    {
        const double t = tau(r.alpha, r.tau_v);
        check(std::fabs(t - r.tau) < 1e-12, "tau against the table", t, r.tau);
        const Transmittance o = optics(r.alpha, r.tau_v);
        check(std::fabs(o.beam - r.beam) < 1e-12, "beam against the table", o.beam, r.beam);
        check(std::fabs(total(t) - r.total) < 1e-12, "total against the table", total(t), r.total);
        check(std::fabs(o.diffuse - r.diffuse) < 1e-12, "diffuse against the table", o.diffuse, r.diffuse);
    }

    // delta-Eddington invariance
    for (double g : { 0.5, 0.75, G_WATER, 0.9 })
    {
        for (double t : { 0.03, 1.0, 5.0, 25.0 })
        {
            const double gs = g / (1.0 + g);       // the scaled asymmetry
            const double ts = (1.0 - g * g) * t;   // the scaled optical depth
            check(std::fabs((1.0 - gs) * ts - (1.0 - g) * t) < 1e-12, "(1 - g') tau' = (1 - g) tau", (1.0 - gs) * ts, (1.0 - g) * t);
        }
    }

    // the shader parameters follow the constants
    {
        const ShaderParams p = shader_params(7.6);
        check(p.visual_depth == 7.6f && p.alpha_step == (float) (1.0 / 255.0), "shader params: tau_v and step");
        check(std::fabs(p.beam_coefficient - (1.0 - 0.7225)) < 1e-7, "shader params: 1 - g^2", p.beam_coefficient);
        check(std::fabs(p.total_coefficient - 0.1125) < 1e-7, "shader params: 3/4 (1 - g)", p.total_coefficient);
    }

    if (failures)
    {
        std::printf("llcloudoptics_test: %d failures\n", failures);
        return 1;
    }
    std::printf("llcloudoptics_test: OK\n");
    return 0;
}
