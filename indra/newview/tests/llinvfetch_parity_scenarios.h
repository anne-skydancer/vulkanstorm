/**
 * @file llinvfetch_parity_scenarios.h
 * @brief The inventory fetch wire scenarios (recorder R3b; survey 3 section
 *        8.2, layer B, and the Second Life fallbacks).
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Vulkanstorm Viewer Project
 * Licensed under the GNU Lesser General Public License, version 2.1.
 * $/LicenseInfo$
 */
#ifndef LL_LLINVFETCH_PARITY_SCENARIOS_H
#define LL_LLINVFETCH_PARITY_SCENARIOS_H

#include "llinvfetch_parity_support.h"

#include <functional>
#include <set>
#include <string>
#include <vector>

namespace vks_fetch
{
    struct ScenarioInfo
    {
        std::string id;
        std::string title;
    };
    // Every scenario, in run order. The order is part of the recording: the
    // AIS 403-at-depth-0 notification is chosen by a function-static flag
    // (first in the process: InventoryLimitReachedAISAlert), s02 reads the
    // cache s01 wrote, and the pool size changes once (sequence scenarios
    // on a 2-coroutine pool, then concurrency scenarios on 20).
    const std::vector<ScenarioInfo>& scenarios();

    // Runs every scenario in order (only those in `only` when it is not
    // empty: a subset may differ from a full run, see above) and hands each
    // case and its response bodies to sink.
    typedef std::function<void(const ScenarioInfo&, const Case&, const std::string& responses)> sink_t;
    // skip: scenarios not run (a named rule of the test; reported there).
    void run_scenarios(const std::string& work_dir, const std::set<std::string>& only,
                       const std::set<std::string>& skip, const sink_t& sink);

    // The pre-D2 reference (README, "D2"): the Second Life legacy-capability
    // and UDP fallbacks, which the D2 port removes. Rule
    // PRE_D2_SL_FALLBACKS skips exactly these, each with the D2 targets that
    // replace it.
    const std::vector<std::pair<std::string, std::string> >& pre_d2_sl_fallbacks();

    // The bench of test 3 (sub-rung R3b): the frozen C++ scheduler against
    // the Rust-backed one, best of `rounds` timings of `calls` calls each.
    struct BenchRow
    {
        std::string name;
        double cpp, rust;
    };
    std::vector<BenchRow> run_bench(const std::string& work_dir, int rounds, long calls);
}

#endif
