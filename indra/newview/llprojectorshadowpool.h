/**
 * @file llprojectorshadowpool.h
 * @brief The projector shadow pool's GL-free rules: tile size, atlas level, buddy placement,
 *        repack plan, dirty boxes, box-in-pyramid test and the scheduler's ordering
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

#ifndef LL_LLPROJECTORSHADOWPOOL_H
#define LL_LLPROJECTORSHADOWPOOL_H

// Header-only, std only and free of GL and viewer types, so that
// indra/newview/tests/llprojectorshadowpool_test.cpp can check the rules standalone. The GL class
// (llprojectorshadows.h, stage P2) maps a record's key to its drawable and runs these rules.
// Every number follows doc/shadows spec "Projector shadow pool" (reports/projector-pool-2026-11-13,
// design-final.md sections 3 to 4.5). Techniques: a texture-atlas allocator of the buddy kind
// (Knowlton 1965), shadow-map atlases for local lights (Crytek and id Tech 6 style tiling, GPU
// Gems 3 ch. 10 on caster culling), conservative box-against-plane culling (Assarsson and
// Moller 2000). No code is taken from any of them.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace llprojectorpool
{

// ---------------------------------------------------------------------------------------------
// Tile size (section 4.2, D4) and atlas level (D3)
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kMinTile = 64;      // the floor: a tile never goes below it
constexpr uint32_t kMinBase = 128;     // clamp of the light's base size
constexpr uint32_t kMaxBase = 2048;
constexpr int      kMaxLevel = 5;      // 2048 >> 5 = 64: below this nothing changes

constexpr float kShrinkAbove = 0.60f;  // demand above this: down by as many levels as it takes to get at or below it (one repack)
constexpr float kGrowAtMost = 0.45f;   // demand of the next level up at or below this ...
constexpr float kGrowHoldSeconds = 3.0f; // ... for this long: one level up

inline uint32_t pow2Round(double v)
{
    if (!(v > 1.0))
    {
        return 1;
    }
    // nearest power of two in the log domain
    const double l = std::min(std::floor(std::log2(v) + 0.5), 31.0); // v >= 2^32 would overflow the shift
    return uint32_t(1) << uint32_t(l);
}

// d_ref  = min(1.5 * radius, 4 m)
// N_base = clamp(pow2_round(2 * tan(fov / 2) * d_ref / 4 mm), 128, 2048)
inline uint32_t baseSize(float radius, float fov_radians)
{
    const double d_ref = std::min(1.5 * double(radius), 4.0);
    // tan() is non-monotone past pi: clamp the half angle to [0, 1.55] rad, far beyond any real fov
    const double half = std::min(std::max(0.5 * double(fov_radians), 0.0), 1.55);
    const double texels = 2.0 * std::tan(half) * d_ref / 0.004;
    const uint32_t n = pow2Round(texels);
    return std::min(std::max(n, kMinBase), kMaxBase);
}

// Atlas edge by video memory (spec 4.2, D20). vram_mb is gGLManager.mVRAM (MB, 0 = unknown), max_texture the driver's
// GL_MAX_TEXTURE_SIZE (0 = unknown, not limiting), dedicated whether vram_mb is the card's own memory (the driver's memory
// info) and not a budget of shared system memory. Two D32F atlases: 32 MiB at 2048, 128 MiB at 4096, 512 MiB at 8192.
constexpr uint32_t kAtlasSmall = 2048;   // below 3 GB
constexpr uint32_t kAtlasDefault = 4096;
constexpr uint32_t kAtlasLarge = 8192;   // from 11 GB of dedicated memory: the 4096 atlas holds four 2048 tiles, the 8192 one sixteen
constexpr uint32_t kSmallVramMb = 3072;
constexpr uint32_t kLargeVramMb = 11264; // a "12 GB" card often reports a little under 12288 MB

inline uint32_t atlasTier(uint32_t vram_mb, uint32_t max_texture, bool dedicated)
{
    uint32_t size = kAtlasDefault;
    if (vram_mb > 0 && vram_mb < kSmallVramMb)
    {
        size = kAtlasSmall;
    }
    else if (dedicated && vram_mb >= kLargeVramMb)
    {
        size = kAtlasLarge;
    }
    // a driver that cannot make a texture that wide gets the largest tier it can (never below the smallest)
    while (size > kAtlasSmall && max_texture > 0 && size > max_texture)
    {
        size >>= 1;
    }
    return size;
}

// the memory of the two atlases of one tier (D32F, 4 bytes a texel), in bytes
constexpr uint64_t atlasBytes(uint32_t size) { return 2ull * size * size * 4ull; }

// The penumbra's size rules (P2b), the C++ twins of sampleProjectorShadow's (shadowStochasticUtil.glsl), per axis of the map
// (u: k = k.x, v: k = k.y). k: uv per metre across the axis at unit axial distance; w: the receiver's axial distance from the
// projector's centre (m); n: the tile size (texels); c1: the depth term of the projector's matrix (z = c0 - c1 / w).
constexpr float kRadiusMaxM = 0.04f;        // ST_SPOT_RADIUS_MAX_M: the largest radius of the search and filter, metres at the receiver
constexpr float kKernelTexelsMin = 1.0f;    // ST_SPOT_KERNEL_MIN
constexpr float kKernelTexelsMax = 12.0f;   // ST_SPOT_KERNEL_TEXELS_MAX
constexpr float kEmitterM = 0.01f;          // metres: every lamp is a 1 cm light (SL's minimum prim size), whatever its prim
constexpr float kSoftnessDefault = 0.23f;   // RenderShadowLightSize's default: the emitter's (and the sun's) size multiplier

// clamp(ST_SPOT_RADIUS_MAX_M * k * N / w, 1, 12)
inline float radiusCapTexels(float k, uint32_t n, float w)
{
    return std::min(std::max(kRadiusMaxM * k * (float)n / w, kKernelTexelsMin), kKernelTexelsMax);
}

// the emitter's extent across the beam, in metres: 1 cm times the softness (RenderShadowLightSize), the same on both axes and
// for every lamp. A negative or non-finite softness gets the default
inline float emitterExtent(float softness)
{
    return kEmitterM * (std::isfinite(softness) && softness >= 0.0f ? softness : kSoftnessDefault);
}

// texels of filter radius per unit of map depth between receiver and blocker: (0.5 / c1) * light * k * N
inline float penumbraTexelsPerDepth(float light, float k, uint32_t n, float c1)
{
    return (0.5f / c1) * light * k * (float)n;
}

// N_i(s) = max(64, N_base >> s)
inline uint32_t levelSize(uint32_t base, int level)
{
    const int s = std::min(std::max(level, 0), 31);
    return std::max(kMinTile, base >> s);
}

// U(s) = sum N_i(s)^2 / A^2
inline double demand(const std::vector<uint32_t>& bases, int level, uint32_t atlas)
{
    double sum = 0.0;
    for (uint32_t b : bases)
    {
        const double n = double(levelSize(b, level));
        sum += n * n;
    }
    const double a = double(atlas);
    return sum / (a * a);
}

// The global level with its hysteresis. One instance per atlas.
class LevelController
{
public:
    int level() const { return mLevel; }
    void reset() { mLevel = 0; mHold = 0.0f; }

    // Call once per frame. dt_seconds is the frame time; placement_failed asks for one more step
    // down (a repack that did not fit after compaction). Returns true when the level changed.
    bool update(const std::vector<uint32_t>& bases, uint32_t atlas, float dt_seconds, bool placement_failed)
    {
        // a timer glitch (NaN, negative) counts as no time; a long stall (teleport, alt-tab) as one short frame, so the
        // 3 s hold is 3 s of stable frames
        dt_seconds = std::isfinite(dt_seconds) ? std::min(std::max(dt_seconds, 0.0f), 0.25f) : 0.0f;
        const int before = mLevel;
        while (mLevel < kMaxLevel && demand(bases, mLevel, atlas) > kShrinkAbove)
        {
            ++mLevel;
            mHold = 0.0f;
        }
        if (placement_failed && mLevel < kMaxLevel && mLevel == before)
        {
            ++mLevel;
            mHold = 0.0f;
        }
        if (mLevel == before && mLevel > 0)
        {
            if (demand(bases, mLevel - 1, atlas) <= kGrowAtMost)
            {
                mHold += dt_seconds;
                if (mHold >= kGrowHoldSeconds)
                {
                    --mLevel;
                    mHold = 0.0f;
                }
            }
            else
            {
                mHold = 0.0f;
            }
        }
        else if (mLevel == 0)
        {
            mHold = 0.0f;
        }
        return mLevel != before;
    }

private:
    int   mLevel = 0;
    float mHold = 0.0f;
};

// ---------------------------------------------------------------------------------------------
// Reach with hysteresis (section 4.1, D17)
// ---------------------------------------------------------------------------------------------

inline float spotReach(float far_clip) { return std::min(far_clip, 128.0f); }

// Add at cam_dist <= D, drop at cam_dist > D + max(2 m, 0.1 D).
inline bool inRange(bool was_in, float cam_dist, float reach)
{
    if (was_in)
    {
        return cam_dist <= reach + std::max(2.0f, 0.1f * reach);
    }
    return cam_dist <= reach;
}

// ---------------------------------------------------------------------------------------------
// Pose change thresholds (section 4.4, light pose row)
// ---------------------------------------------------------------------------------------------

struct Pose
{
    uint64_t region = 0;
    float pos[3] = { 0, 0, 0 };
    float rot[4] = { 0, 0, 0, 1 };
    float scale[3] = { 1, 1, 1 };
    float fov = 0.f;
    float radius = 0.f;
};

// position > 1 mm, rotation > 1e-4, scale > 1 mm, fov > 1e-4, radius > 1 cm
inline bool poseChanged(const Pose& a, const Pose& b)
{
    if (a.region != b.region)
    {
        return true;
    }
    for (int i = 0; i < 3; ++i)
    {
        if (std::fabs(a.pos[i] - b.pos[i]) > 0.001f || std::fabs(a.scale[i] - b.scale[i]) > 0.001f)
        {
            return true;
        }
    }
    float dot = 0.f;
    for (int i = 0; i < 4; ++i)
    {
        dot += a.rot[i] * b.rot[i];
    }
    const float sign = dot < 0.f ? -1.f : 1.f; // q and -q are the same rotation
    for (int i = 0; i < 4; ++i)
    {
        if (std::fabs(a.rot[i] - sign * b.rot[i]) > 1e-4f)
        {
            return true;
        }
    }
    return std::fabs(a.fov - b.fov) > 1e-4f || std::fabs(a.radius - b.radius) > 0.01f;
}

// ---------------------------------------------------------------------------------------------
// Buddy allocator for square power-of-two tiles in a square power-of-two atlas
// ---------------------------------------------------------------------------------------------

struct Rect
{
    uint32_t x = 0, y = 0, size = 0;
    bool operator==(const Rect& o) const { return x == o.x && y == o.y && size == o.size; }
};

class BuddyAtlas
{
public:
    BuddyAtlas() = default;
    explicit BuddyAtlas(uint32_t atlas_size) { reset(atlas_size); }

    // atlas_size should be a power of two >= kMinTile; anything else is rounded down to one, so
    // no rect can ever lie outside the atlas
    void reset(uint32_t atlas_size)
    {
        atlas_size = std::max<uint32_t>(atlas_size, kMinTile);
        while (atlas_size & (atlas_size - 1))
        {
            atlas_size &= atlas_size - 1;
        }
        mAtlas = atlas_size;
        mOrders = 0;
        while ((uint32_t(kMinTile) << mOrders) < atlas_size)
        {
            ++mOrders;
        }
        mFree.assign(mOrders + 1, std::set<uint64_t>());
        mFree[mOrders].insert(key(0, 0));
    }

    uint32_t atlasSize() const { return mAtlas; }

    // Lowest-address free block of that size; false when none fits. Deterministic.
    bool alloc(uint32_t size, Rect& out)
    {
        if (!validSize(size))
        {
            return false;
        }
        const int want = order(size);
        int o = want;
        while (o <= mOrders && mFree[o].empty())
        {
            ++o;
        }
        if (o > mOrders)
        {
            return false;
        }
        uint64_t k = *mFree[o].begin();
        mFree[o].erase(mFree[o].begin());
        uint32_t x = keyX(k), y = keyY(k);
        while (o > want)
        {
            --o;
            const uint32_t h = blockSize(o);
            mFree[o].insert(key(x + h, y));
            mFree[o].insert(key(x, y + h));
            mFree[o].insert(key(x + h, y + h));
        }
        out = { x, y, size };
        return true;
    }

    // Frees a block returned by alloc and merges buddies.
    void free(const Rect& r)
    {
        int o = order(r.size);
        uint32_t x = r.x, y = r.y;
        while (o < mOrders)
        {
            const uint32_t s = blockSize(o);
            const uint32_t px = x & ~s, py = y & ~s; // the parent's origin
            const uint64_t k[4] = { key(px, py), key(px + s, py), key(px, py + s), key(px + s, py + s) };
            const uint64_t self = key(x, y);
            bool all = true;
            for (uint64_t c : k)
            {
                if (c != self && mFree[o].find(c) == mFree[o].end())
                {
                    all = false;
                    break;
                }
            }
            if (!all)
            {
                break;
            }
            for (uint64_t c : k)
            {
                mFree[o].erase(c);
            }
            x = px;
            y = py;
            ++o;
        }
        mFree[o].insert(key(x, y));
    }

    // Texels in free blocks, and the largest free block (fragmentation measure)
    uint64_t freeTexels() const
    {
        uint64_t sum = 0;
        for (int o = 0; o <= mOrders; ++o)
        {
            sum += uint64_t(mFree[o].size()) * blockSize(o) * blockSize(o);
        }
        return sum;
    }
    uint32_t largestFree() const
    {
        for (int o = mOrders; o >= 0; --o)
        {
            if (!mFree[o].empty())
            {
                return blockSize(o);
            }
        }
        return 0;
    }

private:
    static uint64_t key(uint32_t x, uint32_t y) { return (uint64_t(y) << 32) | x; }
    static uint32_t keyX(uint64_t k) { return uint32_t(k & 0xffffffffu); }
    static uint32_t keyY(uint64_t k) { return uint32_t(k >> 32); }
    static uint32_t blockSize(int o) { return uint32_t(kMinTile) << o; }
    bool validSize(uint32_t s) const
    {
        return s >= kMinTile && s <= mAtlas && (s & (s - 1)) == 0;
    }
    static int order(uint32_t size)
    {
        int o = 0;
        while ((uint32_t(kMinTile) << o) < size)
        {
            ++o;
        }
        return o;
    }

    uint32_t mAtlas = 0;
    int mOrders = 0;
    std::vector<std::set<uint64_t>> mFree; // per order, ordered by (y, x): deterministic
};

// ---------------------------------------------------------------------------------------------
// Layout and repack plan (section 4.3)
// ---------------------------------------------------------------------------------------------

struct LayoutRequest
{
    uint32_t key = 0;
    uint32_t size = 0;      // the tile's size at the new level
    bool has_old = false;   // the tile already holds placed content
    Rect old_rect;          // where its content is now
    bool drawn = false;     // the old content is worth moving
};

struct Move
{
    uint32_t key = 0;
    Rect src, dst;
    bool copy = false;      // same size: glCopyImageSubData (bit exact); else a blit + re-render
};

struct RepackPlan
{
    bool ok = false;                 // every request was placed
    std::vector<Rect> placed;        // parallel to the requests (valid when ok)
    std::vector<Move> moves;         // drawn tiles with old content
    std::vector<uint32_t> unplaced;  // keys that did not fit (when !ok)
    BuddyAtlas buddy;                // the allocator after the plan's allocations (valid when ok): the caller adopts it
};

// Places every request into a fresh atlas: descending size, then key, so the same set always
// gives the same layout (T3). Does not touch the old layout; the caller applies the moves
// (S[old] -> L[new], then L -> S in full) and only then adopts the plan.
inline RepackPlan planRepack(const std::vector<LayoutRequest>& requests, uint32_t atlas)
{
    RepackPlan plan;
    plan.placed.assign(requests.size(), Rect());
    std::vector<uint32_t> order(requests.size());
    for (uint32_t i = 0; i < order.size(); ++i)
    {
        order[i] = i;
    }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        if (requests[a].size != requests[b].size)
        {
            return requests[a].size > requests[b].size;
        }
        return requests[a].key < requests[b].key;
    });
    BuddyAtlas fresh(atlas);
    plan.ok = true;
    for (uint32_t i : order)
    {
        Rect r;
        if (fresh.alloc(requests[i].size, r))
        {
            plan.placed[i] = r;
        }
        else
        {
            plan.ok = false;
            plan.unplaced.push_back(requests[i].key);
        }
    }
    if (plan.ok)
    {
        for (size_t i = 0; i < requests.size(); ++i)
        {
            const LayoutRequest& q = requests[i];
            if (q.has_old && q.drawn)
            {
                plan.moves.push_back({ q.key, q.old_rect, plan.placed[i], q.old_rect.size == q.size });
            }
        }
        std::sort(plan.moves.begin(), plan.moves.end(), [](const Move& a, const Move& b) { return a.key < b.key; });
        plan.buddy = fresh;
    }
    return plan;
}

// ---------------------------------------------------------------------------------------------
// Boxes, pyramid planes, the dirty list and the 8 m grid (sections 4.4)
// ---------------------------------------------------------------------------------------------

struct Aabb
{
    float lo[3] = { 0, 0, 0 };
    float hi[3] = { 0, 0, 0 };
};

inline bool overlaps(const Aabb& a, const Aabb& b)
{
    for (int i = 0; i < 3; ++i)
    {
        if (a.hi[i] < b.lo[i] || b.hi[i] < a.lo[i])
        {
            return false;
        }
    }
    return true;
}

// n . p + d >= 0 is inside
struct Plane
{
    float n[3] = { 0, 0, 0 };
    float d = 0;
};

struct Pyramid
{
    Plane planes[6]; // left, right, bottom, top, near, far
    Aabb bounds;     // of the eight corners
};

namespace detail
{
    inline void sub(const float* a, const float* b, float* o) { for (int i = 0; i < 3; ++i) o[i] = a[i] - b[i]; }
    inline void cross(const float* a, const float* b, float* o)
    {
        o[0] = a[1] * b[2] - a[2] * b[1];
        o[1] = a[2] * b[0] - a[0] * b[2];
        o[2] = a[0] * b[1] - a[1] * b[0];
    }
    inline float dot(const float* a, const float* b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
    inline void normalise(float* v)
    {
        const float l = std::sqrt(dot(v, v));
        if (l > 0.f)
        {
            for (int i = 0; i < 3; ++i) v[i] /= l;
        }
    }
    inline Plane through(const float* p0, const float* p1, const float* p2, const float* inside)
    {
        float a[3], b[3], n[3];
        sub(p1, p0, a);
        sub(p2, p0, b);
        cross(a, b, n);
        normalise(n);
        Plane pl;
        for (int i = 0; i < 3; ++i) pl.n[i] = n[i];
        pl.d = -dot(n, p0);
        if (dot(n, inside) + pl.d < 0.f) // orient inwards
        {
            for (int i = 0; i < 3; ++i) pl.n[i] = -pl.n[i];
            pl.d = -pl.d;
        }
        return pl;
    }
}

// The rectangular pyramid of an SL projector. origin and the unit axes are in the same space as the boxes (agent space);
// tan_x and tan_y are the tangents of the half angles across and along the axes (a square pyramid: both tan(fov / 2); the
// tile's render frustum is perspective(fov, aspect): tan_y = tan(fov / 2), tan_x = tan_y * aspect); the planes run from
// `near_d` (> 0) to `far_d` (the light's radius) along the forward axis.
inline Pyramid makePyramid(const float origin[3], const float fwd[3], const float right[3], const float up[3],
                           float tan_x, float tan_y, float near_d, float far_d)
{
    Pyramid py;
    float c[8][3];
    int k = 0;
    for (int zi = 0; zi < 2; ++zi)
    {
        const float z = zi ? far_d : near_d;
        for (int yi = 0; yi < 2; ++yi)
        {
            for (int xi = 0; xi < 2; ++xi)
            {
                const float x = (xi ? 1.f : -1.f) * tan_x * z;
                const float y = (yi ? 1.f : -1.f) * tan_y * z;
                for (int i = 0; i < 3; ++i)
                {
                    c[k][i] = origin[i] + fwd[i] * z + right[i] * x + up[i] * y;
                }
                ++k;
            }
        }
    }
    // corner index = zi * 4 + yi * 2 + xi
    float inside[3];
    for (int i = 0; i < 3; ++i)
    {
        inside[i] = origin[i] + fwd[i] * 0.5f * (near_d + far_d);
    }
    using namespace detail;
    py.planes[0] = through(c[4], c[0 + 4 + 2], c[0], inside);          // x = -1 side
    py.planes[1] = through(c[5], c[1], c[7], inside);                  // x = +1 side
    py.planes[2] = through(c[4], c[0], c[5], inside);                  // y = -1 side
    py.planes[3] = through(c[6], c[7], c[2], inside);                  // y = +1 side
    py.planes[4] = through(c[0], c[1], c[2], inside);                  // near
    py.planes[5] = through(c[4], c[5], c[6], inside);                  // far
    for (int i = 0; i < 3; ++i)
    {
        py.bounds.lo[i] = py.bounds.hi[i] = c[0][i];
    }
    for (int j = 1; j < 8; ++j)
    {
        for (int i = 0; i < 3; ++i)
        {
            py.bounds.lo[i] = std::min(py.bounds.lo[i], c[j][i]);
            py.bounds.hi[i] = std::max(py.bounds.hi[i], c[j][i]);
        }
    }
    return py;
}

// the square pyramid of a full angle `fov` (radians)
inline Pyramid makePyramid(const float origin[3], const float fwd[3], const float right[3], const float up[3],
                           float fov, float near_d, float far_d)
{
    const float t = std::tan(0.5f * fov);
    return makePyramid(origin, fwd, right, up, t, t, near_d, far_d);
}

// Conservative box against the pyramid's bounds and six planes: false when the box lies wholly
// outside the bounds or one plane. It never misses a box that touches the pyramid, up to float
// rounding of the plane normals (a few ulps at a plane); a box near an edge or corner may be
// reported although it does not touch (rare, costs one extra re-render). The bounds test is part of it, so callers need nothing else.
inline bool intersects(const Pyramid& py, const Aabb& box)
{
    if (!overlaps(py.bounds, box))
    {
        return false;
    }
    for (const Plane& pl : py.planes)
    {
        float v[3];
        for (int i = 0; i < 3; ++i)
        {
            v[i] = pl.n[i] >= 0.f ? box.hi[i] : box.lo[i]; // the corner furthest inside
        }
        if (pl.n[0] * v[0] + pl.n[1] * v[1] + pl.n[2] * v[2] + pl.d < 0.f)
        {
            return false;
        }
    }
    return true;
}

// Why a tile was drawn again (spec 7.3, O-2 logs the cause of every re-render). One cause per render: the first that made the
// tile stale since its last draw.
enum class Cause : uint8_t
{
    New = 0,    // never drawn (first fill, a new tile, a pool that was reset)
    LodUp,      // the LOD pin raised an object in the light to its full detail (once per object, then it holds)
    AddRemove,  // an object was added to or removed from the light's volume
    Edit,       // a fixed object in the light was edited, rebuilt or moved by its owner
    Mover,      // an object turned into (or out of) a mover or a physical object
    Lamp,       // the lamp's own reach, angle, size or position changed
    Avatar,     // the tile holds an avatar (drawn into L over S, P4), or the avatar just left
    Lowres,     // the tile was down-sized by a layout change and is drawn again at its new size
    Other,      // anything else: a dirty list that overflowed, a hook off the main thread
    Count
};
constexpr size_t kCauseCount = size_t(Cause::Count);

inline const char* causeName(Cause c)
{
    static const char* const names[kCauseCount] = { "new", "lod_up", "add_remove", "edit", "mover", "lamp", "avatar", "lowres", "other" };
    return size_t(c) < kCauseCount ? names[size_t(c)] : "other";
}

struct CauseCounts
{
    uint64_t n[kCauseCount] = {};
    void add(Cause c) { ++n[size_t(c) < kCauseCount ? size_t(c) : size_t(Cause::Other)]; }
    uint64_t total() const
    {
        uint64_t t = 0;
        for (uint64_t v : n) t += v;
        return t;
    }
};

struct DirtyBox
{
    Aabb box;
    Cause cause = Cause::Other;
};

// Dirty boxes, double buffered (T6): pushes go to the fill buffer; swapForApply() hands the
// filled buffer to the frame and starts an empty one, so a push made while tiles render lands
// in the next frame and is never cleared after a render (F9). A box says "something changed in
// here"; it names no tile, so any consumer that keeps tiles (the projector atlas, later the point-light face
// atlas) can test it against its own volumes.
class DirtyList
{
public:
    // A frame with more boxes than this stops recording and asks for everything to be redrawn (a bounded list; a scene
    // that changes this much at once is a load or a teleport, which redraws every tile anyway)
    static constexpr size_t kMaxPending = 1u << 16;

    void push(const Aabb& box, Cause cause = Cause::Other)
    {
        if (mFill.size() >= kMaxPending)
        {
            mFillOverflow = true;
            return;
        }
        // one drawable's hooks fire in a row (the move, the partition's move, the rebuild): an identical box right after the last
        // adds nothing
        if (!mFill.empty())
        {
            const DirtyBox& last = mFill.back();
            if (last.cause == cause && std::equal(box.lo, box.lo + 3, last.box.lo) && std::equal(box.hi, box.hi + 3, last.box.hi))
            {
                return;
            }
        }
        mFill.push_back({ box, cause });
    }

    // a change that cannot be given a box (a hook that ran off the main thread, a shift of the whole scene): every tile is stale
    void pushEverything() { mFillOverflow = true; }

    // Moves the pending boxes out for this frame; the returned reference stays valid until the
    // next swapForApply().
    const std::vector<DirtyBox>& swapForApply()
    {
        mApply.swap(mFill);
        mFill.clear();
        mApplyOverflow = mFillOverflow;
        mFillOverflow = false;
        return mApply;
    }

    // true when the list just handed out lost boxes (or was told "everything"): treat every tile as stale
    bool everything() const { return mApplyOverflow; }

    // shiftObjects moves agent space: translate pending (not yet applied) boxes
    void translatePending(float dx, float dy, float dz)
    {
        const float d[3] = { dx, dy, dz };
        for (DirtyBox& db : mFill)
        {
            for (int i = 0; i < 3; ++i)
            {
                db.box.lo[i] += d[i];
                db.box.hi[i] += d[i];
            }
        }
    }

    size_t pending() const { return mFill.size(); }
    void clear() { mFill.clear(); mApply.clear(); mFillOverflow = mApplyOverflow = false; }

private:
    std::vector<DirtyBox> mFill, mApply;
    bool mFillOverflow = false, mApplyOverflow = false;
};

// An 8 m grid over the tiles' bounds: a dirty box only runs the six-plane test against tiles
// that share one of its cells (F15). Boxes that are non-finite or cover more than kMaxCells cells
// (a bad extent, a world-sized prim) never loop over cells: a stored one goes to a wide list that
// every query returns, a query one returns every tile (conservative, bounded work).
class TileGrid
{
public:
    static constexpr float kCell = 8.0f;
    static constexpr int64_t kMaxCells = 4096;

    void clear() { mCells.clear(); mWide.clear(); mAll.clear(); }

    // `index` is the caller's tile index; boxes must be in the same space as queries
    void add(uint32_t index, const Aabb& bounds)
    {
        mAll.push_back(index);
        int32_t lo[3], hi[3];
        if (!cellRange(bounds, lo, hi))
        {
            mWide.push_back(index);
            return;
        }
        forCells(lo, hi, [&](uint64_t c) { mCells[c].push_back(index); });
    }

    // Sorted, unique candidate indices whose bounds share a cell with `box`
    void candidates(const Aabb& box, std::vector<uint32_t>& out) const
    {
        out.clear();
        int32_t lo[3], hi[3];
        if (!cellRange(box, lo, hi))
        {
            out = mAll;
        }
        else
        {
            out = mWide;
            forCells(lo, hi, [&](uint64_t c) {
                auto it = mCells.find(c);
                if (it != mCells.end())
                {
                    out.insert(out.end(), it->second.begin(), it->second.end());
                }
            });
        }
        std::sort(out.begin(), out.end());
        out.erase(std::unique(out.begin(), out.end()), out.end());
    }

private:
    static constexpr float kLimitM = 1.0e6f; // coordinates are clamped to this before the cell conversion

    // false when the box is non-finite or spans more than kMaxCells cells
    static bool cellRange(const Aabb& b, int32_t lo[3], int32_t hi[3])
    {
        int64_t cells = 1;
        for (int i = 0; i < 3; ++i)
        {
            if (!std::isfinite(b.lo[i]) || !std::isfinite(b.hi[i]))
            {
                return false;
            }
            const float l = std::max(-kLimitM, std::min(kLimitM, b.lo[i]));
            const float h = std::max(-kLimitM, std::min(kLimitM, b.hi[i]));
            lo[i] = int32_t(std::floor(l / kCell));
            hi[i] = int32_t(std::floor(h / kCell));
            if (hi[i] < lo[i])
            {
                std::swap(lo[i], hi[i]);
            }
            cells *= int64_t(hi[i]) - lo[i] + 1;
            if (cells > kMaxCells)
            {
                return false;
            }
        }
        return true;
    }
    static uint64_t pack(int32_t x, int32_t y, int32_t z)
    {
        return (uint64_t(uint32_t(x) & 0x1fffffu) << 42) | (uint64_t(uint32_t(y) & 0x1fffffu) << 21) |
               uint64_t(uint32_t(z) & 0x1fffffu);
    }
    template <class F>
    static void forCells(const int32_t lo[3], const int32_t hi[3], F&& f)
    {
        for (int32_t x = lo[0]; x <= hi[0]; ++x)
            for (int32_t y = lo[1]; y <= hi[1]; ++y)
                for (int32_t z = lo[2]; z <= hi[2]; ++z)
                    f(pack(x, y, z));
    }

    std::unordered_map<uint64_t, std::vector<uint32_t>> mCells;
    std::vector<uint32_t> mWide, mAll;
};

// ---------------------------------------------------------------------------------------------
// Which tiles a change touches (section 4.4, P3). Keyed by a tile's key, never by "the projector": the point-light face atlas
// (six cached faces per lamp) feeds the same index with its faces' pyramids.
// ---------------------------------------------------------------------------------------------

struct TileVolume
{
    uint32_t key = 0;   // the caller's tile or face key
    Pyramid pyramid;    // the volume the tile's picture depends on (agent space, the dirty boxes' space)
};

struct Hit
{
    uint32_t key = 0;
    Cause cause = Cause::Other;
};

// Rebuilt once per frame from the tiles' current volumes; apply() tests a frame's dirty boxes through the 8 m grid (F15) and
// names, once per tile, the cause of the first box that touched it. A list that overflowed (everything()) touches every tile.
class ChangeIndex
{
public:
    void rebuild(const std::vector<TileVolume>& tiles)
    {
        mTiles = tiles;
        mGrid.clear();
        for (uint32_t i = 0; i < mTiles.size(); ++i)
        {
            mGrid.add(i, mTiles[i].pyramid.bounds);
        }
    }

    size_t size() const { return mTiles.size(); }

    void apply(const std::vector<DirtyBox>& boxes, bool everything, std::vector<Hit>& out)
    {
        out.clear();
        if (mTiles.empty())
        {
            return;
        }
        if (everything)
        {
            for (const TileVolume& t : mTiles)
            {
                out.push_back({ t.key, Cause::Other });
            }
            return;
        }
        std::vector<char> hit(mTiles.size(), 0);
        size_t remaining = mTiles.size();
        for (const DirtyBox& b : boxes)
        {
            if (remaining == 0)
            {
                break; // every tile is already stale: the other boxes cannot add a cause
            }
            mGrid.candidates(b.box, mCand);
            for (uint32_t i : mCand)
            {
                if (!hit[i] && intersects(mTiles[i].pyramid, b.box))
                {
                    hit[i] = 1;
                    --remaining;
                    out.push_back({ mTiles[i].key, b.cause });
                }
            }
        }
    }

private:
    std::vector<TileVolume> mTiles;
    TileGrid mGrid;
    std::vector<uint32_t> mCand;
};

// ---------------------------------------------------------------------------------------------
// The mover classifier and the rules built on it (section 4.4, D9). The viewer reads its objects and fills these structs.
// ---------------------------------------------------------------------------------------------

struct ObjectFlags
{
    bool physical = false;  // the simulator's physics flag (a pushable object)
    bool mover = false;     // sticky for the session: the simulator reported it moving after it was created
};

// An object (or a bridge's root) casts into the fixed layer unless it is physical or a mover. The light's own linkset is the
// fixture: it is never dropped, whatever its flags (P4).
inline bool castsFixed(const ObjectFlags& root, bool is_lights_own_root)
{
    return is_lights_own_root || !(root.physical || root.mover);
}

// A change of an object that is a mover (or physical), or whose root is, does not dirty a tile while the object is active (its
// linkset has a spatial bridge, which the tile's cull drops: it is not in the fixed layer), and a moving part inside a still
// linkset casts with the pose of the last render (its moves never invalidate: the known limit). While the object is static (a
// mover at rest, say a door the simulator moved with no velocity) the tile draws it like any fixed caster, so its changes push.
// The lamp's own linkset is the fixture (castsFixed): its changes always push.
inline bool hookSkips(const ObjectFlags& obj, const ObjectFlags& root, bool active, bool lights_own_root = false)
{
    if (lights_own_root || !active)
    {
        return false;
    }
    return obj.physical || obj.mover || root.physical || root.mover;
}

struct ServerUpdate
{
    bool created_seen = false;  // an earlier update of this object has been processed (this one is not its creating update)
    bool selected = false;      // the agent has the object selected: its edits are not movement
    bool pos_changed = false;   // the update changed the position
    bool rot_changed = false;   // the update changed the rotation
    bool velocity = false;      // nonzero velocity or acceleration
    bool omega = false;         // nonzero angular velocity
    bool is_root = false;       // the object is the root of its linkset
};

// True when the update turns the object into a mover: a velocity or an angular velocity on a root (a child's omega is client
// spin, a known limit), or a server-reported position or rotation change after the creating update unless the agent edits it.
inline bool becomesMover(const ServerUpdate& u)
{
    if (u.is_root && (u.velocity || u.omega))
    {
        return true;
    }
    return u.created_seen && !u.selected && (u.pos_changed || u.rot_changed);
}

// A change of the flags that moves an object in or out of the fixed layer (physics on or off, a mover bit): its extents are
// pushed dirty at the transition, old and new.
inline bool layerTransition(const ObjectFlags& before, const ObjectFlags& after)
{
    return castsFixed(before, false) != castsFixed(after, false);
}

// The drawable's rebuild flags as the viewer defines them (LLDrawable::EDrawableFlags; llshadowdirtyfeed.cpp static_asserts that
// these match), so the rule below is tested with the real values: REBUILD_GEOMETRY is position | texcoord | colour, and
// REBUILD_ALL adds the volume.
constexpr uint32_t kRebuildVolume = 0x100, kRebuildTcoord = 0x200, kRebuildColor = 0x400, kRebuildPosition = 0x800, kRebuildRigged = 0x1000;
constexpr uint32_t kRebuildGeometry = kRebuildPosition | kRebuildTcoord | kRebuildColor;
constexpr uint32_t kRebuildAll = kRebuildGeometry | kRebuildVolume;

// Only a rebuild that can change what a tile shows dirties it: a new volume or rigging, or a position-only rebuild. A texture
// coordinate or colour rebuild (texture animation, a tint) does not (F4), and neither does the whole REBUILD_GEOMETRY set that
// markForUpdate, selection and bump changes raise: it carries position only because it rebuilds everything, not because the
// shape moved. A mesh arriving raises that set too and is counted on its own (LLShadowDirtyFeed::noteMeshLoaded).
inline bool rebuildDirties(uint32_t flags)
{
    if (flags & (kRebuildVolume | kRebuildRigged))
    {
        return true;
    }
    return (flags & kRebuildPosition) != 0 && !((flags & kRebuildTcoord) && (flags & kRebuildColor));
}

// A texture, material, bump or glow change dirties a prim's tiles when the prim had a silhouette-affecting face (blended,
// alpha-masked, alpha-textured) before the change or has one after it: turning such a face opaque removes a hole from the
// shadow, and an opaque face turning alpha cuts one. Opaque to opaque never does (the texture does not show in a depth tile),
// so plain texture swaps and animated textures do not churn. `was` is what the feed last saw; the caller stores `now` after.
// `known` says the feed saw this prim since it started tracking: a prim that predates tracking (login with Stochastic, a switch
// from Legacy) has an unknown `was`, taken as possibly alpha (one conservative push, after which it is known).
inline bool texturedDirties(bool was, bool now, bool known)
{
    return !known || was || now;
}

// ---------------------------------------------------------------------------------------------
// The LOD pin's books (section 4.4, D6). A tile pins the objects it draws at full detail (the viewer raises a counter on each
// object); the books say, per render, which objects the tile now holds and which it lets go of. Keyed by tile and object id.
// ---------------------------------------------------------------------------------------------

struct Id128
{
    uint64_t a = 0, b = 0;
    bool operator<(const Id128& o) const { return a != o.a ? a < o.a : b < o.b; }
    bool operator==(const Id128& o) const { return a == o.a && b == o.b; }
};

class PinBook
{
public:
    // A new render of the tile starts: what it touches from now on is what it holds afterwards.
    void beginRender(uint32_t tile) { ++mTiles[tile].generation; }

    // The tile's render meets the object. True when the tile did not hold it yet (the caller raises the object's counter).
    bool touch(uint32_t tile, const Id128& id)
    {
        Tile& t = mTiles[tile];
        auto it = t.held.find(id);
        if (it != t.held.end())
        {
            it->second = t.generation;
            return false;
        }
        t.held[id] = t.generation;
        ++mRefs[id];
        return true;
    }

    // The render is over: the objects the tile held and did not meet in it (they left the light, or were removed). The caller
    // lowers their counters.
    std::vector<Id128> endRender(uint32_t tile)
    {
        std::vector<Id128> gone;
        auto tt = mTiles.find(tile);
        if (tt == mTiles.end())
        {
            return gone;
        }
        Tile& t = tt->second;
        for (auto it = t.held.begin(); it != t.held.end();)
        {
            if (it->second != t.generation)
            {
                gone.push_back(it->first);
                unref(it->first);
                it = t.held.erase(it);
            }
            else
            {
                ++it;
            }
        }
        return gone;
    }

    // The tile goes away (its lamp left range, the pool was released): everything it held
    std::vector<Id128> releaseTile(uint32_t tile)
    {
        std::vector<Id128> all;
        auto tt = mTiles.find(tile);
        if (tt != mTiles.end())
        {
            for (const auto& e : tt->second.held)
            {
                all.push_back(e.first);
                unref(e.first);
            }
            mTiles.erase(tt);
        }
        return all;
    }

    // The object is gone (killed, its drawable unlinked): no tile holds it any more, whatever the books said. An object created
    // later with the same id is a new object whose counter is zero and must be pinned by its first render. Returns how many tiles
    // held it (the counter the caller lowers on the object, if it still lives).
    uint32_t forget(const Id128& id)
    {
        auto r = mRefs.find(id);
        if (r == mRefs.end())
        {
            return 0;
        }
        const uint32_t n = r->second;
        mRefs.erase(r);
        for (auto& t : mTiles)
        {
            t.second.held.erase(id);
        }
        return n;
    }

    size_t heldBy(uint32_t tile) const
    {
        auto it = mTiles.find(tile);
        return it == mTiles.end() ? 0 : it->second.held.size();
    }
    std::vector<Id128> pinnedIds() const
    {
        std::vector<Id128> ids;
        ids.reserve(mRefs.size());
        for (const auto& e : mRefs)
        {
            ids.push_back(e.first);
        }
        return ids;
    }
    size_t pinnedObjects() const { return mRefs.size(); }              // distinct objects pinned by at least one tile
    uint32_t refs(const Id128& id) const { auto it = mRefs.find(id); return it == mRefs.end() ? 0 : it->second; }

private:
    void unref(const Id128& id)
    {
        auto it = mRefs.find(id);
        if (it != mRefs.end() && --it->second == 0)
        {
            mRefs.erase(it);
        }
    }
    struct Tile
    {
        uint32_t generation = 0;
        std::map<Id128, uint32_t> held; // object -> the generation that last met it
    };
    std::map<uint32_t, Tile> mTiles;
    std::map<Id128, uint32_t> mRefs;
};

// ---------------------------------------------------------------------------------------------
// What a tile needs this frame (section 4.4 and 4.5): one verdict from the facts the viewer reads, so a static scene is
// provably free (no cause: no item).
// ---------------------------------------------------------------------------------------------

struct TileFacts
{
    bool never_drawn = false;     // drawn_frame == 0
    bool pending = false;         // a dirty box touched it since its last draw
    Cause pending_cause = Cause::Other;
    bool pose_stale = false;      // the lamp's pose differs from the pose the tile was drawn with
    bool lamp_moving = false;     // the lamp's pose changed since last frame (a moving lamp is a dynamic item, D10)
    bool lowres = false;          // down-sized by a layout change, not yet drawn at the new size
    bool holds_avatar = false;    // an avatar's extents intersect the light's volume this frame (position only, never the view)
    bool avatar_in_layer = false; // the live atlas L holds an avatar of this tile (drawn over S): it is redrawn, or cleaned once it left
};

struct TileVerdict
{
    bool needs_render = false;    // the tile's fixed layer S is drawn again (and S is copied to L, with the avatars over it when one is in the light)
    bool needs_layer = false;     // only the avatar layer: S -> L for the tile, then the avatars over it (the fixed layer is unchanged)
    bool dynamic = false;         // scheduled with the dynamic items (a moving lamp, or the avatar layer), else with the static ones
    Cause cause = Cause::Other;
};

inline TileVerdict classifyTile(const TileFacts& f)
{
    TileVerdict v;
    if (f.never_drawn)
    {
        v.needs_render = true;
        v.cause = Cause::New;
        return v; // the first fill is the static schedule's (nearest first), never the dynamic one
    }
    // P4: the avatars are in the dynamic layer (over S, in L), so an avatar never makes the fixed layer stale. A moving lamp
    // redraws the whole tile every frame (the dynamic schedule, D10); every other change is the static schedule's.
    v.dynamic = f.lamp_moving;
    if (f.pose_stale || f.lamp_moving)
    {
        v.needs_render = true;
        v.cause = Cause::Lamp;
    }
    else if (f.pending)
    {
        v.needs_render = true;
        v.cause = f.pending_cause;
    }
    else if (f.lowres)
    {
        v.needs_render = true;
        v.cause = Cause::Lowres;
    }
    else if (f.holds_avatar || f.avatar_in_layer)
    {
        // an avatar in the light is drawn again every frame the tile is served (nearest first, the rest under the shared cap);
        // the frame after it left, once more to restore L from S
        v.needs_layer = true;
        v.dynamic = true;
        v.cause = Cause::Avatar;
    }
    return v;
}

// ---------------------------------------------------------------------------------------------
// Scheduler ordering (section 4.5)
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kDynamicDeadlineFrames = 4;
// The cadence of the avatar layer under overload (own-avatar hotfix): the user's own avatar is drawn every frame; the other avatars'
// faces by the camera POSITION's distance to their nearest avatar (never the view direction): within kNearAvatarM every
// kNearDeadlineFrames frames, farther every kDynamicDeadlineFrames. When everything fits the budget every face runs every frame.
constexpr float kNearAvatarM = 10.f;
constexpr uint32_t kNearDeadlineFrames = 2;
constexpr uint32_t kOwnDeadlineFrames = 1;

// Prior for the cost of an item that has no history (never drawn) or a non-finite prediction.
// A placeholder until S0 measures; it is also the floor, so a zero prediction cannot let a whole
// cold start (login, teleport: up to 256 tiles) through one frame. The selection is
// prediction-only; the in-frame LLTimer check of spec 4.5 belongs to the P2 caller.
constexpr float kDefaultItemMs = 0.05f;

inline float itemCost(float predicted)
{
    return std::isfinite(predicted) ? std::max(predicted, kDefaultItemMs) : kDefaultItemMs;
}

// std::sort needs a strict weak order: a NaN distance sorts last
inline float sortDist(float d) { return std::isnan(d) ? std::numeric_limits<float>::infinity() : d; }

struct StaticItem
{
    uint32_t key = 0;
    bool never_drawn = false;   // drawn_frame == 0
    bool pending = false;       // needs a render (ignored when never_drawn): the caller passes only the tiles its verdict (classifyTile) selected
    float cam_dist = 0.f;
    float last_cpu_ms = 0.f;    // prediction of the cost of rendering it
    uint32_t age_frames = 0;    // how long it has waited: frames since the first dirty box that touched it, else since it was drawn
    uint32_t pool = 0;          // which cache the item belongs to (keys are per pool); 0 is the projector pool
    uint32_t avatar_draws = 0;  // avatars the render draws over the tile (min(avatars in the light, A_max)); charged against A_max
};

// Ordered picks for this frame: never-drawn nearest first, then the pending items oldest first (age in frames; the
// distance only breaks a tie), until the CPU cap t_s_ms would be exceeded. The order is round-robin by staleness, so
// no tile waits longer than ceil(N / tiles per frame) frames, whatever its distance. At least one item runs while
// any is pending (progress), even when its predicted cost alone exceeds the cap.
// `a_left` is what the dynamic picks left of A_max: a static item whose avatar draws do not fit waits (the order is kept), except the
// first one, which runs for progress (selectFrame makes room for it).
inline std::vector<uint32_t> selectStatic(const std::vector<StaticItem>& items, float t_s_ms,
                                          uint32_t a_left = std::numeric_limits<uint32_t>::max())
{
    std::vector<const StaticItem*> never, rest;
    for (const StaticItem& it : items)
    {
        if (it.never_drawn)
        {
            never.push_back(&it);
        }
        else if (it.pending)
        {
            rest.push_back(&it);
        }
    }
    auto byDist = [](const StaticItem* a, const StaticItem* b) {
        const float da = sortDist(a->cam_dist), db = sortDist(b->cam_dist);
        return da != db ? da < db : a->key < b->key;
    };
    auto byAgeThenDist = [&byDist](const StaticItem* a, const StaticItem* b) {
        return a->age_frames != b->age_frames ? a->age_frames > b->age_frames : byDist(a, b);
    };
    std::sort(never.begin(), never.end(), byDist);
    std::sort(rest.begin(), rest.end(), byAgeThenDist);

    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const auto* list : { &never, &rest })
    {
        for (const StaticItem* it : *list)
        {
            const float cost = itemCost(it->last_cpu_ms);
            if (!out.empty() && (spent + cost > t_s_ms || avatars + it->avatar_draws > a_left))
            {
                return out; // over the cap or A_max: the order is kept, the rest waits
            }
            out.push_back(it->key);
            spent += cost;
            avatars += it->avatar_draws;
        }
    }
    return out;
}

struct DynamicItem
{
    uint32_t key = 0;
    bool avatar = false;        // an avatar item (counts against A_max, at least one draw); else a moving lamp (no avatar draw)
    uint32_t age_frames = 0;    // frames since last drawn
    float cam_dist = 0.f;
    float last_cpu_ms = 0.f;
    uint32_t avatar_draws = 0;  // avatars this item draws
    uint32_t pool = 0;          // which cache the item belongs to (keys are per pool); 0 is the projector pool
    bool layer = false;         // an avatar layer face (the distance cadence applies); false for a moving lamp's whole tile
    bool own = false;           // a layer face whose layer holds the user's own avatar: drawn every frame, exempt from the budget
};

// How many frames an item may wait while the frame is over budget: 1 for the own avatar's face (every frame), kNearDeadlineFrames for
// a layer face whose nearest other avatar is within kNearAvatarM of the camera position, else kDynamicDeadlineFrames (also the
// moving lamps' tiles and a face that only restores L from S).
inline uint32_t dynamicDeadline(const DynamicItem& it)
{
    if (it.own)
    {
        return kOwnDeadlineFrames;
    }
    if (it.layer && it.avatar_draws > 0 && it.cam_dist <= kNearAvatarM)
    {
        return kNearDeadlineFrames;
    }
    return kDynamicDeadlineFrames;
}

// Dynamic picks: the faces of the own avatar first (every frame, whatever they cost: they are exempt from the budget, but their cost is
// spent from it and they count against A_max), then the items past their own deadline (dynamicDeadline: 2 frames for a near avatar's
// face, 4 for the rest; the most overdue first), then nearest first (for an avatar face: the camera position's distance to its nearest
// other avatar), then the one that has waited longest, so ties rotate instead of starving by key.
// Budget: CPU cap t_d_ms and at most a_max avatar draws (an item with more avatars draws its nearest a_max). No floor beyond that,
// with one exception that prevents permanent starvation, for the FIRST pick of a frame only (selectFrame keeps it only as the
// progress pick, so it passes allow_over = false when the frame has spent): an item past its deadline runs even if its predicted cost
// alone exceeds the cap. Other items over budget keep their last content. When everything fits, every item runs every frame.
inline std::vector<uint32_t> selectDynamic(const std::vector<DynamicItem>& items, float t_d_ms, uint32_t a_max, bool allow_over = true)
{
    std::vector<const DynamicItem*> order;
    for (const DynamicItem& it : items)
    {
        order.push_back(&it);
    }
    auto late = [](const DynamicItem* it) { // frames past the deadline, 1 on the frame it is due (0 when not yet due)
        const uint32_t d = dynamicDeadline(*it);
        return it->age_frames >= d ? it->age_frames - d + 1 : 0u;
    };
    std::sort(order.begin(), order.end(), [&late](const DynamicItem* a, const DynamicItem* b) {
        if (a->own != b->own)
        {
            return a->own;
        }
        const uint32_t la = late(a), lb = late(b);
        if (la != lb)
        {
            return la > lb;
        }
        const float ca = sortDist(a->cam_dist), cb = sortDist(b->cam_dist);
        if (ca != cb)
        {
            return ca < cb;
        }
        return a->age_frames != b->age_frames ? a->age_frames > b->age_frames : a->key < b->key;
    });
    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const DynamicItem* it : order)
    {
        const float cost = itemCost(it->last_cpu_ms);
        const uint32_t draws = it->avatar ? std::min(std::max(it->avatar_draws, 1u), a_max) : 0u;
        const bool may_over = allow_over && out.empty() && late(it) > 0;
        if ((spent + cost > t_d_ms && !may_over && !it->own) || avatars + draws > a_max)
        {
            continue; // skip this one; a cheaper one further down may still fit
        }
        out.push_back(it->key);
        spent += cost;
        avatars += draws;
    }
    return out;
}

// The smoothed prediction of an avatar face's CPU cost (an EMA, factor 0.25): one slow measurement (driver time lands in the CPU
// measurement) does not push a face out of the frame. Each sample is clamped to kCostSampleMaxMs (2 x kFrameCapMs, asserted below the
// cap) before it is blended, so one hitch cannot make a face "heavy" for dozens of frames. A zero prediction (a record that has not
// drawn a layer yet; itemCost floors it at kDefaultItemMs) is seeded by the first real sample. A non-finite or negative sample leaves
// the prediction as it was.
constexpr float kCostSmoothing = 0.25f;
constexpr float kCostSampleMaxMs = 0.70f;
inline float smoothedCostMs(float previous, float sample)
{
    if (!std::isfinite(sample) || sample < 0.f)
    {
        return previous;
    }
    sample = std::min(sample, kCostSampleMaxMs);
    if (!std::isfinite(previous) || previous <= 0.f)
    {
        return sample;
    }
    return previous + kCostSmoothing * (sample - previous);
}

// ---------------------------------------------------------------------------------------------
// The avatar layer's footprint (C3): the part of a tile the avatars cover
// ---------------------------------------------------------------------------------------------
//
// L's tile is S's tile plus the avatars. A face copies S -> L and draws only inside the rectangle the avatars cover (their extents,
// grown by a margin, projected through the tile's matrices), not over the whole tile. The invariant that makes it exact: outside the
// rectangle the last pass drew (`layerRect`), L equals S bit for bit, because every avatar draw is scissored to the rectangle it was
// computed for. The next face copies S -> L over the union of the old and the new rectangle (the old avatar's pixels are restored,
// the new ones are drawn over) and the result equals a whole-tile copy followed by the same scissored draw. When the footprint is
// unknown (a corner at or behind the eye plane, a non-finite matrix, an impostored avatar) or covers most of the tile, the
// rectangle is the whole tile: the same rule, not a second method.

struct TexelRect // [x0, x1) x [y0, y1) in tile texels
{
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    bool empty() const { return x1 <= x0 || y1 <= y0; }
    int64_t area() const { return empty() ? 0 : int64_t(x1 - x0) * int64_t(y1 - y0); }
    bool operator==(const TexelRect& o) const { return (empty() && o.empty()) || (x0 == o.x0 && y0 == o.y0 && x1 == o.x1 && y1 == o.y1); }
};

// The margin around an avatar's extents. The extents are what can lag: LLVOAvatar refreshes them only every `period` frames (4 up to 5
// avatars, 8 at 10, 20 at 25) and translates them by the pelvis in between, so a limb or a rigged attachment (inside the extents, with
// no padding of its own) can leave the box by what it moved in that time. The margin therefore grows with the period, with the speed
// (running, flying) and with the size of the box.
constexpr float kFootprintMarginM = 0.3f;        // base, at the avatar, in metres
constexpr float kFootprintStaleM = 0.04f;        // plus this many metres per frame of the extents' refresh period
constexpr float kFootprintSpeedSeconds = 0.15f;  // plus this many seconds of the avatar's speed (running, flying)
constexpr float kFootprintSizeFraction = 0.15f;  // plus this share of the box diagonal (a wing or a tail swings in proportion)
constexpr int32_t kFootprintMarginTexels = 2;    // plus a rounding margin in the tile
constexpr float kFootprintMaxFraction = 0.5f;    // above this share of the tile area the whole tile is copied

inline TexelRect wholeTileRect(uint32_t n) { return TexelRect{ 0, 0, int32_t(n), int32_t(n) }; }

inline TexelRect unionRect(const TexelRect& a, const TexelRect& b)
{
    if (a.empty())
    {
        return b.empty() ? TexelRect() : b;
    }
    if (b.empty())
    {
        return a;
    }
    return TexelRect{ std::min(a.x0, b.x0), std::min(a.y0, b.y0), std::max(a.x1, b.x1), std::max(a.y1, b.y1) };
}

inline TexelRect clampRect(const TexelRect& r, uint32_t n)
{
    TexelRect o{ std::max(r.x0, 0), std::max(r.y0, 0), std::min(r.x1, int32_t(n)), std::min(r.y1, int32_t(n)) };
    return o.empty() ? TexelRect() : o;
}

// the distance from a point to a box (0 inside)
inline float pointBoxDistance(const float p[3], const Aabb& b)
{
    float d2 = 0.f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = std::max(std::max(b.lo[i] - p[i], p[i] - b.hi[i]), 0.f);
        d2 += d * d;
    }
    return std::sqrt(d2);
}

inline Aabb inflateBox(const Aabb& b, float m)
{
    Aabb o = b;
    for (int i = 0; i < 3; ++i)
    {
        o.lo[i] -= m;
        o.hi[i] += m;
    }
    return o;
}

// The margin (metres) around an avatar's extents `box` for its speed (m/s) and the frames between two refreshes of the extents.
inline float footprintMarginM(float speed_mps, int32_t extent_period_frames, const Aabb& box)
{
    float diag2 = 0.f;
    for (int i = 0; i < 3; ++i)
    {
        const float d = box.hi[i] - box.lo[i];
        diag2 += d * d;
    }
    const float diag = std::isfinite(diag2) ? std::sqrt(diag2) : 0.f;
    return kFootprintMarginM + float(std::max(extent_period_frames, 0)) * kFootprintStaleM +
           (std::isfinite(speed_mps) ? std::max(speed_mps, 0.f) : 0.f) * kFootprintSpeedSeconds + diag * kFootprintSizeFraction;
}

// The tile rectangle one box covers through `m`, the column-major clip matrix (projection * view, agent space to clip space) of the
// tile. False when it is unknown: a corner at or behind the eye plane, or a non-finite value.
inline bool boxFootprint(const float m[16], const Aabb& box, uint32_t n, TexelRect& out)
{
    float lo_x = std::numeric_limits<float>::max(), lo_y = lo_x, hi_x = -lo_x, hi_y = -lo_x;
    for (int c = 0; c < 8; ++c)
    {
        const float x = (c & 1) ? box.hi[0] : box.lo[0];
        const float y = (c & 2) ? box.hi[1] : box.lo[1];
        const float z = (c & 4) ? box.hi[2] : box.lo[2];
        const float cx = m[0] * x + m[4] * y + m[8] * z + m[12];
        const float cy = m[1] * x + m[5] * y + m[9] * z + m[13];
        const float cw = m[3] * x + m[7] * y + m[11] * z + m[15];
        if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(cw) || cw <= 1e-4f)
        {
            return false;
        }
        const float tx = (cx / cw * 0.5f + 0.5f) * float(n);
        const float ty = (cy / cw * 0.5f + 0.5f) * float(n);
        lo_x = std::min(lo_x, tx);
        hi_x = std::max(hi_x, tx);
        lo_y = std::min(lo_y, ty);
        hi_y = std::max(hi_y, ty);
    }
    // out of the int range (a huge box close to the eye): unknown
    const float lim = 1e7f;
    if (lo_x < -lim || lo_y < -lim || hi_x > lim || hi_y > lim)
    {
        return false;
    }
    TexelRect r{ int32_t(std::floor(lo_x)) - kFootprintMarginTexels, int32_t(std::floor(lo_y)) - kFootprintMarginTexels,
                 int32_t(std::ceil(hi_x)) + kFootprintMarginTexels, int32_t(std::ceil(hi_y)) + kFootprintMarginTexels };
    out = clampRect(r, n);
    return true;
}

// The footprint of a face: the union over its avatars' (already grown) boxes, or the whole tile when any is unknown or the union covers
// more than kFootprintMaxFraction of the tile. No avatar: empty.
inline TexelRect faceFootprint(const float m[16], const std::vector<Aabb>& boxes, uint32_t n)
{
    TexelRect u;
    for (const Aabb& b : boxes)
    {
        TexelRect r;
        if (!boxFootprint(m, b, n, r))
        {
            return wholeTileRect(n);
        }
        u = unionRect(u, r);
    }
    if (double(u.area()) > double(kFootprintMaxFraction) * double(n) * double(n))
    {
        return wholeTileRect(n);
    }
    return u;
}

// What a layer pass copies S -> L: the old rectangle (L holds avatar pixels there) and the new one; the whole tile when the old one is
// not known (a fresh atlas, a repack).
inline TexelRect layerCopyRect(bool prev_valid, const TexelRect& prev, const TexelRect& now, uint32_t n)
{
    return prev_valid ? clampRect(unionRect(prev, now), n) : wholeTileRect(n);
}

// The per-frame CPU budget of ALL the shadow caches' tile work together (P4), in milliseconds, and its split. One number, shared by
// every pool through the frame ledger (below): the dynamic layer (a moving lamp's tile, the avatar layers) and the static tiles
// (a dirty or first-fill tile) are spent from it, never from two caps that add up (before P4: 0.35 + 0.30 ms).
//   - kFrameCapMs 0.35 ms is 4.4 % of an 8 ms frame (the basis is an assumed 8 ms frame, 125 fps, not a measured one). The +5 %
//     light and shadow allowance is about 0.4 ms of that frame, so 0.05 ms stays free for the rest of the pool's CPU work.
//   - Priority: the dynamic layer first. An avatar or a moving lamp is seen to lag at once; a dirty static tile only shows a stale
//     shadow of something that changed, and its wait is bounded by the age order of selectStatic. Two regimes (dynamicShareMs): while a
//     static tile waits, the dynamic layer gets kDynamicCapMs 0.20 ms; while none waits it gets the whole remaining cap (up to 7 faces
//     at the 0.05 ms floor), since the other 0.15 ms would go unused.
//   - The static tiles get what the dynamic layer left: at least kStaticCapMs = kFrameCapMs - kDynamicCapMs = 0.15 ms (two measured
//     tiles of 0.07 ms) while the dynamic layer takes its 0.20 ms, all 0.35 ms in a frame without dynamic work.
//   - The sum of the predicted costs of a frame's picks never exceeds kFrameCapMs, except for one single item whose own prediction is
//     above what is left, which runs alone when nothing else was spent in the frame (progress; selectFrame).
// The measured cost only predicts what fits; it does not set the budget.
constexpr float kFrameCapMs = 0.35f;
constexpr float kDynamicCapMs = 0.20f;
constexpr float kStaticCapMs = kFrameCapMs - kDynamicCapMs;
static_assert(kCostSampleMaxMs == 2.f * kFrameCapMs, "a cost sample is clamped to twice the frame cap");
// A_max: the avatar draws of a frame (one per avatar in a lamp's volume, at least one per avatar layer). The CPU share binds first
// (0.20 ms / the 0.05 ms scheduler floor = 4 faces a frame while a static tile waits, 0.35 / 0.05 = 7 when none does); 24 is the
// ceiling a cheaper face could reach.
constexpr uint32_t kAvatarDrawsMax = 24;

// The dynamic layer's share of the cap `left_ms`: kDynamicCapMs while a static tile waits (the static schedule keeps the rest), the
// whole of what is left when none does (the unused 0.15 ms is no use to anyone else).
inline float dynamicShareMs(bool statics_pending, float left_ms)
{
    const float left = std::max(left_ms, 0.f);
    return statics_pending ? std::min(kDynamicCapMs, left) : left;
}

// Several pools share the selection: an item's key is packed with its pool so that the merged lists keep the scheduler's order
// functions, and a pick is unpacked afterwards. Keys stay below 2^28 (a pool's counter of records).
constexpr uint32_t kPoolKeyBits = 28;
inline uint32_t packKey(uint32_t pool, uint32_t key) { return (pool << kPoolKeyBits) | (key & ((1u << kPoolKeyBits) - 1u)); }
inline uint32_t unpackPool(uint32_t packed) { return packed >> kPoolKeyBits; }
inline uint32_t unpackKey(uint32_t packed) { return packed & ((1u << kPoolKeyBits) - 1u); }

struct FramePicks
{
    std::vector<uint32_t> dynamic; // packed keys, in the order of service
    std::vector<uint32_t> fixed;   // packed keys of the static schedule
    float predicted_ms = 0.f;      // the sum of the predicted costs of both lists
};

// The frame's merged selection under the shared cap `cap_ms` (the cap minus what earlier work of the frame already spent, kFrameCapMs
// at the start): the dynamic items first (selectDynamic, up to dynamicShareMs and A_max), then the static ones in what is left.
// The first static candidate always has its place if it fits the cap: the dynamic layer gives up its last faces for it. A first
// static candidate that is above the whole cap runs alone as the progress pick once it has waited kDynamicDeadlineFrames frames (or was
// never drawn), and the dynamic picks of that frame are dropped, so a large tile is never starved by a standing avatar and the overrun
// is one item. `progress_ok` says nothing was spent yet in this frame: only then may an item above the cap run; otherwise the cap holds
// without exception. An avatar face that is over its share runs alone, every frame, as the progress pick (the overrun is that one
// item; a moving lamp's tile only once it is overdue); the dynamic share is the whole remaining cap when no static item waits
// (dynamicShareMs).
inline FramePicks selectFrame(const std::vector<StaticItem>& statics, const std::vector<DynamicItem>& dynamics, float cap_ms,
                              uint32_t a_max, bool progress_ok)
{
    FramePicks out;
    std::map<uint32_t, float> dyn_cost, st_cost; // the two lists have a key space each
    std::map<uint32_t, uint32_t> dyn_draws, st_draws;
    std::vector<DynamicItem> d = dynamics;
    for (DynamicItem& it : d)
    {
        it.key = packKey(it.pool, it.key);
        dyn_cost[it.key] = itemCost(it.last_cpu_ms);
        dyn_draws[it.key] = it.avatar ? std::min(std::max(it.avatar_draws, 1u), a_max) : 0u;
    }
    std::vector<StaticItem> st = statics;
    for (StaticItem& it : st)
    {
        it.key = packKey(it.pool, it.key);
        it.avatar_draws = std::min(it.avatar_draws, a_max);
        st_cost[it.key] = itemCost(it.last_cpu_ms);
        st_draws[it.key] = it.avatar_draws;
    }
    // a sum of float costs that equals the cap must fit it: the budgets carry a tolerance far below any cost (itemCost's floor is 0.05 ms)
    constexpr float kEps = 1e-4f;
    float left = cap_ms;
    std::set<uint32_t> own_keys; // the own avatar's faces: never dropped (every frame, whatever the cap says)
    for (const DynamicItem& it : d)
    {
        if (it.own)
        {
            own_keys.insert(it.key);
        }
    }
    auto dropLastOther = [&](std::vector<uint32_t>& list) { // drops the last pick that is not an own face; false when only own faces remain
        for (size_t i = list.size(); i > 0; --i)
        {
            if (!own_keys.count(list[i - 1]))
            {
                list.erase(list.begin() + (i - 1));
                return true;
            }
        }
        return false;
    };
    auto sumCost = [&](const std::vector<uint32_t>& list) {
        float ms = 0.f;
        for (uint32_t k : list)
        {
            ms += dyn_cost[k];
        }
        return ms;
    };
    if (left <= 0.f && !progress_ok)
    {
        out.dynamic = selectDynamic(d, 0.f, a_max, false); // only the own avatar's faces run past an exhausted cap
        out.predicted_ms = sumCost(out.dynamic);
        return out;
    }
    out.dynamic = selectDynamic(d, dynamicShareMs(!statics.empty(), left) + kEps, a_max, progress_ok);
    float dyn_ms = sumCost(out.dynamic);
    if (dyn_ms > left + kEps && !(progress_ok && out.dynamic.size() == 1))
    {
        // an over-cap item may only run alone, as the progress pick; the own avatar's faces stay (they are exempt)
        while (dyn_ms > left + kEps && dropLastOther(out.dynamic))
        {
            dyn_ms = sumCost(out.dynamic);
        }
    }
    left -= dyn_ms;
    uint32_t dyn_avatars = 0;
    for (uint32_t k : out.dynamic)
    {
        dyn_avatars += dyn_draws[k];
    }
    // the static schedule is never starved by the dynamic one: its first candidate (the order of selectStatic: never-drawn nearest,
    // then oldest) is guaranteed a place every frame when it fits the cap at all; the dynamic layer gives up its last faces for it
    if (!st.empty())
    {
        const std::vector<uint32_t> head = selectStatic(st, 0.f);
        const float head_ms = head.empty() ? 0.f : st_cost[head[0]];
        const uint32_t head_draws = head.empty() ? 0u : st_draws[head[0]];
        if (head_ms <= cap_ms + kEps)
        {
            while (head_ms > left + kEps && !out.dynamic.empty())
            {
                size_t i = out.dynamic.size();
                while (i > 0 && own_keys.count(out.dynamic[i - 1]))
                {
                    --i; // an own face is never given up
                }
                if (i == 0)
                {
                    break; // only own faces remain: the head waits for a frame with room
                }
                dyn_ms -= dyn_cost[out.dynamic[i - 1]];
                dyn_avatars -= dyn_draws[out.dynamic[i - 1]];
                out.dynamic.erase(out.dynamic.begin() + (i - 1));
                left = cap_ms - dyn_ms;
            }
        }
        else if (progress_ok && !head.empty())
        {
            // the head alone is above the cap: it takes the progress pick once it has waited (or has never been drawn)
            for (const StaticItem& it : st)
            {
                if (it.key == head[0] && (it.never_drawn || it.age_frames >= kDynamicDeadlineFrames))
                {
                    // the other dynamic picks give way; the own avatar's faces stay and the head runs beside them (a bounded overrun
                    // of that one item, so neither guarantee starves the other)
                    while (dropLastOther(out.dynamic))
                    {
                    }
                    dyn_ms = sumCost(out.dynamic);
                    dyn_avatars = 0;
                    for (uint32_t k : out.dynamic)
                    {
                        dyn_avatars += dyn_draws[k];
                    }
                    left = out.dynamic.empty() ? cap_ms : 0.f;
                    break;
                }
            }
        }
        // the head's avatar draws count against A_max too, but only where the head will be served (its CPU fits the cap, or it took the
        // progress pick above): the dynamic layer gives up its last avatar faces for it (head_draws <= a_max); faces without avatar draws
        // free nothing and stay
        if (head_ms <= cap_ms + kEps || out.dynamic.empty())
        {
            while (head_draws + dyn_avatars > a_max && dyn_avatars > 0)
            {
                size_t i = out.dynamic.size();
                while (i > 0 && (dyn_draws[out.dynamic[i - 1]] == 0 || own_keys.count(out.dynamic[i - 1])))
                {
                    --i;
                }
                if (i == 0)
                {
                    break; // only own faces hold the avatar draws: the head waits
                }
                const uint32_t k = out.dynamic[i - 1];
                dyn_ms -= dyn_cost[k];
                dyn_avatars -= dyn_draws[k];
                out.dynamic.erase(out.dynamic.begin() + (i - 1));
                left = cap_ms - dyn_ms;
            }
        }
    }
    // an overdue head that has no room beside the own avatar's faces runs beside them as the progress pick (one bounded overrun)
    bool head_beside_own = false;
    if (progress_ok && !st.empty() && !out.dynamic.empty())
    {
        const std::vector<uint32_t> head = selectStatic(st, 0.f);
        if (!head.empty() && st_cost[head[0]] > left + kEps)
        {
            for (const StaticItem& it : st)
            {
                head_beside_own = head_beside_own || (it.key == head[0] && (it.never_drawn || it.age_frames >= kDynamicDeadlineFrames));
            }
        }
    }
    out.fixed = selectStatic(st, std::max(left, 0.f) + kEps, a_max - dyn_avatars);
    float st_ms = 0.f;
    for (uint32_t k : out.fixed)
    {
        st_ms += st_cost[k];
    }
    if (st_ms > left + kEps && !(progress_ok && (out.dynamic.empty() || head_beside_own) && out.fixed.size() == 1))
    {
        // over what is left: only the single progress pick may be; any other overrun is cut back to what fits
        while (!out.fixed.empty() && st_ms > left + kEps)
        {
            st_ms -= st_cost[out.fixed.back()];
            out.fixed.pop_back();
        }
    }
    out.predicted_ms = dyn_ms + st_ms;
    return out;
}

// The frame's spending, shared by every pool that runs a tile render in it (main thread only): a pool reads what the pools before it
// spent and takes its cap from the rest.
struct FrameLedger
{
    uint32_t frame = 0;
    bool have = false;
    float spent_ms = 0.f;
    void begin(uint32_t f)
    {
        if (!have || f != frame)
        {
            frame = f;
            have = true;
            spent_ms = 0.f;
        }
    }
    void add(float ms) { spent_ms += ms; }
    float left(float cap_ms) const { return cap_ms - spent_ms; }
    bool untouched() const { return spent_ms <= 0.f; }
};

// The in-frame gate of one pick under the shared cap: it runs while the measured time left is positive. The one exemption is the first
// static pick of a frame in which nothing else rendered (`alone`: ledger untouched and no dynamic picks): the selection kept it inside
// the cap, or it is the progress pick. A first static pick behind spent dynamic work or an earlier cache is refused.
inline bool mayRunPick(float left_ms, bool dynamic, bool first_pick, bool alone, bool own = false)
{
    return left_ms > 0.f || own || (!dynamic && first_pick && alone);
}

// the one ledger of the process (main thread only); a pool begins the frame with its frame count and adds what its renders measure
inline FrameLedger& frameLedger()
{
    static FrameLedger ledger;
    return ledger;
}

// The lag of the avatar layer (P4): how many avatar faces (a lamp's tile with an avatar in its light is one face) a frame can draw,
// and so how long the last face of a set waits. A frame draws min(A_max left after the faces drawn every frame, the dynamic CPU
// share / the cost of one face) faces (the share is kDynamicCapMs while a static tile waits, else the whole cap); the scheduler floors
// the cost of an item at kDefaultItemMs.
inline uint32_t layerFacesPerFrame(float dynamic_cap_ms, float per_face_ms, uint32_t a_max, uint32_t every_frame_draws)
{
    const uint32_t spare = a_max > every_frame_draws ? a_max - every_frame_draws : 0u;
    const float cost = itemCost(per_face_ms);
    const float by_cpu = std::floor(dynamic_cap_ms / cost + 1e-4f);
    const uint32_t cpu_faces = by_cpu > 0.f ? uint32_t(by_cpu) : 1u; // the progress pick: at least one face runs
    return std::min(spare, cpu_faces);
}

// Frames until every one of `faces` faces has been drawn once, at `per_frame` a frame.
inline uint32_t layerLagFrames(uint32_t faces, uint32_t per_frame)
{
    return per_frame == 0 ? 0u : (faces + per_frame - 1) / per_frame;
}

// The change feed's owners (llshadowdirtyfeed.h): which caches track, and the lamp roots each keeps, merged for the lookup. GL-free
// and viewer-free so the unit test checks the rules. Each owner is one bit; owner indices 0 and 1 today.
constexpr uint32_t kOwnerBits = 2;

enum class OwnerEdge { Neither, First, Last };

// the mask after an owner switches on or off, and the edge the feed acts on: First (the first owner on: the hooks start recording),
// Last (the last one off: nobody reads the pending boxes any more: drop them). Another owner leaving changes neither.
inline OwnerEdge ownerEdge(uint32_t was, uint32_t now)
{
    return (was == 0 && now != 0) ? OwnerEdge::First : ((was != 0 && now == 0) ? OwnerEdge::Last : OwnerEdge::Neither);
}

inline uint32_t ownerMaskAfter(uint32_t mask, uint32_t owner_bit, bool on)
{
    return on ? (mask | owner_bit) : (mask & ~owner_bit);
}

template <class T>
struct OwnerLists
{
    std::vector<T> by[kOwnerBits];
    std::vector<T> merged; // the sorted union

    // replaces one owner's list (sorted and de-duplicated in place); true when it changed
    bool set(uint32_t owner, std::vector<T>& list)
    {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
        if (by[owner] == list)
        {
            return false;
        }
        by[owner].swap(list);
        rebuild();
        return true;
    }
    void clear(uint32_t owner)
    {
        by[owner].clear();
        rebuild();
    }
    void clearAll()
    {
        for (auto& v : by)
        {
            v.clear();
        }
        merged.clear();
    }
    bool contains(const T& v) const { return std::binary_search(merged.begin(), merged.end(), v); }

private:
    void rebuild()
    {
        merged.clear();
        for (const auto& v : by)
        {
            merged.insert(merged.end(), v.begin(), v.end());
        }
        std::sort(merged.begin(), merged.end());
        merged.erase(std::unique(merged.begin(), merged.end()), merged.end());
    }
};

// The frame's one save and restore of the GL state the tile renders change: the first scope saves, the last restores (a second
// pool's update inside the same frame scope neither saves nor restores again).
struct ScopeCounter
{
    int depth = 0;
    bool enter() { return depth++ == 0; }
    bool leave() { return --depth == 0; }
};

// The prediction of a record that has never been drawn: the running average of tile renders, but at most a quarter of the
// static cap, so that the first average (a placeholder above the cap) does not let a cold start (login, teleport) through
// one tile per frame; the first timed render replaces it with the record's own cost.
inline float coldItemMs(float cost_ema_ms)
{
    return std::min(itemCost(cost_ema_ms), kFrameCapMs / 4.f);
}

} // namespace llprojectorpool

#endif // LL_LLPROJECTORSHADOWPOOL_H
