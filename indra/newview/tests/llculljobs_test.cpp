// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Tests for llculljobs.h and for the shape of multi-core stage 5c: the sun cascades' culls as one batch of frame jobs and
// the render-map build in chunks (reports/multicore-5a-2026-11-18/report.md section 2). No viewer objects: a model octree
// holds what a cull reads from LLViewerOctreeGroup (bounds, object bounds, element count, SKIP_FRUSTUM_CHECK, DIRTY) and
// what it writes (the per-camera stamps), and the model's culler follows LLViewerOctreeCull::traverse / visit /
// checkObjects and LLViewerOctreeGroup::rebound line by line. The fragments, the runners and the merges are the real ones.
//   1  the culls of four cascades over several regions' partitions: one job per cascade and partition (0 to 7 helpers,
//      serial mode, and the single chunk of a probe pass) against the serial culls in LLPipeline::updateCull's order:
//      every list element for element, the node counts, and every group's stamps. The octrees hold single-child chains
//      with SKIP_FRUSTUM_CHECK, subtrees fully inside a frustum, empty groups, and partitions that render by group and
//      by drawable
//   2  deliberate breaks, which the comparison of test 1 must catch: two fragments swapped in the merge, a fragment
//      dropped, a job given another cascade's camera id, a partition root left dirty before the batch
//   3  the render-map build: a serial pre-pass (the skip rules and the rebuild) and chunks over the kept groups (several
//      grains, 0 to 7 helpers, serial mode) against the former loop: the list of every render type and both lists of
//      blended groups, element for element; and the break of two chunks swapped in the merge
//   4  opt-in bench (LL_CULL_JOBS_BENCH=1): the cull batch and the render-map build at 0 to 7 helpers

#include "linden_common.h"

#include "../llculljobs.h"

#include "llframejobs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "lltut.h"

using LL::FrameJobs;

namespace
{
    struct Started
    {
        explicit Started(size_t helpers) { FrameJobs::start(helpers); }
        ~Started()
        {
            FrameJobs::setSerial(false);
            FrameJobs::stop();
        }
    };

    // A small deterministic generator, so that every run and every platform tests the same data.
    struct Lcg
    {
        uint32_t mState;
        explicit Lcg(uint32_t seed) : mState(seed) {}
        uint32_t next()
        {
            mState = mState * 1664525u + 1013904223u;
            return mState >> 8;
        }
        uint32_t below(uint32_t n) { return next() % n; }
        float unit() { return (float)(next() & 0xffff) / 65535.f; }
    };

    constexpr int CASCADES = 4;
    constexpr int CAMERAS = 9;        // LLViewerCamera::NUM_CAMERAS
    constexpr int CAMERA_SUN0 = 1;    // CAMERA_SUN_SHADOW0
    constexpr int CAMERA_WATER0 = 7;  // stamps below it also write the "any camera" stamp

    struct Box
    {
        float mMin[3];
        float mMax[3];
    };

    // LLCamera::AABBInFrustum for a box-shaped frustum: 0 outside, 1 partly inside, 2 fully inside
    int boxInBox(const Box& frustum, const Box& b)
    {
        bool inside = true;
        for (int a = 0; a < 3; ++a)
        {
            if (b.mMax[a] < frustum.mMin[a] || b.mMin[a] > frustum.mMax[a])
            {
                return 0;
            }
            if (b.mMin[a] < frustum.mMin[a] || b.mMax[a] > frustum.mMax[a])
            {
                inside = false;
            }
        }
        return inside ? 2 : 1;
    }

    struct Partition;

    // LLViewerOctreeGroup with its octree node
    struct Group
    {
        std::vector<Group*> mChildren;
        std::vector<Box>    mElements;       // the objects of this node
        Box                 mBounds = {};       // this node and its children (mBounds / mExtents)
        Box                 mObjectBounds = {}; // the objects of this node (mObjectBounds)
        Box                 mNodeBox = {};      // the octree cell, for building
        bool                mDirty = true;
        bool                mSkipFrustumCheck = false;
        Partition*          mPartition = nullptr;
        int                 mVisible[CAMERAS] = {};
        std::atomic<int>    mAnyVisible{ 0 };
        int                 mIndex = 0;      // creation order, for messages
    };

    struct Partition
    {
        Group* mRoot = nullptr;
        bool   mRenderByGroup = true;
        bool   mCulled = true;               // LLPipeline::cullsPartition
    };

    struct Scene
    {
        std::vector<std::unique_ptr<Group>> mGroups;
        std::vector<Partition>              mPartitions; // every region's, in updateCull's loop order
        Box                                 mCameras[CASCADES];
        int                                 mFrame = 100;
    };

    void unionBox(Box& into, const Box& b, bool first)
    {
        for (int a = 0; a < 3; ++a)
        {
            into.mMin[a] = first ? b.mMin[a] : std::min(into.mMin[a], b.mMin[a]);
            into.mMax[a] = first ? b.mMax[a] : std::max(into.mMax[a], b.mMax[a]);
        }
    }

    // LLViewerOctreeGroup::rebound
    void rebound(Group& g)
    {
        if (!g.mDirty)
        {
            return;
        }
        for (size_t i = 0; i < g.mElements.size(); ++i)
        {
            unionBox(g.mObjectBounds, g.mElements[i], i == 0);
        }
        if (g.mChildren.size() == 1 && g.mElements.empty())
        {
            Group& child = *g.mChildren[0];
            rebound(child);
            g.mBounds = child.mBounds; //copy single child's bounding box
            child.mSkipFrustumCheck = true;
        }
        else if (g.mChildren.empty())
        { //copy object bounding box if this is a leaf
            g.mBounds = g.mObjectBounds;
        }
        else
        {
            for (size_t i = 0; i < g.mChildren.size(); ++i)
            {
                Group& child = *g.mChildren[i];
                child.mSkipFrustumCheck = false;
                rebound(child);
                unionBox(g.mBounds, child.mBounds, i == 0);
            }
            if (!g.mElements.empty())
            {
                unionBox(g.mBounds, g.mObjectBounds, false);
            }
        }
        g.mDirty = false;
    }

    Group* newGroup(Scene& scene, Partition* part, const Box& cell)
    {
        scene.mGroups.emplace_back(new Group);
        Group* g = scene.mGroups.back().get();
        g->mPartition = part;
        g->mNodeBox = cell;
        g->mIndex = (int)scene.mGroups.size() - 1;
        return g;
    }

    Box randomBoxIn(Lcg& rng, const Box& cell, float max_fraction)
    {
        Box b;
        for (int a = 0; a < 3; ++a)
        {
            const float size = cell.mMax[a] - cell.mMin[a];
            const float w = size * max_fraction * (0.05f + 0.95f * rng.unit());
            const float lo = cell.mMin[a] + (size - w) * rng.unit();
            b.mMin[a] = lo;
            b.mMax[a] = lo + w;
        }
        return b;
    }

    // A subtree with the shapes the cull distinguishes: nodes with and without objects, leaves, single-child chains
    // (with and without objects of their own), and nodes with several children. Every leaf holds an object, as in the
    // viewer's octree, except a few empty ones (an empty group is visited but never pushed).
    void buildSubtree(Scene& scene, Lcg& rng, Group* g, int depth)
    {
        const uint32_t shape = depth <= 0 ? 0 : rng.below(10);
        uint32_t children = 0;
        if (shape >= 1 && shape <= 3)
        {
            children = 1; // a chain
        }
        else if (shape >= 4)
        {
            children = 2 + rng.below(4);
        }
        const bool leaf = children == 0;
        const bool own_objects = leaf ? rng.below(12) != 0 : rng.below(3) == 0;
        if (own_objects)
        {
            const uint32_t n = 1 + rng.below(3);
            for (uint32_t i = 0; i < n; ++i)
            {
                g->mElements.push_back(randomBoxIn(rng, g->mNodeBox, leaf ? 0.9f : 0.3f));
            }
        }
        for (uint32_t c = 0; c < children; ++c)
        {
            Group* child = newGroup(scene, g->mPartition, randomBoxIn(rng, g->mNodeBox, children == 1 ? 0.95f : 0.6f));
            g->mChildren.push_back(child);
            buildSubtree(scene, rng, child, depth - 1);
        }
        if (leaf && g->mElements.empty())
        {
            // an empty leaf: its bounds are its cell, so the frustum test still has something to read
            g->mObjectBounds = g->mNodeBox;
        }
    }

    void buildScene(Scene& scene, uint32_t seed, int regions, int partitions_per_region, int depth)
    {
        Lcg rng(seed);
        const size_t partitions = (size_t)regions * (size_t)partitions_per_region;
        scene.mPartitions.resize(partitions);
        for (size_t p = 0; p < partitions; ++p)
        {
            Partition& part = scene.mPartitions[p];
            part.mRenderByGroup = (p % 3) != 1;  // the bridge partition renders by drawable
            part.mCulled = (p % 7) != 5;         // a partition whose render type the pass does not draw
            const float x = 256.f * (float)(p / partitions_per_region);
            const Box cell = { { x, 0.f, 0.f }, { x + 256.f, 256.f, 128.f } };
            part.mRoot = newGroup(scene, &part, cell);
            buildSubtree(scene, rng, part.mRoot, depth);
        }
        // cascade frusta of growing size around a point of the first region: the far ones hold whole subtrees
        for (int j = 0; j < CASCADES; ++j)
        {
            const float r = 12.f * (float)(1 << (2 * j));
            scene.mCameras[j] = { { 128.f - r, 128.f - r, 32.f - r }, { 128.f + r, 128.f + r, 32.f + r } };
        }
    }

    void resetStamps(Scene& scene)
    {
        for (auto& g : scene.mGroups)
        {
            for (int& v : g->mVisible)
            {
                v = 0;
            }
            g->mAnyVisible.store(0);
        }
    }

    // --- the cull -------------------------------------------------------------------------------------------------------

    struct CullResult // LLCullResult's lists a cull fills, and LLPipeline::mNumVisibleNodes
    {
        std::vector<Group*> mVisibleGroups;
        std::vector<Group*> mDrawableGroups;
        int                 mNodes = 0;
        void pushVisibleGroup(Group* g) { mVisibleGroups.push_back(g); }
        void pushDrawableGroup(Group* g) { mDrawableGroups.push_back(g); }
        bool operator==(const CullResult& rhs) const
        {
            return mVisibleGroups == rhs.mVisibleGroups && mDrawableGroups == rhs.mDrawableGroups && mNodes == rhs.mNodes;
        }
    };

    // LLViewerOctreeCull with LLOctreeCullShadow's tests; PROCESS is processGroup
    template <typename PROCESS>
    struct Culler
    {
        const Box& mCamera;
        PROCESS&   mProcess;
        int        mRes = 0;

        Culler(const Box& camera, PROCESS& process) : mCamera(camera), mProcess(process) {}

        bool checkObjects(const Group& g)
        {
            if (g.mElements.empty()) //no elements
            {
                return false;
            }
            else if (g.mChildren.empty()) //leaf state, already checked tightest bounding box
            {
                return true;
            }
            else if (mRes == 1 && !boxInBox(mCamera, g.mObjectBounds)) //no objects in frustum
            {
                return false;
            }
            return true;
        }

        void visit(Group& g)
        {
            if (checkObjects(g))
            {
                mProcess(g);
            }
        }

        void traverseBase(Group& g) // LLOctreeTraveler::traverse
        {
            visit(g);
            for (Group* child : g.mChildren)
            {
                traverse(*child);
            }
        }

        void traverse(Group& g)
        {
            if (mRes == 2 || (mRes && g.mSkipFrustumCheck))
            {   //fully in, just add everything
                traverseBase(g);
            }
            else
            {
                mRes = boxInBox(mCamera, g.mBounds);
                if (mRes)
                { //at least partially in, run on down
                    traverseBase(g);
                }
                mRes = 0;
            }
        }
    };

    // The viewer's octree never holds an empty group that passes checkObjects; isEmpty() is still the first test of
    // markNotCulled, so the model keeps a flag for it.
    bool isEmpty(const Group& g) { return g.mElements.empty(); }

    void stamp(Group& g, int camera, int frame) // LLViewerOctreeGroup::setVisibleFor
    {
        g.mVisible[camera] = frame;
        if (camera < CAMERA_WATER0)
        {
            g.mAnyVisible.store(frame, std::memory_order_relaxed);
        }
    }

    // LLPipeline::updateCull for cascade j (LLSpatialPartition::cull with LLPipeline::markNotCulled)
    void serialCull(Scene& scene, int j, CullResult& result)
    {
        for (Partition& part : scene.mPartitions)
        {
            if (!part.mCulled)
            {
                continue;
            }
            rebound(*part.mRoot);
            auto process = [&](Group& g)
            {
                if (isEmpty(g))
                {
                    return;
                }
                stamp(g, CAMERA_SUN0 + j, scene.mFrame);
                if (!g.mPartition->mRenderByGroup)
                {
                    result.pushDrawableGroup(&g);
                }
                else
                {
                    result.pushVisibleGroup(&g);
                }
                ++result.mNodes;
            };
            Culler<decltype(process)> culler(scene.mCameras[j], process);
            culler.traverse(*part.mRoot);
        }
    }

    using Fragment = LLCullJobs::CullFragment<Group>;

    struct Item
    {
        Partition* mPartition;
        int        mCascade;
    };

    enum Break
    {
        BREAK_NONE,
        BREAK_SWAP,        // two fragments change places in the merge
        BREAK_DROP,        // a fragment is left out of the merge
        BREAK_CAMERA_ID,   // a job stamps for another cascade
        BREAK_DIRTY_ROOT,  // a root is not rebounded before the batch
    };

    // LLSpatialPartition::cullShadowJob with LLOctreeCullShadowJob
    void cullJob(Scene& scene, const Item& item, int camera_id, Fragment& out)
    {
        auto process = [&](Group& g)
        {
            if (isEmpty(g))
            {
                return;
            }
            stamp(g, camera_id, scene.mFrame);
            if (!g.mPartition->mRenderByGroup)
            {
                out.mDrawableGroups.push_back(&g);
            }
            else
            {
                out.mVisibleGroups.push_back(&g);
            }
            ++out.mNodes;
        };
        Culler<decltype(process)> culler(scene.mCameras[item.mCascade], process);
        culler.traverse(*item.mPartition->mRoot);
    }

    // LLPipeline::updateCullSunCascades
    void jobsCull(Scene& scene, CullResult* results, std::vector<Fragment>& fragments, bool one_chunk, Break fault = BREAK_NONE)
    {
        std::vector<Item> items;
        size_t first[CASCADES + 1];
        Group* stale_root = nullptr;
        for (int j = 0; j < CASCADES; ++j)
        {
            first[j] = items.size();
            for (Partition& part : scene.mPartitions)
            {
                if (part.mCulled)
                {
                    if (fault == BREAK_DIRTY_ROOT && part.mRoot->mDirty && (!stale_root || stale_root == part.mRoot))
                    {
                        stale_root = part.mRoot; // this root's tree keeps its old bounds for the batch
                    }
                    else
                    {
                        rebound(*part.mRoot);
                    }
                    items.push_back({ &part, j });
                }
            }
        }
        first[CASCADES] = items.size();

        const size_t count = items.size();
        LLCullJobs::runItems(count, one_chunk ? count : 1, fragments, [&](size_t i, Fragment& fragment)
        {
            int id = CAMERA_SUN0 + items[i].mCascade;
            if (fault == BREAK_CAMERA_ID && i == count / 2)
            {
                id = CAMERA_SUN0 + (items[i].mCascade + 1) % CASCADES;
            }
            cullJob(scene, items[i], id, fragment);
        });

        if (fault == BREAK_SWAP)
        {
            // the first two items of one cascade that both feed the list of visible groups (two that feed different
            // lists could change places unseen: the lists are separate)
            bool swapped = false;
            for (size_t i = 0; i < count && !swapped; ++i)
            {
                for (size_t k = i + 1; k < count && items[k].mCascade == items[i].mCascade && !swapped; ++k)
                {
                    if (!fragments[i].mVisibleGroups.empty() && !fragments[k].mVisibleGroups.empty())
                    {
                        std::swap(fragments[i], fragments[k]);
                        swapped = true;
                    }
                }
            }
        }
        bool dropped = false;
        for (int j = 0; j < CASCADES; ++j)
        {
            for (size_t i = first[j]; i < first[j + 1]; ++i)
            {
                if (fault == BREAK_DROP && !dropped && fragments[i].mNodes != 0)
                {
                    dropped = true;
                    continue;
                }
                results[j].mNodes += LLCullJobs::mergeCull(fragments[i], results[j]);
            }
        }
    }

    // the lists as creation indices, to compare two scenes built from one seed
    std::vector<int> flatten(const CullResult* results)
    {
        std::vector<int> flat;
        for (int j = 0; j < CASCADES; ++j)
        {
            flat.push_back(results[j].mNodes);
            flat.push_back((int)results[j].mVisibleGroups.size());
            for (Group* g : results[j].mVisibleGroups)
            {
                flat.push_back(g->mIndex);
            }
            flat.push_back((int)results[j].mDrawableGroups.size());
            for (Group* g : results[j].mDrawableGroups)
            {
                flat.push_back(g->mIndex);
            }
        }
        return flat;
    }

    // the path from `g` down to a leaf with an object whose bounds lie outside `frustum` (false when there is none)
    bool findLeafOutside(Group& g, const Box& frustum, std::vector<Group*>& path)
    {
        path.push_back(&g);
        if (g.mChildren.empty())
        {
            if (!g.mElements.empty() && boxInBox(frustum, g.mBounds) == 0)
            {
                return true;
            }
        }
        for (Group* child : g.mChildren)
        {
            if (findLeafOutside(*child, frustum, path))
            {
                return true;
            }
        }
        path.pop_back();
        return false;
    }

    struct Stamps
    {
        std::vector<int> mValues;
        bool operator==(const Stamps& rhs) const { return mValues == rhs.mValues; }
    };

    Stamps readStamps(Scene& scene)
    {
        Stamps s;
        s.mValues.reserve(scene.mGroups.size() * (CAMERAS + 1));
        for (auto& g : scene.mGroups)
        {
            for (int v : g->mVisible)
            {
                s.mValues.push_back(v);
            }
            s.mValues.push_back(g->mAnyVisible.load());
        }
        return s;
    }

    void markAllDirty(Scene& scene)
    {
        for (auto& g : scene.mGroups)
        {
            g->mDirty = true;
        }
    }

    // --- the render map -------------------------------------------------------------------------------------------------

    constexpr size_t TYPES = 24;
    constexpr uint32_t TYPE_ALPHA = 7;
    constexpr uint32_t TYPE_ALPHA_RIGGED = 8;

    struct DrawInfo
    {
        uint32_t mCount;
    };

    struct MapGroup
    {
        std::unordered_map<uint32_t, std::vector<DrawInfo*>> mDrawMap;
        std::vector<DrawInfo*> mPending[2]; // what a rebuild puts in the draw map (type 1 and type TYPE_ALPHA)
        bool mDead = false;
        bool mHidden = false;               // the auto-hide rule
        bool mNeedsRebuild = false;         // NEW_DRAWINFO and GEOM_DIRTY
    };

    struct RenderMap // LLCullResult's lists the build fills
    {
        std::vector<DrawInfo*> mLists[TYPES];
        std::vector<MapGroup*> mAlphaGroups;
        std::vector<MapGroup*> mRiggedAlphaGroups;
        void pushDrawInfo(size_t type, DrawInfo* info) { mLists[type].push_back(info); }
        void appendDrawInfos(size_t type, DrawInfo* const* infos, size_t count) { mLists[type].insert(mLists[type].end(), infos, infos + count); }
        void pushAlphaGroup(MapGroup* g) { mAlphaGroups.push_back(g); }
        void pushRiggedAlphaGroup(MapGroup* g) { mRiggedAlphaGroups.push_back(g); }
        bool operator==(const RenderMap& rhs) const
        {
            for (size_t t = 0; t < TYPES; ++t)
            {
                if (mLists[t] != rhs.mLists[t])
                {
                    return false;
                }
            }
            return mAlphaGroups == rhs.mAlphaGroups && mRiggedAlphaGroups == rhs.mRiggedAlphaGroups;
        }
    };

    struct MapScene
    {
        std::vector<std::unique_ptr<DrawInfo>> mInfos;
        std::vector<MapGroup>                  mGroups;
        bool                                   mTypeMask[TYPES];
    };

    void buildMapScene(MapScene& scene, uint32_t seed, size_t groups)
    {
        Lcg rng(seed);
        scene.mGroups.resize(groups);
        for (size_t t = 0; t < TYPES; ++t)
        {
            scene.mTypeMask[t] = (t % 5) != 3; // hasRenderType
        }
        scene.mTypeMask[TYPE_ALPHA] = true;
        scene.mTypeMask[TYPE_ALPHA_RIGGED] = true;
        auto newInfo = [&]()
        {
            scene.mInfos.emplace_back(new DrawInfo{ rng.below(900) });
            return scene.mInfos.back().get();
        };
        for (MapGroup& g : scene.mGroups)
        {
            g.mDead = rng.below(40) == 0;
            g.mHidden = rng.below(30) == 0;
            g.mNeedsRebuild = rng.below(15) == 0;
            const uint32_t types = rng.below(5);
            for (uint32_t k = 0; k < types; ++k)
            {
                std::vector<DrawInfo*>& list = g.mDrawMap[rng.below((uint32_t)TYPES)];
                const uint32_t n = 1 + rng.below(4);
                for (uint32_t i = 0; i < n; ++i)
                {
                    list.push_back(newInfo());
                }
            }
            if (g.mNeedsRebuild)
            {
                g.mPending[0].push_back(newInfo());
                g.mPending[0].push_back(newInfo());
                g.mPending[1].push_back(newInfo());
            }
        }
    }

    // the loop's skip rules (dead; auto-hide)
    bool mapSkips(const MapGroup& g) { return g.mDead || g.mHidden; }

    // LLSpatialGroup::rebuildGeom: the group's own draw map is made anew
    void mapRebuild(MapGroup& g)
    {
        if (g.mNeedsRebuild)
        {
            g.mDrawMap.clear();
            g.mDrawMap[1] = g.mPending[0];
            g.mDrawMap[TYPE_ALPHA] = g.mPending[1];
            g.mNeedsRebuild = false;
        }
    }

    // LLPipeline::renderMapPushGroup
    template <typename SINK>
    void mapPushGroup(const MapScene& scene, MapGroup& g, SINK& sink)
    {
        for (auto& entry : g.mDrawMap)
        {
            if (!scene.mTypeMask[entry.first])
            {
                continue;
            }
            for (DrawInfo* info : entry.second)
            {
                sink.pushDrawInfo(entry.first, info);
            }
        }
        if (g.mDrawMap.find(TYPE_ALPHA) != g.mDrawMap.end())
        {
            sink.pushAlphaGroup(&g);
        }
        if (g.mDrawMap.find(TYPE_ALPHA_RIGGED) != g.mDrawMap.end())
        {
            sink.pushRiggedAlphaGroup(&g);
        }
    }

    // the former loop of LLPipeline::postSort
    void serialMap(MapScene& scene, RenderMap& out)
    {
        for (MapGroup& g : scene.mGroups)
        {
            if (mapSkips(g))
            {
                continue;
            }
            mapRebuild(g);
            mapPushGroup(scene, g, out);
        }
    }

    using MapFragment = LLCullJobs::RenderMapFragment<DrawInfo, MapGroup, TYPES>;

    // LLPipeline::buildRenderMapJobs
    size_t jobsMap(MapScene& scene, RenderMap& out, std::vector<MapFragment>& fragments, size_t grain, bool swap_chunks = false)
    {
        std::vector<MapGroup*> kept;
        for (MapGroup& g : scene.mGroups)
        {
            if (mapSkips(g))
            {
                continue;
            }
            mapRebuild(g);
            kept.push_back(&g);
        }
        const size_t chunks = LLCullJobs::runChunks(kept.size(), grain, fragments, [&](size_t i, MapFragment& fragment)
        {
            mapPushGroup(scene, *kept[i], fragment);
        });
        if (swap_chunks && chunks > 1)
        {
            std::swap(fragments[0], fragments[1]);
        }
        for (size_t c = 0; c < chunks; ++c)
        {
            LLCullJobs::mergeRenderMap(fragments[c], out);
        }
        return chunks;
    }

    double seconds()
    {
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }
}

namespace tut
{
    struct cull_jobs_data
    {
    };
    typedef test_group<cull_jobs_data> cull_jobs_group;
    typedef cull_jobs_group::object object;
    cull_jobs_group cull_jobs_tests("LLCullJobs");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("cascade culls as jobs equal the serial culls: lists, node counts and stamps");

        size_t pushed = 0;
        size_t skip_flags = 0;
        const uint32_t seeds[] = { 20261119u, 5u, 77u };
        for (uint32_t seed : seeds)
        {
            Scene scene;
            buildScene(scene, seed, 3, 9, 7);

            // the serial reference: cascade by cascade, as LLPipeline::updateCull does
            CullResult serial[CASCADES];
            resetStamps(scene);
            for (int j = 0; j < CASCADES; ++j)
            {
                serialCull(scene, j, serial[j]);
                pushed += serial[j].mVisibleGroups.size() + serial[j].mDrawableGroups.size();
            }
            const Stamps serial_stamps = readStamps(scene);
            for (auto& g : scene.mGroups)
            {
                skip_flags += g->mSkipFrustumCheck ? 1 : 0;
            }
            ensure("cascade 3 sees more than cascade 0", serial[3].mNodes > serial[0].mNodes);
            ensure("both kinds of partition are found", !serial[3].mVisibleGroups.empty() && !serial[3].mDrawableGroups.empty());

            std::vector<Fragment> fragments;
            for (size_t helpers = 0; helpers <= 7; ++helpers)
            {
                Started started(helpers);
                for (int mode = 0; mode < 3; ++mode) // jobs, serial mode, one chunk (a probe pass)
                {
                    FrameJobs::setSerial(mode == 1);
                    for (int repeat = 0; repeat < 4; ++repeat)
                    {
                        markAllDirty(scene);
                        resetStamps(scene);
                        CullResult jobs[CASCADES];
                        jobsCull(scene, jobs, fragments, mode == 2);
                        for (int j = 0; j < CASCADES; ++j)
                        {
                            ensure("the jobs' lists equal the serial lists", jobs[j] == serial[j]);
                        }
                        ensure("the jobs' stamps equal the serial stamps", readStamps(scene) == serial_stamps);
                    }
                }
            }
        }
        ensure("the scenes push groups", pushed > 2000);
        ensure("the scenes hold single-child chains", skip_flags > 50);
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("deliberate breaks of the cull jobs are caught");

        Scene scene;
        buildScene(scene, 20261119u, 3, 9, 7);
        CullResult serial[CASCADES];
        resetStamps(scene);
        for (int j = 0; j < CASCADES; ++j)
        {
            serialCull(scene, j, serial[j]);
        }
        const Stamps serial_stamps = readStamps(scene);

        Started started(3);
        std::vector<Fragment> fragments;
        const Break breaks[] = { BREAK_NONE, BREAK_SWAP, BREAK_DROP, BREAK_CAMERA_ID, BREAK_DIRTY_ROOT };
        for (Break fault : breaks)
        {
            resetStamps(scene);
            if (fault == BREAK_DIRTY_ROOT)
            {
                // Two equal scenes. In both an object of a leaf that cascade 0 does not see moves into every cascade's
                // frustum after the trees were rebounded, which leaves the leaf and its ancestors dirty with stale
                // bounds. The serial culls rebound and find the leaf; the batch whose root was not rebounded does not.
                Scene moved[2];
                CullResult results[2][CASCADES];
                for (int k = 0; k < 2; ++k)
                {
                    buildScene(moved[k], 20261119u, 3, 9, 7);
                    for (Partition& part : moved[k].mPartitions)
                    {
                        rebound(*part.mRoot);
                    }
                    std::vector<Group*> path;
                    ensure("a leaf outside cascade 0 exists", findLeafOutside(*moved[k].mPartitions[0].mRoot, moved[k].mCameras[0], path));
                    path.back()->mElements[0] = { { 120.f, 120.f, 28.f }, { 136.f, 136.f, 36.f } };
                    for (Group* g : path)
                    {
                        g->mDirty = true; // LLViewerOctreeGroup::unbound
                    }
                    if (k == 0)
                    {
                        for (int j = 0; j < CASCADES; ++j)
                        {
                            serialCull(moved[k], j, results[k][j]);
                        }
                    }
                    else
                    {
                        jobsCull(moved[k], results[k], fragments, false, fault);
                    }
                }
                ensure("a root left dirty before the batch is caught", flatten(results[0]) != flatten(results[1]));

                // and with the roots rebounded, the same two scenes agree
                CullResult again[CASCADES];
                jobsCull(moved[1], again, fragments, false);
                ensure("rebounded before the batch: equal", flatten(results[0]) == flatten(again));
                continue;
            }

            CullResult jobs[CASCADES];
            jobsCull(scene, jobs, fragments, false, fault);
            bool lists_equal = true;
            for (int j = 0; j < CASCADES; ++j)
            {
                lists_equal = lists_equal && jobs[j] == serial[j];
            }
            const bool stamps_equal = readStamps(scene) == serial_stamps;
            switch (fault)
            {
            case BREAK_NONE:
                ensure("no break: equal", lists_equal && stamps_equal);
                break;
            case BREAK_SWAP:
                ensure("two fragments swapped in the merge are caught", !lists_equal);
                break;
            case BREAK_DROP:
                ensure("a dropped fragment is caught", !lists_equal);
                break;
            case BREAK_CAMERA_ID:
                ensure("a job with another cascade's camera id is caught", !stamps_equal);
                break;
            default:
                break;
            }
        }
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("the render map built in chunks equals the former loop");

        const uint32_t seeds[] = { 20261119u, 9u };
        size_t infos = 0;
        for (uint32_t seed : seeds)
        {
            MapScene reference_scene;
            buildMapScene(reference_scene, seed, 3000);
            RenderMap serial;
            serialMap(reference_scene, serial);
            for (size_t t = 0; t < TYPES; ++t)
            {
                infos += serial.mLists[t].size();
                ensure("a masked type stays empty", reference_scene.mTypeMask[t] || serial.mLists[t].empty());
            }
            ensure("blended groups are collected", serial.mAlphaGroups.size() > 50 && serial.mRiggedAlphaGroups.size() > 20);

            std::vector<MapFragment> fragments;
            const size_t grains[] = { 1, 7, 128, 5000 };
            for (size_t helpers = 0; helpers <= 7; ++helpers)
            {
                Started started(helpers);
                for (int mode = 0; mode < 2; ++mode)
                {
                    FrameJobs::setSerial(mode == 1);
                    for (size_t grain : grains)
                    {
                        // a scene of its own for every run (the rebuild changes the groups); the same seed gives the same
                        // groups, but other DrawInfo objects, so the lists are compared by position in the scene
                        MapScene scene;
                        buildMapScene(scene, seed, 3000);
                        RenderMap jobs;
                        const size_t chunks = jobsMap(scene, jobs, fragments, grain);
                        ensure("one chunk when the grain covers the groups", grain < 5000 || chunks == 1);

                        MapScene scene2;
                        buildMapScene(scene2, seed, 3000);
                        RenderMap serial2;
                        serialMap(scene2, serial2);
                        // map the pointers of both runs to indices
                        auto indexOf = [](const MapScene& s, const RenderMap& m)
                        {
                            std::unordered_map<const DrawInfo*, size_t> info_index;
                            for (size_t i = 0; i < s.mInfos.size(); ++i)
                            {
                                info_index[s.mInfos[i].get()] = i;
                            }
                            std::vector<size_t> flat;
                            for (size_t t = 0; t < TYPES; ++t)
                            {
                                flat.push_back(m.mLists[t].size());
                                for (DrawInfo* info : m.mLists[t])
                                {
                                    flat.push_back(info_index[info]);
                                }
                            }
                            flat.push_back(m.mAlphaGroups.size());
                            for (MapGroup* g : m.mAlphaGroups)
                            {
                                flat.push_back((size_t)(g - s.mGroups.data()));
                            }
                            flat.push_back(m.mRiggedAlphaGroups.size());
                            for (MapGroup* g : m.mRiggedAlphaGroups)
                            {
                                flat.push_back((size_t)(g - s.mGroups.data()));
                            }
                            return flat;
                        };
                        ensure("the chunks' render map equals the former loop's", indexOf(scene, jobs) == indexOf(scene2, serial2));
                    }
                }
            }

            // the break: two chunks swapped in the merge
            {
                Started started(3);
                MapScene scene;
                buildMapScene(scene, seed, 3000);
                RenderMap jobs;
                jobsMap(scene, jobs, fragments, 128, true);
                MapScene scene2;
                buildMapScene(scene2, seed, 3000);
                RenderMap serial2;
                serialMap(scene2, serial2);
                bool equal = jobs.mAlphaGroups.size() == serial2.mAlphaGroups.size();
                for (size_t i = 0; equal && i < jobs.mAlphaGroups.size(); ++i)
                {
                    equal = (jobs.mAlphaGroups[i] - scene.mGroups.data()) == (serial2.mAlphaGroups[i] - scene2.mGroups.data());
                }
                ensure("two chunks swapped in the merge are caught", !equal);
            }
        }
        ensure("the scenes hold draw infos", infos > 5000);
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("bench (opt-in: LL_CULL_JOBS_BENCH=1)");
        const char* bench = getenv("LL_CULL_JOBS_BENCH");
        if (!bench || !*bench || *bench == '0')
        {
            return;
        }

        // Time-boxed: a few hundred passes a width. The model's cull is cheaper per node than the viewer's (no SIMD plane
        // tests, no cache misses over a real scene), so the figures show the batch's scaling and overhead, not the
        // viewer's milliseconds.
        Scene scene;
        buildScene(scene, 20261119u, 1, 9, 11);
        size_t groups = scene.mGroups.size();
        CullResult serial[CASCADES];
        for (int j = 0; j < CASCADES; ++j)
        {
            serialCull(scene, j, serial[j]);
        }
        std::printf("cull bench: %zu groups, 9 partitions, 4 cascades, nodes found %d %d %d %d\n", groups, serial[0].mNodes,
            serial[1].mNodes, serial[2].mNodes, serial[3].mNodes);

        const int passes = 300;
        double serial_us = 0.0;
        {
            const double start = seconds();
            for (int p = 0; p < passes; ++p)
            {
                CullResult r[CASCADES];
                for (int j = 0; j < CASCADES; ++j)
                {
                    serialCull(scene, j, r[j]);
                }
            }
            serial_us = (seconds() - start) * 1e6 / passes;
        }
        std::printf("  serial culls (updateCull x 4): %.1f us a pass\n", serial_us);
        std::vector<Fragment> fragments;
        for (size_t helpers = 0; helpers <= 7; ++helpers)
        {
            Started started(helpers);
            FrameJobs::beginFrameWork();
            const double start = seconds();
            for (int p = 0; p < passes; ++p)
            {
                CullResult r[CASCADES];
                jobsCull(scene, r, fragments, false);
            }
            const double us = (seconds() - start) * 1e6 / passes;
            FrameJobs::endFrameWork();
            std::printf("  jobs, %zu helpers: %.1f us a pass (%.2fx)\n", helpers, us, serial_us / us);
        }

        MapScene map_scene;
        buildMapScene(map_scene, 20261119u, 2000);
        for (MapGroup& g : map_scene.mGroups)
        {
            mapRebuild(g);
        }
        double map_serial_us = 0.0;
        {
            const double start = seconds();
            for (int p = 0; p < passes; ++p)
            {
                RenderMap out;
                serialMap(map_scene, out);
            }
            map_serial_us = (seconds() - start) * 1e6 / passes;
        }
        std::printf("render-map bench: 2000 groups, grain 128\n  former loop: %.1f us a build\n", map_serial_us);
        std::vector<MapFragment> map_fragments;
        for (size_t helpers = 0; helpers <= 7; ++helpers)
        {
            Started started(helpers);
            FrameJobs::beginFrameWork();
            const double start = seconds();
            for (int p = 0; p < passes; ++p)
            {
                RenderMap out;
                jobsMap(map_scene, out, map_fragments, 128);
            }
            const double us = (seconds() - start) * 1e6 / passes;
            FrameJobs::endFrameWork();
            std::printf("  chunks, %zu helpers: %.1f us a build (%.2fx)\n", helpers, us, map_serial_us / us);
        }
    }
}
