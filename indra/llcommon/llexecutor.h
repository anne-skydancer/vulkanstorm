// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   llexecutor.h
 * @brief  The main-thread executor that runs LL::Task coroutines (doc/clean/plan.md, "Coroutines").
 *
 * It reproduces the run order of the Boost.Fiber scheduler LLCoros used, minus the quirks that ran coroutines inside
 * other main-thread code:
 *
 * - One FIFO queue of ready entries. A wake-up (an event for a waiting task, a timer, a yield) appends at once, in the
 *   waker's context.
 * - Two sentinels in the same queue. TIMERS stands for Boost's dispatcher fibre: when it comes up it moves the wake-ups
 *   that arrived from other threads, then the expired timers (in deadline, then insertion order), to the back, and
 *   queues itself behind them. MAIN marks the end of a pump.
 * - pump() (the viewer's doFrame, once per frame) queues MAIN and runs entries until MAIN comes back. Whatever was
 *   woken while the pump runs lies behind MAIN and runs at the next pump: that is the frame boundary.
 * - Wake-ups run only at the pump. A launch runs the new task to its first wait and returns; it does not run the
 *   ready queue (decision D1-A of the plan).
 *
 * Tasks run on the executor's thread only. wake() is the one call that may come from any thread.
 */

#if ! defined(LL_LLEXECUTOR_H)
#define LL_LLEXECUTOR_H

#include "llinstancetracker.h"
#include "lltask.h"
#include "stdtypes.h"

#include <boost/fiber/fss.hpp>

#include <atomic>
#include <chrono>
#include <coroutine>
#include <cstdint>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace LL
{

class Executor;

/**
 * The record of one coroutine: its name, status, "consuming" flag and local storage. LLCoros keeps one per launched
 * coroutine (a fibre today, a task on the executor) and one thread_local for the default coroutine of each thread.
 */
class LL_COMMON_API CoroData: public LLInstanceTracker<CoroData, std::string>
{
public:
    explicit CoroData(const std::string& name);
    /// for the thread_local record of the default coroutine on each thread
    explicit CoroData(int n);
    ~CoroData() override;
    CoroData(const CoroData&) = delete;
    CoroData& operator=(const CoroData&) = delete;

    // tweaked name of the coroutine; empty for the default coroutine of a thread
    const std::string mName;
    // set_consuming() state
    bool mConsuming{false};
    // setStatus() state
    std::string mStatus;
    F64 mCreationTime; // since epoch

    // For a task: the executor that runs it and the frame of its root wrapper. Unused for a fibre.
    Executor* mExecutor{nullptr};
    std::coroutine_handle<> mRoot;

    // Local storage, see LocalPtr. The record owns the values and deletes them with itself.
    void* localGet(std::uint64_t key) const;
    /// replace the value for @a key, deleting the old one; a null @a value forgets the key
    void localSet(std::uint64_t key, void* value, void (*deleter)(void*));

private:
    struct Slot
    {
        void* value;
        void (*deleter)(void*);
    };
    std::unordered_map<std::uint64_t, Slot> mLocals;
};

/// a new key for a LocalPtr: unique for the life of the process, so a LocalPtr built where an old one was never sees
/// the old one's values
LL_COMMON_API std::uint64_t newLocalKey() noexcept;

/**
 * Per-coroutine storage. On a task of the executor the value lives in that task's record; on a fibre, and on the
 * default coroutine of a thread, it lives in a fibre-specific pointer (the fibre branch goes with the fibres).
 *
 * A LocalPtr is not copyable or movable. A task's value is deleted with its record.
 */
template<typename T>
class LocalPtr
{
public:
    LocalPtr() = default;
    LocalPtr(const LocalPtr&) = delete;
    LocalPtr& operator=(const LocalPtr&) = delete;

    T* get() const
    {
        if (CoroData* record = currentTaskRecord())
        {
            return static_cast<T*>(record->localGet(mKey));
        }
        return mFibre.get();
    }
    T* operator->() const { return get(); }
    T& operator*() const { return *get(); }
    /// Adopt @a value for the running coroutine, deleting its old value; the argument may be null.
    void reset(T* value = nullptr)
    {
        if (CoroData* record = currentTaskRecord())
        {
            record->localSet(mKey, value, [](void* p) { delete static_cast<T*>(p); });
        }
        else
        {
            mFibre.reset(value);
        }
    }

private:
    static CoroData* currentTaskRecord();

    const std::uint64_t mKey{newLocalKey()};
    boost::fibers::fiber_specific_ptr<T> mFibre;
};

/// A claim on the one wake-up a waiter takes. Whoever calls claim() first (the event, the timer, a close) delivers the
/// result and wakes the task; everyone else sees false and does nothing. That makes "first event wins" and
/// "the timeout races an event" exactly-once. The claim is thread safe and LL::Channel uses it across threads; the
/// event awaiters (lleventcoro.h) are not: their listeners run on the executor's thread.
struct WaitClaim
{
    std::atomic<bool> mClaimed{false};
    // written by the winner before it wakes the task
    bool mTimedOut{false};

    bool claim() noexcept { return ! mClaimed.exchange(true, std::memory_order_acq_rel); }
};

class LL_COMMON_API Executor
{
public:
    using Clock = std::chrono::steady_clock;
    /// identifies a timer: (deadline, insertion sequence)
    using TimerKey = std::pair<Clock::time_point, std::uint64_t>;

    /// The executor belongs to the thread that builds it: tasks run, and the queue and the timers are used, only there.
    Executor();
    /// destroys the frames of tasks still alive (see shutdown())
    ~Executor();
    Executor(const Executor&) = delete;
    Executor& operator=(const Executor&) = delete;

    /// the executor running the current task, or nullptr when no task is running on this thread
    static Executor* current();
    /// the record of the task running on this thread, or nullptr
    static CoroData* currentRecord();
    /// true on the thread this executor belongs to
    bool onOwnerThread() const;

    /**
     * Start a task: call @a fn under a new record named @a name and run what it returns to its first wait. Returns
     * after that; it does not run other ready tasks. @a name must be distinct (LLCoros::launch makes it so).
     * @a fn is kept alive in the task's root frame until the task ends, so a capturing lambda that is itself the
     * coroutine is safe here (and only through launch(), see lltask.h); the lambdas the plan prescribes are plain
     * functions that return the Task of a by-value coroutine.
     */
    template<TaskFactory F>
    void launch(const std::string& name, F fn);

    /// One frame's pass over the ready queue (see the file comment). Executor thread only, not from inside a task.
    void pump();

    /// Make @a handle ready: append it to the ready queue. Any thread. @a record is the task it belongs to.
    void wake(std::coroutine_handle<> handle, CoroData* record);

    /// Run @a handle (of the task @a record) at the first pump after @a delay, or later. Returns the key for
    /// cancelTimer(). On expiry, if @a claim can still be claimed, it is set to timed out and the task is woken.
    TimerKey addTimer(Clock::duration delay, std::coroutine_handle<> handle, CoroData* record, WaitClaim* claim);
    void cancelTimer(const TimerKey& key);

    /// A leaf awaiter calls this from its destructor when its frame is destroyed while it waits: no queued entry may
    /// resume a dead frame.
    void forget(std::coroutine_handle<> handle);

    /**
     * The drain at shutdown, called before the singletons go: pump up to @a maxPumps times while tasks remain, then
     * log the survivors and destroy their frames (the awaiters unlink themselves). Returns how many were destroyed.
     */
    std::size_t shutdown(int maxPumps = 10);

    /// Rethrow the first exception a task left uncaught (not Stop, not LLContinueError). The main loop calls this
    /// after the pump. A no-op when there is none.
    void rethrow();

    /// number of tasks alive
    std::size_t taskCount() const { return mLive.size(); }

private:
    struct Entry
    {
        enum class Kind: unsigned char { Task, Timers, Main };
        Kind kind{Kind::Task};
        std::coroutine_handle<> handle;
        CoroData* record{nullptr};
    };
    struct Timer
    {
        std::coroutine_handle<> handle;
        CoroData* record{nullptr};
        WaitClaim* claim{nullptr};
    };
    struct RootTask
    {
        struct promise_type
        {
            RootTask get_return_object() noexcept
            {
                return RootTask{ std::coroutine_handle<promise_type>::from_promise(*this) };
            }
            std::suspend_always initial_suspend() const noexcept { return {}; }
            // the frame goes away by itself when the root finishes
            std::suspend_never final_suspend() const noexcept { return {}; }
            void return_void() const noexcept {}
            // runRoot catches everything
            void unhandled_exception() const noexcept { std::terminate(); }
        };
        std::coroutine_handle<promise_type> mHandle;
    };
    struct Scope;

    template<typename F>
    // NOLINTNEXTLINE(performance-unnecessary-value-param): the frame must own the callable, a reference would dangle
    static RootTask runRoot(Executor* self, std::unique_ptr<CoroData> record, F fn);
    void track(CoroData* record);
    void start(CoroData* record);
    void retire(CoroData* record);
    void handleUncaught(CoroData& record, const std::exception_ptr& error);
    void resume(const Entry& entry);
    const std::thread::id mOwner{std::this_thread::get_id()};
    bool mPumping{false};
    std::deque<Entry> mReady;
    std::map<TimerKey, Timer> mTimers;
    std::uint64_t mTimerSeq{0};
    std::vector<CoroData*> mLive;

    // wake-ups from other threads, moved to mReady when TIMERS comes up
    std::mutex mRemoteMutex;
    std::vector<Entry> mRemote;

    struct Uncaught
    {
        std::string name;
        std::exception_ptr error;
    };
    std::queue<Uncaught> mUncaught;
};

template<typename T>
CoroData* LocalPtr<T>::currentTaskRecord()
{
    return Executor::currentRecord();
}

// A coroutine takes its parameters by value; @a fn is the copy that keeps the callable alive for the whole task.
template<typename F>
// NOLINTNEXTLINE(performance-unnecessary-value-param): the frame must own the callable, a reference would dangle
Executor::RootTask Executor::runRoot(Executor* self, std::unique_ptr<CoroData> record, F fn)
{
    CoroData* rec = record.get();
    try
    {
        Task<void> task = fn();
        co_await std::move(task);
    }
    catch (...)
    {
        // No co_await in a handler: this only classifies, logs and stashes.
        self->handleUncaught(*rec, std::current_exception());
    }
    self->retire(rec);
}

template<TaskFactory F>
void Executor::launch(const std::string& name, F fn)
{
    auto record = std::make_unique<CoroData>(name);
    record->mExecutor = this;
    CoroData* rec = record.get();
    RootTask root = runRoot(this, std::move(record), std::move(fn));
    rec->mRoot = root.mHandle;
    // the frame exists but has not started: if registering it throws, destroy it (and with it the record)
    struct Guard
    {
        std::coroutine_handle<> frame;
        ~Guard()
        {
            if (frame)
            {
                frame.destroy();
            }
        }
    } guard{ root.mHandle };
    track(rec);
    guard.frame = nullptr;
    start(rec);
}

} // namespace LL

#endif /* ! defined(LL_LLEXECUTOR_H) */
