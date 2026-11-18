/**
 * @file llpointshadowpool.h
 * @brief The point lights' shadow faces, GL-free rules: a lamp's six faces, the face a direction falls in, a face's size from its
 *        lamp, its projection and volume, the packed face rects of the lookup, the scheduler's keys and the own avatar's lamps
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#ifndef LL_LLPOINTSHADOWPOOL_H
#define LL_LLPOINTSHADOWPOOL_H

// Header-only, std only and free of GL and viewer types, like llprojectorshadowpool.h (whose valve, buddy atlas, change index,
// pin books, verdict and scheduler the point lights reuse as they are): indra/newview/tests/llpointshadowpool_test.cpp checks the
// rules standalone, and scripts/shadows/point_faces_g1.py checks the shader's twins (stPointPick and the face's uv and depth in
// shadowStochasticUtil.glsl) against the same numbers. The GL half is llpointshadows.h. Doc: doc/shadows/point-light-shadows.md.
//
// Technique: a cube shadow map per lamp (six 90-degree perspective faces around the light's centre), stored as six tiles of an atlas
// with a guard band around each face's interior, so a filter kernel near a face edge reads the neighbour face's content from the same
// tile (the cube-map seam handled by over-wide faces: Gerasimov, GPU Gems 1 ch. 12 "Omnidirectional Shadow Mapping", and the guard
// bands of tiled shadow atlases). The face is picked by the direction's major axis, as a cube map's face selection (OpenGL 4.6 core,
// 8.13 "Cube Map Texture Selection", with fixed tie rules here). No code is taken from any of them.

#include "llprojectorshadowpool.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace llpointpool
{

constexpr uint32_t kFaces = 6;
// texels on each side of a face's interior: the filter's widest tap (kPointLamp.kernel_max) plus the bilinear footprint stays inside the
// face's tile, so every tap of a receiver reads its own face (the neighbour's content past the seam is in the guard)
constexpr uint32_t kGuard = 16;
constexpr uint32_t kFaceMin = 128;  // the valve's floor for a face: an interior of 96 texels behind the guards
constexpr uint32_t kFaceMax = 1024; // the largest face (a lamp's six faces at 1024 take 6 M texels: 38 % of the 4096 atlas)
constexpr float kNearM = 0.02f;     // the faces' near plane (m): casters closer to the light's centre than this do not cast
constexpr float kFarScale = 1.5f;   // the far plane is the light's lit size, radius x 1.5 (the deferred pass lights nothing beyond it)
static_assert(kGuard >= uint32_t(llprojectorpool::kPointLamp.kernel_max) + 2, "a face's guard covers the filter's widest tap plus the bilinear footprint");
static_assert(kFaceMin > 2 * kGuard, "a face has an interior");
static_assert(llprojectorpool::kMinBase >= kFaceMin, "the size from the lamp is never below the faces' floor");

// The six faces in agent axes (+X, -X, +Y, -Y, +Z, -Z): the view of face f looks along `fwd` with `right` and `up` across, a rotation
// (right x up = -fwd, the eye's +z). A point d (agent axes, from the light's centre) has the face coordinates (right . d, up . d, fwd . d);
// those of the faces below are swizzles of d with signs, the same in the shader's stPointPick.
struct FaceAxes
{
    float fwd[3];
    float right[3];
    float up[3];
};
constexpr FaceAxes kFaceAxes[kFaces] = {
    { { 1.f, 0.f, 0.f }, { 0.f, -1.f, 0.f }, { 0.f, 0.f, 1.f } },  // +X
    { { -1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 0.f, 1.f } },  // -X
    { { 0.f, 1.f, 0.f }, { 1.f, 0.f, 0.f }, { 0.f, 0.f, 1.f } },   // +Y
    { { 0.f, -1.f, 0.f }, { -1.f, 0.f, 0.f }, { 0.f, 0.f, 1.f } }, // -Y
    { { 0.f, 0.f, 1.f }, { -1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } },  // +Z
    { { 0.f, 0.f, -1.f }, { 1.f, 0.f, 0.f }, { 0.f, 1.f, 0.f } },  // -Z
};

// The face a direction falls in: its major axis, ties to x, then y (so |x| = |y| >= |z| is an x face), a component of zero (either sign)
// to the positive face; xyw: the face coordinates (right . d, up . d, fwd . d). The shader's stPointPick makes the same comparisons in
// the same order, so the two agree exactly (scripts/shadows/point_faces_g1.py). A NaN direction gives a face whose w is NaN.
inline int pickFace(const float d[3], float xyw[3])
{
    const float ax = std::fabs(d[0]), ay = std::fabs(d[1]), az = std::fabs(d[2]);
    if (ax >= ay && ax >= az)
    {
        if (d[0] >= 0.f)
        {
            xyw[0] = -d[1]; xyw[1] = d[2]; xyw[2] = d[0];
            return 0;
        }
        xyw[0] = d[1]; xyw[1] = d[2]; xyw[2] = -d[0];
        return 1;
    }
    if (ay >= az)
    {
        if (d[1] >= 0.f)
        {
            xyw[0] = d[0]; xyw[1] = d[2]; xyw[2] = d[1];
            return 2;
        }
        xyw[0] = -d[0]; xyw[1] = d[2]; xyw[2] = -d[1];
        return 3;
    }
    if (d[2] >= 0.f)
    {
        xyw[0] = -d[0]; xyw[1] = d[1]; xyw[2] = d[2];
        return 4;
    }
    xyw[0] = d[0]; xyw[1] = d[1]; xyw[2] = -d[2];
    return 5;
}

// The tangent of a face's half angle at size n: the interior (n - 2 kGuard texels) spans the 90-degree face (tangent 1), the guard
// widens it to (n / 2) / (n / 2 - kGuard): 106.3 degrees at 128, 97.2 at 256, 93.6 at 512, 91.8 at 1024. A size at or below the guards
// is taken as the floor.
inline double faceTanHalf(uint32_t n)
{
    const double h = 0.5 * double(n > 2 * kGuard ? n : kFaceMin);
    return h / (h - double(kGuard));
}

// A face's size from its lamp alone (L2's rule: 4 mm texels at the reference depth min(1.5 r, 4 m), the interior spanning the face's two
// tangents, plus the guards), a power of two in [kFaceMin, kFaceMax]. Every face of a lamp has this size; the valve shrinks a lamp's six
// faces together (one band per lamp), so faces of one lamp have one size except for the frames a resize is being drawn.
inline uint32_t faceBaseSize(double radius)
{
    return std::min(llprojectorpool::tileBaseSize(2.0, radius, kGuard), kFaceMax);
}

// The far plane of a lamp's faces: its lit size
inline float faceFar(float radius)
{
    return kFarScale * radius;
}

// The stored depth of a face: the perspective depth of the render's projection (faceProjection) biased to [0, 1], z = c0 - c1 / w with w the
// axial distance from the light's centre: the projectors' form, so the lamps' one filter applies unchanged (stSpotFilterTile).
struct FaceDepth
{
    double c0 = 1.0;
    double c1 = 0.0;
};

inline FaceDepth faceDepth(double near_m, double far_m)
{
    FaceDepth d;
    if (far_m > near_m && near_m > 0.0)
    {
        d.c0 = far_m / (far_m - near_m);
        d.c1 = d.c0 * near_m;
    }
    return d;
}

// The column-major projection of a face (the matrix glm::frustum(-t n, t n, -t n, t n, n, f) gives): x_clip = x / t, y_clip = y / t,
// w_clip = w, depth as faceDepth after the bias. Built from the tangent itself, so the render and the lookup (u = 0.5 + 0.5 x / (w t)) use
// one number.
inline void faceProjection(double tan_half, double near_m, double far_m, float m[16])
{
    for (int i = 0; i < 16; ++i)
    {
        m[i] = 0.f;
    }
    m[0] = m[5] = float(1.0 / tan_half);
    m[10] = float(-(far_m + near_m) / (far_m - near_m));
    m[11] = -1.f;
    m[14] = float(-2.0 * far_m * near_m / (far_m - near_m));
}

// The column-major view of face f from the light's centre (agent space): rows right, up and -fwd, translated by -centre.
inline void faceView(int face, const float centre[3], float m[16])
{
    const FaceAxes& a = kFaceAxes[face];
    const float* rows[3] = { a.right, a.up, a.fwd };
    for (int r = 0; r < 3; ++r)
    {
        const float s = r == 2 ? -1.f : 1.f;
        float t = 0.f;
        for (int c = 0; c < 3; ++c)
        {
            m[c * 4 + r] = s * rows[r][c];
            t -= s * rows[r][c] * centre[c];
        }
        m[12 + r] = t;
    }
    m[3] = m[7] = m[11] = 0.f;
    m[15] = 1.f;
}

// The face's tile coordinates of a point with face coordinates xyw (pickFace): u, v in [0, 1] over the whole tile (guard included), and
// the stored depth.
inline void faceTexCoord(const float xyw[3], double tan_half, const FaceDepth& depth, float uvz[3])
{
    const double w = double(xyw[2]);
    uvz[0] = float(0.5 + 0.5 * double(xyw[0]) / (w * tan_half));
    uvz[1] = float(0.5 + 0.5 * double(xyw[1]) / (w * tan_half));
    uvz[2] = float(depth.c0 - depth.c1 / w);
}

// The volume a face's picture depends on (agent space): the pyramid of its render frustum, guard included (tan_half), from the near to
// the far plane. With tan_half 1 it is the face's interior: the camera position's distance to it and the own avatar's face.
inline llprojectorpool::Pyramid facePyramid(const float centre[3], int face, float tan_half, float near_m, float far_m)
{
    const FaceAxes& a = kFaceAxes[face];
    return llprojectorpool::makePyramid(centre, a.fwd, a.right, a.up, tan_half, tan_half, near_m, far_m);
}

// The camera position's distance to the lamp's lit sphere (0 inside): the band the valve shrinks the lamp's six faces by, and the order of a
// lamp's first fill. One distance per lamp, so its faces are always in one band.
inline float lampDistance(const float cam[3], const float centre[3], float far_m)
{
    float d2 = 0.f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = cam[i] - centre[i];
        d2 += d * d;
    }
    return std::max(std::sqrt(d2) - far_m, 0.f);
}

// ---------------------------------------------------------------------------------------------
// Keys: a face record is lamp << 3 | face; the scheduler's item for a whole lamp (its first fill, a move: the six faces drawn in one
// render from one pose, so the lookup never mixes two poses of a lamp) is lamp << 3 | kLampItem. The pool packs them with kPoolPoints.
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kFaceBits = 3;
constexpr uint32_t kLampItem = 7;
constexpr uint32_t kLampIds = 1u << (llprojectorpool::kPoolKeyBits - kFaceBits); // lamp ids stay below this
constexpr uint32_t faceKey(uint32_t lamp, uint32_t face) { return (lamp << kFaceBits) | face; }
constexpr uint32_t lampOf(uint32_t key) { return key >> kFaceBits; }
constexpr uint32_t faceOf(uint32_t key) { return key & ((1u << kFaceBits) - 1u); }
static_assert(kLampItem >= kFaces && kLampItem < (1u << kFaceBits), "the whole-lamp item's index is not a face");

// ---------------------------------------------------------------------------------------------
// The lookup's face rects (pointLightF.glsl, multiPointLightF.glsl: two vec4 per lamp hold its six faces). A face's rect is packed into
// one float: x / 64 + 64 (y / 64) + 4096 log2(n / 64). Every rect of a buddy atlas of at most 4096 is a multiple of 64 there, so the
// three fields are small integers and the float holds them exactly (below 2^15). 0 is no rect (a face at least 128 has log2 >= 1): the
// shader reads it as "not drawn".
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kPackAtlasMax = 4096;

inline float packFaceRect(const llprojectorpool::Rect& r)
{
    if (r.size < kFaceMin || r.x % 64u || r.y % 64u || r.x + r.size > kPackAtlasMax || r.y + r.size > kPackAtlasMax || (r.size & (r.size - 1)))
    {
        return 0.f;
    }
    uint32_t code = 0;
    while ((64u << code) < r.size)
    {
        ++code;
    }
    return float(r.x / 64u + 64u * (r.y / 64u) + 4096u * code);
}

// The shader's unpacking (stPointFaceTile), for the test: false for 0 (not drawn)
inline bool unpackFaceRect(float packed, llprojectorpool::Rect& out)
{
    if (!(packed >= 1.f))
    {
        return false;
    }
    const float code = std::floor(packed / 4096.f);
    const float rest = packed - 4096.f * code;
    const float row = std::floor(rest / 64.f);
    const float col = rest - 64.f * row;
    out.x = uint32_t(col) * 64u;
    out.y = uint32_t(row) * 64u;
    out.size = 64u << uint32_t(code);
    return code >= 1.f;
}

// ---------------------------------------------------------------------------------------------
// The own avatar's faces (owner decision P2): drawn every frame, past the cap, only under the kOwnEveryFrameLamps lamps that light the
// avatar most; its faces under the other lamps go by the near avatars' 2-frame deadline under the cap, the strongest first. The
// projector rule (every face of the own avatar every frame) would take 55 to 110 faces a frame under a living room's lamps.
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kOwnEveryFrameLamps = 4;

// The light a lamp gives a point at `distance` (m): the deferred pass's attenuation, calcLegacyDistanceAttenuation(distance / size, falloff)
// (deferredUtil.glsl), times the light's luminance; size is the lit size (radius x 1.5). 0 beyond the lit size.
inline float lampIrradiance(float luminance, float distance, float size, float falloff)
{
    if (!(size > 0.f) || !(luminance > 0.f) || !std::isfinite(distance))
    {
        return 0.f;
    }
    const float f = std::isfinite(falloff) ? falloff : 0.f;
    const float a = 1.f - std::min(std::max((distance / size + f) / (1.f + f), 0.f), 1.f);
    return luminance * a * a * 2.f;
}

// Which lamps' own-avatar faces run every frame: the kOwnEveryFrameLamps with the most irradiance at the avatar (lamp id breaks a tie, a
// lamp with none never does). `lamps` is (lamp id, irradiance); returns a flag per entry and, in `rank`, each entry's place in the order
// (0 the strongest): the order of the others' service.
inline std::vector<bool> ownEveryFrame(const std::vector<std::pair<uint32_t, float>>& lamps, std::vector<uint32_t>* rank = nullptr)
{
    std::vector<size_t> order(lamps.size());
    for (size_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    auto strength = [&lamps](size_t i) { const float e = lamps[i].second; return std::isfinite(e) ? e : 0.f; };
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const float ea = strength(a), eb = strength(b);
        return ea != eb ? ea > eb : lamps[a].first < lamps[b].first;
    });
    std::vector<bool> every(lamps.size(), false);
    if (rank)
    {
        rank->assign(lamps.size(), 0u);
    }
    for (size_t k = 0; k < order.size(); ++k)
    {
        if (k < kOwnEveryFrameLamps && strength(order[k]) > 0.f)
        {
            every[order[k]] = true;
        }
        if (rank)
        {
            (*rank)[order[k]] = uint32_t(k);
        }
    }
    return every;
}

// The pose of a lamp's faces: its centre and its radius (the far plane). The faces are aligned to the agent axes, so the lamp's rotation
// never enters: a prim spinning about any axis keeps its faces. Kept in llprojectorpool::Pose (rotation identity, unit scale, no fov), so
// poseChanged compares the centre (1 mm) and the radius (1 cm).
inline llprojectorpool::Pose lampPose(uint64_t region, const float local[3], float radius)
{
    llprojectorpool::Pose p;
    p.region = region;
    for (int i = 0; i < 3; ++i)
    {
        p.pos[i] = local[i];
    }
    p.radius = radius;
    return p;
}

} // namespace llpointpool

#endif // LL_LLPOINTSHADOWPOOL_H
