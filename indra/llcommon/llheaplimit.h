// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llheaplimit.h
 * @brief The memory the viewer may still use under its heap limit (LLMemory::updateMemoryInfo).
 *
 * Free of other viewer headers: LLMemory (llmemory.cpp) applies the rule, and
 * indra/llcommon/tests/llheaplimit_test.cpp checks it standalone.
 *
 * Why: the heap left (the limit, MaxHeapSize64, 16 GB by default, minus the working set) was computed in
 * U32 kilobytes. Once the working set passed the limit the difference wrapped to about 4 TB, so the
 * available memory read as the system's again instead of none, and the texture bias that the low-memory
 * reading raises (LLViewerTexture::isSystemMemoryLow) let go just where it was needed most.
 */

#ifndef LL_LLHEAPLIMIT_H
#define LL_LLHEAPLIMIT_H

#include <algorithm>
#include <cstdint>

namespace LLHeapLimit
{
    // The memory available to the viewer, in KB: what the system has available, but no more than what is left
    // of the heap limit, which is none once the working set has reached it. Computed in 64 bits: no wrap.
    constexpr std::uint32_t availableKB(std::uint32_t system_available_kb, std::uint32_t heap_limit_kb,
                                        std::uint32_t working_set_kb)
    {
        const std::int64_t heap_left_kb = std::max<std::int64_t>(std::int64_t(heap_limit_kb) - std::int64_t(working_set_kb), 0);
        return std::uint32_t(std::min<std::int64_t>(system_available_kb, heap_left_kb));
    }
}

#endif // LL_LLHEAPLIMIT_H
