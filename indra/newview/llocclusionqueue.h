// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llocclusionqueue.h
 * @brief Which spatial groups LLPipeline::markOccluder queues for an occlusion query.
 *
 * Renderer-free (plain flags, no GL, no viewer types); indra/newview/tests/llocclusionqueue_test.cpp
 * drives it in a model of the cull pass.
 *
 * The rule that keeps every occluded group recoverable. A group whose query returns 0 samples is marked
 * OCCLUDED together with its whole subtree (STATE_MODE_DIFF). The mark leaves again only when that group is
 * queried once more, and a group is queried again only when LLOctreeCull::earlyFail reaches it: earlyFail
 * calls markOccluder for an OCCLUDED group whose parent is not OCCLUDED, and it never occlusion-culls the
 * partition's root. So by induction on depth every OCCLUDED group has a way back, provided the root itself
 * is never OCCLUDED, which holds exactly when the root is never queried. Hence:
 *   - the root (no parent) is never queued as the group and never queued or flagged as the parent;
 *   - an empty, non-root parent whose flag is set is queued in the same call, so doOcclusion clears the flag.
 * Queuing the empty root (B2, 1.0.1.82419) let one 0-sample result hide the whole partition for good: the
 * root is never re-tested, its children inherit OCCLUDED and markOccluder skips children of an OCCLUDED parent.
 */

#ifndef LL_LLOCCLUSIONQUEUE_H
#define LL_LLOCCLUSIONQUEUE_H

namespace LLOcclusionQueue
{
    struct GroupFlags
    {
        bool hasParent;     // false only for the partition's root
        bool occluded;      // OCCLUDED
        bool active;        // ACTIVE_OCCLUSION
        bool empty;         // getElementCount() == 0
        bool needsUpdate;   // needsUpdate()
    };

    struct Plan
    {
        bool queueGroup;    // push the group, set its ACTIVE_OCCLUSION
        bool queueParent;   // push the parent, set its ACTIVE_OCCLUSION
    };

    // parent is read only when group.hasParent. useOcclusion is sUseOcclusion > 1.
    inline Plan plan(bool useOcclusion, const GroupFlags& group, const GroupFlags& parent)
    {
        Plan p{ false, false };
        if (!useOcclusion || group.active || !group.hasParent || parent.occluded)
        {
            return p; // only top-most groups are queued, and the root never is
        }
        p.queueGroup = true;
        p.queueParent = parent.hasParent && !parent.active && parent.empty && parent.needsUpdate;
        return p;
    }
}

#endif // LL_LLOCCLUSIONQUEUE_H
