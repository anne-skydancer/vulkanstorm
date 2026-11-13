// SPDX-License-Identifier: GPL-3.0-or-later
// TEMPORARY stage-0 particle CPU probe (codex/particles-s0-probe). Never merged.
// Main-thread only. Counters are reset every frame by LLParticleProbe::frameEnd().
#pragma once

#include "llprofiler.h"
#include "linden_common.h"
#include <chrono>
#include <intrin.h>

// 1: wrap every face fill / batch step in a Tracy zone too (distorts the timing); 0: accumulated ticks only
#define PARTICLE_PROBE_FACE_ZONES 0

namespace LLParticleProbe
{
    struct Frame
    {
        U64 puts = 0, put_scanned = 0;
        U64 groups_created = 0, groups_deleted = 0;
        U64 sgroups_rebuilt = 0;
        U64 faces = 0, drawinfos = 0;
        U64 fill_ticks = 0, batch_ticks = 0, upload_ticks = 0;
        U64 upload_bytes = 0, upload_buffers = 0;
    };
    inline Frame f;      // current frame
    inline Frame sum;    // sums for the 1 Hz log
    inline U64 sum_frames = 0, sum_parts_max = 0, sum_parts_total = 0, sum_groups_total = 0;
    inline bool sUploadingParticle = false;  // set around the flush of a particle buffer

    inline double ticksPerUs()
    {
        static double rate = []()
        {
            auto t0 = std::chrono::steady_clock::now();
            U64 c0 = __rdtsc();
            while (std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(20)) {}
            U64 c1 = __rdtsc();
            auto t1 = std::chrono::steady_clock::now();
            double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
            return double(c1 - c0) / us;
        }();
        return rate;
    }

    struct Ticks   // adds elapsed TSC ticks to a counter on scope exit
    {
        U64& dst; U64 t0;
        explicit Ticks(U64& d) : dst(d), t0(__rdtsc()) {}
        ~Ticks() { dst += __rdtsc() - t0; }
    };
}
