// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   llcoros_order_test.cpp
 * @brief  The run-order oracle of the coroutines (doc/clean/plan.md, "Coroutines").
 *
 * It records the order in which the main loop, coroutine wake-ups, yields, timers and launches run, as a trace string
 * per scenario, through LLCoros::launch() and LLCoros' own executor. The strings were first derived from Boost.Fiber
 * 1.90 (src/scheduler.cpp, context.cpp, fiber.cpp) and proven in CI against the fibre LLCoros; scenarios 1, 2, 5, 6, 7
 * and 8 kept their strings when the fibres went. Scenarios 3 and 4 (marked MODEL-DIFF) changed by design: wake-ups run
 * only at the frame's pump, so a launch runs the new coroutine to its first wait and returns.
 *
 * The test's own main thread plays the viewer's main loop: frame() is doFrame's pump (llappviewer.cpp). The same
 * scenarios run on a test executor in lltask_test.cpp.
 */

#include "linden_common.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "lltask.h"
#include "llevents.h"
#include "llsd.h"
#include "../test/lltut.h"
#include "../test/lltestapp.h"

#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
// LLApp::setStatus is protected; the scenarios that test the stop path need it.
struct OrderTestApp: public LLTestApp
{
    static void status(EAppStatus s) { setStatus(s); }
};

std::string sTrace;
void mark(const std::string& what)
{
    if (! sTrace.empty())
        sTrace += ' ';
    sTrace += what;
}

// doFrame's pump: the per-frame event, then one pass over the ready coroutines, then any stashed coroutine exception.
void frame()
{
    LLEventPumps::instance().obtain("mainloop").post(LLSD());
    LLCoros::instance().executor().pump();
    LLCoros::instance().rethrow();
}

// Let earlier tests' leftovers run out, so that only the dispatcher is ready when a scenario starts.
void settle()
{
    for (int i = 0; i < 5; ++i)
        frame();
    sTrace.clear();
}
} // anonymous namespace

namespace tut
{
struct order_data
{
    OrderTestApp app;
};
typedef test_group<order_data> order_group;
typedef order_group::object object;
order_group orderGroup("llcoros_order");

// 1. Wake-ups run at the pump in wake order. A coroutine woken by another coroutine during the pump, and a coroutine
//    that yields during the pump, both run at the next frame's pump.
template<> template<>
void object::test<1>()
{
    set_test_name("wake order and frame boundary");
    settle();
    LLEventStream pa("order_pa", true), pb("order_pb", true), pc("order_pc", true);
    LLCoros::instance().launch("order_A", [&pa]() -> LL::Task<void>
        {
            mark("A0"); co_await llcoro::suspendUntilEventOn(pa); mark("A1"); co_await llcoro::suspend();
            mark("A2");
        });
    LLCoros::instance().launch("order_B", [&pb, &pc]() -> LL::Task<void>
        {
            mark("B0"); co_await llcoro::suspendUntilEventOn(pb); mark("B1"); pc.post(LLSD()); mark("B2");
        });
    LLCoros::instance().launch("order_C", [&pc]() -> LL::Task<void>
        {
            mark("C0"); co_await llcoro::suspendUntilEventOn(pc); mark("C1");
        });
    mark("M:post");
    pb.post(LLSD());
    pa.post(LLSD());
    mark("M:f1"); frame();
    mark("M:f2"); frame();
    mark("M:f3"); frame();
    ensure_equals(sTrace, "A0 B0 C0 M:post M:f1 B1 B2 A1 M:f2 C1 A2 M:f3");
}

// 2. A timer that has expired is noticed by the dispatcher during one pump and runs at the next.
template<> template<>
void object::test<2>()
{
    set_test_name("timer runs the frame after it is noticed");
    settle();
    LLCoros::instance().launch("order_T", []() -> LL::Task<void>
        {
            mark("T0"); co_await llcoro::suspendUntilTimeout(0.001f); mark("T1");
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mark("f1"); frame();
    mark("f2"); frame();
    mark("f3");
    ensure_equals(sTrace, "T0 f1 f2 T1 f3");
}

// 3. MODEL-DIFF. A launch from the main loop runs the new coroutine to its first wait and returns: the coroutine that
//    was already ready waits for the pump. (The Boost.Fiber LLCoros let every coroutine that was already ready run
//    first: "P0 L0 P1 M:afterL f1".)
template<> template<>
void object::test<3>()
{
    set_test_name("launch from the main loop does not run the ready queue");
    settle();
    LLEventStream pp("order_pp", true), pl("order_pl", true);
    LLCoros::instance().launch("order_P", [&pp]() -> LL::Task<void>
        {
            mark("P0"); co_await llcoro::suspendUntilEventOn(pp); mark("P1");
        });
    pp.post(LLSD());
    LLCoros::instance().launch("order_L", [&pl]() -> LL::Task<void>
        {
            mark("L0"); co_await llcoro::suspendUntilEventOn(pl); mark("L1");
        });
    mark("M:afterL");
    mark("f1"); frame();
    ensure_equals(sTrace, "P0 L0 M:afterL f1 P1");
    pl.post(LLSD()); frame(); // let order_L finish
}

// 4. MODEL-DIFF. A launch from inside a coroutine returns after the new coroutine's first wait: the launcher
//    continues at once. (The Boost.Fiber LLCoros put the launcher behind the main fibre, so it continued at the next
//    frame's pump: "Q0 F0 Q1 R0 F1 Q2 F2".)
template<> template<>
void object::test<4>()
{
    set_test_name("launch from a coroutine lets the launcher continue");
    settle();
    LLEventStream pq("order_pq", true), pr("order_pr", true);
    LLCoros::instance().launch("order_Q", [&pq, &pr]() -> LL::Task<void>
        {
            mark("Q0");
            co_await llcoro::suspendUntilEventOn(pq);
            mark("Q1");
            LLCoros::instance().launch("order_R", [&pr]() -> LL::Task<void>
                {
                    mark("R0"); co_await llcoro::suspendUntilEventOn(pr);
                });
            mark("Q2");
        });
    pq.post(LLSD());
    mark("F0"); frame();
    mark("F1"); frame();
    mark("F2");
    ensure_equals(sTrace, "Q0 F0 Q1 R0 Q2 F1 F2");
    pr.post(LLSD()); frame(); // let order_R finish
}

// 5. Only the first event satisfies a waiter; a second event on the same pump before the waiter runs is not
//    delivered to it and not consumed. A consuming waiter stops the listeners after it from seeing the event.
//    LLEventPump calls listeners that name no order in reverse alphabetical order of their names (LLDependencies keeps
//    them in a std::map and its topological sort comes out reversed): order_tail, then order_K, then order_E. The names
//    pin the order; the scheduler has nothing to do with it.
template<> template<>
void object::test<5>()
{
    set_test_name("first event wins, consuming");
    settle();
    LLEventStream pe("order_pe", true);
    LLCoros::instance().launch("order_E", [&pe]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOn(pe); mark("E:" + v.asString());
        });
    LLCoros::instance().launch("order_K", [&pe]() -> LL::Task<void>
        {
            LLCoros::OverrideConsuming consuming(true);
            LLSD v = co_await llcoro::suspendUntilEventOn(pe); mark("K:" + v.asString());
        });
    std::string late;
    LLTempBoundListener tail(pe.listen("order_tail", [&late](const LLSD& v) { late += v.asString(); return false; }));
    pe.post("one");
    pe.post("two");
    frame();
    // tail sees both; K takes "one" and consumes it (so E never sees it) and ignores "two" (not consumed, E takes it).
    ensure_equals(sTrace, "K:one E:two");
    ensure_equals(late, "onetwo");
}

// 6. Event-or-timeout: the event wins when it comes first; the timeout result is returned otherwise.
template<> template<>
void object::test<6>()
{
    set_test_name("event or timeout");
    settle();
    LLEventStream pw("order_pw", true);
    LLCoros::instance().launch("order_W1", [&pw]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(pw, 10.f, "timeout"); mark("W1:" + v.asString());
        });
    LLCoros::instance().launch("order_W2", []() -> LL::Task<void>
        {
            LLEventStream never("order_never", true);
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(never, 0.001f, "timeout");
            mark("W2:" + v.asString());
        });
    pw.post("event");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    frame();
    frame();
    ensure_equals(sTrace, "W1:event W2:timeout");
}

// 7. A status change on "LLApp" wakes every waiter with Stopping; the coroutine's top level logs it and it ends.
template<> template<>
void object::test<7>()
{
    set_test_name("LLApp status stops waiters");
    settle();
    LLEventStream ps("order_ps", true);
    LLCoros::instance().launch("order_S", [&ps]() -> LL::Task<void>
        {
            try { co_await llcoro::suspendUntilEventOn(ps); mark("S:event"); }
            catch (const LLCoros::Stopping&) { mark("S:stopping"); throw; }
        });
    OrderTestApp::status(LLApp::APP_STATUS_QUITTING);
    frame();
    OrderTestApp::status(LLApp::APP_STATUS_RUNNING);
    ensure_equals(sTrace, "S:stopping");
}

// 8. An exception that is not LLContinueError is stashed and rethrown on the main loop after the pump, on every
//    platform. (The Boost.Fiber LLCoros terminated the process on Windows.)
template<> template<>
void object::test<8>()
{
    set_test_name("uncaught exception is rethrown after the pump");
    settle();
    LLEventStream px("order_px", true);
    LLCoros::instance().launch("order_X", [&px]() -> LL::Task<void>
        {
            co_await llcoro::suspendUntilEventOn(px); throw std::runtime_error("order_X");
        });
    px.post(LLSD());
    std::string what;
    try { frame(); }
    catch (const std::runtime_error& e) { what = e.what(); }
    ensure_equals(what, "order_X");
}
} // namespace tut
