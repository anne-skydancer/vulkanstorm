/**
 * @file   coroutine_test.cpp
 * @author Nat Goodspeed
 * @date   2009-04-22
 * @brief  Test for coroutine.
 *
 * $LicenseInfo:firstyear=2009&license=viewerlgpl$
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

#define BOOST_RESULT_OF_USE_TR1 1
#include <boost/bind.hpp>
#include <boost/range.hpp>
#include <boost/utility.hpp>

#include "linden_common.h"

#include <chrono>
#include <iostream>
#include <string>
#include <thread>
#include <typeinfo>

#include "../test/lltut.h"
#include "../test/lltestapp.h"
#include "llsd.h"
#include "llsdutil.h"
#include "llevents.h"
#include "llcoros.h"
#include "lleventfilter.h"
#include "lleventcoro.h"
#include "lltask.h"

using namespace llcoro;

/*****************************************************************************
*   Test helpers
*****************************************************************************/
/// Simulate an event API whose response is immediate: sent on receipt of the
/// initial request, rather than after some delay. This is the case that
/// distinguishes postAndSuspend() from calling post(), then calling
/// suspendUntilEventOn().
class ImmediateAPI
{
public:
    ImmediateAPI():
        mPump("immediate", true)
    {
        mPump.listen("API", boost::bind(&ImmediateAPI::operator(), this, _1));
    }

    LLEventPump& getPump() { return mPump; }

    // Invoke this with an LLSD map containing:
    // ["value"]: Integer value. We will reply with ["value"] + 1.
    // ["reply"]: Name of LLEventPump on which to send response.
    bool operator()(const LLSD& event) const
    {
        LLSD::Integer value(event["value"]);
        LLEventPumps::instance().obtain(event["reply"]).post(value + 1);
        return false;
    }

private:
    LLEventStream mPump;
};

/// The main loop's part: one frame's pass over the ready coroutines (doFrame's pump)
static void frame()
{
    LLCoros::instance().executor().pump();
}

/*****************************************************************************
*   TUT
*****************************************************************************/
namespace tut
{
    struct test_data
    {
        ImmediateAPI immediateAPI;
        std::string replyName;
        LLSD result;
        LLTestApp testApp;

        LL::Task<void> waitForEventOn1();
        LL::Task<void> coroPump();
        LL::Task<void> postAndWait1();
        LL::Task<void> coroPumpPost();
    };
    typedef test_group<test_data> coroutine_group;
    typedef coroutine_group::object object;
    coroutine_group coroutinegrp("coroutine");

    LL::Task<void> test_data::waitForEventOn1()
    {
        result = co_await suspendUntilEventOn("source");
    }

    template<> template<>
    void object::test<1>()
    {
        set_test_name("waitForEventOn1");
        LLCoros::instance().launch("test<1>", [this](){ return waitForEventOn1(); });
        // the coroutine is waiting for the event
        LLEventPumps::instance().obtain("source").post("received");
        // a wake-up runs at the pump, not before
        ensure("should not have run before the pump", result.isUndefined());
        frame();
        ensure_equals(result.asString(), "received");
    }

    LL::Task<void> test_data::coroPump()
    {
        LLCoroEventPump waiter;
        replyName = waiter.getName();
        result = co_await waiter.suspend();
    }

    template<> template<>
    void object::test<2>()
    {
        set_test_name("coroPump");
        LLCoros::instance().launch("test<2>", [this](){ return coroPump(); });
        LLEventPumps::instance().obtain(replyName).post("received");
        // give coroPump() a chance to run
        frame();
        ensure_equals(result.asString(), "received");
    }

    LL::Task<void> test_data::postAndWait1()
    {
        result = co_await postAndSuspend(LLSDMap("value", 17),       // request event
                                         immediateAPI.getPump(),     // requestPump
                                         "reply1",                   // replyPump
                                         "reply");                   // request["reply"] = name
    }

    template<> template<>
    void object::test<3>()
    {
        set_test_name("postAndWait1");
        // the reply comes while the request is posted: the coroutine does not even suspend
        LLCoros::instance().launch("test<3>", [this](){ return postAndWait1(); });
        ensure_equals(result.asInteger(), 18);
    }

    LL::Task<void> test_data::coroPumpPost()
    {
        LLCoroEventPump waiter;
        result = co_await waiter.postAndSuspend(LLSDMap("value", 17),
                                                immediateAPI.getPump(), "reply");
    }

    template<> template<>
    void object::test<4>()
    {
        set_test_name("coroPumpPost");
        LLCoros::instance().launch("test<4>", [this](){ return coroPumpPost(); });
        ensure_equals(result.asInteger(), 18);
    }

    // Don't use ensure() from within a coroutine: ensure() failure throws tut::fail, which would not propagate out
    // to the main test driver, which would result in an odd failure. The coroutine collects its data instead.
    template <class PUMP>
    LL::Task<void> listener(PUMP* pump, bool* running, LLSD* data)
    {
        // important for this test that we consume posted values
        LLCoros::set_consuming(true);
        // should immediately retrieve 'first' without waiting
        LL_DEBUGS() << "listener coro waiting for first" << LL_ENDL;
        data->append(co_await llcoro::suspendUntilEventOnWithTimeout(*pump, 0.1f, LLSD()));
        // Wait for 'second' because it's not already pending.
        LL_DEBUGS() << "listener coro waiting for second" << LL_ENDL;
        data->append(co_await llcoro::suspendUntilEventOnWithTimeout(*pump, 0.1f, LLSD()));
        // and wait for 'third', which should involve no further waiting
        LL_DEBUGS() << "listener coro waiting for third" << LL_ENDL;
        data->append(co_await llcoro::suspendUntilEventOnWithTimeout(*pump, 0.1f, LLSD()));
        LL_DEBUGS() << "listener coro done" << LL_ENDL;
        *running = false;
    }

    template <class PUMP>
    void test()
    {
        PUMP pump(typeid(PUMP).name());
        bool running{false};
        LLSD data{LLSD::emptyArray()};
        // start things off by posting once before even starting the listener
        // coro
        LL_DEBUGS() << "test() posting first" << LL_ENDL;
        LLSD first{LLSDMap("desc", "first")("value", 0)};
        bool consumed = pump.post(first);
        ensure("should not have consumed first", ! consumed);
        // now launch the coro
        LL_DEBUGS() << "test() launching listener coro" << LL_ENDL;
        running = true;
        LLCoros::instance().launch(
            "listener",
            [&pump, &running, &data]() { return listener(&pump, &running, &data); });
        // back from coro at the point where it's waiting for 'second'
        LL_DEBUGS() << "test() posting second" << LL_ENDL;
        LLSD second{llsd::map("desc", "second", "value", 1)};
        consumed = pump.post(second);
        ensure("should have consumed second", consumed);
        // This is a key point: even though we've post()ed the value for which
        // the coroutine is waiting, it's actually still suspended until the
        // main loop pumps the coroutines. The coroutine will only pick up one
        // value at a time from our 'pump'. It's important to exercise the
        // case when we post() two values before it picks up either.
        LL_DEBUGS() << "test() posting third" << LL_ENDL;
        LLSD third{llsd::map("desc", "third", "value", 2)};
        consumed = pump.post(third);
        ensure("should NOT yet have consumed third", ! consumed);
        // now just pump until the coro finishes -- which it eventually will, given
        // that all its suspend calls have short timeouts.
        for (int frames = 0; running && frames < 100; ++frames)
        {
            LL_DEBUGS() << "test() waiting for coro done" << LL_ENDL;
            frame();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        ensure("the coroutine did not finish", ! running);
        // okay, verify expected results
        ensure_equals("should have received three values", data,
                      llsd::array(first, second, third));
        LL_DEBUGS() << "test() done" << LL_ENDL;
    }

    template<> template<>
    void object::test<5>()
    {
        set_test_name("LLEventMailDrop");
        tut::test<LLEventMailDrop>();
    }

    template<> template<>
    void object::test<6>()
    {
        set_test_name("LLEventLogProxyFor<LLEventMailDrop>");
        tut::test< LLEventLogProxyFor<LLEventMailDrop> >();
    }
}
