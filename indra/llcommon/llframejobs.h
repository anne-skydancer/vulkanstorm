// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// LL::FrameJobs: the fork-join job system of the multi-core study's stage 2 (doc/perf/multicore-simd-study.md section 6.2).
//
// A fixed set of helper threads plus the calling thread run the chunks of one parallelFor at a time. The thread that
// called start() (the main thread) is the only one that dispatches; it takes part in every batch and returns when every
// chunk is done. There is no queue, no allocation and no std::function per batch.
//
// Bit-exact results. The partition is fixed: chunk c covers the indices [c * grain, min(count, (c + 1) * grain)) whatever
// the number of helpers, and whether the batch runs serially or not. Which thread runs a chunk, and in which order the
// chunks run, is not fixed. A job that needs a result equal to the serial one therefore writes into a slot of its own
// index or chunk, and the caller merges the slots in index order after parallelFor returns. Helpers run with the
// floating-point control state (flush-to-zero, rounding) the owner had at start().
//
// Job-body rules (study section 6.2): no LLPointer copies, no GL calls, no markDead / killObject, no first use of an
// LLCachedControl, no floating-point reduction across chunks. The calling thread deletes nothing while a batch runs.
// A build with VKS_FRAMEJOBS_GUARDS (development only) asserts the first two.
//
// Helpers park (std::atomic::wait) outside the frame-work window. Inside it (beginFrameWork to endFrameWork, the
// CPU-preparation part of display()) they spin between batches, since a parked helper needs 9 to 34 microseconds (median) to wake.
// They are woken at the window's start only when the previous window dispatched a batch, and otherwise at the first batch,
// so a frame without parallel work costs nothing.
#ifndef LL_LLFRAMEJOBS_H
#define LL_LLFRAMEJOBS_H

#include <cstddef>
#include <memory>
#include <type_traits>

namespace LL
{
    struct FrameJobsImpl;

    class FrameJobs
    {
    public:
        // Starts the helper threads; the calling thread becomes the owner. Does nothing when already started.
        static void start(size_t helpers);
        // Joins the helpers. Owner thread only; does nothing when not started.
        static void stop();
        // Number of helper threads (0 when not started). Owner thread only.
        static size_t helperCount();

        // Serial mode runs every batch on the calling thread, chunk by chunk in index order, with the same partition.
        // Owner thread only.
        static void setSerial(bool serial);
        static bool isSerial();

        // The window in which idle helpers spin instead of parking. Owner thread only; calls from other threads do nothing.
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
        static bool inJob() { return tInJob; }

        static size_t chunkCount(size_t count, size_t grain)
        {
            return grain == 0 ? count : (count + grain - 1) / grain;
        }

        // Calls fn(begin, end, chunk) once for every chunk of [0, count), grain indices a chunk (a grain of 0 counts as 1).
        // Returns when all chunks are done. Chunks may run at the same time on different threads; choose the grain so that a
        // chunk is worth a dispatch, and call this only for batches above about 50 microseconds of work. A single chunk, a call from
        // inside a job, a call from a thread that is not the owner, serial mode and a stopped system all run on the calling
        // thread in index order.
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

        // The width rule (study section 6.2): helpers = clamp(fast physical cores - 3, 2, 7). The main thread is one more worker; the
        // three cores left are for the GL driver thread, the decode pools and the window thread.
        static size_t widthForCores(size_t fast_physical_cores);
        // Physical cores that share the calling thread's L3 cache and that the process may run on. Falls back to
        // std::thread::hardware_concurrency() where the topology cannot be read. Never 0.
        static size_t fastPhysicalCores();

    private:
        friend struct FrameJobsImpl; // llframejobs.cpp
        using chunk_fn = void (*)(void* ctx, size_t begin, size_t end, size_t chunk);
        static void run(size_t count, size_t grain, chunk_fn fn, void* ctx);

        static inline thread_local bool tInJob = false;
    };
} // namespace LL

// Development guard for code that a job body must never reach (GL calls, plain reference counts). It needs llerror.h at the
// place of use and expands to nothing unless the build sets VKS_FRAMEJOBS_GUARDS.
#if VKS_FRAMEJOBS_GUARDS
#define LL_FRAMEJOBS_ASSERT_NOT_IN_JOB() llassert_always_msg(!LL::FrameJobs::inJob(), "reached from inside a FrameJobs job")
#else
#define LL_FRAMEJOBS_ASSERT_NOT_IN_JOB() ((void)0)
#endif

#endif // LL_LLFRAMEJOBS_H
