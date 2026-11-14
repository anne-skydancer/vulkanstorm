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
    Avatar,     // the tile holds an avatar (drawn into S until P4), or the avatar just left
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

// A change of an object that is a mover (or physical), or whose root is, never dirties a tile: it is not in the fixed layer, and a
// moving part inside a still linkset casts with the pose of the last render (its moves never invalidate: the known limit).
inline bool hookSkips(const ObjectFlags& obj, const ObjectFlags& root)
{
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

// Only a rebuild that can change what a tile shows dirties it: the volume, the position or the rigging. A texture
// coordinate or colour rebuild (texture animation, a tint) never does (F4).
inline bool rebuildDirties(uint32_t flags, uint32_t volume_position_rigged_mask)
{
    return (flags & volume_position_rigged_mask) != 0;
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

    size_t heldBy(uint32_t tile) const
    {
        auto it = mTiles.find(tile);
        return it == mTiles.end() ? 0 : it->second.held.size();
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
    bool holds_avatar = false;    // an avatar's extents intersect the light's volume this frame
    bool drawn_with_avatar = false; // its last draw included an avatar (its picture has one baked in until P4)
};

struct TileVerdict
{
    bool needs_render = false;
    bool dynamic = false;         // scheduled with the dynamic items (a moving lamp or an avatar under it), else with the static ones
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
    v.dynamic = f.lamp_moving || f.holds_avatar;
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
    else if (f.holds_avatar || f.drawn_with_avatar)
    {
        v.needs_render = true; // an avatar in the light is redrawn every frame; the frame after it left, once more to clean the picture
        v.cause = Cause::Avatar;
    }
    return v;
}

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
    bool pending = false;       // needs a render (ignored when never_drawn; P2 has no change tracking, so every placed tile)
    float cam_dist = 0.f;
    float last_cpu_ms = 0.f;    // prediction of the cost of rendering it
    uint32_t age_frames = 0;    // frames since it became stale (P3: since the first dirty box that touched it): the order of service
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
// frame (the basis is an assumed 8 ms frame, 125 fps, not a measured one): it fits four measured tiles (0.07 ms each,
// owner session 2026-11-14) with a margin. The +5 % light and shadow budget is about 0.4 ms of that frame, so 0.05 ms
// is left, and kDynamicCapMs below does not fit in it: P4 must re-divide the cap between the static and dynamic tiles
// (it is not claimed to fit today; no dynamic tile is drawn before P4). The measured cost only predicts what fits; it
// does not set the budget.
constexpr float kStaticCapMs = 0.35f;
// Placeholder until S0 measures (spec 4.5); a devel-only debug override may replace it
constexpr float kDynamicCapMs = 0.30f;
constexpr uint32_t kAvatarDrawsMax = 24;

// The prediction of a record that has never been drawn: the running average of tile renders, but at most a quarter of the
// static cap, so that the first average (a placeholder above the cap) does not let a cold start (login, teleport) through
// one tile per frame; the first timed render replaces it with the record's own cost.
inline float coldItemMs(float cost_ema_ms)
{
    return std::min(itemCost(cost_ema_ms), kStaticCapMs / 4.f);
}

} // namespace llprojectorpool

#endif // LL_LLPROJECTORSHADOWPOOL_H
