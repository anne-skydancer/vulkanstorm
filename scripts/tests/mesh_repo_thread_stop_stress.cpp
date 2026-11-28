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
// and for the wait strategies: "legacy" (the old loop), "fixed" (wait on a predicate that checks the exit flag,
// the flag set under the condition's mutex) and "real" (the loop of LLMeshRepoThread::run() now, and the viewer's
// shutdown: a wake-pending flag and mShuttingDown, both set under the condition's mutex; the app's exit flag
// (LLApp::isExiting) set with no lock and no notify, then cleanup()'s changeAndBroadcast after the gap; a wake()
// running at the same moment as the thread leaves the loop; the thread's tail after the loop, which must leave the
// condition's mutex free; a wait with a 5 ms timeout while requests are in flight).
// NOTE: the "real" model mirrors the loop structure of LLMeshRepoThread::run() in indra/newview/llmeshrepository.cpp
// (loop condition, predicate, wait / waitFor choice, the break after the wait, the tail).  Change this test whenever
// run() changes.
// "real" also checks that no wake-up is lost (every wake() leads to a pass within a second, 3 wakes at different
// timings per run) and that the thread wakes by itself, with no wake(), while requests are in flight.
//
// Build:  g++ -O2 -std=c++17 mesh_repo_thread_stop_stress.cpp -o mesh_repo_thread_stop_stress   (or clang++)
// Run:    mesh_repo_thread_stop_stress [iterations] [gap_us] [legacy|fixed|real]
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
    template <class Pred> bool waitFor(std::chrono::milliseconds timeout, Pred pred)
    {
        std::unique_lock<std::mutex> lock(mMutex);
        return mCond.wait_for(lock, timeout, pred);
    }
    template <class Change> void changeAndBroadcast(Change change)
    {
        {
            std::unique_lock<std::mutex> lock(mMutex);
            change();
        }
        mCond.notify_all();
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
    bool mReal = false;
    // "real" model: mWakePending is guarded by mSignal's mutex
    bool mWakePending = false;
    std::atomic<bool> mShuttingDown{false}; // written under mSignal's mutex (cleanup()), read in the predicate
    std::atomic<bool> mBusy{false};    // requests in flight
    std::atomic<int> mPasses{0};

    void wake() { mSignal.changeAndBroadcast([this] { mWakePending = true; }); }
    // The viewer's quit: LLApp::isExiting() is set with no lock and no notify; cleanup() follows after the gap.
    void quit(int gap_us = 0)
    {
        mExiting.store(true);
        if (gap_us > 0) { spin_us(gap_us); }
        mSignal.changeAndBroadcast([this] { mShuttingDown.store(true); });
    }

    void thread_main()
    {
        std::mt19937 rng(std::random_device{}());
        while (!mExiting.load() && !(mReal && mShuttingDown.load()))
        {
            if (mReal)
            {
                auto ready = [this]
                {
                    if (mExiting.load() || mShuttingDown.load()) { return true; }
                    if (mWakePending) { mWakePending = false; return true; }
                    return false;
                };
                if (mBusy.load()) { mSignal.waitFor(std::chrono::milliseconds(5), ready); }
                else { mSignal.wait(ready); }
                mPasses.fetch_add(1);
            }
            else if (mFixed)
            {
                mSignal.wait([this] { return mExiting.load(); });
            }
            else
            {
                mSignal.wait();
            }
            if (mExiting.load() || (mReal && mShuttingDown.load()))
            {
                break;
            }
            spin_us(5 + (int)(rng() % 40)); // the loop body (http update, queues)
        }
        // the tail of run(): no unlock of mSignal; quitThread() is not modelled
        mStopped.store(true);
    }
};
} // namespace

int main(int argc, char** argv)
{
    const int iterations = argc > 1 ? std::atoi(argv[1]) : 2000;
    const int gap_us = argc > 2 ? std::atoi(argv[2]) : 0;
    const bool real = argc > 3 && std::strcmp(argv[3], "real") == 0;
    const bool fixed = real || (argc > 3 && std::strcmp(argv[3], "fixed") == 0);
    if (iterations <= 0 || gap_us < 0 || (argc > 3 && !fixed && std::strcmp(argv[3], "legacy") != 0))
    {
        std::fprintf(stderr, "usage: %s [iterations] [gap_us] [legacy|fixed|real]\n", argv[0]);
        return 2;
    }

    std::mt19937 rng(12345);
    int hangs = 0;
    for (int i = 0; i < iterations; ++i)
    {
        Run run;
        run.mFixed = fixed;
        run.mReal = real;
        run.mBusy.store(real && (i % 2 == 0)); // half of the runs quit with requests in flight
        std::thread repo([&run] { run.thread_main(); });

        // a few "frames": the main thread signals the repo thread
        const int frames = (int)(rng() % 6);
        for (int f = 0; f < frames; ++f)
        {
            spin_us(10 + (int)(rng() % 50));
            if (real) { run.wake(); } else { run.mSignal.signal(); }
        }
        spin_us((int)(rng() % 60));

        // quit: the flag, then (after gap_us) the one broadcast of cleanup()
        std::thread waker;
        if (real)
        {
            // a wake() at the same moment as the thread leaves the loop (the main loop's last frame)
            const int wake_delay = (int)(rng() % 80);
            waker = std::thread([&run, wake_delay] { spin_us(wake_delay); run.wake(); });
            run.quit(gap_us);
        }
        else if (fixed)
        {
            std::lock_guard<std::mutex> lock(run.mSignal.mMutex);
            run.mExiting.store(true);
        }
        else
        {
            run.mExiting.store(true);
        }
        if (!real)
        {
            if (gap_us > 0)
            {
                spin_us(gap_us);
            }
            run.mSignal.broadcast();
        }

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
        if (waker.joinable()) { waker.join(); }
        if (real)
        {
            // the thread's tail must not leave the condition's mutex held
            if (!run.mSignal.mMutex.try_lock())
            {
                ++hangs;
                std::printf("mutex left locked at iteration %d\n", i);
            }
            else
            {
                run.mSignal.mMutex.unlock();
            }
        }
    }

    // "real" only: (1) no lost wake-up while idle, (2) the thread wakes by itself while requests are in flight
    int lost = 0;
    int no_self_wake = 0;
    if (real)
    {
        for (int i = 0; i < 200; ++i)
        {
            Run run;
            run.mFixed = run.mReal = true;
            std::thread repo([&run] { run.thread_main(); });
            for (int k = 0; k < 3; ++k)
            {
                spin_us((int)(rng() % 200));
                const int before = run.mPasses.load();
                run.wake();
                const auto deadline = clk::now() + std::chrono::seconds(1);
                while (run.mPasses.load() == before && clk::now() < deadline) { std::this_thread::yield(); }
                if (run.mPasses.load() == before) { ++lost; }
            }
            run.quit();
            repo.join();
        }
        for (int i = 0; i < 5; ++i)
        {
            Run run;
            run.mFixed = run.mReal = true;
            run.mBusy.store(true);
            std::thread repo([&run] { run.thread_main(); });
            std::this_thread::sleep_for(std::chrono::milliseconds(120));
            // 120 ms at 5 ms is about 24 passes with no wake(); be generous for a loaded machine
            if (run.mPasses.load() < 5) { ++no_self_wake; }
            run.quit();
            repo.join();
        }
        std::printf("real: lost wake-ups=%d, runs without self wake=%d\n", lost, no_self_wake);
    }
    std::printf("mode=%s gap_us=%d iterations=%d hangs=%d\n", real ? "real" : fixed ? "fixed" : "legacy", gap_us, iterations, hangs);
    return (hangs || lost || no_self_wake) ? 1 : 0;
}
