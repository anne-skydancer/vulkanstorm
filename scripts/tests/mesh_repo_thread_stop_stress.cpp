// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Start/stop stress test for the wait pattern of LLMeshRepoThread::run() (finding 9 of the mesh requests study).
//
// The repo thread loops "while (!isExiting) { mSignal->wait(); ... }".  LLCondition::wait() waits with no
// predicate and signal()/broadcast() set no state, so a wake-up that arrives between the loop check and the wait is
// lost.  This program models the loop with a copy of LLCondition (indra/llcommon/llmutex.cpp: a std::mutex and a
// std::condition_variable, wait() locks and waits with no predicate, notify without the lock), starts and stops it
// in a loop, always joins (with a timeout that reports a hang), and counts the hangs for two shutdown timings:
//   gap 0   : the exit flag and the only broadcast come back to back (worst case, an artificial one)
//   gap N us: the broadcast follows the flag by N microseconds (the viewer: LLApp::isExiting is set when the
//             quit starts, LLMeshRepository::shutdown -> cleanup() broadcasts much later, after the main loop ended)
// and for the two wait strategies: "legacy" (as today) and "fixed" (wait on a predicate that checks the exit flag,
// the flag set under the condition's mutex).
//
// Build:  g++ -O2 -std=c++17 mesh_repo_thread_stop_stress.cpp -o mesh_repo_thread_stop_stress   (or clang++)
// Run:    mesh_repo_thread_stop_stress [iterations] [gap_us] [legacy|fixed]
// Exit code: 0 no hang, 1 at least one hang, 2 usage.

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <random>
#include <thread>

namespace
{
using clk = std::chrono::steady_clock;

// copy of LLCondition's behaviour
struct Condition
{
    std::mutex mMutex;
    std::condition_variable mCond;
    void wait()
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock);
    }
    template <class Pred> void wait(Pred pred)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        mCond.wait(lock, pred);
    }
    void signal() { mCond.notify_one(); }
    void broadcast() { mCond.notify_all(); }
};

void spin_us(int us)
{
    const auto end = clk::now() + std::chrono::microseconds(us);
    while (clk::now() < end) {}
}

struct Run
{
    Condition mSignal;
    std::atomic<bool> mExiting{false};
    std::atomic<bool> mStopped{false};
    bool mFixed = false;

    void thread_main()
    {
        std::mt19937 rng(std::random_device{}());
        while (!mExiting.load())
        {
            if (mFixed)
            {
                mSignal.wait([this] { return mExiting.load(); });
            }
            else
            {
                mSignal.wait();
            }
            if (mExiting.load())
            {
                break;
            }
            spin_us(5 + (int)(rng() % 40)); // the loop body (http update, queues)
        }
        mStopped.store(true);
    }
};
} // namespace

int main(int argc, char** argv)
{
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int gap_us = argc > 2 ? std::atoi(argv[2]) : 0;
    const bool fixed = argc > 3 && std::strcmp(argv[3], "fixed") == 0;
    if (iterations <= 0 || gap_us < 0 || (argc > 3 && !fixed && std::strcmp(argv[3], "legacy") != 0))
    {
        std::fprintf(stderr, "usage: %s [iterations] [gap_us] [legacy|fixed]\n", argv[0]);
        return 2;
    }

    std::mt19937 rng(12345);
    int hangs = 0;
    for (int i = 0; i < iterations; ++i)
    {
        Run run;
        run.mFixed = fixed;
        std::thread repo([&run] { run.thread_main(); });

        // a few "frames": the main thread signals the repo thread
        const int frames = (int)(rng() % 6);
        for (int f = 0; f < frames; ++f)
        {
            spin_us(10 + (int)(rng() % 50));
            run.mSignal.signal();
        }
        spin_us((int)(rng() % 60));

        // quit: the flag, then (after gap_us) the one broadcast of cleanup()
        if (fixed)
        {
            std::lock_guard<std::mutex> lock(run.mSignal.mMutex);
            run.mExiting.store(true);
        }
        else
        {
            run.mExiting.store(true);
        }
        if (gap_us > 0)
        {
            spin_us(gap_us);
        }
        run.mSignal.broadcast();

        // always join, with a timeout that reports a hang
        const auto deadline = clk::now() + std::chrono::seconds(2);
        while (!run.mStopped.load() && clk::now() < deadline)
        {
            std::this_thread::yield();
        }
        if (!run.mStopped.load())
        {
            ++hangs;
            std::printf("HANG at iteration %d (thread did not stop within 2 s)\n", i);
            // release it so that it can be joined
            while (!run.mStopped.load())
            {
                run.mSignal.broadcast();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
        repo.join();
    }
    std::printf("mode=%s gap_us=%d iterations=%d hangs=%d\n", fixed ? "fixed" : "legacy", gap_us, iterations, hangs);
    return hangs ? 1 : 0;
}
