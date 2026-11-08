// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llatmosphericlight.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llatmosphericlight_test.cpp
// The vectors in llatmosphericlight_vectors.inc come from atmosphericsFuncs.glsl run on a GPU
// (scripts/light/gen_atmosphere_vectors.py); the header must reproduce them to 1e-4 relative
// (the GLSL is float). Also checked:
//   - the sun's light falls with the light's elevation (the Beer-Lambert airmass) and is the moon's
//     when the sun is not up;
//   - clouds do not change `sunlit` (today's behaviour, stage L2 changes it) but lift the ambient;
//   - the ambient keeps some light opposite the sun (ambientLighting) and is grey outside classic mode.

#include "llatmosphericlight.h"

#include <cmath>
#include <cstdio>

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

bool near(double a, double b, double rel = 1e-4, double abs_tol = 1e-6)
{
    return std::fabs(a - b) <= rel * std::fmax(std::fabs(a), std::fabs(b)) + abs_tol;
}

const double kVectors[][28] = {
#include "llatmosphericlight_vectors.inc"
};

LLAtmosphericLight::Inputs inputs_of(const double* v)
{
    LLAtmosphericLight::Inputs in;
    in.sunlight_color = { v[0], v[1], v[2] };
    in.moonlight_color = { v[3], v[4], v[5] };
    in.sun_up_factor = static_cast<int>(v[6]);
    in.ambient_color = { v[7], v[8], v[9] };
    in.blue_density = { v[10], v[11], v[12] };
    in.haze_density = v[13];
    in.cloud_shadow = v[14];
    in.density_multiplier = v[15];
    in.max_y = v[16];
    in.light_y = v[17];
    in.sky_sunlight_scale = v[18];
    in.sky_ambient_scale = v[19];
    in.classic_mode = static_cast<int>(v[20]);
    return in;
}

LLAtmosphericLight::Inputs reference()
{
    LLAtmosphericLight::Inputs in;
    in.sunlight_color = { 1.0, 0.9, 0.8 };
    in.moonlight_color = { 0.2, 0.2, 0.3 };
    in.ambient_color = { 0.3, 0.3, 0.4 };
    in.blue_density = { 0.24, 0.45, 0.76 };
    in.haze_density = 0.7;
    in.density_multiplier = 0.00018;
    in.max_y = 1605.0;
    in.light_y = 0.8;
    return in;
}
} // namespace

int main()
{
    using namespace LLAtmosphericLight;

    const int count = static_cast<int>(sizeof(kVectors) / sizeof(kVectors[0]));
    check(count >= 64, "enough GLSL vectors", count);
    for (int i = 0; i < count; ++i)
    {
        const double* v = kVectors[i];
        const Result r = calc_sunlit_amblit(inputs_of(v), v[21]);
        const double got[6] = { r.sunlit.x, r.sunlit.y, r.sunlit.z, r.amblit.x, r.amblit.y, r.amblit.z };
        for (int k = 0; k < 6; ++k)
        {
            if (!near(got[k], v[22 + k]))
            {
                std::printf("vector %d value %d: C++ %.9g GLSL %.9g\n", i, k, got[k], v[22 + k]);
                check(false, "C++ equals the GLSL");
            }
        }
    }

    // elevation: a lower sun is dimmer, and redder (blue is extinguished more)
    {
        Inputs in = reference();
        double last = 1e9, last_ratio = 1e9;
        for (double y : { 1.0, 0.7, 0.4, 0.2, 0.1, 0.05 })
        {
            in.light_y = y;
            const Result r = calc_sunlit_amblit(in, 1.0);
            check(r.sunlit.x < last, "sunlit falls with elevation", r.sunlit.x, last);
            check(r.sunlit.z / r.sunlit.x < last_ratio, "sunlit reddens with elevation");
            last = r.sunlit.x;
            last_ratio = r.sunlit.z / r.sunlit.x;
        }
    }

    // the moon's light replaces the sun's when the sun is not up
    {
        Inputs in = reference();
        in.sun_up_factor = 0;
        in.sky_sunlight_scale = 1.0;
        in.classic_mode = 1;
        const Result r = calc_sunlit_amblit(in, 1.0);
        check(r.sunlit.x < 0.2 && r.sunlit.z > r.sunlit.x, "moonlight colour", r.sunlit.x, r.sunlit.z);
    }

    // clouds: sunlit unchanged (L2 changes this), ambient lifted
    {
        Inputs clear = reference(), cloudy = reference();
        cloudy.cloud_shadow = 0.8;
        const Result a = calc_sunlit_amblit(clear, 1.0), b = calc_sunlit_amblit(cloudy, 1.0);
        check(a.sunlit.x == b.sunlit.x && a.sunlit.y == b.sunlit.y && a.sunlit.z == b.sunlit.z, "clouds leave sunlit");
        check(b.amblit.x > a.amblit.x, "clouds lift the ambient", a.amblit.x, b.amblit.x);
    }

    // ambientLighting: facing the sun or away it is 1 at n.l = 0 and 0.75 at |n.l| = 1; outside
    // classic mode the ambient is grey
    {
        check(near(ambient_lighting(0.0), 1.0), "ambientLighting 0");
        check(near(ambient_lighting(1.0), 0.75), "ambientLighting +1", ambient_lighting(1.0));
        check(near(ambient_lighting(-1.0), 0.75), "ambientLighting -1");
        Inputs in = reference();
        in.classic_mode = 0;
        const Result r = calc_sunlit_amblit(in, 0.3);
        check(r.amblit.x == r.amblit.y && r.amblit.y == r.amblit.z, "grey ambient outside classic mode");
        in.classic_mode = 1;
        const Result c = calc_sunlit_amblit(in, 0.3);
        check(c.amblit.z > c.amblit.x, "classic ambient keeps its colour");
    }

    std::printf(failures ? "llatmosphericlight_test: %d FAILED\n" : "llatmosphericlight_test: OK (%d vectors)\n", failures ? failures : count);
    return failures ? 1 : 0;
}
