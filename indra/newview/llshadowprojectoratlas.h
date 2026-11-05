// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llshadowprojectoratlas.h
 * @brief Projector shadow tiles: a camera-independent atlas with caching and an update budget
 *        (doc/shadows/shadow-model-rework.md §16, stage S7b).
 *
 * Renderer-free (no GL, no viewer types; lights are identified by an opaque pointer), so the
 * future Vulkan renderer uses it unchanged; indra/newview/tests/llshadowprojectoratlas_test.cpp
 * checks it, including the scenarios that failed in the S7 build (turning among projectors, lights
 * partly off-screen, turning away and back).
 *
 * The rule: every shadow-casting projector whose cone reaches within the shadow distance of the
 * camera POSITION has a tile and casts its shadow, whatever way the camera looks. Nothing here
 * takes the camera's orientation. A tile is rendered once and kept until the projector changes,
 * a caster inside its cone changes, or a safety timer expires; at most `max_renders_per_frame`
 * tiles are re-rendered per frame, ranked so that new tiles come first and the rest by
 * importance x age, so many moving lights cannot spike the frame time and none starves.
 *
 * Ideas from the Tabula Rasa deferred renderer (GPU Gems 3, ch. 19): a pool of shadow maps
 * assigned by priority and requested size, static maps kept, dynamic ones regenerated only when
 * something dynamic is inside the light's volume, one or two rebuilds per frame.
 */

#ifndef LL_LLSHADOWPROJECTORATLAS_H
#define LL_LLSHADOWPROJECTORATLAS_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace LLShadowProjectorAtlas
{
    struct Vec3
    {
        float x = 0.f, y = 0.f, z = 0.f;
    };

    inline float dist2(const Vec3& a, const Vec3& b)
    {
        const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
        return dx * dx + dy * dy + dz * dz;
    }

    // ---- configuration (per quality tier) --------------------------------------------------
    struct Config
    {
        int atlas_w = 3072;                 // texels; multiples of root
        int atlas_h = 3072;
        int root = 1024;                    // the largest tile, and the allocator's root cell
        int min_tile = 128;
        int border = 8;                     // texels around each tile's map, wider than the filter
        int max_lights = 12;                // projector shadows at once
        int max_renders_per_frame = 2;
        float shadow_distance = 128.f;      // metres: RenderShadowDistance
        float avatar_range = 25.f;          // avatars cast into a tile only within this camera distance
        float safety_refresh = 2.f;         // seconds: every tile is re-rendered at least this often
        float fade_in = 0.25f;              // seconds, after a tile's first render
        float fade_out = 0.25f;
        float evict_ratio = 1.5f;           // a newcomer must outscore the weakest holder by this
        float range_slack = 1.1f;           // a tile is dropped beyond this x the shadow distance
        float resize_dwell = 3.f;           // seconds between size changes of a tile
        float texels_per_pixel = 1.f;       // tile size target: shadow texels per screen pixel of the lit area
    };

    // ---- per-frame input ----------------------------------------------------------------------
    struct LightInput
    {
        const void* id = nullptr;
        Vec3 pos;                           // the projector's position
        Vec3 axis;                          // unit vector of the cone's axis
        float radius = 0.f;                 // range, metres
        float fov = 1.f;                    // the cone's full angle, radians
        float lum = 1.f;                    // luminance of the light's colour
        std::uint64_t fingerprint = 0;      // changes whenever the light's transform, size, fov, texture change
        bool static_dirty = false;          // a static caster changed inside the cone
        bool dynamic_dirty = false;         // a dynamic, non-avatar caster changed inside the cone
        bool avatar_dirty = false;          // an avatar (or attachment) changed inside the cone
    };

    struct CameraInput
    {
        Vec3 pos;
        float pixels_per_radian = 1000.f;   // screen height in pixels over the vertical field of view
    };

    // ---- the allocator: a buddy quadtree over a grid of root cells -----------------------------
    class Allocator
    {
    public:
        struct Block
        {
            int x = -1, y = -1, size = 0;
            bool valid() const { return size > 0; }
        };

        Allocator() = default;
        Allocator(int w, int h, int root, int min_size) { reset(w, h, root, min_size); }

        void reset(int w, int h, int root, int min_size)
        {
            m_root = root;
            m_min = min_size;
            m_levels = 0;
            for (int s = root; s >= min_size; s >>= 1)
            {
                ++m_levels;
            }
            m_free.assign(m_levels, {});
            for (int y = 0; y + root <= h; y += root)
            {
                for (int x = 0; x + root <= w; x += root)
                {
                    m_free[0].push_back({ x, y, root });
                }
            }
        }

        // The smallest power-of-two size from min to root that holds `wanted` texels.
        int roundSize(int wanted) const
        {
            int s = m_min;
            while (s < wanted && s < m_root)
            {
                s <<= 1;
            }
            return s;
        }

        Block alloc(int size)
        {
            const int level = levelOf(size);
            if (level < 0)
            {
                return {};
            }
            // find the smallest free block at or above this size, splitting down
            int from = level;
            while (from >= 0 && m_free[from].empty())
            {
                --from;
            }
            if (from < 0)
            {
                return {};
            }
            Block b = m_free[from].back();
            m_free[from].pop_back();
            while (from < level)
            {
                const int half = b.size >> 1;
                ++from;
                m_free[from].push_back({ b.x + half, b.y, half });
                m_free[from].push_back({ b.x, b.y + half, half });
                m_free[from].push_back({ b.x + half, b.y + half, half });
                b.size = half;
            }
            return b;
        }

        void free(const Block& block)
        {
            if (!block.valid())
            {
                return;
            }
            Block b = block;
            int level = levelOf(b.size);
            while (level > 0)
            {
                const int parent_size = b.size << 1;
                const int px = (b.x / parent_size) * parent_size;
                const int py = (b.y / parent_size) * parent_size;
                // the three siblings must all be free to merge
                std::vector<int> idx;
                bool all = true;
                for (int qy = 0; qy < 2 && all; ++qy)
                {
                    for (int qx = 0; qx < 2 && all; ++qx)
                    {
                        const int sx = px + qx * b.size, sy = py + qy * b.size;
                        if (sx == b.x && sy == b.y)
                        {
                            continue;
                        }
                        int found = -1;
                        for (size_t i = 0; i < m_free[level].size(); ++i)
                        {
                            if (m_free[level][i].x == sx && m_free[level][i].y == sy)
                            {
                                found = (int)i;
                                break;
                            }
                        }
                        if (found < 0)
                        {
                            all = false;
                        }
                        else
                        {
                            idx.push_back(found);
                        }
                    }
                }
                if (!all)
                {
                    break;
                }
                std::sort(idx.rbegin(), idx.rend());
                for (int i : idx)
                {
                    m_free[level].erase(m_free[level].begin() + i);
                }
                b = { px, py, parent_size };
                --level;
            }
            m_free[levelOf(b.size)].push_back(b);
        }

        int freeTexels() const
        {
            long long total = 0;
            for (int l = 0; l < m_levels; ++l)
            {
                total += (long long)m_free[l].size() * (m_root >> l) * (m_root >> l);
            }
            return (int)std::min<long long>(total, 0x7fffffff);
        }

    private:
        int levelOf(int size) const
        {
            if (size > m_root || size < m_min)
            {
                return -1;
            }
            int level = 0;
            for (int s = m_root; s > size; s >>= 1)
            {
                ++level;
            }
            return (m_root >> level) == size ? level : -1;
        }

        int m_root = 0, m_min = 0, m_levels = 0;
        std::vector<std::vector<Block>> m_free;
    };

    // ---- the tile state -------------------------------------------------------------------------
    struct Tile
    {
        const void* id = nullptr;
        Allocator::Block block;             // the tile's square in the atlas
        std::uint64_t fingerprint = 0;
        float importance = 0.f;
        double last_render = -1.0;
        double size_changed = 0.0;
        bool valid = false;                 // has been rendered since it was placed
        bool fading_out = false;
        float fade = 0.f;                   // 0..1 shadow strength (fade-in, fade-out); 0 while !valid
        bool include_avatars = false;
    };

    struct Render
    {
        const void* id;
        int x, y, size;                     // the tile
        int border;
        bool include_avatars;
        bool first;                         // the tile's first render since it was placed
    };

    // Projector shadow weight by distance from the camera: full out to 80 % of the shadow
    // distance, gone at the shadow distance. Euclidean: independent of the view direction.
    inline float distanceFade(float distance, float shadow_distance)
    {
        const float f = (shadow_distance - distance) / (0.2f * shadow_distance);
        return f < 0.f ? 0.f : (f > 1.f ? 1.f : f);
    }

    // Vertical field of view for a tile's shadow map so that the cone fills the map inside the
    // border: tan(fov_render / 2) = tan(fov / 2) x size / (size - 2 border).
    inline float renderFov(float fov, int size, int border)
    {
        const float k = (float)size / (float)(size - 2 * border);
        return 2.f * std::atan(std::tan(0.5f * fov) * k);
    }

    // ---- the atlas ------------------------------------------------------------------------------
    class Atlas
    {
    public:
        explicit Atlas(const Config& cfg = Config()) { configure(cfg); }

        void configure(const Config& cfg)
        {
            m_cfg = cfg;
            m_alloc.reset(cfg.atlas_w, cfg.atlas_h, cfg.root, cfg.min_tile);
            m_tiles.clear();
            m_renders.clear();
        }

        // The cached maps are no longer valid (the sun/device changed, the atlas was reallocated).
        void invalidateAll()
        {
            for (Tile& t : m_tiles)
            {
                t.valid = false;
                t.fade = 0.f;
            }
        }

        // One frame. now: seconds; dt: seconds since the last frame. After the call, renders()
        // lists the tiles to render this frame (before lighting) and tiles() their state.
        void update(const CameraInput& cam, const std::vector<LightInput>& lights, float dt, double now)
        {
            m_renders.clear();

            // light lookup by id
            std::unordered_map<const void*, const LightInput*> by_id;
            by_id.reserve(lights.size());
            for (const LightInput& l : lights)
            {
                by_id[l.id] = &l;
            }

            // 1. tiles of lights that vanished or are out of range start to fade out; faded ones are freed
            for (size_t i = 0; i < m_tiles.size();)
            {
                Tile& t = m_tiles[i];
                auto it = by_id.find(t.id);
                bool gone = it == by_id.end();
                if (!gone)
                {
                    const LightInput& l = *it->second;
                    const float reach = std::sqrt(dist2(cam.pos, l.pos)) - l.radius;
                    gone = reach > m_cfg.shadow_distance * m_cfg.range_slack || l.radius <= 0.001f || l.lum <= 0.f;
                }
                t.fading_out = gone;
                if (!gone)
                {
                    t.importance = importance(cam, *it->second);
                }
                if (gone && t.fade <= 0.f)
                {
                    m_alloc.free(t.block);
                    m_tiles.erase(m_tiles.begin() + i);
                    continue;
                }
                ++i;
            }

            // 2. candidates: every light whose cone reaches within the shadow distance of the camera
            struct Cand
            {
                const LightInput* l;
                float importance;
                float dist;
            };
            std::vector<Cand> cands;
            for (const LightInput& l : lights)
            {
                const float d = std::sqrt(dist2(cam.pos, l.pos));
                if (d - l.radius >= m_cfg.shadow_distance || l.radius <= 0.001f || l.lum <= 0.f)
                {
                    continue;
                }
                cands.push_back({ &l, importance(cam, l), d });
            }
            std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.importance > b.importance; });
            if ((int)cands.size() > m_cfg.max_lights)
            {
                cands.resize(m_cfg.max_lights);
            }

            // 3. place tiles: existing ones are kept; new ones get the size their projected size asks
            // for, but no more than their share of the atlas (so that every candidate fits: 3/4 of
            // the area split evenly, rounded down to a power of two; few lights get big tiles)
            const int share = tileShare((int)cands.size());
            for (const Cand& c : cands)
            {
                Tile* t = findTile(c.l->id);
                const int wanted = std::min(m_alloc.roundSize((int)std::ceil(wantedTexels(cam, *c.l, c.dist))), share);
                if (t)
                {
                    t->importance = c.importance;
                    t->fading_out = false;
                    // resize by at least a factor of two, after a dwell
                    if ((wanted >= t->block.size * 2 || wanted * 4 <= t->block.size) && now - t->size_changed >= m_cfg.resize_dwell)
                    {
                        Allocator::Block nb = m_alloc.alloc(wanted);
                        if (nb.valid())
                        {
                            m_alloc.free(t->block);
                            t->block = nb;
                            t->valid = false;
                            t->fade = 0.f;
                            t->size_changed = now;
                        }
                    }
                    continue;
                }
                Allocator::Block b = placeTile(wanted, c.importance);
                if (!b.valid())
                {
                    continue;
                }
                Tile nt;
                nt.id = c.l->id;
                nt.block = b;
                nt.importance = c.importance;
                nt.size_changed = now;
                m_tiles.push_back(nt);
            }

            // 4. which tiles are dirty, and in which class
            struct Dirty
            {
                Tile* t;
                int klass;                  // 0 not rendered yet, 1 changed, 2 safety refresh
                float score;
                bool avatars;
            };
            std::vector<Dirty> dirty;
            for (Tile& t : m_tiles)
            {
                if (t.fading_out)
                {
                    continue;
                }
                const LightInput& l = *by_id[t.id];
                const bool avatars = std::sqrt(dist2(cam.pos, l.pos)) <= m_cfg.avatar_range;
                const double age = t.last_render < 0.0 ? 1.0e9 : now - t.last_render;
                int klass = -1;
                if (!t.valid)
                {
                    klass = 0;
                }
                else if (l.fingerprint != t.fingerprint || l.static_dirty || l.dynamic_dirty ||
                         (l.avatar_dirty && avatars) || avatars != t.include_avatars)
                {
                    klass = 1;
                }
                else if (age >= m_cfg.safety_refresh)
                {
                    klass = 2;
                }
                if (klass >= 0)
                {
                    dirty.push_back({ &t, klass, std::sqrt(t.importance) * (0.05f + (float)std::min(age, 60.0)), avatars });
                }
            }
            std::sort(dirty.begin(), dirty.end(), [](const Dirty& a, const Dirty& b) {
                if (a.klass != b.klass)
                {
                    return a.klass < b.klass;
                }
                return a.score > b.score;
            });

            // 5. render at most N; the first-time tiles go first and the budget is shared by the rest
            int budget = m_cfg.max_renders_per_frame;
            for (const Dirty& d : dirty)
            {
                if (budget <= 0)
                {
                    break;
                }
                // (changed tiles sort before safety refreshes, so a refresh only uses what is left)
                Tile& t = *d.t;
                const LightInput& l = *by_id[t.id];
                m_renders.push_back({ t.id, t.block.x, t.block.y, t.block.size, m_cfg.border, d.avatars, !t.valid });
                t.fingerprint = l.fingerprint;
                t.last_render = now;
                t.include_avatars = d.avatars;
                t.valid = true;
                --budget;
            }

            // 6. fades
            for (Tile& t : m_tiles)
            {
                if (t.fading_out)
                {
                    t.fade = std::max(0.f, t.fade - dt / std::max(m_cfg.fade_out, 1.0e-3f));
                }
                else if (t.valid)
                {
                    t.fade = std::min(1.f, t.fade + dt / std::max(m_cfg.fade_in, 1.0e-3f));
                }
                else
                {
                    t.fade = 0.f;
                }
            }
        }

        const std::vector<Tile>& tiles() const { return m_tiles; }
        const std::vector<Render>& renders() const { return m_renders; }
        const Config& config() const { return m_cfg; }

        const Tile* find(const void* id) const
        {
            for (const Tile& t : m_tiles)
            {
                if (t.id == id)
                {
                    return &t;
                }
            }
            return nullptr;
        }

        int freeTexels() const { return m_alloc.freeTexels(); }

    private:
        Tile* findTile(const void* id)
        {
            for (Tile& t : m_tiles)
            {
                if (t.id == id)
                {
                    return &t;
                }
            }
            return nullptr;
        }

        // The largest tile a candidate may take when n lights share the atlas.
        int tileShare(int n) const
        {
            const double area = 0.75 * (double)m_cfg.atlas_w * (double)m_cfg.atlas_h / (double)std::max(n, 1);
            int s = m_cfg.root;
            while (s > m_cfg.min_tile && (double)s * (double)s > area)
            {
                s >>= 1;
            }
            return s;
        }

        // Power of the light reaching the camera's surroundings: luminance x radius^2 x the cone's
        // solid angle over the squared distance from the camera position to the middle of the cone.
        static float importance(const CameraInput& cam, const LightInput& l)
        {
            const Vec3 mid = { l.pos.x + l.axis.x * l.radius * 0.5f, l.pos.y + l.axis.y * l.radius * 0.5f,
                               l.pos.z + l.axis.z * l.radius * 0.5f };
            const float d2 = std::max(dist2(cam.pos, mid), 1.f);
            const float solid = 6.2831853f * (1.f - std::cos(0.5f * l.fov));
            return l.lum * l.radius * l.radius * solid / d2;
        }

        // Shadow texels asked for: the lit area's width on screen, in pixels, from its nearest point.
        float wantedTexels(const CameraInput& cam, const LightInput& l, float cam_dist) const
        {
            const float width = 2.f * l.radius * std::tan(0.5f * std::min(l.fov, 3.0f));
            const float d = std::max(cam_dist - l.radius * 0.5f, 1.f);
            return std::max(width / d * cam.pixels_per_radian * m_cfg.texels_per_pixel, (float)m_cfg.min_tile);
        }

        // Allocate a tile; if the atlas is full, try smaller sizes, then evict the weakest holder
        // if the newcomer outscores it by the hysteresis.
        Allocator::Block placeTile(int wanted, float importance)
        {
            for (int pass = 0; pass < 2; ++pass)
            {
                for (int s = wanted; s >= m_cfg.min_tile; s >>= 1)
                {
                    Allocator::Block b = m_alloc.alloc(s);
                    if (b.valid())
                    {
                        return b;
                    }
                }
                if (pass == 1)
                {
                    break;
                }
                // evict the weakest holder that the newcomer clearly outscores
                int weakest = -1;
                for (size_t i = 0; i < m_tiles.size(); ++i)
                {
                    if (weakest < 0 || m_tiles[i].importance < m_tiles[weakest].importance)
                    {
                        weakest = (int)i;
                    }
                }
                if (weakest < 0 || importance <= m_tiles[weakest].importance * m_cfg.evict_ratio)
                {
                    break;
                }
                m_alloc.free(m_tiles[weakest].block);
                m_tiles.erase(m_tiles.begin() + weakest);
            }
            return {};
        }

        Config m_cfg;
        Allocator m_alloc;
        std::vector<Tile> m_tiles;
        std::vector<Render> m_renders;
    };
}

#endif // LL_LLSHADOWPROJECTORATLAS_H
