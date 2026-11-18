// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LL::FrameJobs, see llframejobs.h.
//
// How a batch is shared without a lock:
//
// - mEpoch is odd while a batch is open and even otherwise. The owner writes the batch description while it is even and no
//   helper is inside, then stores the next odd value; that store publishes the description.
// - A helper that sees a new odd epoch e adds itself to mActive and reads the epoch again. Only if it is still e does it
//   touch the batch. The owner closes the batch by storing e + 1 and then waits for mActive to reach 0. Both sides use
//   sequentially consistent operations, so either the owner sees the helper in mActive and waits for it, or the helper sees
//   the closed epoch and leaves the batch alone. A helper therefore never reads a description the owner is rewriting, and
//   never claims a chunk of a batch it has not read.
// - Chunks are claimed with a fetch_add on mNext. The owner claims chunks too and closes the batch only when none is left,
//   so it never waits for a helper to wake: what it waits for is helpers finishing chunks they already hold.
// - Parking: a helper adds itself to mParked and waits on mEpoch with the value it last handled; the wait returns at once
//   if the epoch has moved. The owner notifies after storing the epoch whenever mParked is not 0.

#include "linden_common.h"

#include "llframejobs.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <exception>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#if LL_WINDOWS
#include "llwin32headers.h"
#elif LL_LINUX
#include <fstream>
#include <sched.h>
#include <set>
#endif

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#define LL_FRAMEJOBS_X86 1
#else
#include <cfenv>
#define LL_FRAMEJOBS_X86 0
#endif

namespace
{
    constexpr size_t NO_CHUNK = ~size_t(0);
    constexpr size_t CACHE_LINE = 64;

    inline void cpuPause()
    {
#if LL_FRAMEJOBS_X86
        _mm_pause();
#else
        std::this_thread::yield();
#endif
    }

#if LL_FRAMEJOBS_X86
    // MXCSR without the sticky exception flags: rounding, flush-to-zero, denormals-are-zero and the exception masks.
    using fp_state_t = unsigned int;
    inline fp_state_t readFPState() { return _mm_getcsr() & ~0x3Fu; }
    inline void writeFPState(const fp_state_t& state) { _mm_setcsr(state); }
    inline bool sameFPState(const fp_state_t& a, const fp_state_t& b) { return a == b; }
#else
    using fp_state_t = std::fenv_t;
    inline fp_state_t readFPState()
    {
        fp_state_t state;
        std::fegetenv(&state);
        return state;
    }
    inline void writeFPState(const fp_state_t& state) { std::fesetenv(&state); }
    inline bool sameFPState(const fp_state_t&, const fp_state_t&) { return true; }
#endif

    struct State
    {
        alignas(CACHE_LINE) std::atomic<uint32_t> mEpoch{ 0 };  // odd: a batch is open
        alignas(CACHE_LINE) std::atomic<size_t> mNext{ 0 };     // next chunk to claim
        alignas(CACHE_LINE) std::atomic<uint32_t> mActive{ 0 }; // helpers that may be touching the batch
        alignas(CACHE_LINE) std::atomic<uint32_t> mParked{ 0 }; // helpers waiting on mEpoch
        std::atomic<bool> mWindow{ false };                     // idle helpers spin instead of parking
        std::atomic<bool> mQuit{ false };

        // The batch. Written by the owner while the epoch is even and mActive is 0.
        alignas(CACHE_LINE) void (*mFn)(void*, size_t, size_t, size_t) = nullptr;
        void*  mCtx = nullptr;
        size_t mCount = 0;
        size_t mGrain = 1;
        size_t mChunks = 0;

        // The lowest chunk that threw, and its exception.
        std::atomic<size_t> mFailChunk{ NO_CHUNK };
        std::mutex          mErrorMutex;
        std::exception_ptr  mError;

        // Owner thread only.
        bool mSerial = false;
        bool mUsedThisWindow = false;
        bool mUsedLastWindow = false;

        fp_state_t               mFPState{};
        std::vector<std::thread> mThreads;
    };

    // Created by start(), deleted by stop(). If the process ends without stop() it is never destroyed, so no joinable
    // std::thread is destructed during static destruction.
    State* sState = nullptr;
    thread_local bool tOwner = false;
} // namespace

namespace LL
{
    // Private access for the functions of this file.
    struct FrameJobsImpl
    {
        struct JobFlag
        {
            bool mWas = FrameJobs::tInJob;
            JobFlag() { FrameJobs::tInJob = true; }
            ~JobFlag() { FrameJobs::tInJob = mWas; }
        };

        static void workChunks(State& s)
        {
            const size_t chunks = s.mChunks;
            const size_t count = s.mCount;
            const size_t grain = s.mGrain;
            JobFlag in_job;
            for (;;)
            {
                const size_t chunk = s.mNext.fetch_add(1, std::memory_order_relaxed);
                if (chunk >= chunks)
                {
                    break;
                }
                if (chunk > s.mFailChunk.load(std::memory_order_relaxed))
                {
                    continue; // a serial loop would have stopped before this chunk
                }
                const size_t begin = chunk * grain;
                try
                {
                    s.mFn(s.mCtx, begin, std::min(count, begin + grain), chunk);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(s.mErrorMutex);
                    if (chunk < s.mFailChunk.load(std::memory_order_relaxed))
                    {
                        s.mFailChunk.store(chunk, std::memory_order_relaxed);
                        s.mError = std::current_exception();
                    }
                }
            }
        }

        static void helperMain(State& s, [[maybe_unused]] std::string name)
        {
            LL_PROFILER_SET_THREAD_NAME(name.c_str());
            writeFPState(s.mFPState);

            uint32_t seen = 0; // the epoch value this helper has dealt with
            for (;;)
            {
                const uint32_t epoch = s.mEpoch.load();
                if (epoch != seen)
                {
                    seen = epoch;
                    if (epoch & 1)
                    {
                        s.mActive.fetch_add(1);
                        if (s.mEpoch.load() == epoch)
                        {
                            LL_PROFILE_ZONE_NAMED("FrameJobs helper");
                            workChunks(s);
                        }
                        s.mActive.fetch_sub(1);
                    }
                    continue;
                }
                if (s.mQuit.load(std::memory_order_relaxed))
                {
                    break;
                }
                if (s.mWindow.load(std::memory_order_relaxed))
                {
                    cpuPause();
                    continue;
                }
                s.mParked.fetch_add(1);
                s.mEpoch.wait(epoch);
                s.mParked.fetch_sub(1);
            }
        }

        // Moves the epoch to `epoch` and wakes the parked helpers.
        static void publish(State& s, uint32_t epoch)
        {
            s.mEpoch.store(epoch);
            if (s.mParked.load() != 0)
            {
                s.mEpoch.notify_all();
            }
        }

        static void runSerial(size_t count, size_t grain, void (*fn)(void*, size_t, size_t, size_t), void* ctx)
        {
            JobFlag in_job;
            for (size_t begin = 0, chunk = 0; begin < count; begin += grain, ++chunk)
            {
                fn(ctx, begin, std::min(count, begin + grain), chunk);
            }
        }
    };

    void FrameJobs::start(size_t helpers)
    {
        if (sState)
        {
            return;
        }
        State* s = new State;
        s->mFPState = readFPState();
        s->mThreads.reserve(helpers);
        try
        {
            for (size_t i = 0; i < helpers; ++i)
            {
                s->mThreads.emplace_back(FrameJobsImpl::helperMain, std::ref(*s),
                                         "FrameJobs:" + std::to_string(i + 1) + "/" + std::to_string(helpers));
            }
        }
        catch (const std::system_error&)
        {
            // The system refused a thread: run with the helpers that did start.
        }
        sState = s;
        tOwner = true;
    }

    void FrameJobs::stop()
    {
        if (!tOwner || !sState || tInJob)
        {
            return;
        }
        State* s = sState;
        s->mQuit.store(true);
        s->mWindow.store(false);
        // An even step: no batch opens, but every waiting helper sees the epoch move.
        FrameJobsImpl::publish(*s, s->mEpoch.load() + 2);
        for (std::thread& thread : s->mThreads)
        {
            thread.join();
        }
        sState = nullptr;
        tOwner = false;
        delete s;
    }

    size_t FrameJobs::helperCount()
    {
        return (tOwner && sState) ? sState->mThreads.size() : 0;
    }

    void FrameJobs::setSerial(bool serial)
    {
        if (tOwner && sState)
        {
            sState->mSerial = serial;
        }
    }

    bool FrameJobs::isSerial()
    {
        return !(tOwner && sState) || sState->mSerial || sState->mThreads.empty();
    }

    void FrameJobs::beginFrameWork()
    {
        if (!tOwner || !sState || tInJob)
        {
            return;
        }
        State& s = *sState;
        s.mWindow.store(true);
        if (s.mUsedLastWindow && !s.mSerial)
        {
            // The last window had parallel work, so this one probably has: have the helpers spinning before the first batch.
            FrameJobsImpl::publish(s, s.mEpoch.load() + 2);
        }
    }

    void FrameJobs::endFrameWork()
    {
        if (!tOwner || !sState || tInJob)
        {
            return;
        }
        State& s = *sState;
        s.mWindow.store(false);
        s.mUsedLastWindow = s.mUsedThisWindow;
        s.mUsedThisWindow = false;
    }

    void FrameJobs::run(size_t count, size_t grain, chunk_fn fn, void* ctx)
    {
        if (count == 0)
        {
            return;
        }
        if (grain == 0)
        {
            grain = 1;
        }
        const size_t chunks = (count + grain - 1) / grain;
        if (chunks == 1 || !tOwner || tInJob || !sState || sState->mSerial || sState->mThreads.empty())
        {
            FrameJobsImpl::runSerial(count, grain, fn, ctx);
            return;
        }

        LL_PROFILE_ZONE_NAMED("FrameJobs batch");
        State& s = *sState;
#if VKS_FRAMEJOBS_GUARDS
        // Something changed the owner's rounding or flush-to-zero state after start(): helpers would compute other bits.
        llassert_always_msg(sameFPState(readFPState(), s.mFPState), "FrameJobs: the owner's floating-point state changed");
#endif
        s.mFn = fn;
        s.mCtx = ctx;
        s.mCount = count;
        s.mGrain = grain;
        s.mChunks = chunks;
        s.mFailChunk.store(NO_CHUNK, std::memory_order_relaxed);
        s.mNext.store(0, std::memory_order_relaxed);
        s.mUsedThisWindow = true;

        const uint32_t epoch = s.mEpoch.load(std::memory_order_relaxed) + 1; // odd: the epoch is even between batches
        FrameJobsImpl::publish(s, epoch);
        FrameJobsImpl::workChunks(s);
        s.mEpoch.store(epoch + 1);
        {
            LL_PROFILE_ZONE_NAMED("FrameJobs join");
            for (unsigned spins = 0; s.mActive.load() != 0; ++spins)
            {
                if ((spins & 0x3FF) == 0x3FF)
                {
                    std::this_thread::yield(); // a helper lost its core in the middle of a chunk
                }
                else
                {
                    cpuPause();
                }
            }
        }
        if (s.mFailChunk.load(std::memory_order_relaxed) != NO_CHUNK)
        {
            std::exception_ptr error = s.mError;
            s.mError = nullptr;
            std::rethrow_exception(error);
        }
    }

    size_t FrameJobs::widthForCores(size_t fast_physical_cores)
    {
        return std::clamp<size_t>(fast_physical_cores, 5, 10) - 3; // clamp(cores - 3, 2, 7) without going below zero
    }

    size_t FrameJobs::fastPhysicalCores()
    {
        size_t cores = 0;
#if LL_WINDOWS
        // The process's cores in the calling thread's processor group, cut down to those under the same L3 cache.
        PROCESSOR_NUMBER here{};
        GetCurrentProcessorNumberEx(&here);
        DWORD_PTR process_mask = 0;
        DWORD_PTR system_mask = 0;
        if (!GetProcessAffinityMask(GetCurrentProcess(), &process_mask, &system_mask) || process_mask == 0)
        {
            process_mask = ~DWORD_PTR(0); // a process that spans processor groups reports 0
        }
        const KAFFINITY here_bit = KAFFINITY(1) << here.Number;

        auto read = [](LOGICAL_PROCESSOR_RELATIONSHIP relation, std::vector<char>& buffer) -> DWORD
        {
            DWORD bytes = 0;
            GetLogicalProcessorInformationEx(relation, nullptr, &bytes);
            if (bytes == 0)
            {
                return 0;
            }
            buffer.resize(bytes);
            if (!GetLogicalProcessorInformationEx(relation,
                    reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data()), &bytes))
            {
                return 0;
            }
            return bytes;
        };

        std::vector<char> buffer;
        KAFFINITY domain = process_mask;
        for (DWORD offset = 0, bytes = read(RelationCache, buffer); offset < bytes;)
        {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
            if (info->Cache.Level == 3 && info->Cache.GroupMask.Group == here.Group && (info->Cache.GroupMask.Mask & here_bit))
            {
                domain = info->Cache.GroupMask.Mask & process_mask;
                break;
            }
            offset += info->Size;
        }
        for (DWORD offset = 0, bytes = read(RelationProcessorCore, buffer); offset < bytes;)
        {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
            if (info->Processor.GroupMask[0].Group == here.Group && (info->Processor.GroupMask[0].Mask & domain))
            {
                ++cores;
            }
            offset += info->Size;
        }
#elif LL_LINUX
        // The CPUs this process may run on, cut down to those sharing the current CPU's level-3 cache; one physical core
        // per distinct list of sibling threads.
        auto read_line = [](const std::string& path) -> std::string
        {
            std::ifstream file(path);
            std::string line;
            std::getline(file, line);
            return line;
        };
        // A sysfs CPU list such as "0-7,16-23".
        auto parse_list = [](const std::string& list) -> std::set<int>
        {
            std::set<int> cpus;
            size_t pos = 0;
            while (pos < list.size())
            {
                size_t used = 0;
                int first = 0;
                try
                {
                    first = std::stoi(list.substr(pos), &used);
                }
                catch (const std::exception&)
                {
                    break;
                }
                pos += used;
                int last = first;
                if (pos < list.size() && list[pos] == '-')
                {
                    try
                    {
                        last = std::stoi(list.substr(pos + 1), &used);
                    }
                    catch (const std::exception&)
                    {
                        break;
                    }
                    pos += 1 + used;
                }
                for (int cpu = first; cpu <= last; ++cpu)
                {
                    cpus.insert(cpu);
                }
                if (pos < list.size() && list[pos] == ',')
                {
                    ++pos;
                }
                else
                {
                    break;
                }
            }
            return cpus;
        };

        cpu_set_t allowed;
        CPU_ZERO(&allowed);
        const int here = sched_getcpu();
        if (here >= 0 && sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
        {
            const std::string cpu_dir = "/sys/devices/system/cpu/cpu";
            std::set<int> domain;
            for (int index = 0; index < 10 && domain.empty(); ++index)
            {
                const std::string cache_dir = cpu_dir + std::to_string(here) + "/cache/index" + std::to_string(index);
                if (read_line(cache_dir + "/level") == "3")
                {
                    domain = parse_list(read_line(cache_dir + "/shared_cpu_list"));
                }
            }
            std::set<std::string> physical;
            for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            {
                if (CPU_ISSET(cpu, &allowed) && (domain.empty() || domain.count(cpu)))
                {
                    const std::string siblings = read_line(cpu_dir + std::to_string(cpu) + "/topology/thread_siblings_list");
                    physical.insert(siblings.empty() ? std::to_string(cpu) : siblings);
                }
            }
            cores = physical.size();
        }
#endif
        if (cores == 0)
        {
            cores = std::thread::hardware_concurrency();
        }
        return std::max<size_t>(cores, 1);
    }
} // namespace LL
