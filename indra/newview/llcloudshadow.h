// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llcloudshadow.h
 * @brief Cloud shadows on the ground (stage L2 of the light study, doc/light/light-model-study.md
 *        section 3): where a world point looks into the flat cloud layer along the light, and how the
 *        cloud transmittance map (CTM) is laid out in the world. Renderer-free.
 *
 * The flat layer is the EEP cloud pass (deferred/cloudsV.glsl, cloudsF.glsl): the texture
 * coordinates of the sky dome, a cap of a sphere of radius R = DOME_RADIUS whose centre is
 * DOME_OFFSET * R below the camera, with planar coordinates taken from the horizontal position
 * of the dome point (llvowlsky.cpp buildStripsBuffer). So one texture repeat covers 2 R cloud_scale
 * metres horizontally (12.6 km at cloud_scale 0.42), and the layer lies at the dome point's
 * horizontal offset from the camera.
 *
 * Dome parallax (decision 2): the sky adds (camera.x, -camera.y) / (2 R cloud_scale) to its texture
 * coordinates, camera in world metres. The clouds then stay where they are when the camera walks,
 * and the cloud seen in a direction from a point is a function of the point's world position
 * and the direction only. The ground's lookup is the same function with the light's direction:
 *     texcoord(p, L) = layer_texcoord(p + dome_offset(L)),
 * so at p = the camera it is exactly the texture coordinate the sky draws over the sun.
 *
 * The map is a function of world position, the light and the EEP values and the scroll; never of
 * the camera. Its texel grid is anchored in the world and moves in whole steps of extent / 8, so a
 * static scene reads the same texels while the camera moves (the same idea as the stable cascades,
 * llshadowcascadefit.h). indra/newview/tests/llcloudshadow_test.cpp checks all of it.
 *
 * World axes: x east, y north, z up (the viewer's region frame).
 */

#ifndef LL_LLCLOUDSHADOW_H
#define LL_LLCLOUDSHADOW_H

#include <cmath>

namespace LLCloudShadow
{
// LLSettingsSky::DOME_RADIUS and DOME_OFFSET
constexpr double DOME_RADIUS = 15000.0;
constexpr double DOME_OFFSET = 0.96;
// the +50 m the cloud shader adds to the vertex height (cloudsF.glsl on this line) and the 512 m of
// the altitude fade
constexpr double CLOUD_VERTEX_LIFT = 50.0;
constexpr double ALTITUDE_FADE_START = 512.0;

constexpr int MAP_RESOLUTION = 512;
constexpr double DEFAULT_EXTENT = 4096.0; // metres, 8 m texels
constexpr int SNAP_CELLS = 8;             // the origin moves in steps of extent / SNAP_CELLS

struct Vec2
{
    double x = 0.0, y = 0.0;
};

struct Vec3
{
    double x = 0.0, y = 0.0, z = 0.0;
};

// The horizontal offset (metres, east and north) from the camera to the point of the dome cap that
// a ray in the world direction `d` (unit, d.z >= 0) meets. The dome sphere has radius R and its
// centre is DOME_OFFSET * R below the camera.
inline Vec2 dome_offset(const Vec3& d, double R = DOME_RADIUS, double dome_offset_fraction = DOME_OFFSET)
{
    const double h = dome_offset_fraction * R;
    // |(0, 0, h) + t d| = R, with the camera at height h above the centre; the far root
    const double b = h * d.z;
    const double t = -b + std::sqrt(b * b - h * h + R * R);
    return { t * d.x, t * d.y };
}

// The cloud pass's texcoord0 (after the "/ cloud_scale" and before the scroll) of the layer point
// q (world metres, east and north), with the sky's parallax included. This is
//   dome (u, v) = ((1 - z0) / 2, (1 - x0) / 2)     z0 = east / R, x0 = north / R   (llvowlsky.cpp)
//   texcoord0   = ((-u - 0.5) / s + 0.5, (v - 0.5) / s + 0.5)                      (cloudsV.glsl)
// plus the camera's parallax, which turns the dome's offset from the camera into the point's own
// position.
inline Vec2 layer_texcoord(const Vec2& q, double cloud_scale, double R = DOME_RADIUS)
{
    const double k = 1.0 / (2.0 * R * cloud_scale);
    return { 0.5 - 1.0 / cloud_scale + q.x * k, 0.5 - q.y * k };
}

// The texcoord0 of the sky pass for a view direction `d` from the camera at `camera` (world metres):
// the dome's own coordinate plus the parallax term the cloud shader adds.
inline Vec2 sky_texcoord(const Vec3& d, const Vec2& camera, double cloud_scale, double R = DOME_RADIUS)
{
    const Vec2 o = dome_offset(d, R);
    const double k = 1.0 / (2.0 * R * cloud_scale);
    // the dome's coordinate (see layer_texcoord), without the camera's position
    const Vec2 dome = { 0.5 - 1.0 / cloud_scale + o.x * k, 0.5 - o.y * k };
    return { dome.x + camera.x * k, dome.y - camera.y * k };
}

// The parallax term itself, as the cloud shader's `cloud_parallax` uniform.
inline Vec2 parallax(const Vec2& camera, double cloud_scale, double R = DOME_RADIUS)
{
    const double k = 1.0 / (2.0 * R * cloud_scale);
    return { camera.x * k, -camera.y * k };
}

// The texcoord0 of the ground lookup at world point p (east, north) for the light direction L.
inline Vec2 ground_texcoord(const Vec2& p, const Vec3& L, double cloud_scale, double R = DOME_RADIUS)
{
    const Vec2 o = dome_offset(L, R);
    return layer_texcoord({ p.x + o.x, p.y + o.y }, cloud_scale, R);
}

// cloudsF.glsl: altitude_blend_factor for the dome point in direction L. It does not depend on the
// camera: the dome moves with it.
inline double altitude_blend(const Vec3& L, double max_y, double R = DOME_RADIUS, double dome_offset_fraction = DOME_OFFSET)
{
    const double h = dome_offset_fraction * R;
    const Vec2 o = dome_offset(L, R, dome_offset_fraction);
    const double height = std::sqrt(R * R - o.x * o.x - o.y * o.y); // the dome point's height above the centre
    const double rel_y = height - h + CLOUD_VERTEX_LIFT;
    const double f = (rel_y + ALTITUDE_FADE_START) / max_y;
    return f < 0.0 ? 0.0 : (f > 1.0 ? 1.0 : f);
}

// The map's layout in the world. `extent` metres square, MAP_RESOLUTION texels; the origin (the
// south-west corner) is a multiple of extent / SNAP_CELLS in world coordinates, chosen so that the
// camera is in the middle cell block.
struct Layout
{
    double extent = DEFAULT_EXTENT;
    Vec2 origin;

    double texel() const { return extent / MAP_RESOLUTION; }
    double snap() const { return extent / SNAP_CELLS; }
};

inline Layout layout_for_camera(const Vec2& camera, double extent = DEFAULT_EXTENT)
{
    Layout l;
    l.extent = extent;
    const double s = l.snap();
    l.origin = { std::floor((camera.x - extent * 0.5) / s + 0.5) * s, std::floor((camera.y - extent * 0.5) / s + 0.5) * s };
    return l;
}

// The world position of the centre of texel (i, j).
inline Vec2 texel_center(const Layout& l, int i, int j)
{
    return { l.origin.x + (i + 0.5) * l.texel(), l.origin.y + (j + 0.5) * l.texel() };
}

// The map coordinate (0..1) of a world point.
inline Vec2 map_uv(const Layout& l, const Vec2& p)
{
    return { (p.x - l.origin.x) / l.extent, (p.y - l.origin.y) / l.extent };
}
} // namespace LLCloudShadow

#endif // LL_LLCLOUDSHADOW_H
