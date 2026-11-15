// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Per-frame event counters for the multi-core study's stage 1 (doc/perf/multicore-baseline-2026-11.md): how often the skin
// palette is built, how many cull and state-sort passes run, how many projector tiles and avatar layers are rendered. Each
// counter is added to where the event happens and plotted once a frame, just before the Tracy frame mark, as a Tracy plot.
//
// With Tracy off (LL_PROFILER_CONFIGURATION below LL_PROFILER_CONFIG_TRACY) every macro expands to nothing: no counter
// storage, no atomic, no argument evaluation, no log line. The adders are relaxed atomics because the events can in principle come from
// more than one thread; the plot is read and reset on the main thread only (LLAppViewer::doFrame).
#ifndef LL_LLFRAMECOUNTERS_H
#define LL_LLFRAMECOUNTERS_H

#include "llprofiler.h"

#if defined(LL_PROFILER_CONFIGURATION) && (LL_PROFILER_CONFIGURATION >= LL_PROFILER_CONFIG_TRACY)

#include <atomic>
#include <cstdint>
#include <string>

#include "llerror.h"
#include "lltimer.h"

namespace LLFrameCounters
{
    enum Counter : unsigned
    {
        SkinPaletteCalls,   // initSkinningMatrixPalette calls (K4)
        SkinPaletteJoints,  // matMulUnsafe calls inside them: the sum of the joint counts (K4)
        CullPasses,         // LLPipeline::updateCull
        StateSortPasses,    // LLPipeline::stateSort(camera, result)
        ProjectorTiles,     // projector pool tile renders
        ProjectorAvatarLayers, // projector pool avatar-layer passes
        NumCounters
    };

    inline std::atomic<std::uint32_t> gCounts[NumCounters];

    inline const char* name(unsigned c)
    {
        static const char* const names[NumCounters] = {
            "skin_palette_calls", "skin_palette_joints", "cull_passes", "state_sort_passes",
            "projector_tiles", "projector_avatar_layers"};
        return names[c];
    }

    inline void add(Counter c, std::uint32_t n) { gCounts[c].fetch_add(n, std::memory_order_relaxed); }

    // Main thread, once a frame: plot every counter and reset it. Tracy's csvexport cannot export plots, so the sums over each
    // second also go to the log as one "FrameCounters:" line (frames, then each counter's total).
    inline void plotAndReset()
    {
        static std::uint64_t sums[NumCounters] = {};
        static std::uint32_t frames = 0;
        static F64 window_start = 0.0;

        ++frames;
        for (unsigned c = 0; c < NumCounters; ++c)
        {
            const std::uint32_t n = gCounts[c].exchange(0, std::memory_order_relaxed);
            sums[c] += n;
            LL_PROFILE_PLOT(name(c), (int64_t)n);
        }
        const F64 now = LLTimer::getTotalSeconds();
        if (window_start == 0.0)
        {
            window_start = now;
        }
        else if (now - window_start >= 1.0)
        {
            std::string line = "FrameCounters: frames=" + std::to_string(frames);
            for (unsigned c = 0; c < NumCounters; ++c)
            {
                line += std::string(" ") + name(c) + "=" + std::to_string(sums[c]);
                sums[c] = 0;
            }
            LL_INFOS("FrameCounters") << line << LL_ENDL;
            frames = 0;
            window_start = now;
        }
    }
}

#define LL_FRAME_COUNT_ADD(counter, n) LLFrameCounters::add(LLFrameCounters::counter, (std::uint32_t)(n))
#define LL_FRAME_COUNT_PLOT() LLFrameCounters::plotAndReset()

#else

#define LL_FRAME_COUNT_ADD(counter, n)
#define LL_FRAME_COUNT_PLOT()

#endif

#endif // LL_LLFRAMECOUNTERS_H
