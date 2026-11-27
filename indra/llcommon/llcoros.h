/**
 * @file   llcoros.h
 * @author Nat Goodspeed
 * @date   2009-06-02
 * @brief  Manage running boost::coroutine instances
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

#if ! defined(LL_LLCOROS_H)
#define LL_LLCOROS_H

#include "llexception.h"
#include "llexecutor.h"
#include "lltask.h"
#include "llsingleton.h"
#include <boost/fiber/future/future.hpp>
#include <boost/fiber/future/promise.hpp>
#include <boost/fiber/recursive_mutex.hpp>
#include "mutex.h"
#include <string>
#include <type_traits>
#include <utility>

// e.g. #include LLCOROS_MUTEX_HEADER
// NOLINTNEXTLINE(bugprone-macro-parentheses): the replacement is a header name, which cannot be parenthesised
#define LLCOROS_MUTEX_HEADER   <boost/fiber/mutex.hpp>
// NOLINTNEXTLINE(bugprone-macro-parentheses): the replacement is a header name, which cannot be parenthesised
#define LLCOROS_CONDVAR_HEADER <boost/fiber/condition_variable.hpp>

namespace boost::fibers
{
class mutex;
enum class cv_status;
class condition_variable;
}

class LL_COMMON_API LLCoros: public LLSingleton<LLCoros>
{
    LLSINGLETON(LLCoros);
    ~LLCoros() override;

    void cleanupSingleton() override;
public:
    /// Once the app is quitting (every waiting coroutine has had Stopping
    /// queued), pump the executor up to 10 times so the stopped coroutines
    /// unwind while the systems they use still exist, then log and destroy
    /// the ones that are still waiting. Logs the active coroutines before and
    /// after. Safe to call more than once. Main thread only.
    void drain(const std::string& when);

    // For debugging, return true if on the main coroutine for the current thread
    // Code that should not be executed from a coroutine should be protected by
    // llassert(LLCoros::on_main_coro())
    static bool on_main_coro();

    // For debugging, return true if on the main thread and not in a coroutine
    // Non-thread-safe code in the main loop should be protected by
    // llassert(LLCoros::on_main_thread_main_coro())
    static bool on_main_thread_main_coro();

    /**
     * Create and start running a new coroutine with specified name. The name
     * string you pass is a suggestion; it will be tweaked for uniqueness. The
     * actual name is returned to you.
     *
     * A coroutine is an LL::Task (lltask.h) running on the main loop's
     * executor. @a callable is nullary and returns the Task of the coroutine,
     * which takes its parameters BY VALUE (reference parameters dangle once
     * the coroutine suspends). Usage looks like this, for (e.g.) two
     * coroutine parameters:
     * @code
     * class MyClass
     * {
     * public:
     *     ...
     *     LL::Task<void> myCoroutineMethod(std::string, LLSD);
     *     ...
     * };
     * ...
     * std::string name = LLCoros::instance().launch(
     *    "mycoro", [this] { return myCoroutineMethod("somestring", LLSD(17)); });
     * @endcode
     *
     * The callable should be a plain function, or a lambda that is NOT itself
     * the coroutine, returning the Task of a by-value coroutine. A lambda
     * that is itself a coroutine and captures is safe only here, because the
     * launch keeps it alive for the life of the task (see lltask.h).
     *
     * launch() tweaks the suggested name so it won't collide with any
     * existing coroutine instance, creates the coroutine, registers it with
     * the tweaked name and runs it until its first wait. It does not run
     * other ready coroutines: those run when the main loop pumps the executor
     * once per frame (LL::Executor::pump()). At the first wait it returns the
     * tweaked name.
     */
    template<typename F>
        requires LL::TaskFactory<std::decay_t<F>>
    std::string launch(const std::string& prefix, F&& callable)
    {
        const std::string name(generateDistinctName(prefix));
        mExecutor.launch(name, std::decay_t<F>(std::forward<F>(callable)));
        return name;
    }

    /// the executor that runs this process's tasks; the main loop pumps it once per frame
    LL::Executor& executor() { return mExecutor; }

    /**
     * Abort a running coroutine by name. Normally, when a coroutine either
     * runs to completion or terminates with an exception, LLCoros quietly
     * cleans it up. This is for use only when you must explicitly interrupt
     * one prematurely. Returns @c true if the specified name was found and
     * still running at the time.
     */
//  bool kill(const std::string& name);

    /**
     * From within a coroutine, look up the (tweaked) name string by which
     * this coroutine is registered. Returns the empty string if not found
     * (e.g. if the coroutine was launched by hand rather than using
     * LLCoros::launch()).
     */
    static std::string getName();

    /**
     * rethrow() is called by the main loop, after it pumps the executor, to
     * propagate an exception from any coroutine into the main loop, where it
     * can engage the normal unhandled-exception machinery, up to and
     * including crash reporting.
     *
     * The executor keeps a queue of otherwise-uncaught exceptions from
     * terminated coroutines (Stop and LLContinueError are only logged). Each
     * call to rethrow() pops the first of those and rethrows it. When the
     * queue is empty (normal case), rethrow() is a no-op.
     */
    void rethrow();

    /**
     * This variation returns a name suitable for log messages: the explicit
     * name for an explicitly-launched coroutine, or "mainN" for the default
     * coroutine on a thread.
     */
    static std::string logname();

    /// diagnostic
    void printActiveCoroutines(const std::string& when=std::string());

    /**
     * Most coroutines, most of the time, don't "consume" the events for which
     * they're suspending. This way, an arbitrary number of listeners (whether
     * coroutines or simple callbacks) can be registered on a particular
     * LLEventPump, every listener responding to each of the events on that
     * LLEventPump. But a particular coroutine can assert that it will consume
     * each event for which it suspends. (See also llcoro::postAndSuspend(),
     * llcoro::VoidListener)
     */
    static void set_consuming(bool consuming);
    static bool get_consuming();

    /**
     * RAII control of the consuming flag
     */
    class OverrideConsuming
    {
    public:
        OverrideConsuming(bool consuming):
            mPrevConsuming(get_consuming())
        {
            set_consuming(consuming);
        }
        OverrideConsuming(const OverrideConsuming&) = delete;
        ~OverrideConsuming()
        {
            set_consuming(mPrevConsuming);
        }

    private:
        bool mPrevConsuming;
    };

    /// set string coroutine status for diagnostic purposes
    static void setStatus(const std::string& status);
    static std::string getStatus();

    /// RAII control of status
    class TempStatus
    {
    public:
        TempStatus(const std::string& status):
            mOldStatus(getStatus())
        {
            setStatus(status);
        }
        TempStatus(const TempStatus&) = delete;
        ~TempStatus()
        {
            setStatus(mOldStatus);
        }

    private:
        std::string mOldStatus;
    };

    /// thrown by checkStop()
    // It may sound ironic that Stop is derived from LLContinueError, but the
    // point is that LLContinueError is the category of exception that should
    // not immediately crash the viewer. Stop and its subclasses are to notify
    // coroutines that the viewer intends to shut down. The expected response
    // is to terminate the coroutine, rather than abort the viewer.
    struct Stop: public LLContinueError
    {
        Stop(const std::string& what): LLContinueError(what) {}
    };

    /// early stages
    struct Stopping: public Stop
    {
        Stopping(const std::string& what): Stop(what) {}
    };

    /// cleaning up
    struct Stopped: public Stop
    {
        Stopped(const std::string& what): Stop(what) {}
    };

    /// cleaned up -- not much survives!
    struct Shutdown: public Stop
    {
        Shutdown(const std::string& what): Stop(what) {}
    };

    /// Call this intermittently if there's a chance your coroutine might
    /// continue running into application shutdown. Throws Stop if LLCoros has
    /// been cleaned up.
    static void checkStop();

    /**
     * Aliases for promise and future (still used by WorkQueue::waitForResult()).
     */
    template <typename T>
    using Promise = boost::fibers::promise<T>;
    template <typename T>
    using Future = boost::fibers::future<T>;
    template <typename T>
    static Future<T> getFuture(Promise<T>& promise) { return promise.get_future(); }

    // use mutex, lock, condition_variable suitable for coroutines (until the standard ones replace them)
    using Mutex = boost::fibers::mutex;
    using RMutex = boost::fibers::recursive_mutex;
    using LockType = std::unique_lock<Mutex>;
    using cv_status = boost::fibers::cv_status;
    using ConditionVariable = boost::fibers::condition_variable;

    /// for data local to each running coroutine
    template <typename T>
    using local_ptr = LL::LocalPtr<T>;

private:
    std::string generateDistinctName(const std::string& prefix) const;
    using CoroData = LL::CoroData;
    static CoroData& get_CoroData();

    // Runs the tasks; the main loop pumps it once per frame.
    LL::Executor mExecutor;
};

namespace llcoro
{

inline
std::string logname() { return LLCoros::logname(); }

} // llcoro

#endif /* ! defined(LL_LLCOROS_H) */
