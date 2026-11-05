// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowprojectorselect.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowprojectorselect_test.cpp
// The behaviour that keeps projector shadows from vanishing as the camera turns
// (doc/shadows/shadow-model-rework.md §9.4, stage S7):
//   - a held projector keeps its slot while drawn, and through short absences (grace time);
//   - a newcomer takes a slot only when it outscores the weaker held projector by the hysteresis;
//   - fades take a fixed time, whatever happens to the camera;
//   - the score depends on the light and the camera's distance to its lit cone only.

#include "llshadowprojectorselect.h"

#include <cstdio>
#include <cstdlib>
#include <vector>

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

    using namespace LLShadowProjectorSelect;
    const float DT = 1.f / 60.f;

    void run(Slot slots[2], const std::vector<Candidate>& c, float seconds)
    {
        for (float t = 0.f; t < seconds; t += DT)
        {
            update(slots, c, DT);
        }
    }

    bool holds(const Slot slots[2], const void* id)
    {
        return (slots[0].id == id && slots[0].fade >= 1.f) || (slots[1].id == id && slots[1].fade >= 1.f);
    }

    void test_fill_and_fade()
    {
        int a, b;
        Slot slots[2];
        std::vector<Candidate> c = { { &a, 2.f }, { &b, 1.f } };
        run(slots, c, FADE_SECONDS * 0.5f);
        check(!holds(slots, &a), "half a fade: not yet full");
        run(slots, c, FADE_SECONDS * 0.6f);
        check(holds(slots, &a) && holds(slots, &b), "both candidates shadowed after one fade time");
    }

    void test_grace()
    {
        int a, b;
        Slot slots[2];
        std::vector<Candidate> both = { { &a, 2.f }, { &b, 1.f } };
        std::vector<Candidate> only_b = { { &b, 1.f } };
        run(slots, both, 1.f);
        // the camera turns past a for less than the grace time: a keeps its full shadow
        run(slots, only_b, GRACE_SECONDS * 0.8f);
        check(holds(slots, &a), "a projector briefly out of view keeps its shadow");
        run(slots, both, 0.2f);
        check(holds(slots, &a) && holds(slots, &b), "and is still held when it comes back");
        // gone for longer: its slot fades out (and a free candidate would take it)
        run(slots, only_b, GRACE_SECONDS + FADE_SECONDS + 0.1f);
        check(!holds(slots, &a) && holds(slots, &b), "a projector gone for longer than the grace time is released");
    }

    void test_hysteresis()
    {
        int a, b, c;
        Slot slots[2];
        run(slots, { { &a, 2.f }, { &b, 1.f } }, 1.f);
        // a newcomer only a little stronger than the weaker held one: no swap (no popping)
        run(slots, { { &a, 2.f }, { &b, 1.f }, { &c, 1.2f * 1.f } }, 2.f);
        check(holds(slots, &a) && holds(slots, &b) && !holds(slots, &c), "a slightly stronger newcomer does not take a slot");
        // much stronger: it takes the weaker slot after one fade out and in
        run(slots, { { &a, 2.f }, { &b, 1.f }, { &c, 2.f * HYSTERESIS } }, 2.f * FADE_SECONDS + 0.1f);
        check(holds(slots, &a) && holds(slots, &c) && !holds(slots, &b), "a much stronger newcomer replaces the weaker one");
    }

    void test_score()
    {
        // the camera's distance is the only camera input: the same light, distance, same score
        const float s1 = score(1.f, 6.f, 1.0f, 16.f);
        const float s2 = score(1.f, 6.f, 1.0f, 16.f);
        check(s1 == s2 && s1 > 0.f, "score is a pure function", s1, s2);
        check(score(1.f, 6.f, 1.0f, 64.f) < s1, "a farther light scores less");
        check(score(1.f, 6.f, 1.4f, 16.f) > s1, "a wider cone scores more");
        check(score(2.f, 6.f, 1.0f, 16.f) > s1, "a brighter light scores more");
        check(score(1.f, 6.f, 1.0f, 0.f) == score(1.f, 6.f, 1.0f, 1.f), "inside one metre the distance stops counting");
    }

    void test_no_candidates()
    {
        int a;
        Slot slots[2];
        run(slots, { { &a, 1.f } }, 1.f);
        run(slots, {}, GRACE_SECONDS + FADE_SECONDS + 0.1f);
        check(slots[0].id == nullptr && slots[1].id == nullptr, "no candidates: the slots empty after grace and fade");
    }
}

int main()
{
    test_fill_and_fade();
    test_grace();
    test_hysteresis();
    test_score();
    test_no_candidates();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("llshadowprojectorselect: all checks passed\n");
    return EXIT_SUCCESS;
}
