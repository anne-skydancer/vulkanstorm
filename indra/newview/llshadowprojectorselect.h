// SPDX-License-Identifier: LGPL-2.1-or-later OR GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llshadowprojectorselect.h
 * @brief Which projectors get the projector shadow maps: chosen from the scene, stable while the
 *        camera turns (doc/shadows/shadow-model-rework.md §9.4, stage S7).
 *
 * Renderer-free (no GL, no viewer types; candidates are identified by an opaque pointer), so the
 * future Vulkan renderer can use it unchanged; indra/newview/tests/llshadowprojectorselect_test.cpp
 * checks it.
 *
 * Why: the shadow slots used to be re-picked every frame by the screen-space pixel area of a
 * sphere placed along the camera's at-axis, so turning the camera changed the ranking; and the
 * slots faded at a rate scaled by the camera's speed. A projector lighting the floor in front of
 * you lost its shadow to a bigger one at the side, or popped while you walked. Here:
 *   - the score is the light's power reaching the camera's surroundings: luminance x radius^2 x
 *     the cone's solid angle, over the squared distance from the camera position to the middle of
 *     the lit cone; it depends on the camera's position only, never its orientation;
 *   - a slot keeps its projector while it is a candidate (drawn this frame: its light box is in
 *     view), and for a grace time after it stops being one, so turning past it does not drop it;
 *   - a free candidate takes a slot only from a projector it outscores by a hysteresis factor;
 *   - fades take a fixed time.
 */

#ifndef LL_LLSHADOWPROJECTORSELECT_H
#define LL_LLSHADOWPROJECTORSELECT_H

#include <cmath>
#include <vector>

namespace LLShadowProjectorSelect
{
    constexpr float GRACE_SECONDS = 0.5f;  // a slot outlives its candidate by this long
    constexpr float HYSTERESIS = 1.5f;     // a newcomer must outscore the weaker held projector by this
    constexpr float FADE_SECONDS = 0.4f;   // full fade in or out

    // Light power reaching the camera's surroundings. lum: the light's luminance; radius: its range
    // (metres); fov: the cone's full angle (radians); dist2: squared distance from the camera
    // position to the middle of the lit cone (light position + axis x radius / 2).
    inline float score(float lum, float radius, float fov, float dist2)
    {
        const float solid = 6.2831853f * (1.f - std::cos(0.5f * fov));
        return lum * radius * radius * solid / (dist2 > 1.f ? dist2 : 1.f);
    }

    struct Candidate
    {
        const void* id;
        float score;
    };

    struct Slot
    {
        const void* id = nullptr;
        float fade = 0.f;     // 0..1: 1 = full shadow
        float missing = 0.f;  // seconds since the projector was last a candidate
    };

    namespace detail
    {
        inline const Candidate* find(const std::vector<Candidate>& cands, const void* id)
        {
            for (const Candidate& c : cands)
            {
                if (c.id == id)
                {
                    return &c;
                }
            }
            return nullptr;
        }
    }

    // One frame: updates both slots from this frame's candidates (any order) over dt seconds.
    inline void update(Slot slots[2], const std::vector<Candidate>& cands, float dt)
    {
        // a held projector stays while it is a candidate or within the grace time
        bool keep[2];
        for (int i = 0; i < 2; ++i)
        {
            if (slots[i].id && detail::find(cands, slots[i].id))
            {
                slots[i].missing = 0.f;
            }
            else if (slots[i].id)
            {
                slots[i].missing += dt;
            }
            keep[i] = slots[i].id && slots[i].missing <= GRACE_SECONDS;
        }

        // the desired pair: the kept projectors, free slots filled by the best free candidates, and
        // the weaker kept one replaced only by a candidate that outscores it by the hysteresis
        const void* desired[2] = { keep[0] ? slots[0].id : nullptr, keep[1] ? slots[1].id : nullptr };
        std::vector<const Candidate*> free_cands;
        for (const Candidate& c : cands)
        {
            if (c.id != slots[0].id && c.id != slots[1].id && c.score > 0.f)
            {
                free_cands.push_back(&c);
            }
        }
        for (size_t a = 0; a < free_cands.size(); ++a) // best first (a few candidates at most)
        {
            for (size_t b = a + 1; b < free_cands.size(); ++b)
            {
                if (free_cands[b]->score > free_cands[a]->score)
                {
                    const Candidate* t = free_cands[a];
                    free_cands[a] = free_cands[b];
                    free_cands[b] = t;
                }
            }
        }
        for (const Candidate* c : free_cands)
        {
            int free_slot = !desired[0] ? 0 : (!desired[1] ? 1 : -1);
            if (free_slot >= 0)
            {
                desired[free_slot] = c->id;
                continue;
            }
            const Candidate* c0 = detail::find(cands, desired[0]);
            const Candidate* c1 = detail::find(cands, desired[1]);
            const float s0 = c0 ? c0->score : 0.f;
            const float s1 = c1 ? c1->score : 0.f;
            const int weak = s0 <= s1 ? 0 : 1;
            if (c->score > HYSTERESIS * (weak == 0 ? s0 : s1))
            {
                desired[weak] = c->id;
            }
            break; // the best free candidate could not take a slot: no weaker one can
        }

        // fade towards the desired projector; an emptied slot takes its new one once faded out
        const float step = dt / FADE_SECONDS;
        for (int i = 0; i < 2; ++i)
        {
            if (slots[i].id && slots[i].id == desired[i])
            {
                slots[i].fade = std::fmin(slots[i].fade + step, 1.f);
            }
            else
            {
                slots[i].fade = std::fmax(slots[i].fade - step, 0.f);
                if (slots[i].fade <= 0.f || !slots[i].id)
                {
                    slots[i].id = desired[i];
                    slots[i].fade = 0.f;
                    slots[i].missing = 0.f;
                }
            }
        }
    }
}

#endif // LL_LLSHADOWPROJECTORSELECT_H
