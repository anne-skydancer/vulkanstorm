// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   lltask_bench_test.cpp
 * @brief  Micro-benchmark of the AIS-style recursion, plain functions against LL::Task coroutines.
 *
 * doc/clean/plan.md ("Coroutines", stage 4): the AIS parse (AISUpdate::parseUpdate and the nine functions below it,
 * llaisapi.cpp) is a recursion over an LLSD tree that became a chain of Task coroutines. Every call of the chain
 * allocates a coroutine frame, takes its LLSD parameter by value (const) and, at each parseEmbedded, tests the timers inline and awaits checkTimeout
 * only when a timer expired. This test measures what that costs per node, in the fibre bridge (nothing in the tree suspends, so every
 * task runs to its end at once, as the AIS parse does unless its time budget runs out).
 *
 * Three variants walk the same tree, interleaved round by round (so that a loaded machine hits all three alike):
 * - plain: ordinary recursion over const LLSD&;
 * - task: the same recursion as Task coroutines taking the LLSD by value;
 * - task+check: like task, and each node tests an unexpired timer flag inline, as the AIS parse does (no frame on the usual path).
 *
 * The test fails only when the three disagree on the answer. The medians are written to stderr and, when the
 * environment names a file in GITHUB_STEP_SUMMARY (CI), appended there as a table. A sanitizer build checks the
 * answers and reports nothing.
 */

#include "linden_common.h"
#include "llsd.h"
#include "lltask.h"
#include "../test/lltut.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace
{
constexpr int FANOUT = 8;
constexpr int DEPTH = 4; // 1 + 8 + 64 + 512 + 4096 = 4681 nodes, about the size of a 5,000-item AIS fetch
constexpr int ROUNDS = 41;

int sNodes = 0;

LLSD makeTree(int depth, int& next_id)
{
    LLSD node;
    node["id"] = next_id++;
    node["name"] = "category";
    ++sNodes;
    if (depth > 0)
    {
        LLSD kids = LLSD::emptyArray();
        for (int i = 0; i < FANOUT; ++i)
        {
            kids.append(makeTree(depth - 1, next_id));
        }
        node["_embedded"]["categories"] = kids;
    }
    return node;
}

// What each node does besides recursing: read two fields, as parseCategory reads a category map.
inline long long visit(const LLSD& node)
{
    return node["id"].asInteger() + static_cast<long long>(node["name"].asStringRef().size());
}

long long walkPlain(const LLSD& node)
{
    long long sum = visit(node);
    if (node.has("_embedded"))
    {
        const LLSD& kids = node["_embedded"]["categories"];
        for (LLSD::array_const_iterator it = kids.beginArray(); it != kids.endArray(); ++it)
        {
            sum += walkPlain(*it);
        }
    }
    return sum;
}

LL::Task<long long> walkTask(const LLSD node)
{
    long long sum = visit(node);
    if (node.has("_embedded"))
    {
        const LLSD kids = node["_embedded"]["categories"];
        for (LLSD::array_const_iterator it = kids.beginArray(); it != kids.endArray(); ++it)
        {
            sum += co_await walkTask(*it);
        }
    }
    co_return sum;
}

// checkTimeout() on its slow path only: the caller tests the timers inline first, so the usual
// path allocates no frame.
static volatile bool sTimerExpired = false;

LL::Task<void> checkNothing()
{
    co_return;
}

LL::Task<long long> walkTaskChecked(const LLSD node)
{
    if (sTimerExpired) co_await checkNothing();
    long long sum = visit(node);
    if (node.has("_embedded"))
    {
        const LLSD kids = node["_embedded"]["categories"];
        for (LLSD::array_const_iterator it = kids.beginArray(); it != kids.endArray(); ++it)
        {
            sum += co_await walkTaskChecked(*it);
        }
    }
    co_return sum;
}

using Clock = std::chrono::steady_clock;

double median(std::vector<double> v)
{
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

double minimum(const std::vector<double>& v)
{
    return *std::min_element(v.begin(), v.end());
}
} // anonymous namespace

namespace tut
{
struct task_bench_data
{
};
typedef test_group<task_bench_data> task_bench_group;
typedef task_bench_group::object object;
task_bench_group task_bench_group_("lltask_bench");

// 1. The AIS-style recursion over about 5,000 nodes, plain against Task coroutines.
template<> template<>
void object::test<1>()
{
    set_test_name("AIS-style recursion, plain against Task");

    sNodes = 0;
    int next_id = 0;
    const LLSD tree = makeTree(DEPTH, next_id);
    const int nodes = sNodes;
    ensure_equals("tree size", nodes, 4681);

    const long long expected = walkPlain(tree);
    // Warm up all three (allocator, caches) and check that they agree.
    ensure_equals("task answer", walkTask(tree).get(), expected);
    ensure_equals("task+check answer", walkTaskChecked(tree).get(), expected);

    std::vector<double> plain, task, checked;
    long long sink = 0;
    for (int round = 0; round < ROUNDS; ++round)
    {
        auto t0 = Clock::now();
        sink += walkPlain(tree);
        auto t1 = Clock::now();
        sink += walkTask(tree).get();
        auto t2 = Clock::now();
        sink += walkTaskChecked(tree).get();
        auto t3 = Clock::now();
        plain.push_back(std::chrono::duration<double, std::nano>(t1 - t0).count() / nodes);
        task.push_back(std::chrono::duration<double, std::nano>(t2 - t1).count() / nodes);
        checked.push_back(std::chrono::duration<double, std::nano>(t3 - t2).count() / nodes);
    }
    ensure_equals("sums agree", sink, expected * 3 * ROUNDS);

#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define LL_BENCH_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define LL_BENCH_SANITIZED 1
#endif
#endif
#if defined(LL_BENCH_SANITIZED)
    // A sanitizer build instruments every call: the numbers would say nothing about the code.
    return;
#endif
    char line[512];
    std::snprintf(line, sizeof(line),
                  "| %d nodes, %d rounds | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |", nodes, ROUNDS,
                  median(plain), median(task), median(checked), minimum(plain), minimum(task), minimum(checked));
    std::cerr << "lltask_bench (ns per node: median plain/task/task+check, min plain/task/task+check)\n"
              << line << std::endl;

    if (const char* summary = std::getenv("GITHUB_STEP_SUMMARY"))
    {
        std::ofstream out(summary, std::ios::app);
        if (out)
        {
            out << "### AIS-style recursion micro-benchmark (fibre bridge, ns per node)\n\n"
                << "| tree | plain median | task median | task+check median | plain min | task min | task+check min |\n"
                << "|---|---|---|---|---|---|---|\n"
                << line << "\n\n";
        }
    }
}
} // namespace tut
