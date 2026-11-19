// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llculljobs.h
 * @brief The fragments and merges of multi-core stage 5c: cull jobs and the render-map build on LL::FrameJobs.
 *
 * Renderer-free (no GL, no viewer types: the group and draw-info types are template parameters);
 * indra/newview/tests/llculljobs_test.cpp drives it on a model octree.
 *
 * The rule that keeps the lists equal to the serial ones, element for element: the work is cut into a fixed list of
 * items (one per cascade and partition for the culls, a fixed number of visible groups for the render map), in the order
 * the serial code visits them. A job writes only the fragment of its own item, in its own traversal order, and the main
 * thread appends the fragments in item order after the batch. Which thread ran an item, and when, cannot show.
 *
 * Job-body rules (llframejobs.h): a job copies no LLPointer, makes no GL call and deletes nothing; the main thread takes
 * part in the batch and deletes nothing while it runs. The fragments keep their capacity from frame to frame.
 */

#ifndef LL_LLCULLJOBS_H
#define LL_LLCULLJOBS_H

#include "llframejobs.h"

#include <cstddef>
#include <vector>

namespace LLCullJobs
{
    // What one cull job found in its partition: the groups in traversal order, as LLPipeline::markNotCulled pushes them
    // (a partition renders either by group or by drawable, so one of the two lists stays empty), and the nodes counted.
    template <typename GROUP>
    struct CullFragment
    {
        std::vector<GROUP*> mVisibleGroups;  // render by group
        std::vector<GROUP*> mDrawableGroups; // render by drawable
        int                 mNodes = 0;

        void clear()
        {
            mVisibleGroups.clear();
            mDrawableGroups.clear();
            mNodes = 0;
        }
    };

    // What one chunk of the render-map build collected from its visible groups, in their order: the draw infos of every
    // render type, and the groups with a blended or a rigged blended entry.
    template <typename INFO, typename GROUP, size_t TYPES>
    struct RenderMapFragment
    {
        std::vector<INFO*>  mInfos[TYPES];
        std::vector<GROUP*> mAlphaGroups;
        std::vector<GROUP*> mRiggedAlphaGroups;

        void clear()
        {
            for (std::vector<INFO*>& list : mInfos)
            {
                list.clear();
            }
            mAlphaGroups.clear();
            mRiggedAlphaGroups.clear();
        }

        // the sink interface of the serial build (LLCullResult's)
        void pushDrawInfo(size_t type, INFO* info) { mInfos[type].push_back(info); }
        void pushAlphaGroup(GROUP* group) { mAlphaGroups.push_back(group); }
        void pushRiggedAlphaGroup(GROUP* group) { mRiggedAlphaGroups.push_back(group); }
    };

    // Clears the first `count` fragments for a batch, making them first if needed (the list never shrinks). Main thread,
    // before the batch: a job then only appends to the fragment of its own item or chunk.
    template <typename FRAGMENT>
    void prepare(std::vector<FRAGMENT>& fragments, size_t count)
    {
        if (fragments.size() < count)
        {
            fragments.resize(count);
        }
        for (size_t i = 0; i < count; ++i)
        {
            fragments[i].clear();
        }
    }

    // Runs fn(index, fragments[index]) for every index of [0, count), `grain` indices a chunk (LL::FrameJobs::parallelFor:
    // the partition is the same whatever the number of helpers). One fragment an index.
    template <typename FRAGMENT, typename FN>
    void runItems(size_t count, size_t grain, std::vector<FRAGMENT>& fragments, FN&& fn)
    {
        prepare(fragments, count);
        LL::FrameJobs::parallelFor(count, grain, [&fragments, &fn](size_t begin, size_t end, size_t)
        {
            for (size_t i = begin; i < end; ++i)
            {
                fn(i, fragments[i]);
            }
        });
    }

    // Runs fn(index, fragments[chunk]) for every index of [0, count) in index order within its chunk, `grain` indices a
    // chunk. One fragment a chunk; returns the number of chunks.
    template <typename FRAGMENT, typename FN>
    size_t runChunks(size_t count, size_t grain, std::vector<FRAGMENT>& fragments, FN&& fn)
    {
        const size_t chunks = LL::FrameJobs::chunkCount(count, grain);
        prepare(fragments, chunks);
        LL::FrameJobs::parallelFor(count, grain, [&fragments, &fn](size_t begin, size_t end, size_t chunk)
        {
            for (size_t i = begin; i < end; ++i)
            {
                fn(i, fragments[chunk]);
            }
        });
        return chunks;
    }

    // Appends a cull fragment to the cull result `sink` (pushVisibleGroup, pushDrawableGroup) and returns its node count.
    template <typename GROUP, typename SINK>
    int mergeCull(const CullFragment<GROUP>& fragment, SINK& sink)
    {
        for (GROUP* group : fragment.mVisibleGroups)
        {
            sink.pushVisibleGroup(group);
        }
        for (GROUP* group : fragment.mDrawableGroups)
        {
            sink.pushDrawableGroup(group);
        }
        return fragment.mNodes;
    }

    // Appends a render-map fragment to the cull result `sink` (appendDrawInfos, pushAlphaGroup, pushRiggedAlphaGroup).
    template <typename INFO, typename GROUP, size_t TYPES, typename SINK>
    void mergeRenderMap(const RenderMapFragment<INFO, GROUP, TYPES>& fragment, SINK& sink)
    {
        for (size_t type = 0; type < TYPES; ++type)
        {
            const std::vector<INFO*>& list = fragment.mInfos[type];
            if (!list.empty())
            {
                sink.appendDrawInfos(type, list.data(), list.size());
            }
        }
        for (GROUP* group : fragment.mAlphaGroups)
        {
            sink.pushAlphaGroup(group);
        }
        for (GROUP* group : fragment.mRiggedAlphaGroups)
        {
            sink.pushRiggedAlphaGroup(group);
        }
    }
} // namespace LLCullJobs

#endif // LL_LLCULLJOBS_H
