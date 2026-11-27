/**
 * @file llcoproceduremanager_test.cpp
 * @author Brad
 * @date 2019-02
 * @brief LLCoprocedureManager unit test
 *
 * $LicenseInfo:firstyear=2019&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llwin32headers.h"

#include "linden_common.h"
#include "llsdserialize.h"

#include "../llcoproceduremanager.h"
#include "lleventcoro.h"

#include <functional>

#include "../test/lltut.h"
#include "../test/lltestapp.h"


#if LL_WINDOWS
// disable unreachable code warnings
#pragma warning(disable: 4702)
#endif

LLCoreHttpUtil::HttpCoroutineAdapter::HttpCoroutineAdapter(std::string name, LLCore::HttpRequest::policy_t policyId)
{
}

void LLCoreHttpUtil::HttpCoroutineAdapter::cancelSuspendedOperation()
{
}

LLCoreHttpUtil::HttpCoroutineAdapter::~HttpCoroutineAdapter()
{
}

LLCore::HttpRequest::HttpRequest()
{
}

LLCore::HttpRequest::~HttpRequest()
{
}

namespace
{
// A coprocedure is a coroutine that returns an LL::Task; this one has nothing to wait for. The test lambda is not
// itself the coroutine (its captures would dangle after a suspension, lltask.h).
LL::Task<void> finished()
{
    co_return;
}

// A coprocedure that waits before it finishes: the pool must await it, so what comes after the wait still runs, and
// still counts as the coprocedure.
LL::Task<void> waitsThenFinishes(int& state)
{
    state = 1;
    co_await llcoro::suspend(); // one tick: the task runs again at the next pump
    state = 2;
}

// Counts the destructions of the locals a suspended coprocedure holds.
struct DrainProbe
{
    int& mDestroyed;
    explicit DrainProbe(int& destroyed): mDestroyed(destroyed) {}
    DrainProbe(const DrainProbe&) = delete;
    DrainProbe& operator=(const DrainProbe&) = delete;
    ~DrainProbe() { ++mDestroyed; }
};

// A coprocedure that waits for an event nobody posts: it is still suspended when LLCoros::drain() runs.
LL::Task<void> waitsForever(int& started, int& resumed, int& destroyed)
{
    DrainProbe probe(destroyed);
    ++started;
    co_await llcoro::suspendUntilEventOn(std::string("DrainTestNobodyPosts"));
    ++resumed;
}

// The main loop's part: pump the coroutines, a bounded number of frames, until @a done holds.
template<typename DONE>
bool pumpUntil(DONE done)
{
    for (int frame = 0; frame < 100 && ! done(); ++frame)
    {
        LLCoros::instance().executor().pump();
    }
    return done();
}
} // namespace

namespace tut
{
    struct coproceduremanager_test
    {
        // The awaiters of a coprocedure begin with LLCoros::checkStop(), which throws Stopping unless an LLApp is
        // running: test 2 waits, so it needs one.
        LLTestApp mApp;

        coproceduremanager_test()
        {
        }

        ~coproceduremanager_test()
        {
            LLCoprocedureManager::instance().close();
        }
    };
    typedef test_group<coproceduremanager_test> coproceduremanager_t;
    typedef coproceduremanager_t::object coproceduremanager_object_t;
    tut::coproceduremanager_t tut_coproceduremanager("LLCoprocedureManager");


    template<> template<>
    void coproceduremanager_object_t::test<1>()
    {
        int foo = 0;
        LLCoprocedureManager::instance().initializePool("PoolName");
        LLCoprocedureManager::instance().enqueueCoprocedure("PoolName", "ProcName",
            [&foo] (LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t ptr, LLUUID id) {
                foo = 1;
                return finished();
            });

        ensure("coprocedure did not run", pumpUntil([&foo] { return foo == 1; }));

        LLCoprocedureManager::instance().close("PoolName");
    }

    template<> template<>
    void coproceduremanager_object_t::test<2>()
    {
        int state = 0;
        LLCoprocedureManager::instance().initializePool("PoolWait");
        LLCoprocedureManager::instance().enqueueCoprocedure("PoolWait", "WaitProc",
            [&state] (LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, LLUUID) {
                return waitsThenFinishes(state);
            });

        ensure("the coprocedure did not run to its end after waiting", pumpUntil([&state] { return state == 2; }));

        LLCoprocedureManager::instance().close("PoolWait");
    }

    // LLCoros::drain() with a pool that has suspended coprocedures and queued work: the survivors are destroyed (their
    // locals with them), nothing resumes and nothing queued starts, and closing the pool afterwards is clean.
    template<> template<>
    void coproceduremanager_object_t::test<3>()
    {
        int started = 0, resumed = 0, destroyed = 0;
        LLCoprocedureManager& manager = LLCoprocedureManager::instance();
        manager.initializePool("PoolDrain");
        // more than any pool has invokers, so that some stay queued
        const int total = 64;
        for (int i = 0; i < total; ++i)
        {
            manager.enqueueCoprocedure("PoolDrain", "DrainProc",
                [&started, &resumed, &destroyed] (LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t, LLUUID) {
                    return waitsForever(started, resumed, destroyed);
                });
        }

        // the default pool has 5 invokers (llcoproceduremanager.cpp): each takes one coprocedure and waits in it
        const int running = 5;
        ensure("the invokers did not all start a coprocedure", pumpUntil([&started] { return started == running; }));
        pumpUntil([] { return false; }); // settle: the full bounded number of frames
        ensure_equals("more coprocedures started than the pool has invokers", started, running);
        ensure_equals("a suspended coprocedure ended early", destroyed, 0);
        ensure("the test wants work still queued", running < total);

        LLCoros::instance().drain("test");

        ensure_equals("the survivors' locals were not all destroyed", destroyed, running);
        ensure_equals("a coprocedure resumed after the wait", resumed, 0);
        ensure_equals("queued work started during the drain", started, running);
        ensure_equals("tasks remain after the drain", LLCoros::instance().executor().taskCount(), size_t(0));

        // the invokers are gone: pumping resumes nothing and closing the pool is clean
        pumpUntil([] { return false; });
        manager.close("PoolDrain");
        pumpUntil([] { return false; });
        ensure_equals("a coprocedure resumed after the pool closed", resumed, 0);
        ensure_equals("queued work started after the pool closed", started, running);
    }
}  // namespace tut
