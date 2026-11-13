// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llshadowcascadefit.h
 * @brief Stable sun shadow cascades: split distances, slice bounding spheres, world-anchored
 *        texel snapping and the light view's world-locked up axis.
 *
 * Renderer-free (no GL, no viewer types): LLPipeline::generateSunShadow uses it with OpenGL,
 * the future Vulkan renderer can use it unchanged, and indra/newview/tests/
 * llshadowcascadefit_test.cpp checks its invariants (stage S1).
 *
 * Why: the cascades used to be refitted every frame to the occlusion-free visible extents, with
 * the light view anchored at the camera and turned with it, and a perspective warp chosen per
 * frame. The splits, the texel size and the texel grid's phase and rotation all followed the
 * camera, so shadows swam, popped and vanished as the camera moved or turned. Here a cascade
 * depends only on the shadow distance, the camera's field of view and position, and the light:
 *   - split distances from the shadow distance (RenderShadowDistance) and the split exponent;
 *   - each cascade covers the bounding sphere of its camera-frustum slice, which does not change
 *     when the camera turns, with its radius quantised so that it only changes in steps;
 *   - the light view's up axis is fixed to the world (north), so the grid does not turn;
 *   - the orthographic box is moved in whole texels of a grid anchored at the world origin, so a
 *     static scene samples the same texels every frame while the camera moves.
 */

#ifndef LL_LLSHADOWCASCADEFIT_H
#define LL_LLSHADOWCASCADEFIT_H

#include <array>
#include <cmath>

namespace LLShadowCascadeFit
{
    struct Vec3
    {
        float x, y, z;
    };

    // Split distances d[0..4] (view depth, metres): d[0] = near, d[4] = far, between them
    // near + (far - near) * (i / 4)^exponent, the first split bumped by 1.25 as before (the
    // transition padding of sampleDirectionalShadow's blend). Depends on nothing else.
    inline std::array<float, 5> splitDistances(float near_dist, float far_dist, float exponent)
    {
        std::array<float, 5> d{};
        const float range = far_dist - near_dist;
        const float e = exponent > 0.1f ? exponent : 0.1f;
        d[0] = near_dist;
        for (int i = 1; i <= 4; ++i)
        {
            d[i] = near_dist + range * std::pow((float)i / 4.f, e);
        }
        d[1] *= 1.25f;
        if (d[1] > d[2])
        {
            d[1] = d[2];
        }
        return d;
    }

    struct Sphere
    {
        float distance; // of the centre along the camera's view axis, metres
        float radius;   // metres
    };

    // The smallest sphere centred on the view axis that holds a frustum slice from view depth n
    // to f, where k = tan(angle between the view axis and the frustum's corner rays). Corners
    // lie at (radial k*d, axial d); the centre is where the near and far corners are equally far,
    // or the far plane's centre when that point lies beyond it.
    inline Sphere sliceSphere(float k, float n, float f)
    {
        const float kk = 1.f + k * k;
        float c = 0.5f * (n + f) * kk;
        if (c > f)
        {
            c = f;
        }
        const float df = f - c;
        const float r = std::sqrt(df * df + f * f * k * k);
        return { c, r };
    }

    // Radius rounded up to the next 1/16 of its power-of-two octave: the size changes only in
    // steps of at most 6.25 %, and not at all while the fov and the splits are unchanged.
    inline float quantizeRadius(float r)
    {
        if (!(r > 0.f))
        {
            return r;
        }
        const float octave = std::exp2(std::floor(std::log2(r)));
        const float step = octave / 16.f;
        return std::ceil(r / step) * step;
    }

    struct Rect
    {
        float min_x, min_y, max_x, max_y;
    };

    // The orthographic box of a cascade in light-view space: it holds the square [c - r, c + r]
    // and spans exactly res texels of size t = 2r / (res - 1) per axis, its corner moved to a
    // whole number of texels from the anchor (the light-view position of the world origin), so
    // the texel grid stays fixed in the world while the camera moves.
    inline Rect snappedSquare(float cx, float cy, float r, float res_x, float res_y, float anchor_x, float anchor_y)
    {
        const float tx = 2.f * r / (res_x - 1.f);
        const float ty = 2.f * r / (res_y - 1.f);
        Rect out;
        out.min_x = anchor_x + std::floor((cx - r - anchor_x) / tx) * tx;
        out.min_y = anchor_y + std::floor((cy - r - anchor_y) / ty) * ty;
        out.max_x = out.min_x + res_x * tx;
        out.max_y = out.min_y + res_y * ty;
        return out;
    }

    struct DepthRange
    {
        float z_min; // farthest from the light (light-view z, larger towards the light)
        float z_max; // nearest to the light
    };

    // Light-view depth range of a cascade: the sphere, plus headroom towards the light so that
    // casters up to one radius above it keep their own depth. Casters nearer the light than that
    // are rasterised at the near plane by depth clamping (renderShadow), so they still occlude.
    inline DepthRange depthRange(float cz, float r, float headroom = 1.f)
    {
        return { cz - r, cz + r + headroom * r };
    }

    // Stage S2, shared by every shadow method that calls the method-neutral block of shadowUtil.glsl (SHADOW_COMMON). The shader
    // manager passes these to the shaders as defines (SHADOW_FOOTPRINT_M and the others, same names),
    // so the constants live only here.
    //   - SHADOW_FOOTPRINT_M: how far a modern method's filter may reach from the receiver, metres. It
    //     is also the caster cull guard (casterCullMargin): a caster that can reach a pixel's filter is
    //     always drawn into the cascade. One radius for both, so they cannot drift apart.
    //   - SHADOW_BIAS_TEXELS: the constant depth bias, in texels of the cascade at the pixel, which the
    //     shader converts with the map's depth scale: the same length in metres in every cascade.
    //   - SHADOW_NORMAL_OFFSET_TEXELS: the sample position moves along the surface's geometric normal by
    //     this many texels times sin(angle to the light).
    //   - SHADOW_PLANE_BIAS_MAX_TEXELS: the receiver-plane bias of one tap never exceeds this many
    //     texels (a plane edge-on to the light has no usable slope).
    //   - SHADOW_GRAD_MAX: the largest depth gradient (depth per uv) of a receiver plane; a plane edge-on to
    //     the light is clamped to it.
    constexpr float SHADOW_FOOTPRINT_M = 0.25f;
    constexpr float SHADOW_BIAS_TEXELS = 1.0f;
    constexpr float SHADOW_NORMAL_OFFSET_TEXELS = 1.0f;
    constexpr float SHADOW_PLANE_BIAS_MAX_TEXELS = 8.0f;
    constexpr float SHADOW_GRAD_MAX = 10000.0f;

    // Width of the border, metres, around a cascade's slice inside which casters are still drawn into
    // the cascade: the larger of 16 texels (the filter kernels of the wide cascades) and the footprint
    // plus a texel (the bilinear tap at its edge), plus the normal offset of the sample position.
    inline float casterCullMargin(float texel_m, float normal_offset_m)
    {
        const float kernel = 16.f * texel_m;
        const float footprint = SHADOW_FOOTPRINT_M + texel_m;
        return (kernel > footprint ? kernel : footprint) + normal_offset_m;
    }

    // The shadow distance takes only the steps 32, 64, ... 256 m: any other value (an older
    // setting, a preset, the debug settings) is rounded to the nearest step and clamped.
    constexpr float SHADOW_DISTANCE_STEP = 32.f;
    constexpr float SHADOW_DISTANCE_MAX = 256.f;
    inline float snapShadowDistance(float metres)
    {
        if (!(metres > SHADOW_DISTANCE_STEP)) // also NaN
        {
            return SHADOW_DISTANCE_STEP;
        }
        const float steps = std::floor(metres / SHADOW_DISTANCE_STEP + 0.5f);
        const float snapped = steps * SHADOW_DISTANCE_STEP;
        return snapped < SHADOW_DISTANCE_MAX ? snapped : SHADOW_DISTANCE_MAX;
    }

    // Up axis of the light view: world north (+y) projected onto the light's plane, or east
    // (+x) when the light is within about 2.6 degrees of the north-south axis. Some switch is
    // unavoidable (no tangent field on the sphere is continuous everywhere), so it sits next
    // to the singularity, where a light almost never is: a light crossing the switch turns the
    // texel grid once, and anywhere else the up follows the light continuously. The result is
    // orthogonal to the light direction.
    constexpr float LIGHT_UP_SWITCH = 0.999f; // |light.y| beyond this uses east
    inline Vec3 lightUp(const Vec3& light_dir)
    {
        const float len = std::sqrt(light_dir.x * light_dir.x + light_dir.y * light_dir.y + light_dir.z * light_dir.z);
        const Vec3 d = { light_dir.x / len, light_dir.y / len, light_dir.z / len };
        Vec3 ref = { 0.f, 1.f, 0.f };
        if (std::fabs(d.y) > LIGHT_UP_SWITCH)
        {
            ref = { 1.f, 0.f, 0.f };
        }
        const float dp = ref.x * d.x + ref.y * d.y + ref.z * d.z;
        Vec3 up = { ref.x - d.x * dp, ref.y - d.y * dp, ref.z - d.z * dp };
        const float ul = std::sqrt(up.x * up.x + up.y * up.y + up.z * up.z);
        return { up.x / ul, up.y / ul, up.z / ul };
    }

    // The two rows of the world-anchored seeds of the forward programs (shadowStochasticUtil.glsl, stNoise): for a view-space
    // point p, u = u[0..2] . p + u[3] and v = v[0..2] . p + v[3] are the point's coordinates in metres in the plane across the
    // light's direction, in the world. For the column-major modelview mv = [R | t] (p = R w + t): w = R^T p + C with C = -R^T t,
    // so u = t1 . w = (R t1) . p + t1 . C, with t1 and t2 the unit vectors across the light (the pair of the shaders' sun rays,
    // so that it is a function of the light alone). All zeros (the shader keeps the pixel seeds) for no light.
    struct SeedPlane
    {
        float u[4];
        float v[4];
    };
    inline SeedPlane worldSeedPlane(const float mv[16], const Vec3& light_world)
    {
        SeedPlane out = { { 0.f, 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f, 0.f } };
        const float len = std::sqrt(light_world.x * light_world.x + light_world.y * light_world.y + light_world.z * light_world.z);
        if (!(len > 1.0e-4f))
        {
            return out;
        }
        const float l[3] = { light_world.x / len, light_world.y / len, light_world.z / len };
        const bool use_x = std::fabs(l[0]) < 0.9f;
        const float ref[3] = { use_x ? 1.f : 0.f, use_x ? 0.f : 1.f, 0.f };
        float t1[3] = { l[1] * ref[2] - l[2] * ref[1], l[2] * ref[0] - l[0] * ref[2], l[0] * ref[1] - l[1] * ref[0] };
        const float n1 = std::sqrt(t1[0] * t1[0] + t1[1] * t1[1] + t1[2] * t1[2]);
        for (float& c : t1)
        {
            c /= n1;
        }
        const float t2[3] = { l[1] * t1[2] - l[2] * t1[1], l[2] * t1[0] - l[0] * t1[2], l[0] * t1[1] - l[1] * t1[0] };
        // C = -R^T t, with R(r, c) = mv[4 c + r]
        float cam[3];
        for (int j = 0; j < 3; ++j)
        {
            cam[j] = -(mv[4 * j] * mv[12] + mv[4 * j + 1] * mv[13] + mv[4 * j + 2] * mv[14]);
        }
        const float* ts[2] = { t1, t2 };
        float* rows[2] = { out.u, out.v };
        for (int k = 0; k < 2; ++k)
        {
            for (int i = 0; i < 3; ++i)
            {
                rows[k][i] = mv[i] * ts[k][0] + mv[4 + i] * ts[k][1] + mv[8 + i] * ts[k][2]; // R t_k
            }
            rows[k][3] = ts[k][0] * cam[0] + ts[k][1] * cam[1] + ts[k][2] * cam[2];
        }
        return out;
    }
}

#endif // LL_LLSHADOWCASCADEFIT_H
