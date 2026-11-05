// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowprojectoratlas.h (no viewer libraries):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowprojectoratlas_test.cpp
//
// The scenarios that failed in the S7 build and in the viewer before it: turning in place among
// 1, 2, 3 and 5 projectors, projector volumes partly off-screen, turning away and back. A shadow
// counts as present in a frame when the tile is rendered and fully faded in. "Drawn" means the
// projector's light box is inside the camera's frustum, i.e. the lit area can be seen. The old
// Firestorm selection (two slots re-picked every frame from the drawn projectors by the pixel
// area of a sphere along the camera's axis, fading at the camera's speed) is ported here for
// contrast; it must fail where the atlas must not.
// Also: the allocator's invariants, the update budget (a static room costs nothing at rest, a
// club of moving lights never exceeds the per-frame limit and no tile starves), eviction
// hysteresis, range handling and the avatar rule.

#include "llshadowprojectoratlas.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>

using namespace LLShadowProjectorAtlas;

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

    const float PI = 3.14159265f;
    const float DT = 1.f / 60.f;

    struct Cam
    {
        Vec3 pos;
        float yaw = 0.f, pitch = 0.f;       // radians
        Vec3 at() const
        {
            return { std::cos(pitch) * std::cos(yaw), std::cos(pitch) * std::sin(yaw), std::sin(pitch) };
        }
    };

    // is a sphere inside the camera's view frustum (60 degrees vertical, 16:9)?
    bool sphereInFrustum(const Cam& cam, const Vec3& c, float radius)
    {
        const Vec3 at = cam.at();
        const Vec3 up0 = { 0.f, 0.f, 1.f };
        // left = up0 x at
        Vec3 left = { up0.y * at.z - up0.z * at.y, up0.z * at.x - up0.x * at.z, up0.x * at.y - up0.y * at.x };
        const float ll = std::sqrt(left.x * left.x + left.y * left.y + left.z * left.z);
        left = { left.x / ll, left.y / ll, left.z / ll };
        const Vec3 up = { at.y * left.z - at.z * left.y, at.z * left.x - at.x * left.z, at.x * left.y - at.y * left.x };
        const Vec3 d = { c.x - cam.pos.x, c.y - cam.pos.y, c.z - cam.pos.z };
        const float z = d.x * at.x + d.y * at.y + d.z * at.z;
        const float x = d.x * left.x + d.y * left.y + d.z * left.z;
        const float y = d.x * up.x + d.y * up.y + d.z * up.z;
        if (z < -radius)
        {
            return false;
        }
        const float th = std::tan(30.f * PI / 180.f), tw = th * 16.f / 9.f;
        const float ny = std::sqrt(1.f + th * th), nx = std::sqrt(1.f + tw * tw);
        return std::fabs(y) - z * th <= radius * ny && std::fabs(x) - z * tw <= radius * nx;
    }

    struct Lamp
    {
        int tag = 0;                        // the id (its address)
        Vec3 pos;
        Vec3 axis{ 0.f, 0.f, -1.f };
        float radius = 8.f, fov = 1.2f, lum = 1.f;
        std::uint64_t fingerprint = 1;
    };

    LightInput asInput(const Lamp& l, const void* id)
    {
        LightInput in;
        in.id = id;
        in.pos = l.pos;
        in.axis = l.axis;
        in.radius = l.radius;
        in.fov = l.fov;
        in.lum = l.lum;
        in.fingerprint = l.fingerprint;
        return in;
    }

    // ---- the old Firestorm selection, ported ------------------------------------------------------
    struct OldSelector
    {
        const void* slot[2] = { nullptr, nullptr };
        float fade[2] = { 0.f, 0.f };

        static float priority(const Lamp& l, const Cam& cam)
        {
            const Vec3 at = cam.at();
            Vec3 p = { l.pos.x + l.axis.x * l.radius - at.x * l.radius, l.pos.y + l.axis.y * l.radius - at.y * l.radius,
                       l.pos.z + l.axis.z * l.radius - at.z * l.radius };
            float dist = std::sqrt(dist2(p, cam.pos));
            if (dist < 16.f)
            {
                dist = dist / 16.f * dist;
            }
            const float ang = std::atan(1.732f * l.radius / std::max(dist, 1.0e-3f));
            const float r = ang * (1080.f / (60.f * PI / 180.f));
            return r * r * PI;
        }

        void step(const std::vector<Lamp>& lamps, const std::vector<const void*>& ids, const Cam& cam, float dt, float speed)
        {
            const void* target[2] = { nullptr, nullptr };
            float pri[2] = { 0.f, 0.f };
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                if (!sphereInFrustum(cam, lamps[i].pos, lamps[i].radius * 1.5f))
                {
                    continue;
                }
                float m = priority(lamps[i], cam);
                const void* pot = ids[i];
                for (int k = 0; k < 2; ++k)
                {
                    if (m > pri[k])
                    {
                        std::swap(target[k], pot);
                        std::swap(pri[k], m);
                    }
                }
            }
            const float amt = dt * std::max(speed, 1.f);
            for (int k = 0; k < 2; ++k)
            {
                if (slot[k] && (slot[k] == target[0] || slot[k] == target[1]))
                {
                    fade[k] = std::min(fade[k] + amt, 1.f);
                }
                else
                {
                    fade[k] = std::max(fade[k] - amt, 0.f);
                    if (fade[k] == 0.f || !slot[k])
                    {
                        slot[k] = target[0] != slot[(k + 1) % 2] ? target[0] : target[1];
                    }
                }
            }
        }

        bool shadowed(const void* id) const
        {
            for (int k = 0; k < 2; ++k)
            {
                if (slot[k] == id && fade[k] >= 0.99f)
                {
                    return true;
                }
            }
            return false;
        }
    };

    std::vector<Lamp> ring(int n, float r, float h)
    {
        std::vector<Lamp> lamps(n);
        for (int i = 0; i < n; ++i)
        {
            const float a = 2.f * PI * (float)i / (float)n + 0.3f;
            lamps[i].tag = i;
            lamps[i].pos = { r * std::cos(a), r * std::sin(a), h };
            lamps[i].radius = 7.f + (float)(i % 3);
            lamps[i].fov = 1.0f + 0.1f * (float)(i % 4);
            lamps[i].lum = 1.f + 0.2f * (float)(i % 3);
        }
        return lamps;
    }

    struct Frames
    {
        long drawn = 0, atlas_missing = 0, old_missing = 0;
    };

    // Run a camera path; count the frames in which a drawn projector lacks a full shadow.
    template <class PathFn>
    Frames run(const std::vector<Lamp>& lamps, PathFn path, float seconds, float settle, Config cfg = Config())
    {
        Atlas atlas(cfg);
        OldSelector old;
        std::vector<char> ids(lamps.size());
        std::vector<const void*> idp(lamps.size());
        for (size_t i = 0; i < lamps.size(); ++i)
        {
            idp[i] = &ids[i];
        }
        Frames f;
        double now = 0.0;
        Cam prev = path(0.f);
        for (float t = 0.f; t < seconds; t += DT, now += DT)
        {
            const Cam cam = path(t);
            std::vector<LightInput> in;
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                in.push_back(asInput(lamps[i], idp[i]));
            }
            CameraInput ci;
            ci.pos = cam.pos;
            atlas.update(ci, in, DT, now);
            const float speed = std::sqrt(dist2(cam.pos, prev.pos)) / DT;
            old.step(lamps, idp, cam, DT, speed);
            prev = cam;
            if (t < settle)
            {
                continue;
            }
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                if (!sphereInFrustum(cam, lamps[i].pos, lamps[i].radius * 1.5f))
                {
                    continue;
                }
                ++f.drawn;
                const Tile* tile = atlas.find(idp[i]);
                if (!tile || !tile->valid || tile->fade < 0.99f)
                {
                    ++f.atlas_missing;
                }
                if (!old.shadowed(idp[i]))
                {
                    ++f.old_missing;
                }
            }
        }
        return f;
    }

    void test_allocator()
    {
        std::mt19937 rng(11);
        Allocator a(3072, 3072, 1024, 128);
        std::vector<Allocator::Block> held;
        const int sizes[] = { 128, 256, 512, 1024 };
        for (int step = 0; step < 4000; ++step)
        {
            if (held.empty() || (rng() % 3) != 0)
            {
                const Allocator::Block b = a.alloc(sizes[rng() % 4]);
                if (b.valid())
                {
                    check(b.x >= 0 && b.y >= 0 && b.x + b.size <= 3072 && b.y + b.size <= 3072, "block inside the atlas");
                    check(b.x % b.size == 0 && b.y % b.size == 0, "block aligned to its size", b.x, b.size);
                    for (const auto& o : held)
                    {
                        const bool overlap = b.x < o.x + o.size && o.x < b.x + b.size && b.y < o.y + o.size && o.y < b.y + b.size;
                        check(!overlap, "blocks never overlap");
                    }
                    held.push_back(b);
                }
            }
            else
            {
                const size_t i = rng() % held.size();
                a.free(held[i]);
                held.erase(held.begin() + i);
            }
        }
        for (const auto& b : held)
        {
            a.free(b);
        }
        check(a.freeTexels() == 3072 * 3072, "all freed: the whole atlas is free", a.freeTexels(), 3072.0 * 3072.0);
        // coalescing: after freeing everything a full root can be allocated again, 9 times
        int roots = 0;
        while (a.alloc(1024).valid())
        {
            ++roots;
        }
        check(roots == 9, "freed blocks merge back into root cells", roots, 9);
        check(a.roundSize(1) == 128 && a.roundSize(129) == 256 && a.roundSize(700) == 1024 && a.roundSize(5000) == 1024, "tile size rounding");
    }

    void test_math()
    {
        check(distanceFade(10.f, 128.f) == 1.f && distanceFade(128.f, 128.f) == 0.f && distanceFade(200.f, 128.f) == 0.f, "distance fade ends");
        check(std::fabs(distanceFade(115.2f, 128.f) - 0.5f) < 1e-3f, "distance fade middle", distanceFade(115.2f, 128.f), 0.5);
        const float f = renderFov(1.2f, 512, 8);
        check(f > 1.2f && std::fabs(std::tan(0.5f * f) - std::tan(0.6f) * 512.f / 496.f) < 1e-5f, "render fov covers the cone inside the border");
    }

    // ---- the failing scenarios ----------------------------------------------------------------------
    void test_turning(int n)
    {
        const auto lamps = ring(n, 7.f, 3.f);
        auto path = [](float t) { Cam c; c.pos = { 0.f, 0.f, 1.7f }; c.yaw = t * 60.f * PI / 180.f; c.pitch = -0.26f; return c; };
        const Frames f = run(lamps, path, 12.f, 1.f);
        std::printf("  turning 60 deg/s among %d projectors: drawn %ld, frames lacking a full shadow: atlas %ld, old selection %ld\n", n, f.drawn,
                    f.atlas_missing, f.old_missing);
        check(f.drawn > 0, "projectors are drawn while turning");
        check(f.atlas_missing == 0, "atlas: a shadow in every frame a projector is drawn", (double)f.atlas_missing, n);
        if (n >= 3)
        {
            check(f.old_missing > 0, "the old selection fails here (the scenario can fail)", (double)f.old_missing, n);
        }
    }

    void test_partly_offscreen()
    {
        // projectors at the edge of the view, a still camera: the lit area is half in view
        std::vector<Lamp> lamps(5);
        for (int i = 0; i < 5; ++i)
        {
            const float a = (-0.9f + 0.45f * (float)i);   // radians off the view axis, past the frustum's half-width at some
            lamps[i].tag = i;
            lamps[i].pos = { 12.f * std::cos(a), 12.f * std::sin(a), 3.f };
            lamps[i].radius = 9.f;
        }
        auto path = [](float) { Cam c; c.pos = { 0.f, 0.f, 1.7f }; c.yaw = 0.f; c.pitch = -0.2f; return c; };
        const Frames f = run(lamps, path, 4.f, 1.f);
        std::printf("  5 projectors with volumes partly off-screen, still camera: drawn %ld, lacking a shadow: atlas %ld, old selection %ld\n",
                    f.drawn, f.atlas_missing, f.old_missing);
        check(f.atlas_missing == 0, "atlas: partly off-screen projectors keep their shadow", (double)f.atlas_missing);
        check(f.old_missing > 0, "the old selection drops some (only 2 slots)", (double)f.old_missing);
    }

    void test_turn_away_and_back()
    {
        const auto lamps = ring(3, 6.f, 3.f);
        for (float away : { 0.5f, 1.f, 3.f, 10.f })
        {
            // look at lamp 0, turn 180 degrees away in 0.5 s, wait, turn back in 0.5 s
            const float a0 = std::atan2(lamps[0].pos.y, lamps[0].pos.x);
            auto path = [&](float t) {
                Cam c;
                c.pos = { 0.f, 0.f, 1.7f };
                c.pitch = -0.26f;
                float k;
                if (t < 2.f) k = 0.f;
                else if (t < 2.5f) k = (t - 2.f) / 0.5f;
                else if (t < 2.5f + away) k = 1.f;
                else if (t < 3.f + away) k = 1.f - (t - 2.5f - away) / 0.5f;
                else k = 0.f;
                c.yaw = a0 + PI * k;
                return c;
            };
            // count only frames after the turn back
            const Frames f = run(lamps, path, 6.f + away, 3.f + away);
            std::printf("  turn away and back after %4.1f s: drawn %ld, lacking a shadow: atlas %ld, old selection %ld\n", away, f.drawn,
                        f.atlas_missing, f.old_missing);
            check(f.drawn > 0, "the lit area is seen again after turning back", away);
            check(f.atlas_missing == 0, "atlas: the shadow is there the moment the lit area is seen again", (double)f.atlas_missing, away);
        }
    }

    // ---- budget ---------------------------------------------------------------------------------------
    void test_static_room()
    {
        Config cfg;
        Atlas atlas(cfg);
        std::vector<Lamp> lamps = ring(10, 6.f, 3.f);
        std::vector<char> ids(lamps.size());
        int first_frames = 0, max_per_frame = 0;
        long rest_renders = 0;
        double now = 0.0;
        for (int f = 0; f < 60 * 30; ++f, now += DT)
        {
            std::vector<LightInput> in;
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                in.push_back(asInput(lamps[i], &ids[i]));
            }
            CameraInput ci;
            ci.pos = { 0.f, 0.f, 1.7f };
            atlas.update(ci, in, DT, now);
            const int n = (int)atlas.renders().size();
            max_per_frame = std::max(max_per_frame, n);
            if (n && f < 60)
            {
                first_frames = f + 1;
            }
            if (f >= 120)
            {
                rest_renders += n;
            }
        }
        // 10 tiles are rendered once, within the budget; at rest only the safety refresh remains
        const double expected = 10.0 * (28.0 / cfg.safety_refresh);
        std::printf("  static room, 10 lamps: all placed within %d frames; at rest %ld renders in 28 s (the safety refresh: ~%.0f), at most %d per frame\n",
                    first_frames, rest_renders, expected, max_per_frame);
        check(first_frames <= 10 / cfg.max_renders_per_frame + 1, "10 new tiles within the budget", first_frames);
        check(max_per_frame <= cfg.max_renders_per_frame, "never more than the per-frame budget", max_per_frame);
        check(rest_renders <= (long)(expected * 1.3) && rest_renders >= (long)(expected * 0.7), "at rest only the 2 s safety refresh", (double)rest_renders, expected);
        for (const Tile& t : atlas.tiles())
        {
            check(t.valid && t.fade >= 0.99f, "every lamp has a full shadow at rest");
        }
        check(atlas.tiles().size() == 10, "10 tiles", (double)atlas.tiles().size());
    }

    void test_club(const Config& cfg, int nlights, const char* label)
    {
        Atlas atlas(cfg);
        std::vector<Lamp> lamps = ring(nlights, 9.f, 4.f);
        std::vector<char> ids(lamps.size());
        int max_per_frame = 0;
        double now = 0.0, max_age = 0.0;
        long frames = 0;
        for (int f = 0; f < 60 * 20; ++f, now += DT)
        {
            std::vector<LightInput> in;
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                lamps[i].fingerprint += 1;   // every light moves every frame
                in.push_back(asInput(lamps[i], &ids[i]));
            }
            CameraInput ci;
            ci.pos = { 0.f, 0.f, 1.7f };
            atlas.update(ci, in, DT, now);
            max_per_frame = std::max(max_per_frame, (int)atlas.renders().size());
            if (f > 240)
            {
                ++frames;
                for (const Tile& t : atlas.tiles())
                {
                    max_age = std::max(max_age, now - t.last_render);
                }
            }
        }
        const double ideal = std::ceil((double)std::min<int>(nlights, cfg.max_lights) / cfg.max_renders_per_frame) / 60.0;
        std::printf("  %s: %d moving lights, %d shadowed, budget %d per frame: at most %d per frame, a tile is at most %.0f ms old (a round robin: %.0f ms)\n",
                    label, nlights, (int)atlas.tiles().size(), cfg.max_renders_per_frame, max_per_frame, max_age * 1000.0, ideal * 1000.0);
        check(max_per_frame <= cfg.max_renders_per_frame, "club: never above the per-frame budget", max_per_frame);
        check((int)atlas.tiles().size() == std::min(nlights, cfg.max_lights), "club: the most important lights hold tiles", (double)atlas.tiles().size());
        check(max_age <= ideal * 2.5 + 0.1, "club: no tile starves", max_age, ideal);
        check(frames > 0, "ran");
    }

    void test_hysteresis()
    {
        Config cfg;
        cfg.max_lights = 12;
        Atlas atlas(cfg);
        std::vector<Lamp> lamps = ring(14, 8.f, 3.f);
        for (Lamp& l : lamps)
        {
            l.radius = 8.f;
            l.fov = 1.2f;
            l.lum = 1.f;
        }
        std::vector<char> ids(lamps.size());
        std::set<const void*> held_first;
        double now = 0.0;
        int swaps = 0;
        for (int f = 0; f < 60 * 20; ++f, now += DT)
        {
            std::vector<LightInput> in;
            for (size_t i = 0; i < lamps.size(); ++i)
            {
                // equal power, the camera drifts a little: the order of near-equal lights changes
                in.push_back(asInput(lamps[i], &ids[i]));
            }
            CameraInput ci;
            ci.pos = { 0.3f * std::sin((float)now), 0.3f * std::cos((float)now), 1.7f };
            atlas.update(ci, in, DT, now);
            std::set<const void*> now_held;
            for (const Tile& t : atlas.tiles())
            {
                now_held.insert(t.id);
            }
            if (f == 120)
            {
                held_first = now_held;
            }
            else if (f > 120 && now_held != held_first)
            {
                ++swaps;
                held_first = now_held;
            }
        }
        std::printf("  14 near-equal lights, 12 slots, a drifting camera: %d tile swaps in 18 s\n", swaps);
        check(swaps == 0, "near-equal lights do not swap tiles (hysteresis)", swaps);
    }

    void test_range_and_avatars()
    {
        Config cfg;
        cfg.shadow_distance = 64.f;
        Atlas atlas(cfg);
        char a = 0, b = 0;
        Lamp near_lamp, far_lamp;
        near_lamp.pos = { 10.f, 0.f, 3.f };
        far_lamp.pos = { 200.f, 0.f, 3.f };
        double now = 0.0;
        CameraInput ci;
        ci.pos = { 0.f, 0.f, 1.7f };
        for (int f = 0; f < 120; ++f, now += DT)
        {
            atlas.update(ci, { asInput(near_lamp, &a), asInput(far_lamp, &b) }, DT, now);
        }
        check(atlas.find(&a) && !atlas.find(&b), "a light beyond the shadow distance has no tile");
        // the camera walks towards the far light: it gets a tile once its cone reaches within 64 m
        ci.pos = { 140.f, 0.f, 1.7f };
        for (int f = 0; f < 120; ++f, now += DT)
        {
            atlas.update(ci, { asInput(near_lamp, &a), asInput(far_lamp, &b) }, DT, now);
        }
        check(atlas.find(&b) != nullptr && atlas.find(&b)->fade >= 0.99f, "and a tile once the camera is within reach");
        check(atlas.find(&a) == nullptr || atlas.find(&a)->fade < 1.f, "the near light left the range: its tile fades out");
        for (int f = 0; f < 60; ++f, now += DT)
        {
            atlas.update(ci, { asInput(near_lamp, &a), asInput(far_lamp, &b) }, DT, now);
        }
        check(atlas.find(&a) == nullptr, "and is freed after the fade");
        check(atlas.freeTexels() == cfg.atlas_w * cfg.atlas_h - atlas.find(&b)->block.size * atlas.find(&b)->block.size, "its space is back in the atlas");

        // avatars: dirty from an avatar only counts within the avatar range
        Atlas at2(cfg);
        Lamp l;
        l.pos = { 10.f, 0.f, 3.f };
        char c = 0;
        CameraInput cn;
        cn.pos = { 0.f, 0.f, 1.7f };
        double t2 = 0.0;
        for (int f = 0; f < 120; ++f, t2 += DT)
        {
            at2.update(cn, { asInput(l, &c) }, DT, t2);
        }
        long renders_near = 0, renders_far = 0;
        for (int f = 0; f < 60; ++f, t2 += DT)
        {
            LightInput in = asInput(l, &c);
            in.avatar_dirty = true;
            at2.update(cn, { in }, DT, t2);
            renders_near += (long)at2.renders().size();
        }
        l.pos = { 40.f, 0.f, 3.f };   // 40 m away: beyond the 25 m avatar range
        for (int f = 0; f < 120; ++f, t2 += DT)
        {
            at2.update(cn, { asInput(l, &c) }, DT, t2);
        }
        for (int f = 0; f < 60; ++f, t2 += DT)
        {
            LightInput in = asInput(l, &c);
            in.avatar_dirty = true;
            at2.update(cn, { in }, DT, t2);
            renders_far += (long)at2.renders().size();
        }
        std::printf("  avatars moving in a cone: renders in 1 s at 10 m %ld, at 40 m %ld (the 2 s safety refresh only)\n", renders_near, renders_far);
        check(renders_near >= 55, "an avatar moving in a near cone re-renders its tile every frame", (double)renders_near);
        check(renders_far <= 1, "beyond the avatar range avatars do not dirty the tile", (double)renders_far);
    }
}

int main()
{
    test_allocator();
    test_math();
    std::printf("scenarios:\n");
    for (int n : { 1, 2, 3, 5 })
    {
        test_turning(n);
    }
    test_partly_offscreen();
    test_turn_away_and_back();
    std::printf("budget:\n");
    test_static_room();
    Config medium;
    test_club(medium, 30, "club, Medium");
    Config ultra;
    ultra.atlas_w = 6144;
    ultra.atlas_h = 4096;
    ultra.root = 2048;
    ultra.max_lights = 48;
    ultra.max_renders_per_frame = 4;
    test_club(ultra, 48, "club, Ultra");
    test_hysteresis();
    test_range_and_avatars();
    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::printf("llshadowprojectoratlas: all checks passed\n");
    return EXIT_SUCCESS;
}
