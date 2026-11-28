// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llcondition_test.cpp
 * @brief LLCondition's predicate wait, timed wait and changeAndBroadcast, as LLMeshRepoThread::run() uses them.
 */

#include "linden_common.h"

#include "../llmutex.h"
#include "../test/lltut.h"

#include <atomic>
#include <chrono>
#include <thread>

namespace tut
{
    struct llcondition_data
    {
        LLCondition cond;
    };
    typedef test_group<llcondition_data> llcondition_group;
    typedef llcondition_group::object object;
    llcondition_group llconditiongrp("llcondition");

    template<> template<>
    void object::test<1>()
    {
        set_test_name("A change made before the wait is seen by the predicate");
        bool flag = false;
        cond.changeAndBroadcast([&flag] { flag = true; });
        // returns at once: the predicate holds
        cond.wait([&flag] { return flag; });
        ensure("timed wait sees it too", cond.waitFor(std::chrono::milliseconds(1), [&flag] { return flag; }));
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("A timed wait on a predicate that stays false times out");
        ensure("timed out", !cond.waitFor(std::chrono::milliseconds(5), [] { return false; }));
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("Start and stop: the exit flag set without the lock and no notify, then cleanup()'s broadcast");
        // The viewer's quit: LLApp::isExiting() is set with no notify; cleanup() later sets mShuttingDown under the
        // mutex and broadcasts.  The thread must stop, whatever the gap and whatever wake() calls are in between.
        for (int i = 0; i < 200; ++i)
        {
            std::atomic<bool> exiting{false};   // LLApp::isExiting(), no notify
            std::atomic<bool> shutting{false}; // mShuttingDown, written under cond's mutex
            std::atomic<bool> stopped{false};
            bool wake_pending = false; // guarded by cond's mutex, as LLMeshRepoThread::mWakePending
            int passes = 0;            // repo thread only
            std::thread repo(
                [&]
                {
                    while (!exiting.load() && !shutting.load())
                    {
                        cond.wait([&]
                                  {
                                      if (exiting.load() || shutting.load()) { return true; }
                                      if (wake_pending) { wake_pending = false; return true; }
                                      return false;
                                  });
                        if (exiting.load() || shutting.load()) { break; }
                        ++passes;
                    }
                    stopped.store(true);
                });
            for (int k = 0; k < i % 4; ++k)
            {
                cond.changeAndBroadcast([&] { wake_pending = true; });
            }
            exiting.store(true);
            if (i % 2)
            {
                std::this_thread::sleep_for(std::chrono::microseconds(50));
            }
            cond.changeAndBroadcast([&] { shutting.store(true); });
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (!stopped.load() && std::chrono::steady_clock::now() < deadline)
            {
                std::this_thread::yield();
            }
            const bool ok = stopped.load();
            while (!stopped.load())
            {
                cond.broadcast(); // release it so that it can be joined
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            repo.join();
            ensure("the thread stopped", ok);
        }
    }
}
