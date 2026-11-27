// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   lltask_test.cpp
 * @brief  Test of LL::Task, LL::Executor, the awaiters and LL::Channel.
 *
 * Tests 1 to 8 are the scenarios of the run-order oracle (llcoros_order_test.cpp, which runs them through LLCoros) on
 * a test executor: frame() is doFrame's pump, the main thread's own code plays the main loop. Wake-ups run only at the
 * frame's pump (doc/clean/plan.md, "Coroutines"): a launch runs the new task to its first wait and returns.
 */

#include "linden_common.h"
#include "llchannel.h"
#include "llcoros.h"
#include "lleventcoro.h"
#include "llevents.h"
#include "llexecutor.h"
#include "llsd.h"
#include "lltask.h"
#include "../test/lltut.h"
#include "../test/lltestapp.h"

#include <chrono>
#include <condition_variable>
#include <coroutine>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

using namespace std::chrono_literals;

namespace
{
// LLApp::setStatus is protected; the scenarios that test the stop path need it.
struct TaskTestApp: public LLTestApp
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

// Counts live instances, to show that frames and record-local values are destroyed.
struct Counted
{
    Counted() { ++sLive; }
    Counted(const Counted&) = delete;
    Counted& operator=(const Counted&) = delete;
    ~Counted() { --sLive; }
    static int sLive;
};
int Counted::sLive = 0;

LL::LocalPtr<Counted> sLocal;

// A task that waits for one event on the named pump and returns a number derived from it.
LL::Task<int> addEvent(std::string pump, int base)
{
    LLSD event = co_await llcoro::suspendUntilEventOn(pump);
    co_return base + event.asInteger();
}

LL::Task<int> addTwice(std::string pump, int base)
{
    int first = co_await addEvent(pump, base);
    int second = co_await addEvent(pump, first);
    co_return second;
}

LL::Task<void> throwAfterEvent(std::string pump)
{
    co_await llcoro::suspendUntilEventOn(pump);
    throw std::runtime_error("throwAfterEvent");
}

// An awaiter that parks the task until another thread wakes it through the executor.
struct Parked
{
    std::mutex mutex;
    std::condition_variable cv;
    std::coroutine_handle<> handle;
    LL::CoroData* record{nullptr};
    bool parked{false};
};

struct ParkAwaiter
{
    Parked& parked;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> h) const
    {
        std::lock_guard lock(parked.mutex);
        parked.handle = h;
        parked.record = LL::Executor::currentRecord();
        parked.parked = true;
        parked.cv.notify_one();
    }
    void await_resume() const noexcept {}
};
} // anonymous namespace

namespace tut
{
struct task_data
{
    TaskTestApp app;
    LL::Executor exec;

    ~task_data()
    {
        // leave nothing behind for the next test
        exec.shutdown();
        sLocal.reset();
    }

    // doFrame's pump: the per-frame event, then the pass over the ready queue, then any stashed exception.
    void frame()
    {
        LLEventPumps::instance().obtain("mainloop").post(LLSD());
        exec.pump();
        exec.rethrow();
    }

    // Let earlier tests' leftovers run out, so that only the dispatcher is ready when a scenario starts.
    void settle()
    {
        for (int i = 0; i < 5; ++i)
            frame();
        sTrace.clear();
    }
};
typedef test_group<task_data> task_group;
typedef task_group::object object;
task_group taskGroup("lltask");

// 1. Wake-ups run at the pump in wake order. A task woken by another task during the pump, and a task that yields
//    during the pump, both run at the next frame's pump.
template<> template<>
void object::test<1>()
{
    set_test_name("wake order and frame boundary");
    settle();
    LLEventStream pa("order_pa", true), pb("order_pb", true), pc("order_pc", true);
    exec.launch("order_A", [&pa]() -> LL::Task<void>
        {
            mark("A0"); co_await llcoro::suspendUntilEventOn(pa); mark("A1");
            co_await llcoro::suspend(); mark("A2");
        });
    exec.launch("order_B", [&pb, &pc]() -> LL::Task<void>
        {
            mark("B0"); co_await llcoro::suspendUntilEventOn(pb); mark("B1"); pc.post(LLSD()); mark("B2");
        });
    exec.launch("order_C", [&pc]() -> LL::Task<void>
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
    exec.launch("order_T", []() -> LL::Task<void>
        {
            mark("T0"); co_await llcoro::suspendUntilTimeout(0.001f); mark("T1");
        });
    std::this_thread::sleep_for(20ms);
    mark("f1"); frame();
    mark("f2"); frame();
    mark("f3");
    ensure_equals(sTrace, "T0 f1 f2 T1 f3");
}

// 3. MODEL-DIFF. A launch from the main loop runs the new task to its first wait and returns: the task that was already
//    ready waits for the pump.
template<> template<>
void object::test<3>()
{
    set_test_name("launch from the main loop does not run the ready queue");
    settle();
    LLEventStream pp("order_pp", true), pl("order_pl", true);
    exec.launch("order_P", [&pp]() -> LL::Task<void>
        {
            mark("P0"); co_await llcoro::suspendUntilEventOn(pp); mark("P1");
        });
    pp.post(LLSD());
    exec.launch("order_L", [&pl]() -> LL::Task<void>
        {
            mark("L0"); co_await llcoro::suspendUntilEventOn(pl); mark("L1");
        });
    mark("M:afterL");
    mark("f1"); frame();
    ensure_equals(sTrace, "P0 L0 M:afterL f1 P1");
}

// 4. MODEL-DIFF. A launch from inside a task returns after the new task's first wait: the launcher continues at once.
template<> template<>
void object::test<4>()
{
    set_test_name("launch from a task lets the launcher continue");
    settle();
    LLEventStream pq("order_pq", true), pr("order_pr", true);
    LL::Executor* e = &exec;
    exec.launch("order_Q", [e, &pq, &pr]() -> LL::Task<void>
        {
            mark("Q0");
            co_await llcoro::suspendUntilEventOn(pq);
            mark("Q1");
            e->launch("order_R", [&pr]() -> LL::Task<void>
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
    ensure_equals("tasks left", exec.taskCount(), 0u);
}

// 5. Only the first event satisfies a waiter; a second event on the same pump before the waiter runs is not delivered
//    to it and not consumed. A consuming waiter stops later listeners from seeing the event.
//    LLEventPump calls listeners that name no order in reverse alphabetical order of their names (LLDependencies keeps
//    them in a std::map and its topological sort comes out reversed): order_tail, then order_K, then order_E. The names
//    pin the order.
template<> template<>
void object::test<5>()
{
    set_test_name("first event wins, consuming");
    settle();
    LLEventStream pe("order_pe", true);
    exec.launch("order_E", [&pe]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOn(pe); mark("E:" + v.asString());
        });
    exec.launch("order_K", [&pe]() -> LL::Task<void>
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
    exec.launch("order_W1", [&pw]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(pw, 10.f, "timeout");
            mark("W1:" + v.asString());
        });
    exec.launch("order_W2", []() -> LL::Task<void>
        {
            LLEventStream never("order_never", true);
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(never, 0.001f, "timeout");
            mark("W2:" + v.asString());
        });
    pw.post("event");
    std::this_thread::sleep_for(20ms);
    frame();
    frame();
    ensure_equals(sTrace, "W1:event W2:timeout");
}

// 7. A status change on "LLApp" wakes every waiter with Stopping; the root logs it and the task ends.
template<> template<>
void object::test<7>()
{
    set_test_name("LLApp status stops waiters");
    settle();
    LLEventStream ps("order_ps", true);
    exec.launch("order_S", [&ps]() -> LL::Task<void>
        {
            try { co_await llcoro::suspendUntilEventOn(ps); mark("S:event"); }
            catch (const LLCoros::Stopping&) { mark("S:stopping"); throw; }
        });
    TaskTestApp::status(LLApp::APP_STATUS_QUITTING);
    frame();
    TaskTestApp::status(LLApp::APP_STATUS_RUNNING);
    ensure_equals(sTrace, "S:stopping");
    ensure_equals("tasks left", exec.taskCount(), 0u);
}

// 8. An exception that is not LLContinueError is stashed and rethrown on the main loop after the pump (on every
//    platform).
template<> template<>
void object::test<8>()
{
    set_test_name("uncaught exception is rethrown after the pump");
    settle();
    LLEventStream px("order_px", true);
    exec.launch("order_X", [&px]() -> LL::Task<void>
        {
            co_await llcoro::suspendUntilEventOn(px); throw std::runtime_error("order_X");
        });
    px.post(LLSD());
    std::string what;
    try { frame(); }
    catch (const std::runtime_error& e) { what = e.what(); }
    ensure_equals(what, "order_X");
    // an LLContinueError is logged, not rethrown
    exec.launch("order_Y", [&px]() -> LL::Task<void>
        {
            co_await llcoro::suspendUntilEventOn(px); LLTHROW(LLContinueError("order_Y"));
        });
    px.post(LLSD());
    frame();
    ensure_equals("tasks left", exec.taskCount(), 0u);
}

// 9. Tasks await tasks: values, exceptions, and the awaiter that never suspends.
template<> template<>
void object::test<9>()
{
    set_test_name("nested tasks return values and exceptions");
    settle();
    LLEventStream pn("order_pn", true);
    exec.launch("order_N", [&pn]() -> LL::Task<void>
        {
            int sum = co_await addTwice("order_pn", 1);
            mark("sum:" + std::to_string(sum));
            try { co_await throwAfterEvent("order_pn"); }
            catch (const std::runtime_error& e) { mark(e.what()); }
            // a task that is done when awaited does not suspend
            LL::Task<int> done = []() -> LL::Task<int> { co_return 42; }();
            mark(std::to_string(co_await std::move(done)));
        });
    pn.post(10);
    frame();
    pn.post(20);
    frame();
    pn.post(0);
    frame();
    ensure_equals(sTrace, "sum:31 throwAfterEvent 42");
}

// 10. Per-task local storage: every task sees its own value across suspensions, and the record deletes it.
template<> template<>
void object::test<10>()
{
    set_test_name("local_ptr is per task");
    settle();
    LLEventStream pl("order_pl10", true);
    auto body = [&pl](std::string tag) -> LL::Task<void>
    {
        auto* mine = new Counted;
        sLocal.reset(mine);
        co_await llcoro::suspendUntilEventOn(pl);
        mark(tag + (sLocal.get() == mine ? ":mine" : ":WRONG"));
    };
    exec.launch("order_L1", [body]() { return body("one"); });
    exec.launch("order_L2", [body]() { return body("two"); });
    ensure_equals("live while waiting", Counted::sLive, 2);
    ensure("none on the main loop", sLocal.get() == nullptr);
    pl.post(LLSD());
    frame();
    ensure_equals(sTrace, "two:mine one:mine");
    ensure_equals("deleted with the records", Counted::sLive, 0);
}

// 11. Shutdown destroys what is still waiting: no leak, nothing resumes a dead frame, the listeners are gone.
template<> template<>
void object::test<11>()
{
    set_test_name("shutdown destroys waiting tasks");
    settle();
    LLEventStream pd("order_pd", true), pq("order_pq11", true);
    struct Sentinel
    {
        ~Sentinel() { mark("destroyed"); }
    };
    exec.launch("order_D1", [&pd]() -> LL::Task<void>
        {
            Sentinel sentinel;
            co_await llcoro::suspendUntilEventOn(pd);
            mark("D1:resumed");
        });
    // woken, but not yet run when the shutdown comes
    exec.launch("order_D2", [&pq]() -> LL::Task<void>
        {
            Sentinel sentinel;
            co_await llcoro::suspendUntilEventOn(pq);
            mark("D2:resumed");
        });
    pq.post(LLSD());
    ensure_equals("destroyed", exec.shutdown(0), 2u);
    ensure_equals(sTrace, "destroyed destroyed");
    // nothing resumes a destroyed frame, whether it was queued or listening
    pd.post(LLSD());
    pq.post(LLSD());
    frame();
    frame();
    ensure_equals(sTrace, "destroyed destroyed");
}

// 12. A Task that is dropped while it waits is destroyed with its awaiter, which unlinks itself.
template<> template<>
void object::test<12>()
{
    set_test_name("a discarded task unlinks its awaiter");
    settle();
    LLEventStream pg("order_pg", true);
    exec.launch("order_G", [&pg]() -> LL::Task<void>
        {
            {
                LL::Task<int> dropped = addEvent("order_pg", 1);
                ensure("still waiting", ! dropped.done());
            }
            mark("dropped");
            co_await llcoro::suspendUntilEventOn(pg);
            mark("G:event");
        });
    pg.post(5);
    frame();
    ensure_equals(sTrace, "dropped G:event");
}

// 13. A thread other than the executor's can wake a task: the entry goes to the cross-thread list and is moved to the
//     queue when TIMERS comes up.
template<> template<>
void object::test<13>()
{
    set_test_name("wake from another thread");
    settle();
    Parked parked;
    exec.launch("order_Z", [&parked]() -> LL::Task<void>
        {
            mark("Z0"); co_await ParkAwaiter{ parked }; mark("Z1");
        });
    LL::Executor* e = &exec;
    std::thread waker([&parked, e]
        {
            std::unique_lock lock(parked.mutex);
            parked.cv.wait(lock, [&parked] { return parked.parked; });
            e->wake(parked.handle, parked.record);
        });
    for (int i = 0; i < 5000 && exec.taskCount() > 0; ++i)
    {
        frame();
        std::this_thread::sleep_for(1ms);
    }
    waker.join();
    ensure_equals(sTrace, "Z0 Z1");
}

// 14. Channel: first come first served, no suspension when a value is queued, timeout, close.
template<> template<>
void object::test<14>()
{
    set_test_name("channel");
    settle();
    LL::Channel<int> ch(2);
    auto consumer = [&ch](std::string tag) -> LL::Task<void>
    {
        for (;;)
        {
            auto [status, value] = co_await ch.pop_wait_for(5s);
            if (status == LL::ChannelStatus::closed)
            {
                mark(tag + ":closed");
                co_return;
            }
            mark(tag + ":" + (status == LL::ChannelStatus::success ? std::to_string(*value) : "timeout"));
        }
    };
    exec.launch("order_C1", [consumer]() { return consumer("a"); });
    exec.launch("order_C2", [consumer]() { return consumer("b"); });
    ensure("push 1", ch.try_push(1) == LL::ChannelStatus::success);
    ensure("push 2", ch.try_push(2) == LL::ChannelStatus::success);
    frame();
    // the first waiting consumer got the first value
    ensure_equals(sTrace, "a:1 b:2");
    // no consumer waiting: values queue up to the capacity
    sTrace.clear();
    ensure("push 3", ch.try_push(3) == LL::ChannelStatus::success);
    frame();
    ensure_equals(sTrace, "a:3");
    ch.close();
    ensure("push after close", ch.try_push(4) == LL::ChannelStatus::closed);
    frame();
    ensure_equals(sTrace, "a:3 b:closed a:closed");
    ensure_equals("tasks left", exec.taskCount(), 0u);
}

// 15. Channel: a queued value is popped without suspending, full is reported, a waiter times out, and a value queued
//     before close() is still poppable after it.
template<> template<>
void object::test<15>()
{
    set_test_name("channel capacity, timeout and close with queued values");
    settle();
    LL::Channel<int> ch(1);
    ensure("queue", ch.try_push(7) == LL::ChannelStatus::success);
    ensure("full", ch.try_push(8) == LL::ChannelStatus::full);
    exec.launch("order_C3", [&ch]() -> LL::Task<void>
        {
            auto first = co_await ch.pop_wait_for(5s);
            mark("pop:" + std::to_string(*first.value));
            auto timedOut = co_await ch.pop_wait_for(1ms);
            mark(timedOut.status == LL::ChannelStatus::timeout ? "timeout" : "WRONG");
            ch.close();
            auto closed = co_await ch.pop_wait_for(5s);
            mark(closed.status == LL::ChannelStatus::closed ? "closed" : "WRONG");
        });
    // the first pop did not suspend: the task is already waiting on the 1 ms timeout
    ensure_equals(sTrace, "pop:7");
    std::this_thread::sleep_for(20ms);
    frame();
    frame();
    ensure_equals(sTrace, "pop:7 timeout closed");
}

// 16. Channel: a push from another thread wakes the consumer.
template<> template<>
void object::test<16>()
{
    set_test_name("channel push from another thread");
    settle();
    LL::Channel<int> ch(4);
    exec.launch("order_C4", [&ch]() -> LL::Task<void>
        {
            auto popped = co_await ch.pop_wait_for(30s);
            mark(popped.value ? std::to_string(*popped.value) : "none");
        });
    std::thread pusher([&ch] { ch.try_push(99); });
    for (int i = 0; i < 5000 && exec.taskCount() > 0; ++i)
    {
        frame();
        std::this_thread::sleep_for(1ms);
    }
    pusher.join();
    ensure_equals(sTrace, "99");
}

// 17. The record: names, status while waiting, getName() and checkStop() work inside a task.
template<> template<>
void object::test<17>()
{
    set_test_name("coroutine name and status");
    settle();
    LLEventStream pr("order_pr17", true);
    exec.launch("order_NAME", [&pr]() -> LL::Task<void>
        {
            mark(LLCoros::getName());
            LLCoros::checkStop();
            co_await llcoro::suspendUntilEventOn(pr);
            mark(LLCoros::getStatus().empty() ? "status cleared" : "STATUS LEFT");
        });
    LL::CoroData* record = LL::CoroData::getInstance("order_NAME").get();
    ensure("record", record != nullptr);
    ensure_equals("status", record->mStatus, "waiting for order_pr17");
    pr.post(LLSD());
    frame();
    ensure_equals(sTrace, "order_NAME status cleared");
}
// 18. The task that started a child and awaits it is destroyed while it waits: the awaiter owns the child, so the child
//     goes with it and cannot resume the destroyed frame later.
template<> template<>
void object::test<18>()
{
    set_test_name("destroying a waiting parent destroys the child it awaits");
    settle();
    LLEventStream pg("order_pg18", true);
    exec.launch("order_PARENT", []() -> LL::Task<void>
        {
            struct Sentinel
            {
                ~Sentinel() { mark("parent destroyed"); }
            } sentinel;
            int value = co_await addEvent("order_pg18", 1);
            mark("PARENT resumed with " + std::to_string(value));
        });
    ensure_equals("destroyed", exec.shutdown(0), 1u);
    ensure_equals(sTrace, "parent destroyed");
    // the child is gone with the parent: the event finds no listener and nothing resumes freed memory
    pg.post(5);
    frame();
    frame();
    ensure_equals(sTrace, "parent destroyed");
}

// 18b. The task that started a child awaits it: when the event comes later, it resumes in its own record.
template<> template<>
void object::test<21>()
{
    set_test_name("the starter resumes in its own record");
    settle();
    LLEventStream pg("order_pg21", true);
    exec.launch("order_STARTER", []() -> LL::Task<void>
        {
            int value = co_await addEvent("order_pg21", 1);
            mark(LLCoros::getName() + " " + std::to_string(value));
        });
    pg.post(5);
    frame();
    ensure_equals(sTrace, "order_STARTER 6");
}

// 19. A frame destroyed while it waits in pop_wait_for leaves nothing in the channel or the executor.
template<> template<>
void object::test<19>()
{
    set_test_name("destroying a frame waiting in a channel unlinks it");
    settle();
    LL::Channel<int> ch(2);
    exec.launch("order_CW", [&ch]() -> LL::Task<void>
        {
            auto popped = co_await ch.pop_wait_for(30s);
            mark("popped " + std::to_string(popped.value.value_or(-1)));
        });
    ensure_equals("destroyed", exec.shutdown(0), 1u);
    // no waiter is left to be handed the value: it queues, and the cancelled timer does not fire
    ensure("push", ch.try_push(7) == LL::ChannelStatus::success);
    frame();
    exec.launch("order_CW2", [&ch]() -> LL::Task<void>
        {
            auto popped = co_await ch.pop_wait_for(30s);
            mark("popped " + std::to_string(popped.value.value_or(-1)));
        });
    ensure_equals(sTrace, "popped 7");
    // a destroyed waiter is gone from close() too
    exec.launch("order_CW3", [&ch]() -> LL::Task<void>
        {
            auto popped = co_await ch.pop_wait_for(30s);
            mark("popped " + std::to_string(popped.value.value_or(-1)));
        });
    ensure_equals("destroyed", exec.shutdown(0), 1u);
    ch.close();
    frame();
    ensure_equals(sTrace, "popped 7");
}

// 20. close() from another thread races the timeouts of the waiters it wakes (TSan and ASan are the judges).
template<> template<>
void object::test<20>()
{
    set_test_name("channel close races pop timeouts");
    settle();
    LL::Channel<int> ch(4);
    int finished = 0;
    for (int i = 0; i < 8; ++i)
    {
        exec.launch("order_R" + std::to_string(i), [&ch, &finished]() -> LL::Task<void>
            {
                for (;;)
                {
                    auto popped = co_await ch.pop_wait_for(1ms);
                    if (popped.status == LL::ChannelStatus::closed)
                    {
                        ++finished;
                        co_return;
                    }
                }
            });
    }
    std::thread closer([&ch]
        {
            std::this_thread::sleep_for(30ms);
            ch.close();
        });
    for (int i = 0; i < 5000 && exec.taskCount() > 0; ++i)
    {
        frame();
        std::this_thread::sleep_for(1ms);
    }
    closer.join();
    ensure_equals("all saw closed", finished, 8);
}
// 22. The event and the timer are both ready when the pump comes: the event, posted first, wins.
template<> template<>
void object::test<22>()
{
    set_test_name("event before the pump beats an expired timer");
    settle();
    LLEventStream pw("order_pw22", true);
    exec.launch("order_W22", [&pw]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(pw, 0.001f, "timeout");
            mark("W:" + v.asString());
        });
    std::this_thread::sleep_for(20ms);
    pw.post("event");
    frame();
    ensure_equals(sTrace, "W:event");
}

// 23. The timer comes up first in the pump (TIMERS leads the queue), a task that runs after it posts the event: the
//     event finds the claim taken, is neither delivered nor consumed, and the waiter times out.
template<> template<>
void object::test<23>()
{
    set_test_name("timer in the pump beats an event posted later in the same pump");
    settle();
    LLEventStream pw("order_pw23", true), px("order_px23", true);
    std::string late;
    LLTempBoundListener tail(pw.listen("order_zz23", [&late](const LLSD& v) { late += v.asString(); return false; }));
    exec.launch("order_W23", [&pw]() -> LL::Task<void>
        {
            LLSD v = co_await llcoro::suspendUntilEventOnWithTimeout(pw, 0.001f, "timeout");
            mark("W:" + v.asString());
        });
    exec.launch("order_X23", [&px, &pw]() -> LL::Task<void>
        {
            co_await llcoro::suspendUntilEventOn(px);
            mark("X");
            pw.post("event");
        });
    std::this_thread::sleep_for(20ms);
    px.post(LLSD());
    frame();
    frame();
    ensure_equals(sTrace, "X W:timeout");
    // not consumed: the listener behind the waiter's still saw it
    ensure_equals(late, "event");
}

// 24. A task queued by suspend() when the shutdown comes is destroyed, not resumed.
template<> template<>
void object::test<24>()
{
    set_test_name("shutdown destroys a task waiting in suspend()");
    settle();
    struct Sentinel
    {
        ~Sentinel() { mark("destroyed"); }
    };
    exec.launch("order_Y24", []() -> LL::Task<void>
        {
            Sentinel sentinel;
            co_await llcoro::suspend();
            mark("resumed");
        });
    ensure_equals("destroyed", exec.shutdown(0), 1u);
    frame();
    frame();
    ensure_equals(sTrace, "destroyed");
}

// 25. LLCoros::launch runs a Task callable on LLCoros' executor, under the name it returns.
template<> template<>
void object::test<25>()
{
    set_test_name("LLCoros::launch");
    settle();
    LLEventStream pn("order_pn25", true);
    std::string name = LLCoros::instance().launch("order_NATIVE", [&pn]() -> LL::Task<void>
        {
            mark("first " + LLCoros::getName());
            co_await llcoro::suspendUntilEventOn(pn);
            mark("second");
        });
    ensure("distinct name", name.rfind("order_NATIVE", 0) == 0);
    pn.post(LLSD());
    LLCoros::instance().executor().pump();
    ensure_equals(sTrace, "first " + name + " second");
    ensure_equals("tasks left", LLCoros::instance().executor().taskCount(), 0u);
}
} // namespace tut
