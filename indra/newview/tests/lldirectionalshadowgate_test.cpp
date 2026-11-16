// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lldirectionalshadowgate.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lldirectionalshadowgate_test.cpp
// The dark gate of the directional shadow cascades: exactly zero light skips, a light under the threshold skips, the
// fade is 0 up to the resume level and rises smoothly to 1 at twice the threshold, a light hovering at the threshold
// does not flicker the pass, a skipped frame always has strength 0 (no stale cascade map is read with weight), and the
// frame that resumes has a strength above 0 only because it renders first.

#include "lldirectionalshadowgate.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    namespace G = LLDirectionalShadowGate;
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    void test_constants()
    {
        static_assert(G::THRESHOLD > 0.0 && G::RESUME > G::THRESHOLD && G::FULL > G::RESUME);
        check(std::fabs(G::FULL - 2.0 * G::THRESHOLD) < 1e-18, "full strength at twice the threshold");
        // the justification: at the brightest exposure (gain 16) a shadow of the threshold light is under one 8-bit step (3.0e-4)
        check(G::THRESHOLD * 16.0 < 1.0 / (255.0 * 12.92), "the threshold is under one 8-bit step at the brightest exposure");
    }

    void test_decision_light()
    {
        check(G::decisionLight(0, 0, 0, 1.5) == 0.0, "black is zero light");
        check(G::decisionLight(1, 1, 1, 1.0) > 2.99 && G::decisionLight(1, 1, 1, 1.0) < 3.01, "white at scale 1 is 3 (pbrBaseLight's factor)");
        check(std::fabs(G::decisionLight(0.5, 0.5, 0.5, 2.0) - 2.0 * G::decisionLight(0.5, 0.5, 0.5, 1.0)) < 1e-12, "linear in the sky sunlight scale");
        check(G::decisionLight(1, 0, 0, 1) < G::decisionLight(0, 1, 0, 1), "green weighs more than red");
        check(G::decisionLight(-1, std::nan(""), 0, 1) == 0.0, "negative and NaN colours count as 0");
        check(G::decisionLight(1, 1, 1, std::numeric_limits<double>::infinity()) == 0.0, "an infinite scale counts as 0");
        check(G::decisionLight(1.0 / 255, 1.0 / 255, 1.0 / 255, 1.0) > G::FULL, "a code value of 1 is far above the threshold");
    }

    void test_fade()
    {
        check(G::fade(0.0) == 0.0 && G::fade(G::THRESHOLD) == 0.0 && G::fade(G::RESUME) == 0.0, "no strength up to the resume level");
        check(G::fade(G::FULL) == 1.0 && G::fade(1.0) == 1.0 && G::fade(1e9) == 1.0, "full strength from twice the threshold: unchanged above");
        double prev = 0.0;
        bool monotonic = true, bounded = true;
        for (int i = 0; i <= 2000; ++i)
        {
            const double e = G::RESUME + (G::FULL - G::RESUME) * (double)i / 2000.0;
            const double f = G::fade(e);
            monotonic = monotonic && f >= prev;
            bounded = bounded && f >= 0.0 && f <= 1.0;
            prev = f;
        }
        check(monotonic && bounded, "the fade rises monotonically between 0 and 1");
        const double mid = G::fade(0.5 * (G::RESUME + G::FULL));
        check(mid > 0.45 && mid < 0.55, "half way at the middle of the ramp");
        // no pop: the step between two neighbouring lights is tiny
        double worst = 0.0;
        const double lo = G::RESUME * 0.9, hi = G::FULL * 1.1;
        for (int i = 0; i < 100000; ++i)
        {
            const double e = lo + (hi - lo) * (double)i / 100000.0;
            const double e2 = e + (hi - lo) / 100000.0;
            worst = std::fmax(worst, std::fabs(G::fade(e2) - G::fade(e)));
        }
        check(worst < 1e-3, "the fade is continuous (no pop)");
    }

    void test_zero_and_dark()
    {
        G::State s;
        G::Decision d = G::step(s, 0.0);
        check(d.skip && d.strength == 0.0, "exactly zero light skips with strength 0");
        d = G::step(s, G::THRESHOLD * 0.5);
        check(d.skip && d.strength == 0.0, "a light under the threshold skips");
        d = G::step(s, 1.0);
        check(!d.skip && d.strength == 1.0, "a bright light renders at full strength");
    }

    void test_hysteresis()
    {
        // a light hovering around the threshold, noise of +-5 %: it never flickers
        G::State s;
        G::step(s, G::THRESHOLD * 4);
        int changes = 0;
        bool last = false;
        for (int i = 0; i < 1000; ++i)
        {
            const double e = G::THRESHOLD * (1.0 + 0.05 * ((i % 7) - 3) / 3.0);
            const G::Decision d = G::step(s, e);
            if (i > 0 && d.skip != last)
            {
                ++changes;
            }
            last = d.skip;
        }
        check(changes <= 1, "at most one change for a light hovering within 5 % of the threshold");

        // up: skipped at 0.99 T stays skipped at T and 1.05 T, renders from above the resume level
        G::State a;
        G::step(a, 0.99 * G::THRESHOLD);
        check(G::step(a, G::THRESHOLD).skip, "still skipped at the threshold");
        check(G::step(a, 1.05 * G::THRESHOLD).skip, "still skipped inside the hysteresis band");
        const G::Decision up = G::step(a, 1.2 * G::THRESHOLD);
        check(!up.skip, "renders above the resume level");
        check(up.strength > 0.0 && up.strength < 0.1, "the first rendered frame starts the fade near 0 (it renders before it is read)");
        // down: rendering at 1.05 T stays rendering (fade 0), skips only below T
        G::State b;
        G::step(b, 3 * G::THRESHOLD);
        const G::Decision mid = G::step(b, 1.05 * G::THRESHOLD);
        check(!mid.skip && mid.strength == 0.0, "rendering in the band keeps rendering with strength 0");
        check(G::step(b, 0.999 * G::THRESHOLD).skip, "skips below the threshold");
    }

    void test_never_stale()
    {
        // along any path of lights, a skipped frame has strength exactly 0, and a strength above 0 only comes with a render
        // that frame (skip == false); the cascades of that frame are drawn before the shading reads them
        G::State s;
        unsigned seed = 12345u;
        bool ok = true;
        for (int i = 0; i < 200000; ++i)
        {
            seed = seed * 1664525u + 1013904223u;
            const double e = G::THRESHOLD * std::exp(((seed >> 8) / (double)(1u << 24) - 0.5) * 8.0); // 0.018 T .. 55 T
            const G::Decision d = G::step(s, e);
            if (d.skip && d.strength != 0.0)
            {
                ok = false;
            }
            if (!d.skip && d.strength > 0.0 && !(e > G::RESUME))
            {
                ok = false;
            }
            if (!d.skip && e >= G::FULL && d.strength != 1.0)
            {
                ok = false; // unchanged above twice the threshold
            }
            if (e >= G::FULL && d.skip)
            {
                ok = false; // a bright light never skips
            }
        }
        check(ok, "a skipped frame has strength 0; a strength above 0 comes with a render; above 2 x threshold it is 1");
    }

    void test_invalid()
    {
        G::State s;
        G::step(s, 0.0);
        check(s.skipped, "skipped on zero light");
        G::Decision d = G::step(s, 0.0, false);
        check(!d.skip && d.strength == 1.0 && !s.skipped, "no sky yet: render, full strength, state cleared");
        d = G::step(s, std::numeric_limits<double>::quiet_NaN());
        check(!d.skip && d.strength == 1.0, "a NaN light never skips");
        d = G::step(s, std::numeric_limits<double>::infinity());
        check(!d.skip && d.strength == 1.0, "an infinite light never skips");
    }
}

int main()
{
    test_constants();
    test_decision_light();
    test_fade();
    test_zero_and_dark();
    test_hysteresis();
    test_never_stale();
    test_invalid();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lldirectionalshadowgate_test: all checks passed\n");
    return 0;
}
