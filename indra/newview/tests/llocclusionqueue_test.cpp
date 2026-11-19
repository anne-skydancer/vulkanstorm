// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llocclusionqueue.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llocclusionqueue_test.cpp
// A model of the occlusion state machine (LLOcclusionCullingGroup::checkOcclusion / doOcclusion with the
// STATE_MODE_DIFF set and clear, LLOctreeCull::earlyFail, LLPipeline::markNotCulled / markOccluder) with the
// real queue rule plugged in. It checks that after any history of 0-sample results, once the groups are visible
// again every group is un-occluded within a few frames: B2's rule (the empty root queued as the parent) leaves
// the partition hidden for good, the shipped rule does not.

#include "llocclusionqueue.h"

#include <cstdio>
#include <random>
#include <vector>

namespace
{
    int failures = 0;
    void check(bool cond, const char* what, int a = 0)
    {
        if (!cond)
        {
            std::printf("FAIL %s (%d)\n", what, a);
            ++failures;
        }
    }

    struct Node
    {
        int parent = -1;
        std::vector<int> kids;
        bool empty = true;      // no elements of its own
        bool occluded = false;
        bool active = false;
        bool pending = false;
        bool result = false;    // samples > 0 for the issued query
    };

    // B2's rule as shipped in 1.0.1.82419: only the group's parent being OCCLUDED stops the queueing, the root is
    // queued as a group and as the parent of its children.
    LLOcclusionQueue::Plan b2Plan(bool use, const LLOcclusionQueue::GroupFlags& g, const LLOcclusionQueue::GroupFlags& p)
    {
        LLOcclusionQueue::Plan r{ false, false };
        if (!use || g.active || (g.hasParent && p.occluded))
        {
            return r;
        }
        r.queueGroup = true;
        r.queueParent = g.hasParent && !p.active && p.empty && p.needsUpdate;
        return r;
    }

    struct Model
    {
        std::vector<Node> n;
        std::vector<int> queue;
        bool shipped = true;
        bool (*visible)(int node, int frame) = nullptr;

        LLOcclusionQueue::GroupFlags flags(int i) const
        {
            const Node& x = n[i];
            return { x.parent >= 0, x.occluded, x.active, x.empty, true };
        }

        void setDiff(int i) // OctreeTraveler: do not descend into nodes that already have the state
        {
            if (n[i].occluded) return;
            n[i].occluded = true;
            for (int k : n[i].kids) setDiff(k);
        }
        void clearDiff(int i)
        {
            if (!n[i].occluded) return;
            n[i].occluded = false;
            for (int k : n[i].kids) clearDiff(k);
        }

        void checkOcclusion(int i)
        {
            Node& x = n[i];
            if (x.parent >= 0 && n[x.parent].occluded)
            {
                x.pending = false;
                return;
            }
            if (x.pending)
            {
                if (x.result) clearDiff(i); else setDiff(i);
                x.pending = false;
            }
        }

        void markOccluder(int i)
        {
            const bool hasParent = n[i].parent >= 0;
            const LLOcclusionQueue::GroupFlags p = hasParent ? flags(n[i].parent) : LLOcclusionQueue::GroupFlags{};
            const LLOcclusionQueue::Plan pl = shipped ? LLOcclusionQueue::plan(true, flags(i), p) : b2Plan(true, flags(i), p);
            if (pl.queueGroup)
            {
                queue.push_back(i);
                n[i].active = true;
                if (pl.queueParent)
                {
                    queue.push_back(n[i].parent);
                    n[n[i].parent].active = true;
                }
            }
        }

        void cull(int i) // LLOctreeCull::earlyFail, then markNotCulled (worst case: always asks for a query)
        {
            checkOcclusion(i);
            if (n[i].parent >= 0 && n[i].occluded)
            {
                markOccluder(i);
                return;
            }
            if (!n[i].empty) markOccluder(i);
            for (int k : n[i].kids) cull(k);
        }

        void doOcclusion(int frame)
        {
            for (int i : queue)
            {
                if (!n[i].pending)
                {
                    n[i].pending = true;
                    n[i].result = visible(i, frame);
                }
                n[i].active = false;
            }
            queue.clear();
        }

        void frame(int f)
        {
            cull(0);
            doOcclusion(f);
        }

        bool anyHidden() const
        {
            for (const Node& x : n)
                if (x.occluded) return true;
            return false;
        }
    };

    bool hiddenThenVisible(int, int frame) { return frame >= 6 || frame < 2; } // 0 samples on frames 2..5

    // root (empty) -> a (empty) -> b (empty) -> leaf (avatar), and root -> side (an object)
    Model chain(bool shipped, bool (*vis)(int, int))
    {
        Model m;
        m.shipped = shipped;
        m.visible = vis;
        m.n.resize(5);
        const int par[5] = { -1, 0, 1, 2, 0 };
        for (int i = 0; i < 5; ++i)
        {
            m.n[i].parent = par[i];
            if (par[i] >= 0) m.n[par[i]].kids.push_back(i);
        }
        m.n[3].empty = false;
        m.n[4].empty = false;
        return m;
    }

    void test_root_zero_sample_recovers()
    {
        for (int shipped = 0; shipped < 2; ++shipped)
        {
            Model m = chain(shipped != 0, hiddenThenVisible);
            for (int f = 0; f < 40; ++f) m.frame(f);
            if (shipped)
                check(!m.anyHidden(), "shipped rule: every group visible again after a 0-sample window");
            else
                check(m.anyHidden(), "B2 rule: the stuck state reproduces (the test model is meaningful)");
        }
    }

    bool rootOnlyHidden(int node, int frame) { return node != 0 || frame >= 6 || frame < 2; }

    void test_root_is_never_queued()
    {
        Model m = chain(true, rootOnlyHidden);
        for (int f = 0; f < 40; ++f)
        {
            m.frame(f);
            check(!m.n[0].active && !m.n[0].pending && !m.n[0].occluded, "the root is never queued, pending or occluded", f);
        }
    }

    // random trees, random 0-sample windows per node, then all visible: nothing may stay occluded
    int g_seed = 0;
    bool randomVis(int node, int frame)
    {
        if (frame >= 30) return true;
        const unsigned h = static_cast<unsigned>(node) * 2654435761u ^ static_cast<unsigned>(frame) * 40503u ^ static_cast<unsigned>(g_seed) * 69069u;
        return ((h >> 7) % 3) != 0;
    }

    void test_random()
    {
        std::mt19937 rng(12345);
        for (int t = 0; t < 2000; ++t)
        {
            g_seed = t;
            Model m;
            m.shipped = true;
            m.visible = randomVis;
            const int count = 2 + static_cast<int>(rng() % 30);
            m.n.resize(count);
            for (int i = 1; i < count; ++i)
            {
                const int p = static_cast<int>(rng() % i);
                m.n[i].parent = p;
                m.n[p].kids.push_back(i);
            }
            for (int i = 0; i < count; ++i) m.n[i].empty = (rng() % 3) != 0;
            for (int f = 0; f < 80; ++f) m.frame(f);
            check(!m.anyHidden(), "random tree: nothing stays occluded once everything is visible", t);
            check(!m.n[0].occluded, "random tree: the root is never occluded", t);
        }
    }

    void test_plan_cases()
    {
        using G = LLOcclusionQueue::GroupFlags;
        const G root{ false, false, false, true, true };
        const G mid{ true, false, false, true, true };
        const G leaf{ true, false, false, false, true };
        auto p = LLOcclusionQueue::plan(true, leaf, mid);
        check(p.queueGroup && p.queueParent, "leaf under an empty non-root parent queues both");
        p = LLOcclusionQueue::plan(true, leaf, root);
        check(p.queueGroup && !p.queueParent, "leaf under the empty root queues itself only");
        p = LLOcclusionQueue::plan(true, root, G{});
        check(!p.queueGroup && !p.queueParent, "the root is never queued");
        G occ = mid;
        occ.occluded = true;
        p = LLOcclusionQueue::plan(true, leaf, occ);
        check(!p.queueGroup, "children of an OCCLUDED parent are not queued");
        G act = leaf;
        act.active = true;
        p = LLOcclusionQueue::plan(true, act, mid);
        check(!p.queueGroup, "an already active group is not queued twice");
        p = LLOcclusionQueue::plan(false, leaf, mid);
        check(!p.queueGroup, "occlusion off queues nothing");
    }
}

int main()
{
    test_plan_cases();
    test_root_zero_sample_recovers();
    test_root_is_never_queued();
    test_random();
    if (failures)
    {
        std::printf("%d failures\n", failures);
        return 1;
    }
    std::printf("llocclusionqueue_test OK\n");
    return 0;
}
