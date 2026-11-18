// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LL::FrameJobs: the fork-join job system of the multi-core study's stage 2 (doc/perf/multicore-simd-study.md section 6.2).
//
// A fixed set of helper threads plus the calling thread run the chunks of one parallelFor at a time. The thread that
// called start() (the main thread) is the only one that dispatches; it takes part in every batch and returns when every
// chunk is done. There is no queue, no allocation and no std::function per batch, and no lock except where a chunk throws.
//
// Bit-exact results. The partition is fixed: chunk c covers the indices [c * grain, min(count, (c + 1) * grain)) whatever
// the number of helpers, and whether the batch runs serially or not. Which thread runs a chunk, and in which order the
// chunks run, is not fixed. A job that needs a result equal to the serial one therefore writes into a slot of its own
// index or chunk, and the caller merges the slots in index order after parallelFor returns. For every batch the helpers
// take over the floating-point control state (flush-to-zero, rounding) the owner has at the dispatch.
//
// Job-body rules (study section 6.2): no LLPointer copies, no GL calls, no markDead / killObject, no first use of an
// LLCachedControl, no floating-point reduction across chunks. The calling thread deletes nothing while a batch runs.
// A build with VKS_FRAMEJOBS_GUARDS (development only) asserts some of them: LLRefCount::ref / unref and the GL entry
// points that carry LL_FRAMEJOBS_ASSERT_NOT_IN_JOB (llframejobsguard.h).
//
// Helpers park (std::atomic::wait) when idle; a parked helper needs about 9 to 34 microseconds (median, study section 4.4)
// to wake. Inside the frame-work window (beginFrameWork to endFrameWork, the CPU-preparation part of display()) a helper
// that has just been woken or has just finished a batch spins for about 50 microseconds before it parks again, so batches
// that follow each other closely find it awake. The helpers are woken at the window's start only when the previous window
// dispatched a batch, and otherwise at the first batch, so a frame without parallel work costs nothing.
#ifndef LL_LLFRAMEJOBS_H
#define LL_LLFRAMEJOBS_H

#include "llframejobsguard.h"

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace LL
{
    class FrameJobs
    {
    public:
        // Starts the helper threads; the calling thread becomes the owner. Does nothing when already started.
        static void start(size_t helpers);
        // Joins the helpers. Owner thread only, outside jobs; otherwise, and when not started, it does nothing.
        static void stop();
        // Number of helper threads that run (0 when not started). Owner thread only.
        static size_t helperCount();

        // Serial mode runs every batch on the calling thread, chunk by chunk in index order, with the same partition.
        // Owner thread only.
        static void setSerial(bool serial);
        static bool isSerial();

        // The window in which idle helpers may spin before they park. Owner thread only; calls from other threads do nothing.
        static void beginFrameWork();
        static void endFrameWork();

        class FrameWorkScope
        {
        public:
            FrameWorkScope() { beginFrameWork(); }
            ~FrameWorkScope() { end(); }
            FrameWorkScope(const FrameWorkScope&) = delete;
            FrameWorkScope& operator=(const FrameWorkScope&) = delete;
            void end()
            {
                if (mOpen)
                {
                    mOpen = false;
                    endFrameWork();
                }
            }

        private:
            bool mOpen = true;
        };

        // True on any thread while it runs a chunk (serial mode included).
        static bool inJob() { return FrameJobsGuard::tInJob; }

        static size_t chunkCount(size_t count, size_t grain)
        {
            return grain == 0 ? count : count / grain + (count % grain != 0);
        }

        // Calls fn(begin, end, chunk) once for every chunk of [0, count), grain indices a chunk (a grain of 0 counts as 1).
        // Returns when all chunks are done. Chunks may run at the same time on different threads; choose the grain so that a
        // chunk is worth a dispatch, and call this only for batches above about 50 microseconds of work. A single chunk, a
        // call from inside a job, a call from a thread that is not the owner, serial mode and a stopped system all run on
        // the calling thread in index order.
        // All threads call the one fn object: state it owns (the by-value captures of a `mutable` lambda, members of a
        // functor) is shared between the chunks and must not be written.
        // If chunks throw, the exception of the lowest throwing chunk is rethrown here; every chunk below it has run, as in
        // a serial loop, and chunks above it may or may not have.
        template <typename FN>
        static void parallelFor(size_t count, size_t grain, FN&& fn)
        {
            using fn_t = std::remove_reference_t<FN>;
            run(count, grain,
                [](void* ctx, size_t begin, size_t end, size_t chunk) { (*static_cast<fn_t*>(ctx))(begin, end, chunk); },
                const_cast<void*>(static_cast<const void*>(std::addressof(fn))));
        }

        // The width rule: no helpers below 4 fast cores (every batch then runs serially), otherwise
        // clamp(fast cores - 3, 2, 7) and never more than fast cores - 1. The main thread is one more worker; the cores
        // left are for the GL driver thread, the decode pools and the window thread.
        static size_t widthForCores(size_t fast_physical_cores);
        // Physical cores that share the calling thread's L3 cache and that the process may run on; on processors with
        // performance and efficiency cores (Windows), the performance cores among them. Where the topology cannot be read:
        // std::thread::hardware_concurrency(), halved on x86, where two threads a core are likely. Never 0. `source`, if
        // given, receives a static string naming where the number came from. Helpers are not pinned to these cores.
        static size_t fastPhysicalCores(const char** source = nullptr);

        // Parts of the Linux topology reading, here for the unit tests.
        // A sysfs CPU list such as "0-7,16-23", in ascending order without duplicates. Parsing stops at the first malformed
        // part; what was read before it is kept.
        static std::vector<int> parseCpuList(const std::string& list);
        // Physical cores among `cpus`, cut down to those in `domain` unless it is empty: one for every distinct entry of
        // `siblings`, the thread-sibling list of each of `cpus` (same order; an empty entry counts as a core of its own).
        static size_t countPhysicalCores(const std::vector<int>& cpus, const std::vector<int>& domain,
                                         const std::vector<std::string>& siblings);

    private:
        using chunk_fn = void (*)(void* ctx, size_t begin, size_t end, size_t chunk);
        static void run(size_t count, size_t grain, chunk_fn fn, void* ctx);
    };
} // namespace LL

#endif // LL_LLFRAMEJOBS_H
