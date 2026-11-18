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
// - The only lock, mErrorMutex, is taken by a chunk that throws.

#include "linden_common.h"

#include "llframejobs.h"

#include <algorithm>
#include <atomic>
#include <chrono>
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
    using LL::FrameJobsGuard::tInJob;

    constexpr size_t NO_CHUNK = ~size_t(0);
    constexpr size_t CACHE_LINE = 64;
    // How long an idle helper spins inside the frame-work window before it parks, counted from its last wake or batch.
    constexpr std::chrono::microseconds SPIN_TIME{ 50 };

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
    // Gives this thread the owner's state; MXCSR is written only when it differs.
    inline void adoptFPState(const fp_state_t& state)
    {
        if (readFPState() != state)
        {
            _mm_setcsr(state);
        }
    }
#else
    using fp_state_t = std::fenv_t;
    inline fp_state_t readFPState()
    {
        fp_state_t state;
        std::fegetenv(&state);
        return state;
    }
    inline void adoptFPState(const fp_state_t& state) { std::fesetenv(&state); }
#endif

    struct State
    {
        alignas(CACHE_LINE) std::atomic<uint32_t> mEpoch{ 0 };  // odd: a batch is open
        alignas(CACHE_LINE) std::atomic<size_t> mNext{ 0 };     // next chunk to claim
        alignas(CACHE_LINE) std::atomic<uint32_t> mActive{ 0 }; // helpers that may be touching the batch
        alignas(CACHE_LINE) std::atomic<uint32_t> mParked{ 0 }; // helpers waiting on mEpoch
        std::atomic<bool> mWindow{ false };                     // idle helpers may spin before they park
        std::atomic<bool> mQuit{ false };

        // The batch. Written by the owner while the epoch is even and no helper reads it.
        alignas(CACHE_LINE) void (*mFn)(void*, size_t, size_t, size_t) = nullptr;
        void*      mCtx = nullptr;
        size_t     mCount = 0;
        size_t     mGrain = 1;
        size_t     mChunks = 0;
        fp_state_t mFPState{}; // the owner's floating-point control state at the dispatch

        // The lowest chunk that threw, and its exception.
        std::atomic<size_t> mFailChunk{ NO_CHUNK };
        std::mutex          mErrorMutex;
        std::exception_ptr  mError;

        // Owner thread only.
        bool mSerial = false;
        bool mUsedThisWindow = false;
        bool mUsedLastWindow = false;

        std::vector<std::thread> mThreads;
    };

    // Created by start(), deleted by stop(). If the process ends without stop() it is never destroyed, so no joinable
    // std::thread is destructed during static destruction.
    State* sState = nullptr;
    thread_local bool tOwner = false;

    struct JobFlag
    {
        bool mWas = tInJob;
        JobFlag() { tInJob = true; }
        ~JobFlag() { tInJob = mWas; }
    };

    // Chunk `chunk` of [0, count): begin = chunk * grain. Written so that no sum can overflow for a huge grain.
    inline size_t chunkEnd(size_t begin, size_t count, size_t grain)
    {
        return count - begin > grain ? begin + grain : count;
    }

    void workChunks(State& s)
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
                s.mFn(s.mCtx, begin, chunkEnd(begin, count, grain), chunk);
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

    void helperMain(State& s, [[maybe_unused]] std::string name)
    {
        LL_PROFILER_SET_THREAD_NAME(name.c_str());

        uint32_t seen = 0;     // the epoch value this helper has dealt with
        bool     spin = false; // armed by every epoch change, cleared SPIN_TIME later
        unsigned spins = 0;
        std::chrono::steady_clock::time_point spin_end;
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
                        adoptFPState(s.mFPState);
                        workChunks(s);
                    }
                    s.mActive.fetch_sub(1);
                }
                spin = true;
                spins = 0;
                continue;
            }
            if (s.mQuit.load(std::memory_order_relaxed))
            {
                break;
            }
            if (spin && s.mWindow.load(std::memory_order_relaxed))
            {
                cpuPause();
                if ((++spins & 31) == 0) // the clock is read every 32 pauses, first to start the spin time
                {
                    const auto now = std::chrono::steady_clock::now();
                    if (spins == 32)
                    {
                        spin_end = now + SPIN_TIME;
                    }
                    else if (now >= spin_end)
                    {
                        spin = false;
                    }
                }
                continue;
            }
            s.mParked.fetch_add(1);
            s.mEpoch.wait(epoch);
            s.mParked.fetch_sub(1);
        }
    }

    // Moves the epoch to `epoch` and wakes the parked helpers.
    void publish(State& s, uint32_t epoch)
    {
        s.mEpoch.store(epoch);
        if (s.mParked.load() != 0)
        {
            s.mEpoch.notify_all();
        }
    }

    void runSerial(size_t count, size_t grain, size_t chunks, void (*fn)(void*, size_t, size_t, size_t), void* ctx)
    {
        JobFlag in_job;
        for (size_t chunk = 0; chunk < chunks; ++chunk)
        {
            const size_t begin = chunk * grain;
            fn(ctx, begin, chunkEnd(begin, count, grain), chunk);
        }
    }

    // The fallback where the topology cannot be read: logical processors, halved on x86 where two threads a core are likely.
    size_t guessPhysicalCores(const char*& source)
    {
        const size_t logical = std::thread::hardware_concurrency();
#if LL_FRAMEJOBS_X86
        source = "hardware_concurrency / 2 (topology not read)";
        return logical / 2;
#else
        source = "hardware_concurrency (topology not read)";
        return logical;
#endif
    }
} // namespace

namespace LL
{
    void FrameJobs::start(size_t helpers)
    {
        if (sState)
        {
            return;
        }
        State* s = new State;
        s->mThreads.reserve(helpers);
        try
        {
            for (size_t i = 0; i < helpers; ++i)
            {
                s->mThreads.emplace_back(helperMain, std::ref(*s), "FrameJobs:" + std::to_string(i + 1));
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
        publish(*s, s->mEpoch.load() + 2);
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
            // The last window had parallel work, so this one probably has: wake the helpers before the first batch. They
            // park again if none comes within the spin time.
            publish(s, s.mEpoch.load() + 2);
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
        const size_t chunks = chunkCount(count, grain);
        if (chunks == 1 || !tOwner || tInJob || !sState || sState->mSerial || sState->mThreads.empty())
        {
            runSerial(count, grain, chunks, fn, ctx);
            return;
        }

        LL_PROFILE_ZONE_NAMED("FrameJobs batch");
        State& s = *sState;
        s.mFn = fn;
        s.mCtx = ctx;
        s.mCount = count;
        s.mGrain = grain;
        s.mChunks = chunks;
        s.mFPState = readFPState();
        s.mFailChunk.store(NO_CHUNK, std::memory_order_relaxed);
        s.mNext.store(0, std::memory_order_relaxed);
        if (s.mWindow.load(std::memory_order_relaxed))
        {
            s.mUsedThisWindow = true;
        }

        const uint32_t epoch = s.mEpoch.load(std::memory_order_relaxed) + 1; // odd: the epoch is even between batches
        publish(s, epoch);
        workChunks(s);
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
        if (fast_physical_cores < 4)
        {
            // Helpers plus the main thread, the GL driver thread and the decode pools would fight for the cores.
            return 0;
        }
        return std::min(std::clamp<size_t>(fast_physical_cores - 3, 2, 7), fast_physical_cores - 1);
    }

    std::vector<int> FrameJobs::parseCpuList(const std::string& list)
    {
        constexpr int MAX_CPU = 1 << 16;
        std::vector<int> cpus;
        size_t pos = 0;
        // Reads a decimal number at pos; false when there is none or it is out of range.
        auto number = [&](int& value) -> bool
        {
            const size_t start = pos;
            value = 0;
            while (pos < list.size() && list[pos] >= '0' && list[pos] <= '9' && value < MAX_CPU)
            {
                value = value * 10 + (list[pos++] - '0');
            }
            return pos > start && value < MAX_CPU;
        };
        for (;;)
        {
            int first = 0;
            if (!number(first))
            {
                break;
            }
            int last = first;
            if (pos < list.size() && list[pos] == '-')
            {
                ++pos;
                if (!number(last) || last < first)
                {
                    break;
                }
            }
            for (int cpu = first; cpu <= last; ++cpu)
            {
                cpus.push_back(cpu);
            }
            if (pos >= list.size() || list[pos] != ',')
            {
                break;
            }
            ++pos;
        }
        std::sort(cpus.begin(), cpus.end());
        cpus.erase(std::unique(cpus.begin(), cpus.end()), cpus.end());
        return cpus;
    }

    size_t FrameJobs::countPhysicalCores(const std::vector<int>& cpus, const std::vector<int>& domain,
                                         const std::vector<std::string>& siblings)
    {
        std::vector<std::string> cores;
        for (size_t i = 0; i < cpus.size(); ++i)
        {
            if (!domain.empty() && std::find(domain.begin(), domain.end(), cpus[i]) == domain.end())
            {
                continue;
            }
            const bool known = i < siblings.size() && !siblings[i].empty();
            cores.push_back(known ? siblings[i] : "cpu" + std::to_string(cpus[i]));
        }
        std::sort(cores.begin(), cores.end());
        return size_t(std::unique(cores.begin(), cores.end()) - cores.begin());
    }

    size_t FrameJobs::fastPhysicalCores(const char** source_out)
    {
        size_t cores = 0;
        const char* source = "";
#if LL_WINDOWS
        // The process's cores in the calling thread's processor group, cut down to those under the same L3 cache, and of
        // those the ones of the highest efficiency class (the performance cores of a hybrid processor).
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
        int top_class = -1;
        for (DWORD offset = 0, bytes = read(RelationProcessorCore, buffer); offset < bytes;)
        {
            auto* info = reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.data() + offset);
            if (info->Processor.GroupMask[0].Group == here.Group && (info->Processor.GroupMask[0].Mask & domain))
            {
                const int efficiency_class = info->Processor.EfficiencyClass;
                if (efficiency_class > top_class)
                {
                    top_class = efficiency_class;
                    cores = 0;
                }
                cores += (efficiency_class == top_class);
            }
            offset += info->Size;
        }
        source = "L3 cache topology";
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
        cpu_set_t allowed;
        CPU_ZERO(&allowed);
        const int here = sched_getcpu();
        if (here >= 0 && sched_getaffinity(0, sizeof(allowed), &allowed) == 0)
        {
            const std::string cpu_dir = "/sys/devices/system/cpu/cpu";
            std::vector<int> domain;
            for (int index = 0; index < 10 && domain.empty(); ++index)
            {
                const std::string cache_dir = cpu_dir + std::to_string(here) + "/cache/index" + std::to_string(index);
                if (read_line(cache_dir + "/level") == "3")
                {
                    domain = parseCpuList(read_line(cache_dir + "/shared_cpu_list"));
                }
            }
            std::vector<int> cpus;
            std::vector<std::string> siblings;
            for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu)
            {
                if (CPU_ISSET(cpu, &allowed))
                {
                    cpus.push_back(cpu);
                    siblings.push_back(read_line(cpu_dir + std::to_string(cpu) + "/topology/thread_siblings_list"));
                }
            }
            cores = countPhysicalCores(cpus, domain, siblings);
            source = "sysfs topology";
        }
#endif
        if (cores == 0)
        {
            cores = guessPhysicalCores(source);
        }
        if (source_out)
        {
            *source_out = source;
        }
        return std::max<size_t>(cores, 1);
    }
} // namespace LL
