// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Tests for LL::FrameJobs (multi-core stage 2): the fixed partition, serial == parallel bit for bit on a model of the
// pipeline's render-map build, exceptions, nested and foreign calls, and a dispatch stress run.

// Precompiled header
#include "linden_common.h"
// associated header
#include "llframejobs.h"
// std headers
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#if defined(__x86_64__) || defined(_M_X64)
#include <xmmintrin.h>
#define TEST_HAS_MXCSR 1
#else
#define TEST_HAS_MXCSR 0
#endif
// other Linden headers
#include "../test/lltut.h"

using LL::FrameJobs;

namespace
{
    // Stops the job system when a test leaves, also by a failed ensure.
    struct Started
    {
        explicit Started(size_t helpers) { FrameJobs::start(helpers); }
        ~Started() { FrameJobs::stop(); }
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
    };

    // --- A model of LLPipeline::postSort's "build render map" loop -------------------------------------------------------
    // Groups hold a draw map (render type -> draw infos). The loop skips dead and occluded groups and masked-out types,
    // appends every remaining draw info to the list of its type, counts triangles, collects the groups with an alpha entry
    // and computes a distance per group.
    constexpr uint32_t NUM_TYPES = 24;
    constexpr uint32_t TYPE_ALPHA = 7;

    struct DrawInfo
    {
        uint32_t mCount;
    };

    struct Group
    {
        std::map<uint32_t, std::vector<const DrawInfo*>> mDrawMap;
        bool  mDead = false;
        bool  mOccluded = false;
        float mCenter[3] = { 0.f, 0.f, 0.f };
        float mScale = 1.f;
    };

    struct RenderMap
    {
        std::vector<const DrawInfo*> mLists[NUM_TYPES];
        std::vector<const Group*>    mAlphaGroups;
        uint64_t                     mTriangles = 0;

        void append(const RenderMap& other)
        {
            for (uint32_t type = 0; type < NUM_TYPES; ++type)
            {
                mLists[type].insert(mLists[type].end(), other.mLists[type].begin(), other.mLists[type].end());
            }
            mAlphaGroups.insert(mAlphaGroups.end(), other.mAlphaGroups.begin(), other.mAlphaGroups.end());
            mTriangles += other.mTriangles;
        }

        bool operator==(const RenderMap& other) const
        {
            for (uint32_t type = 0; type < NUM_TYPES; ++type)
            {
                if (mLists[type] != other.mLists[type])
                {
                    return false;
                }
            }
            return mAlphaGroups == other.mAlphaGroups && mTriangles == other.mTriangles;
        }
    };

    struct Scene
    {
        std::vector<DrawInfo> mInfos;
        std::vector<Group>    mGroups;
        bool                  mTypeMask[NUM_TYPES];
        float                 mCamera[3];
    };

    void buildScene(Scene& scene, uint32_t seed, size_t num_groups)
    {
        Lcg rng(seed);
        scene.mInfos.resize(num_groups * 6);
        for (DrawInfo& info : scene.mInfos)
        {
            info.mCount = rng.next() % 5000;
        }
        for (uint32_t type = 0; type < NUM_TYPES; ++type)
        {
            scene.mTypeMask[type] = (rng.next() % 5) != 0;
        }
        scene.mTypeMask[TYPE_ALPHA] = true;
        for (float& c : scene.mCamera)
        {
            c = float(rng.next() % 4096) * 0.37f;
        }
        scene.mGroups.resize(num_groups);
        size_t next_info = 0;
        for (Group& group : scene.mGroups)
        {
            group.mDead = (rng.next() % 17) == 0;
            group.mOccluded = (rng.next() % 5) == 0;
            for (float& c : group.mCenter)
            {
                c = float(rng.next() % 4096) * 0.41f;
            }
            // One group in four gets a scale so small that the scaled distance is a denormal: the result then depends on
            // the thread's flush-to-zero state.
            group.mScale = (rng.next() % 4) == 0 ? 1e-30f : 0.01f * float(1 + rng.next() % 100);
            const uint32_t entries = rng.next() % 6;
            for (uint32_t i = 0; i < entries; ++i)
            {
                group.mDrawMap[rng.next() % NUM_TYPES].push_back(&scene.mInfos[next_info++]);
            }
        }
    }

    float groupDistance(const Scene& scene, const Group& group)
    {
        float d2 = 0.f;
        for (int i = 0; i < 3; ++i)
        {
            const float d = group.mCenter[i] - scene.mCamera[i];
            d2 += d * d;
        }
        // 1e-30f * 1e-12f * distance is far below FLT_MIN: zero with flush-to-zero, a denormal without.
        return std::sqrt(d2) * group.mScale * 1e-12f;
    }

    // One group of the loop, into `out`; the distance goes to the group's own slot.
    void addGroup(const Scene& scene, size_t index, RenderMap& out, float* distances)
    {
        const Group& group = scene.mGroups[index];
        if (group.mDead || group.mOccluded)
        {
            return;
        }
        for (const auto& [type, infos] : group.mDrawMap)
        {
            if (!scene.mTypeMask[type])
            {
                continue;
            }
            for (const DrawInfo* info : infos)
            {
                out.mLists[type].push_back(info);
                out.mTriangles += info->mCount;
            }
        }
        if (group.mDrawMap.find(TYPE_ALPHA) != group.mDrawMap.end())
        {
            distances[index] = groupDistance(scene, group);
            out.mAlphaGroups.push_back(&group);
        }
    }

    // Today's loop: one pass over the groups in order.
    void buildReference(const Scene& scene, RenderMap& out, std::vector<float>& distances)
    {
        distances.assign(scene.mGroups.size(), -1.f);
        for (size_t i = 0; i < scene.mGroups.size(); ++i)
        {
            addGroup(scene, i, out, distances.data());
        }
    }

    // The FrameJobs form: one fragment per chunk, merged in chunk order.
    void buildWithJobs(const Scene& scene, size_t grain, RenderMap& out, std::vector<float>& distances)
    {
        distances.assign(scene.mGroups.size(), -1.f);
        std::vector<RenderMap> fragments(FrameJobs::chunkCount(scene.mGroups.size(), grain));
        FrameJobs::parallelFor(scene.mGroups.size(), grain,
            [&](size_t begin, size_t end, size_t chunk)
            {
                for (size_t i = begin; i < end; ++i)
                {
                    addGroup(scene, i, fragments[chunk], distances.data());
                }
            });
        for (const RenderMap& fragment : fragments)
        {
            out.append(fragment);
        }
    }

    bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
    {
        return a.size() == b.size() && (a.empty() || std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0);
    }
} // namespace

namespace tut
{
    struct llframejobs_data
    {
    };
    typedef test_group<llframejobs_data> llframejobs_group;
    typedef llframejobs_group::object object;
    llframejobs_group llframejobsgrp("llframejobs");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("width rule");
        // clamp(cores - 3, 2, 7): 5 helpers on 8 cores, 3 on 6, 2 on 4.
        const size_t expected[] = { 2, 2, 2, 2, 2, 2, 3, 4, 5, 6, 7, 7, 7 };
        for (size_t cores = 0; cores < std::size(expected); ++cores)
        {
            ensure_equals("width for " + std::to_string(cores) + " cores", FrameJobs::widthForCores(cores), expected[cores]);
        }
        ensure_equals("width for 64 cores", FrameJobs::widthForCores(64), size_t(7));
        ensure("the machine has a core", FrameJobs::fastPhysicalCores() >= 1);
        ensure("the fast domain is not larger than the machine",
               FrameJobs::fastPhysicalCores() <= std::max(1u, std::thread::hardware_concurrency()));
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("fixed partition, every index once");
        const size_t counts[] = { 0, 1, 2, 63, 64, 65, 1000, 4099 };
        const size_t grains[] = { 0, 1, 7, 64, 5000 };
        for (size_t helpers : { 0, 1, 2, 5, 7 })
        {
            Started started(helpers);
            ensure_equals("helper count", FrameJobs::helperCount(), helpers);
            for (bool serial : { false, true })
            {
                FrameJobs::setSerial(serial);
                for (size_t count : counts)
                {
                    for (size_t grain : grains)
                    {
                        const size_t step = grain ? grain : 1;
                        const size_t chunks = FrameJobs::chunkCount(count, grain);
                        ensure_equals("chunk count", chunks, (count + step - 1) / step);
                        std::vector<std::atomic<int>> visits(count);
                        std::vector<std::pair<size_t, size_t>> ranges(chunks, { ~size_t(0), ~size_t(0) });
                        std::atomic<int> bad_chunk{ 0 };
                        FrameJobs::parallelFor(count, grain,
                            [&](size_t begin, size_t end, size_t chunk)
                            {
                                if (chunk >= chunks || ranges[chunk].first != ~size_t(0))
                                {
                                    ++bad_chunk;
                                    return;
                                }
                                ranges[chunk] = { begin, end };
                                for (size_t i = begin; i < end; ++i)
                                {
                                    ++visits[i];
                                }
                            });
                        ensure_equals("chunk numbers in range, each once", bad_chunk.load(), 0);
                        for (size_t chunk = 0; chunk < chunks; ++chunk)
                        {
                            ensure_equals("chunk begin", ranges[chunk].first, chunk * step);
                            ensure_equals("chunk end", ranges[chunk].second, std::min(count, (chunk + 1) * step));
                        }
                        for (size_t i = 0; i < count; ++i)
                        {
                            ensure_equals("index visited once", visits[i].load(), 1);
                        }
                    }
                }
            }
        }
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("render map and draw lists: serial == parallel, bit for bit");
        // The viewer's floating-point state (LLVector4a::initClass). The helpers must inherit it: the scene holds values
        // that are zero with flush-to-zero and denormals without.
#if TEST_HAS_MXCSR
        struct RestoreCsr
        {
            unsigned int mCsr = _mm_getcsr();
            ~RestoreCsr() { _mm_setcsr(mCsr); }
        } restore_csr;
        _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
        _MM_SET_ROUNDING_MODE(_MM_ROUND_NEAREST);
#endif
        {
            for (uint32_t seed : { 1u, 2u, 3u })
            {
                Scene scene;
                buildScene(scene, seed, 6000 + 37 * seed);
                RenderMap reference;
                std::vector<float> reference_distances;
                buildReference(scene, reference, reference_distances);
                size_t draw_infos = 0;
                for (const auto& list : reference.mLists)
                {
                    draw_infos += list.size();
                }
                ensure("the scene draws something", draw_infos > 1000 && reference.mAlphaGroups.size() > 100);
                size_t flushed = 0;
                for (float d : reference_distances)
                {
                    flushed += (d == 0.f);
                }
#if TEST_HAS_MXCSR
                ensure("the scene has flushed distances", flushed > 10);
#endif

                for (size_t helpers : { 1, 2, 3, 5, 7 })
                {
                    Started started(helpers);
                    for (bool serial : { true, false })
                    {
                        FrameJobs::setSerial(serial);
                        for (size_t grain : { 1, 16, 64, 1000 })
                        {
                            for (int repeat = 0; repeat < (serial ? 1 : 25); ++repeat)
                            {
                                RenderMap result;
                                std::vector<float> distances;
                                buildWithJobs(scene, grain, result, distances);
                                ensure("render map equals the serial loop's", result == reference);
                                ensure("distances equal the serial loop's, bit for bit", sameBits(distances, reference_distances));
                            }
                        }
                    }
                }
            }
        }
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("helpers take part");
        Started started(2);
        std::mutex mutex;
        std::set<std::thread::id> threads;
        std::atomic<bool> timed_out{ false };
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        FrameJobs::parallelFor(8, 1,
            [&](size_t, size_t, size_t)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex);
                    threads.insert(std::this_thread::get_id());
                }
                // Hold the chunk until a second thread has one too.
                for (;;)
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        if (threads.size() >= 2)
                        {
                            break;
                        }
                    }
                    if (std::chrono::steady_clock::now() > deadline)
                    {
                        timed_out = true;
                        break;
                    }
                    std::this_thread::yield();
                }
            });
        ensure("a helper ran a chunk next to the caller", !timed_out && threads.size() >= 2);
        ensure("not in a job after the batch", !FrameJobs::inJob());
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("the lowest throwing chunk is rethrown, lower chunks have run");
        Started started(5);
        for (bool serial : { false, true })
        {
            FrameJobs::setSerial(serial);
            for (int repeat = 0; repeat < 200; ++repeat)
            {
                std::vector<std::atomic<int>> ran(100);
                std::string what;
                try
                {
                    FrameJobs::parallelFor(100, 1,
                        [&](size_t begin, size_t, size_t chunk)
                        {
                            if (chunk == 10 || chunk == 37 || chunk == 80)
                            {
                                throw std::runtime_error(std::to_string(chunk));
                            }
                            ++ran[begin];
                        });
                }
                catch (const std::runtime_error& e)
                {
                    what = e.what();
                }
                ensure_equals("exception of the lowest throwing chunk", what, std::string("10"));
                for (size_t i = 0; i < 10; ++i)
                {
                    ensure_equals("chunk below the throwing one ran", ran[i].load(), 1);
                }
                for (size_t i = 0; i < ran.size(); ++i)
                {
                    ensure("no chunk ran twice", ran[i].load() <= 1);
                }
                ensure("not in a job after an exception", !FrameJobs::inJob());
            }
            // The system still works.
            std::vector<int> slots(1000, 0);
            FrameJobs::parallelFor(slots.size(), 10, [&](size_t begin, size_t end, size_t) { for (size_t i = begin; i < end; ++i) slots[i] = int(i); });
            for (size_t i = 0; i < slots.size(); ++i)
            {
                ensure_equals("slot after an exception", slots[i], int(i));
            }
        }
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("nested, foreign and serial calls run on the calling thread in order");
        Started started(3);

        // Nested: a chunk that calls parallelFor gets a serial loop on its own thread.
        std::atomic<int> nested_wrong{ 0 };
        std::atomic<int> not_in_job{ 0 };
        std::atomic<int> nested_chunks{ 0 };
        FrameJobs::parallelFor(64, 1,
            [&](size_t, size_t, size_t)
            {
                not_in_job += !FrameJobs::inJob();
                const std::thread::id outer = std::this_thread::get_id();
                size_t expected = 0;
                FrameJobs::parallelFor(20, 2,
                    [&](size_t, size_t, size_t chunk)
                    {
                        nested_wrong += (std::this_thread::get_id() != outer) || (chunk != expected) || !FrameJobs::inJob();
                        ++expected;
                        ++nested_chunks;
                    });
                not_in_job += !FrameJobs::inJob();
            });
        ensure_equals("inJob inside chunks", not_in_job.load(), 0);
        ensure_equals("nested chunks on the outer chunk's thread, in order", nested_wrong.load(), 0);
        ensure_equals("nested chunks all ran", nested_chunks.load(), 64 * 10);

        // Foreign: a thread that is not the owner gets a serial loop too, and cannot stop or steer the system.
        int foreign_wrong = 0;
        size_t foreign_chunks = 0;
        std::thread foreign([&]()
            {
                const std::thread::id self = std::this_thread::get_id();
                FrameJobs::stop();
                FrameJobs::beginFrameWork();
                FrameJobs::parallelFor(50, 5,
                    [&](size_t, size_t, size_t chunk)
                    {
                        foreign_wrong += (std::this_thread::get_id() != self) || (chunk != foreign_chunks);
                        ++foreign_chunks;
                    });
                FrameJobs::endFrameWork();
            });
        foreign.join();
        ensure_equals("foreign chunks on the foreign thread, in order", foreign_wrong, 0);
        ensure_equals("foreign chunks all ran", foreign_chunks, size_t(10));
        ensure_equals("a foreign stop() does nothing", FrameJobs::helperCount(), size_t(3));

        // Serial mode.
        FrameJobs::setSerial(true);
        ensure("serial mode reported", FrameJobs::isSerial());
        const std::thread::id self = std::this_thread::get_id();
        int serial_wrong = 0;
        size_t serial_chunks = 0;
        FrameJobs::parallelFor(1000, 10,
            [&](size_t, size_t, size_t chunk)
            {
                serial_wrong += (std::this_thread::get_id() != self) || (chunk != serial_chunks) || !FrameJobs::inJob();
                ++serial_chunks;
            });
        ensure_equals("serial chunks on the caller, in order", serial_wrong, 0);
        ensure_equals("serial chunks all ran", serial_chunks, size_t(100));
        FrameJobs::setSerial(false);
        ensure("parallel mode reported", !FrameJobs::isSerial());
    }

    template<> template<>
    void object::test<7>()
    {
        set_test_name("stress: back-to-back batches, windows, parking, start and stop");
        const size_t helpers = std::min<size_t>(7, std::max(2u, std::thread::hardware_concurrency()) - 1);
        {
            Started started(helpers);
            std::vector<uint64_t> slots(64);
            uint64_t wrong = 0;
            for (uint32_t batch = 0; batch < 60000; ++batch)
            {
                if (batch % 100 == 0)
                {
                    FrameJobs::beginFrameWork();
                }
                // A different description every batch: a helper that mixed two batches would write a wrong value or slot.
                const size_t count = 1 + (batch * 7) % 64;
                const size_t grain = 1 + batch % 5;
                const uint64_t key = uint64_t(batch) << 20;
                FrameJobs::parallelFor(count, grain,
                    [&slots, key](size_t begin, size_t end, size_t chunk)
                    {
                        for (size_t i = begin; i < end; ++i)
                        {
                            slots[i] = key + (chunk << 8) + i;
                        }
                    });
                for (size_t i = 0; i < count; ++i)
                {
                    wrong += slots[i] != key + ((i / grain) << 8) + i;
                }
                if (batch % 100 == 60)
                {
                    FrameJobs::endFrameWork();
                }
                if (batch % 10000 == 9999)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2)); // let the helpers park
                }
            }
            ensure_equals("every slot of every batch", wrong, uint64_t(0));
        }
        // Start and stop with work right after the start and right before the stop.
        for (int cycle = 0; cycle < 100; ++cycle)
        {
            Started started(1 + cycle % 4);
            std::vector<int> slots(256, 0);
            if (cycle % 2)
            {
                FrameJobs::beginFrameWork();
            }
            FrameJobs::parallelFor(slots.size(), 4, [&](size_t begin, size_t end, size_t) { for (size_t i = begin; i < end; ++i) slots[i] = cycle + 1; });
            for (int slot : slots)
            {
                ensure_equals("slot in a start/stop cycle", slot, cycle + 1);
            }
        }
        ensure_equals("stopped", FrameJobs::helperCount(), size_t(0));
        // Stopped: batches still run, on the caller.
        size_t chunks = 0;
        FrameJobs::parallelFor(10, 1, [&](size_t, size_t, size_t) { ++chunks; });
        ensure_equals("a stopped system runs the chunks itself", chunks, size_t(10));
    }
} // namespace tut
