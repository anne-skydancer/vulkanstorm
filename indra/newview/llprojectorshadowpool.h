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
constexpr float kEmitterMin = 0.02f;        // metres: the emitter extent's lower limit (a point has no edge to render)
constexpr float kEmitterMax = 0.15f;        // metres: the upper limit, the constant of P2: no lamp is softer than before

// clamp(ST_SPOT_RADIUS_MAX_M * k * N / w, 1, 12)
inline float radiusCapTexels(float k, uint32_t n, float w)
{
    return std::min(std::max(kRadiusMaxM * k * (float)n / w, kKernelTexelsMin), kKernelTexelsMax);
}

// the emitter's extent across the beam: the prim's face, clamped (a bad value gets the lower limit)
inline float emitterExtent(float face)
{
    return std::isfinite(face) ? std::min(std::max(face, kEmitterMin), kEmitterMax) : kEmitterMin;
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

// The rectangular pyramid of an SL projector (square tile: aspect 1). origin and the unit axes
// are in the same space as the boxes (agent space); fov is the full angle in radians; the planes
// run from `near_d` (> 0) to `far_d` (the light's radius) along the forward axis.
inline Pyramid makePyramid(const float origin[3], const float fwd[3], const float right[3], const float up[3],
                           float fov, float near_d, float far_d)
{
    const float t = std::tan(0.5f * fov);
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
                const float x = (xi ? 1.f : -1.f) * t * z;
                const float y = (yi ? 1.f : -1.f) * t * z;
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

// Dirty boxes, double buffered (T6): pushes go to the fill buffer; swapForApply() hands the
// filled buffer to the frame and starts an empty one, so a push made while tiles render lands
// in the next frame and is never cleared after a render (F9).
class DirtyList
{
public:
    void push(const Aabb& box) { mFill.push_back(box); }

    // Moves the pending boxes out for this frame; the returned reference stays valid until the
    // next swapForApply().
    const std::vector<Aabb>& swapForApply()
    {
        mApply.swap(mFill);
        mFill.clear();
        return mApply;
    }

    // shiftObjects moves agent space: translate pending (not yet applied) boxes
    void translatePending(float dx, float dy, float dz)
    {
        const float d[3] = { dx, dy, dz };
        for (Aabb& b : mFill)
        {
            for (int i = 0; i < 3; ++i)
            {
                b.lo[i] += d[i];
                b.hi[i] += d[i];
            }
        }
    }

    size_t pending() const { return mFill.size(); }
    void clear() { mFill.clear(); mApply.clear(); }

private:
    std::vector<Aabb> mFill, mApply;
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
// Scheduler ordering (section 4.5)
// ---------------------------------------------------------------------------------------------

constexpr uint32_t kDynamicDeadlineFrames = 4;

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
    bool pending = false;       // dirty or lowres (ignored when never_drawn)
    float stale_ms = 0.f;       // time since dirty_frame
    float cam_dist = 0.f;
    float last_cpu_ms = 0.f;    // prediction of the cost of rendering it
    uint32_t age_frames = 0;    // frames since it was last drawn (the order of service)
};

// Ordered picks for this frame: never-drawn nearest first, then the pending items oldest first (age in frames; the
// distance only breaks a tie), until the CPU cap t_s_ms would be exceeded. The order is round-robin by staleness, so
// no tile waits longer than ceil(N / tiles per frame) frames, whatever its distance. At least one item runs while
// any is pending (progress), even when its predicted cost alone exceeds the cap.
inline std::vector<uint32_t> selectStatic(const std::vector<StaticItem>& items, float t_s_ms)
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
    for (const auto* list : { &never, &rest })
    {
        for (const StaticItem* it : *list)
        {
            const float cost = itemCost(it->last_cpu_ms);
            if (!out.empty() && spent + cost > t_s_ms)
            {
                return out; // over the cap: the order is kept, the rest waits
            }
            out.push_back(it->key);
            spent += cost;
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
};

// Dynamic picks: items older than the 4-frame deadline first (oldest first), then nearest first.
// Budget: CPU cap t_d_ms and at most a_max avatar draws (an item with more avatars draws its
// nearest a_max). No floor beyond that, with one exception that prevents permanent starvation:
// when nothing has been picked yet, an item past the deadline runs even if its predicted cost
// alone exceeds the cap (one such item per frame at most). Other items over budget keep their
// last content.
inline std::vector<uint32_t> selectDynamic(const std::vector<DynamicItem>& items, float t_d_ms, uint32_t a_max)
{
    std::vector<const DynamicItem*> order;
    for (const DynamicItem& it : items)
    {
        order.push_back(&it);
    }
    std::sort(order.begin(), order.end(), [](const DynamicItem* a, const DynamicItem* b) {
        const bool da = a->age_frames >= kDynamicDeadlineFrames, db = b->age_frames >= kDynamicDeadlineFrames;
        if (da != db)
        {
            return da;
        }
        if (da && a->age_frames != b->age_frames)
        {
            return a->age_frames > b->age_frames;
        }
        const float ca = sortDist(a->cam_dist), cb = sortDist(b->cam_dist);
        return ca != cb ? ca < cb : a->key < b->key;
    });
    std::vector<uint32_t> out;
    float spent = 0.f;
    uint32_t avatars = 0;
    for (const DynamicItem* it : order)
    {
        const float cost = itemCost(it->last_cpu_ms);
        const uint32_t draws = it->avatar ? std::min(std::max(it->avatar_draws, 1u), a_max) : 0u;
        const bool overdue = it->age_frames >= kDynamicDeadlineFrames && out.empty();
        if ((spent + cost > t_d_ms && !overdue) || avatars + draws > a_max)
        {
            continue; // skip this one; a cheaper one further down may still fit
        }
        out.push_back(it->key);
        spent += cost;
        avatars += draws;
    }
    return out;
}

// The static cap: a fixed CPU budget of the pool's tile renders per frame, in milliseconds. 0.35 ms is 4.4 % of an 8 ms
// frame: it fits four measured tiles (0.07 ms each, owner session 2026-11-14) with a margin, and leaves 0.05 ms of the
// +5 % light and shadow budget (about 0.4 ms) to the dynamic tiles. The measured cost only predicts what fits; it does not
// set the budget.
constexpr float kStaticCapMs = 0.35f;
// Placeholders until S0 measures (spec 4.5); a devel-only debug override may replace them
constexpr float kDynamicCapMs = 0.30f;
constexpr uint32_t kAvatarDrawsMax = 24;

} // namespace llprojectorpool

#endif // LL_LLPROJECTORSHADOWPOOL_H
