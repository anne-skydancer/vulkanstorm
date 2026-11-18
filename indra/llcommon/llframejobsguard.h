// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The "inside a frame job" flag of LL::FrameJobs (llframejobs.h) and the development guard built on it, kept apart so that
// widely included headers (llrefcount.h) need nothing else of the job system.
//
// LL_FRAMEJOBS_ASSERT_NOT_IN_JOB() marks code a job body must never reach: GL calls, plain (not atomic) reference counts.
// It expands to nothing unless the build sets VKS_FRAMEJOBS_GUARDS (CMake option, development builds only).
#ifndef LL_LLFRAMEJOBSGUARD_H
#define LL_LLFRAMEJOBSGUARD_H

#ifndef VKS_FRAMEJOBS_GUARDS
#define VKS_FRAMEJOBS_GUARDS 0
#endif

namespace LL
{
    namespace FrameJobsGuard
    {
        // True on a thread while it runs a chunk of a FrameJobs batch (serial mode included). Written by llframejobs.cpp only.
        inline thread_local bool tInJob = false;
    }
}

#if VKS_FRAMEJOBS_GUARDS
#include "llerror.h"
#define LL_FRAMEJOBS_ASSERT_NOT_IN_JOB() \
    llassert_always_msg(!LL::FrameJobsGuard::tInJob, "reached from inside a FrameJobs job")
#else
#define LL_FRAMEJOBS_ASSERT_NOT_IN_JOB() ((void)0)
#endif

#endif // LL_LLFRAMEJOBSGUARD_H
