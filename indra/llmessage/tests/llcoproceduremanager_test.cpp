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
}  // namespace tut
