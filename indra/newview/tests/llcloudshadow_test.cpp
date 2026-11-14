// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llcloudshadow.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llcloudshadow_test.cpp
// Stage L2 of the light study, section 3.5. The sky's texture coordinates are rebuilt here from the
// dome's own geometry and the viewer's matrices (llvowlsky.cpp, lldrawpoolwlsky.cpp renderDome,
// cloudsV.glsl), independently of the header's closed forms, and the ground lookup must agree:
//   - at the camera, in the light's direction, the ground lookup is the sky's texture coordinate
//     (exact coupling), for any camera position, light direction and cloud_scale;
//   - the ground lookup depends on the world point and the light only: not on the camera;
//   - the scroll moves the sky and the ground the same way, at 2 R cloud_scale metres per repeat;
//   - the map's texel grid is anchored in the world: moving the camera and re-centring the map keeps
//     every texel centre at the same world position, and the world point of a map coordinate
//     does not change;
//   - the camera is always well inside the map;
//   - the altitude fade does not depend on the camera.

#include "llcloudshadow.h"

#include <cmath>
#include <cstdio>
#include <random>

namespace
{
int failures = 0;

void check(bool cond, const char* what, double a = 0.0, double b = 0.0)
{
    if (!cond)
    {
        std::printf("FAIL %s (%.9g, %.9g)\n", what, a, b);
        ++failures;
    }
}

using namespace LLCloudShadow;

struct D3
{
    double x, y, z;
};

// The sky pass, from the dome's geometry: the vertex on the dome sphere in the ray's direction,
// the viewer's dome frame (y up; x north, z east: lldrawpoolwlsky.cpp renderDome rotates the
// dome's (x, y, z) onto the world's (y, z, x)), llvowlsky.cpp's planar coordinates and
// cloudsV.glsl's flip and scale. Returns texcoord0 including the parallax term.
Vec2 sky_pass_texcoord(const Vec3& dir_world, const Vec2& camera_world, double cloud_scale)
{
    const double R = DOME_RADIUS;
    const D3 cam = { 0.0, DOME_OFFSET * R, 0.0 };             // camPosLocal, the dome frame
    const D3 d = { dir_world.y, dir_world.z, dir_world.x };   // dome x = north, y = up, z = east
    // solve |cam + t d| = R for the far root
    const double b = cam.x * d.x + cam.y * d.y + cam.z * d.z;
    const double c = cam.x * cam.x + cam.y * cam.y + cam.z * cam.z - R * R;
    const double t = -b + std::sqrt(b * b - c);
    const D3 v = { cam.x + t * d.x, cam.y + t * d.y, cam.z + t * d.z };
    const double x0 = v.x / R, z0 = v.z / R; // llvowlsky.cpp: x0 = sin(phi) cos(theta), z0 = sin(phi) sin(theta)
    double u = (-z0 + 1.0) / 2.0, w = (-x0 + 1.0) / 2.0;
    // cloudsV.glsl: vary_texcoord0 = (-texcoord0.x, texcoord0.y); -= 0.5; /= cloud_scale; += 0.5
    double tx = -u, ty = w;
    tx = (tx - 0.5) / cloud_scale + 0.5;
    ty = (ty - 0.5) / cloud_scale + 0.5;
    // the parallax: the camera's world position over 2 R cloud_scale, x as it is, y flipped
    tx += camera_world.x / (2.0 * R * cloud_scale);
    ty += -camera_world.y / (2.0 * R * cloud_scale);
    return { tx, ty };
}

Vec3 random_direction(std::mt19937_64& rng, double min_z)
{
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (;;)
    {
        const double x = u(rng), y = u(rng), z = u(rng);
        const double l = std::sqrt(x * x + y * y + z * z);
        if (l > 0.1 && z / l >= min_z)
        {
            return { x / l, y / l, z / l };
        }
    }
}

bool near(double a, double b, double tol = 1e-9)
{
    return std::fabs(a - b) <= tol * (1.0 + std::fabs(a) + std::fabs(b));
}
} // namespace

int main()
{
    std::mt19937_64 rng(2026110801);
    std::uniform_real_distribution<double> pos(-300000.0, 300000.0), scale(0.05, 2.0);

    // 1. the closed form of the sky's coordinate equals the dome's geometry, and the ground lookup at
    //    the camera in the light's direction equals the sky's
    for (int i = 0; i < 4000; ++i)
    {
        const Vec3 L = random_direction(rng, i % 4 == 0 ? 0.0 : 0.05);
        const Vec2 cam = { pos(rng), pos(rng) };
        const double s = scale(rng);
        const Vec2 sky = sky_pass_texcoord(L, cam, s);
        const Vec2 sky_closed = sky_texcoord(L, cam, s);
        const Vec2 ground = ground_texcoord(cam, L, s);
        check(near(sky.x, sky_closed.x) && near(sky.y, sky_closed.y), "sky closed form equals the dome", sky.x - sky_closed.x, sky.y - sky_closed.y);
        check(near(sky.x, ground.x) && near(sky.y, ground.y), "ground lookup at the camera equals the sky", sky.x - ground.x, sky.y - ground.y);
    }

    // 2. the ground lookup does not depend on the camera; the sky's coordinate shifts with it by the
    //    parallax and nothing else (so the cloud over a world point stays over it)
    for (int i = 0; i < 1000; ++i)
    {
        const Vec3 L = random_direction(rng, 0.05);
        const double s = scale(rng);
        const Vec2 p = { pos(rng), pos(rng) };
        const Vec2 g0 = ground_texcoord(p, L, s);
        const Vec2 c1 = { pos(rng), pos(rng) }, c2 = { pos(rng), pos(rng) };
        const Vec2 sky1 = sky_pass_texcoord(L, c1, s), sky2 = sky_pass_texcoord(L, c2, s);
        const Vec2 par = parallax({ c2.x - c1.x, c2.y - c1.y }, s);
        check(near(sky2.x - sky1.x, par.x) && near(sky2.y - sky1.y, par.y), "moving the camera shifts the sky by the parallax", sky2.x - sky1.x, par.x);
        // the same world point seen from the camera at p: a camera standing at p reads g0 over the light
        const Vec2 sky_at_p = sky_pass_texcoord(L, p, s);
        check(near(sky_at_p.x, g0.x) && near(sky_at_p.y, g0.y), "a camera at p sees the lookup", sky_at_p.x - g0.x, sky_at_p.y - g0.y);
    }

    // 3. a cloud fixed in the layer is under the same texture coordinate however the camera sees it:
    //    the sky's coordinate of the direction from c to the layer point q, with the parallax of c,
    //    equals layer_texcoord(q)
    for (int i = 0; i < 2000; ++i)
    {
        const Vec3 d = random_direction(rng, 0.05);
        const Vec2 cam = { pos(rng), pos(rng) };
        const double s = scale(rng);
        const Vec2 o = dome_offset(d);
        const Vec2 q = { cam.x + o.x, cam.y + o.y };
        const Vec2 lay = layer_texcoord(q, s), sky = sky_pass_texcoord(d, cam, s);
        check(near(lay.x, sky.x) && near(lay.y, sky.y), "layer_texcoord is the sky's coordinate at the dome point", lay.x - sky.x, lay.y - sky.y);
    }

    // 4. scroll: cloud_pos_density1 adds (-scroll.x, +scroll.y) to the coordinate (llsettingsvo.cpp
    //    flips x). The pattern is read at texcoord + (-sx, +sy), so a scroll step (dsx, dsy) reads the
    //    coordinate of the point moved by (-2 R cloud_scale dsx, -2 R cloud_scale dsy): the clouds
    //    drift east for a positive sx and south for a positive sy, 2 R cloud_scale metres per repeat,
    //    the same for the sky's clouds and for the ground
    for (int i = 0; i < 500; ++i)
    {
        const Vec3 L = random_direction(rng, 0.05);
        const double s = scale(rng);
        const Vec2 p = { pos(rng), pos(rng) };
        const double dsx = std::uniform_real_distribution<double>(-0.05, 0.05)(rng), dsy = std::uniform_real_distribution<double>(-0.05, 0.05)(rng);
        const double step = 2.0 * DOME_RADIUS * s;
        const Vec2 g = ground_texcoord(p, L, s);
        const Vec2 moved = ground_texcoord({ p.x - step * dsx, p.y - step * dsy }, L, s);
        // reading at g + (-dsx, dsy) is reading at `moved`
        check(near(g.x - dsx, moved.x) && near(g.y + dsy, moved.y), "a scroll step moves the ground's pattern by 2 R cloud_scale", g.x - dsx - moved.x, g.y + dsy - moved.y);
        // the sky reads the same coordinate for a camera at p
        const Vec2 sky = sky_pass_texcoord(L, { p.x - step * dsx, p.y - step * dsy }, s);
        check(near(g.x - dsx, sky.x) && near(g.y + dsy, sky.y), "the sky's clouds move the same way", g.x - dsx - sky.x, g.y + dsy - sky.y);
    }
    // the speed the default scroll gives, as a plausible cloud speed: (0.2, 0.01) per second / 100
    // is rounded up in the study to ~25 m/s; here 2 R cloud_scale * rate
    {
        const double v = 2.0 * DOME_RADIUS * 0.42 * (0.2 / 100.0);
        check(v > 20.0 && v < 30.0, "default scroll gives a plausible shadow speed in m/s", v);
    }

    // 5. the map is anchored in the world
    for (int i = 0; i < 2000; ++i)
    {
        const double extent = i % 3 == 0 ? 2048.0 : (i % 3 == 1 ? 4096.0 : 8192.0);
        const Vec2 c1 = { pos(rng), pos(rng) };
        const Vec2 c2 = { c1.x + std::uniform_real_distribution<double>(-2000.0, 2000.0)(rng), c1.y + std::uniform_real_distribution<double>(-2000.0, 2000.0)(rng) };
        const Layout l1 = layout_for_camera(c1, extent), l2 = layout_for_camera(c2, extent);
        // origins are multiples of the step, so both grids lie on one lattice
        const double step = l1.snap();
        check(near(std::fmod(std::fabs(l1.origin.x), step) / step + 0.0, 0.0, 1e-6) || near(std::fmod(std::fabs(l1.origin.x), step) / step, 1.0, 1e-6), "origin is on the step lattice (x)");
        check(near(std::fmod(std::fabs(l2.origin.y), step) / step + 0.0, 0.0, 1e-6) || near(std::fmod(std::fabs(l2.origin.y), step) / step, 1.0, 1e-6), "origin is on the step lattice (y)");
        // a texel of the first map and the texel of the second map at the same world point are the
        // same texel: centres coincide
        const int steps_x = static_cast<int>(std::lround((l2.origin.x - l1.origin.x) / l1.texel()));
        const int steps_y = static_cast<int>(std::lround((l2.origin.y - l1.origin.y) / l1.texel()));
        check(near(l1.origin.x + steps_x * l1.texel(), l2.origin.x, 1e-9) && near(l1.origin.y + steps_y * l1.texel(), l2.origin.y, 1e-9), "texel lattices coincide after re-centring");
        const int ti = 100 + i % 200, tj = 50 + i % 300;
        const Vec2 w1 = texel_center(l1, ti, tj);
        const Vec2 w2 = texel_center(l2, ti - steps_x, tj - steps_y);
        check(near(w1.x, w2.x, 1e-9) && near(w1.y, w2.y, 1e-9), "the same world point has the same texel centre in both maps");
        // map_uv inverts texel_center
        const Vec2 uv = map_uv(l1, w1);
        check(near(uv.x, (ti + 0.5) / MAP_RESOLUTION, 1e-9) && near(uv.y, (tj + 0.5) / MAP_RESOLUTION, 1e-9), "map_uv of a texel centre");
        // the ground lookup of that world point does not depend on which map it is read from
        const Vec3 L = random_direction(rng, 0.05);
        const Vec2 g1 = ground_texcoord(w1, L, 0.42), g2 = ground_texcoord(w2, L, 0.42);
        check(near(g1.x, g2.x) && near(g1.y, g2.y), "the lookup of a world point is the same from either map");
        // the camera is in the middle of the map: at least 3/8 of the extent from every edge
        const Vec2 cuv = map_uv(l1, c1);
        check(cuv.x >= 0.37 && cuv.x <= 0.63 && cuv.y >= 0.37 && cuv.y <= 0.63, "the camera is near the middle of the map", cuv.x, cuv.y);
    }

    // 6. snapping: the origin only changes when the camera has moved a step
    {
        const Layout a = layout_for_camera({ 1000.0, 2000.0 });
        const Layout b = layout_for_camera({ 1000.0 + 100.0, 2000.0 - 100.0 });
        check(a.origin.x == b.origin.x && a.origin.y == b.origin.y, "a small move keeps the origin");
        const Layout c = layout_for_camera({ 1000.0 + 600.0, 2000.0 });
        check(c.origin.x != a.origin.x && c.origin.y == a.origin.y, "a move of more than a step moves the origin, in x only");
    }

    // 7. the altitude fade (cloudsF.glsl altitude_blend_factor) is the dome point's height, not the
    //    camera's: rebuilt from the dome point
    for (int i = 0; i < 500; ++i)
    {
        const Vec3 L = random_direction(rng, 0.0);
        const double max_y = 1605.0 + 500.0 * (i % 3);
        const double R = DOME_RADIUS, h = DOME_OFFSET * R;
        const double b = h * L.z;
        const double t = -b + std::sqrt(b * b - h * h + R * R);
        const double rel_y = (h + t * L.z) - h + 50.0; // vertex.y - camPosLocal.y + 50
        double f = (rel_y + 512.0) / max_y;
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        check(near(altitude_blend(L, max_y), f), "altitude fade", altitude_blend(L, max_y), f);
    }
    {
        // overhead the fade is the dome's 600 m cap over the camera plus the lifts
        check(near(altitude_blend({ 0.0, 0.0, 1.0 }, 1605.0), (600.0 + 50.0 + 512.0) / 1605.0, 1e-9), "altitude fade overhead");
    }

    std::printf(failures ? "llcloudshadow_test: %d FAILED\n" : "llcloudshadow_test: OK\n", failures);
    return failures ? 1 : 0;
}
