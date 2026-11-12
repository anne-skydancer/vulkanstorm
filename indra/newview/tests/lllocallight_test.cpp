// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of lllocallight.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/lllocallight_test.cpp
// The emitter radius is clamped to 0.02 to 0.5 m; the exclusion zone of a light is its own drawable's radius within the
// emitter radius and 1 m, and a projector's lies beyond the stretch from its eye to the prim's face.

#include "lllocallight.h"

#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    bool near_to(float a, float b, float eps = 1.0e-5f)
    {
        return std::fabs(a - b) <= eps;
    }

    void test_emitter()
    {
        using namespace LLLocalLight;
        check(clampEmitter(0.0f) == EMITTER_MIN && clampEmitter(-3.f) == EMITTER_MIN, "a tiny emitter is clamped up");
        check(clampEmitter(9.0f) == EMITTER_MAX, "a huge emitter is clamped down");
        check(clampEmitter(0.3f) == 0.3f, "an emitter in range is unchanged");
        check(PROJECTOR_EMITTER_U0 >= EMITTER_MIN && PROJECTOR_EMITTER_U0 <= EMITTER_MAX, "today's radius is in range");
        check(near_to(projectorEmitterRadius(0.5f, 0.5f), std::sqrt(0.25f / 3.14159265f)), "a projector's radius is the disc of equal area");
        check(projectorEmitterRadius(0.01f, 0.01f) == EMITTER_MIN, "a small face is clamped up");
        check(projectorEmitterRadius(20.f, 20.f) == EMITTER_MAX, "a large face is clamped down");
        check(projectorEmitterRadius(-1.f, 2.f) == EMITTER_MIN, "a negative scale is safe");
        check(near_to(pointEmitterRadius(0.2f, 0.6f, 0.4f), 0.2f), "a point light takes half its median extent");
        check(pointEmitterRadius(0.02f, 0.02f, 0.02f) == EMITTER_MIN, "a small bulb is clamped up");
        check(pointEmitterRadius(5.f, 5.f, 5.f) == EMITTER_MAX, "a big one is clamped down");
        check(pointEmitterRadius(0.2f, 0.4f, 0.6f) == pointEmitterRadius(0.6f, 0.2f, 0.4f), "the order of the scale does not matter");
    }

    void test_exclusion()
    {
        using namespace LLLocalLight;
        check(exclusionRadius(0.0f, 0.075f) == 0.075f, "never under the emitter radius");
        check(exclusionRadius(0.4f, 0.075f) == 0.4f, "the drawable's radius");
        check(exclusionRadius(7.0f, 0.075f) == EXCLUSION_MAX, "never beyond 1 m");
        check(exclusionRadius(1.0f, 0.5f) == 1.0f, "exactly 1 m");
        const float fov = 1.0471976f; // 60 degrees
        const float apex = projectorApexToFace(0.5f, fov);
        check(near_to(apex, 0.25f / std::tan(0.5f * fov)), "the apex lies 0.25 / tan(fov / 2) behind a 0.5 m face");
        check(near_to(apex, 0.4330127f, 1.0e-4f), "0.433 m for 60 degrees");
        check(projectorApexToFace(0.5f, 2.0f) < projectorApexToFace(0.5f, 1.0f), "a wider field puts the apex nearer the face");
        check(projectorApexToFace(0.0f, fov) == 0.0f, "a zero face has no stretch");
        check(std::isfinite(projectorApexToFace(1.f, 0.0f)) && std::isfinite(projectorApexToFace(1.f, 7.0f)), "fov limits keep it finite");
        check(near_to(projectorExclusion(0.3f, 0.075f, 0.5f, fov), apex + 0.3f), "a projector's zone is the stretch and the drawable's radius");
        check(projectorExclusion(9.0f, 0.075f, 0.5f, fov) < apex + EXCLUSION_MAX + 1.0e-5f, "the drawable part is capped at 1 m");
        check(projectorExclusion(0.0f, 0.075f, 0.5f, fov) > apex, "and never under the emitter radius");
    }
}

int main()
{
    test_emitter();
    test_exclusion();
    if (failures)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("lllocallight_test: ok\n");
    return 0;
}
