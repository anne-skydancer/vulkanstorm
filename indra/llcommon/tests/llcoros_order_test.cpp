// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   llcoros_order_test.cpp
 * @brief  The run-order oracle for the coroutine switch (clean/coroutines, stage 0).
 *
 * Written against today's Boost.Fiber LLCoros. It records the order in which the main loop, coroutine wake-ups, yields,
 * timers and launches run, as a trace string per scenario. The expected strings were derived by reading Boost.Fiber 1.90
 * src/scheduler.cpp, context.cpp and fiber.cpp; CI is their proof. The same scenarios run on LL::Executor with no
 * fibre in lltask_test.cpp: every expected string is the same there except the two scenarios marked MODEL-DIFF, whose
 * strings follow the decision that wake-ups run only at the frame's pump (doc/clean/plan.md, "Coroutines").
 *
 * The test's own main fibre plays the viewer's main loop: frame() is doFrame's pump (llappviewer.cpp:1728-1740).
 *
 * Switching fibres, it is on the TSan deny list (.github/analysis/tsan/llcommon-deny.txt) until the fibres go.
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

// Under ASan, scenarios 3 and 7 (and the bridge test that repeats 7) report stack-use-after-scope and
// stack-buffer-overflow on variables that are fine, in the first function a resumed fibre calls (the CI run reports one
// victim each time, the first fibre to hit stale shadow memory). Boost.Fiber is prebuilt without ASan annotations, so
// ASan cannot judge these fibre switches; the other scenarios run there, and these run in every other build of the test
// (all pass on Boost.Fiber 1.90 on Windows). The task core has no fibre: lltask_test runs the same scenarios under ASan.
#if defined(__SANITIZE_ADDRESS__)
constexpr bool sAsan = true;
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
constexpr bool sAsan = true;
#else
constexpr bool sAsan = false;
#endif
#else
constexpr bool sAsan = false;
#endif

std::string sTrace;
void mark(const std::string& what)
{
    if (! sTrace.empty())
        sTrace += ' ';
    sTrace += what;
}

// doFrame's pump: the per-frame event, then the main fibre yields once, then any stashed coroutine exception.
void frame()
{
    LLEventPumps::instance().obtain("mainloop").post(LLSD());
    llcoro::suspend();
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
    LLCoros::instance().launch("order_A", [&pa]()
        {
            mark("A0"); llcoro::suspendUntilEventOn(pa); mark("A1"); llcoro::suspend(); mark("A2");
        });
    LLCoros::instance().launch("order_B", [&pb, &pc]()
        {
            mark("B0"); llcoro::suspendUntilEventOn(pb); mark("B1"); pc.post(LLSD()); mark("B2");
        });
    LLCoros::instance().launch("order_C", [&pc]()
        {
            mark("C0"); llcoro::suspendUntilEventOn(pc); mark("C1");
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
    LLCoros::instance().launch("order_T", []()
        {
            mark("T0"); llcoro::suspendUntilTimeout(0.001f); mark("T1");
        });
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    mark("f1"); frame();
    mark("f2"); frame();
    mark("f3");
    ensure_equals(sTrace, "T0 f1 f2 T1 f3");
}

// 3. MODEL-DIFF. A launch from the main fibre runs the new coroutine to its first wait and then lets every coroutine
//    that was already ready run (Boost dispatch: the launcher goes to the back of the ready queue), before the
//    launcher continues. Today: "P0 L0 P1 M:afterL f1". With wake-ups at the frame's pump only, P1 waits for the pump:
//    "P0 L0 M:afterL f1 P1".
template<> template<>
void object::test<3>()
{
    set_test_name("launch from the main fibre drains the ready queue");
    if (sAsan)
    {
        skip("fibre switch: ASan false positives (see sAsan)");
    }
    settle();
    LLEventStream pp("order_pp", true), pl("order_pl", true);
    LLCoros::instance().launch("order_P", [&pp]() { mark("P0"); llcoro::suspendUntilEventOn(pp); mark("P1"); });
    pp.post(LLSD());
    LLCoros::instance().launch("order_L", [&pl]() { mark("L0"); llcoro::suspendUntilEventOn(pl); mark("L1"); });
    mark("M:afterL");
    mark("f1"); frame();
    ensure_equals(sTrace, "P0 L0 P1 M:afterL f1");
}

// 4. MODEL-DIFF. A launch from inside a coroutine puts the launcher behind the main fibre: it continues at the next
//    frame's pump. Today: "Q0 F0 Q1 R0 F1 Q2 F2". With wake-ups at the frame's pump only, the launcher continues
//    at once: "Q0 F0 Q1 R0 Q2 F1 F2".
template<> template<>
void object::test<4>()
{
    set_test_name("launch from a coroutine defers the launcher");
    settle();
    LLEventStream pq("order_pq", true), pr("order_pr", true);
    LLCoros::instance().launch("order_Q", [&pq, &pr]()
        {
            mark("Q0");
            llcoro::suspendUntilEventOn(pq);
            mark("Q1");
            LLCoros::instance().launch("order_R", [&pr]() { mark("R0"); llcoro::suspendUntilEventOn(pr); });
            mark("Q2");
        });
    pq.post(LLSD());
    mark("F0"); frame();
    mark("F1"); frame();
    mark("F2");
    ensure_equals(sTrace, "Q0 F0 Q1 R0 F1 Q2 F2");
    pr.post(LLSD()); frame(); // let order_R finish
}

// 5. Only the first event satisfies a waiter; a second event on the same pump before the waiter runs is not
//    delivered to it and not consumed. A consuming waiter stops the listeners after it from seeing the event.
//    LLEventPump calls listeners that name no order newest first (observed, on fibres and on the executor alike; it
//    comes from LLDependencies' topological sort, not from the scheduler): tail, then K, then E.
template<> template<>
void object::test<5>()
{
    set_test_name("first event wins, consuming");
    settle();
    LLEventStream pe("order_pe", true);
    LLCoros::instance().launch("order_E", [&pe]()
        {
            LLSD v = llcoro::suspendUntilEventOn(pe); mark("E:" + v.asString());
        });
    LLCoros::instance().launch("order_K", [&pe]()
        {
            LLCoros::OverrideConsuming consuming(true);
            LLSD v = llcoro::suspendUntilEventOn(pe); mark("K:" + v.asString());
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
    LLCoros::instance().launch("order_W1", [&pw]()
        {
            LLSD v = llcoro::suspendUntilEventOnWithTimeout(pw, 10.f, "timeout"); mark("W1:" + v.asString());
        });
    LLCoros::instance().launch("order_W2", []()
        {
            LLEventStream never("order_never", true);
            LLSD v = llcoro::suspendUntilEventOnWithTimeout(never, 0.001f, "timeout"); mark("W2:" + v.asString());
        });
    pw.post("event");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    frame();
    frame();
    ensure_equals(sTrace, "W1:event W2:timeout");
}

// 7. A status change on "LLApp" wakes every waiter with Stopping; LLCoros' top level logs it and the coroutine ends.
template<> template<>
void object::test<7>()
{
    set_test_name("LLApp status stops waiters");
    if (sAsan)
    {
        skip("fibre switch: ASan false positives (see sAsan)");
    }
    settle();
    LLEventStream ps("order_ps", true);
    LLCoros::instance().launch("order_S", [&ps]()
        {
            try { llcoro::suspendUntilEventOn(ps); mark("S:event"); }
            catch (const LLCoros::Stopping&) { mark("S:stopping"); throw; }
        });
    OrderTestApp::status(LLApp::APP_STATUS_QUITTING);
    frame();
    OrderTestApp::status(LLApp::APP_STATUS_RUNNING);
    ensure_equals(sTrace, "S:stopping");
}

#if ! LL_WINDOWS
// 8. Linux and macOS: an exception that is not LLContinueError is stashed and rethrown on the main fibre after the pump.
//    (Windows today: it escapes the fibre and terminates the process, llcoros.cpp:327-364; not testable here.)
template<> template<>
void object::test<8>()
{
    set_test_name("uncaught exception is rethrown after the pump");
    settle();
    LLEventStream px("order_px", true);
    LLCoros::instance().launch("order_X", [&px]()
        {
            llcoro::suspendUntilEventOn(px); throw std::runtime_error("order_X");
        });
    px.post(LLSD());
    std::string what;
    try { frame(); }
    catch (const std::runtime_error& e) { what = e.what(); }
    ensure_equals(what, "order_X");
}
#endif
#if ! LL_CORO_NATIVE
// 9-11. The fibre bridge: the same scenarios 1, 5 and 7 written as tasks. LLCoros::launch() of a callable that returns
//    an LL::Task runs it in a fibre, and every co_await of llcoro::co blocks that fibre, so the order is today's.
template<> template<>
void object::test<9>()
{
    set_test_name("bridge: wake order and frame boundary");
    settle();
    LLEventStream pa("order_pa", true), pb("order_pb", true), pc("order_pc", true);
    LLCoros::instance().launch("order_A", [&pa]() -> LL::Task<void>
        {
            mark("A0"); co_await llcoro::co::suspendUntilEventOn(pa); mark("A1"); co_await llcoro::co::suspend();
            mark("A2");
        });
    LLCoros::instance().launch("order_B", [&pb, &pc]() -> LL::Task<void>
        {
            mark("B0"); co_await llcoro::co::suspendUntilEventOn(pb); mark("B1"); pc.post(LLSD()); mark("B2");
        });
    LLCoros::instance().launch("order_C", [&pc]() -> LL::Task<void>
        {
            mark("C0"); co_await llcoro::co::suspendUntilEventOn(pc); mark("C1");
        });
    mark("M:post");
    pb.post(LLSD());
    pa.post(LLSD());
    mark("M:f1"); frame();
    mark("M:f2"); frame();
    mark("M:f3"); frame();
    ensure_equals(sTrace, "A0 B0 C0 M:post M:f1 B1 B2 A1 M:f2 C1 A2 M:f3");
}

template<> template<>
void object::test<10>()
{
    set_test_name("bridge: first event wins, consuming, timeout result");
    settle();
    LLEventStream pe("order_pe", true), pw("order_pw", true);
    LLCoros::instance().launch("order_E", [&pe]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::co::suspendUntilEventOn(pe); mark("E:" + v.asString());
        });
    LLCoros::instance().launch("order_K", [&pe]() -> LL::Task<void>
        {
            LLCoros::OverrideConsuming consuming(true);
            LLSD v = co_await llcoro::co::suspendUntilEventOn(pe); mark("K:" + v.asString());
        });
    LLCoros::instance().launch("order_W", [&pw]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::co::suspendUntilEventOnWithTimeout(pw, 0.001f, "timeout");
            mark("W:" + v.asString());
        });
    std::string late;
    LLTempBoundListener tail(pe.listen("order_tail", [&late](const LLSD& v) { late += v.asString(); return false; }));
    pe.post("one");
    pe.post("two");
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    frame();
    frame();
    ensure_equals(sTrace, "K:one E:two W:timeout");
    ensure_equals(late, "onetwo");
}

template<> template<>
void object::test<11>()
{
    set_test_name("bridge: LLApp status stops a task, an uncaught exception is rethrown");
    if (sAsan)
    {
        skip("fibre switch: ASan false positives (see sAsan)");
    }
    settle();
    LLEventStream ps("order_ps", true);
    LLCoros::instance().launch("order_S", [&ps]() -> LL::Task<void>
        {
            try { co_await llcoro::co::suspendUntilEventOn(ps); mark("S:event"); }
            catch (const LLCoros::Stopping&) { mark("S:stopping"); throw; }
        });
    OrderTestApp::status(LLApp::APP_STATUS_QUITTING);
    frame();
    OrderTestApp::status(LLApp::APP_STATUS_RUNNING);
    ensure_equals(sTrace, "S:stopping");
#if ! LL_WINDOWS
    LLEventStream px("order_px", true);
    LLCoros::instance().launch("order_X", [&px]() -> LL::Task<void>
        {
            co_await llcoro::co::suspendUntilEventOn(px); throw std::runtime_error("order_X");
        });
    px.post(LLSD());
    std::string what;
    try { frame(); }
    catch (const std::runtime_error& e) { what = e.what(); }
    ensure_equals(what, "order_X");
#endif
}
#endif // ! LL_CORO_NATIVE
} // namespace tut
