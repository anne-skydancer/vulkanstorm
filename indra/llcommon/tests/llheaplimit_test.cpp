// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llheaplimit.h (no viewer libraries; not one of llcommon's integration tests):
//   g++ -std=c++20 -O2 -Wall -Wextra -Werror -I indra/llcommon indra/llcommon/tests/llheaplimit_test.cpp
// LLMemory::updateMemoryInfo caps the system's available memory by what is left of the heap limit
// (MaxHeapSize64, 16 GB by default). In U32 kilobytes that difference wrapped once the working set passed the
// limit; the rule computes it in 64 bits and gives none left from the limit on.

#include "llheaplimit.h"

#include <cstdint>
#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    constexpr std::uint32_t KB_PER_GB = 1024u * 1024u;
    constexpr std::uint32_t HEAP_LIMIT = 16u * KB_PER_GB;              // MaxHeapSize64's default
    constexpr std::uint32_t SYSTEM_AVAILABLE = 9u * KB_PER_GB;         // the owner's sessions: 9 to 15 GB free
    constexpr std::uint32_t UNSET_LIMIT = UINT32_MAX;                  // sMaxHeapSizeInKB before initMaxHeapSizeGB

    // llmemory.cpp before the fix: llmin(available, limit - working set), all U32 kilobytes
    std::uint32_t oldAvailableKB(std::uint32_t available, std::uint32_t limit, std::uint32_t working_set)
    {
        const std::uint32_t heap_left = limit - working_set;
        return available < heap_left ? available : heap_left;
    }

    void test_below_the_limit_unchanged()
    {
        // far below the limit: the system's figure, as before
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, 3u * KB_PER_GB) == SYSTEM_AVAILABLE,
              "far below the heap limit the system's available memory is reported");
        // 15.8 GB: the heap left (about 205 MB) caps it, as before (the low-memory band below the limit)
        const std::uint32_t working_set = HEAP_LIMIT - 205u * 1024u;
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, working_set) == 205u * 1024u,
              "near the heap limit what is left of it is reported");
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, working_set) == oldAvailableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, working_set),
              "below the heap limit the rule gives what the old code gave");
        // the limit not set yet: no cap
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, UNSET_LIMIT, 20u * KB_PER_GB) == SYSTEM_AVAILABLE,
              "an unset heap limit caps nothing");
        // less available than the heap leaves: the system's figure
        check(LLHeapLimit::availableKB(100u * 1024u, HEAP_LIMIT, 4u * KB_PER_GB) == 100u * 1024u,
              "the system's figure when it is the smaller");
    }

    void test_at_and_past_the_limit_none_left()
    {
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, HEAP_LIMIT) == 0, "at the heap limit none is left");
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, 17u * KB_PER_GB) == 0, "past the heap limit none is left");
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, UINT32_MAX) == 0, "the largest working set leaves none");
        check(LLHeapLimit::availableKB(SYSTEM_AVAILABLE, 0, 1) == 0, "a zero limit leaves none");

        // the bug: the old U32 difference wrapped to about 4 TB at 17 GB, so the system's 9 GB came back
        check(oldAvailableKB(SYSTEM_AVAILABLE, HEAP_LIMIT, 17u * KB_PER_GB) == SYSTEM_AVAILABLE,
              "the old code reported the system's memory again past the heap limit (the wrap this fixes)");
        check(HEAP_LIMIT - 17u * KB_PER_GB > 4000u * KB_PER_GB, "the old difference wrapped to about 4 TB");
    }

    static_assert(LLHeapLimit::availableKB(10, 20, 15) == 5, "constexpr: the heap left caps");
    static_assert(LLHeapLimit::availableKB(10, 20, 25) == 0, "constexpr: none past the limit");
}

int main()
{
    test_below_the_limit_unchanged();
    test_at_and_past_the_limit_none_left();
    if (failures == 0)
    {
        std::printf("llheaplimit_test: all checks passed\n");
    }
    return failures == 0 ? 0 : 1;
}
