/**
 * @file   lleventcoro.h
 * @author Nat Goodspeed
 * @date   2009-04-29
 * @brief  Utilities to interface between coroutines and events.
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

#if ! defined(LL_LLEVENTCORO_H)
#define LL_LLEVENTCORO_H

#include <chrono>
#include <coroutine>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include "llcoros.h"
#include "llevents.h"
#include "llexecutor.h"

/**
 * Like LLListenerOrPumpName, this is a class intended for parameter lists:
 * accept a <tt>const LLEventPumpOrPumpName&</tt> and you can accept either an
 * <tt>LLEventPump&</tt> or its string name. For a single parameter that could
 * be either, it's not hard to overload the function -- but as soon as you
 * want to accept two such parameters, this is cheaper than four overloads.
 */
class LLEventPumpOrPumpName
{
public:
    /// Pass an actual LLEventPump&
    LLEventPumpOrPumpName(LLEventPump& pump):
        mPump(pump)
    {}
    /// Pass the string name of an LLEventPump
    LLEventPumpOrPumpName(const std::string& pumpname):
        mPump(LLEventPumps::instance().obtain(pumpname))
    {}
    /// Pass string constant name of an LLEventPump. This override must be
    /// explicit, since otherwise passing <tt>const char*</tt> to a function
    /// accepting <tt>const LLEventPumpOrPumpName&</tt> would require two
    /// different implicit conversions: <tt>const char*</tt> -> <tt>const
    /// std::string&</tt> -> <tt>const LLEventPumpOrPumpName&</tt>.
    LLEventPumpOrPumpName(const char* pumpname):
        mPump(LLEventPumps::instance().obtain(pumpname))
    {}
    /// Unspecified: "I choose not to identify an LLEventPump."
    LLEventPumpOrPumpName() = default;
    operator LLEventPump& () const { return *mPump; }
    LLEventPump& getPump() const { return *mPump; }
    operator bool() const { return bool(mPump); }
    bool operator!() const { return ! mPump; }

private:
    boost::optional<LLEventPump&> mPump;
};

namespace llcoro
{

/**
 * Yield control from a coroutine for one "mainloop" tick. If your coroutine
 * runs without suspending for nontrivial time, sprinkle in calls to this
 * function to avoid stalling the rest of the viewer processing.
 */
void suspend();

/**
 * Yield control from a coroutine for at least the specified number of seconds
 */
void suspendUntilTimeout(float seconds);

/**
 * Yield control from a coroutine until the next mainloop's newFrame event.
 */
void suspendUntilNextFrame();

/**
 * Post specified LLSD event on the specified LLEventPump, then suspend for a
 * response on specified other LLEventPump. This is more than mere
 * convenience: the difference between this function and the sequence
 * @code
 * requestPump.post(myEvent);
 * LLSD reply = suspendUntilEventOn(replyPump);
 * @endcode
 * is that the sequence above fails if the reply is posted immediately on
 * @a replyPump, that is, before <tt>requestPump.post()</tt> returns. In the
 * sequence above, the running coroutine isn't even listening on @a replyPump
 * until <tt>requestPump.post()</tt> returns and @c suspendUntilEventOn() is
 * entered. Therefore, the coroutine completely misses an immediate reply
 * event, making it suspend indefinitely.
 *
 * By contrast, postAndSuspend() listens on the @a replyPump @em before posting
 * the specified LLSD event on the specified @a requestPump.
 *
 * @param event LLSD data to be posted on @a requestPump
 * @param requestPump an LLEventPump on which to post @a event. Pass either
 * the LLEventPump& or its string name. However, if you pass a
 * default-constructed @c LLEventPumpOrPumpName, we skip the post() call.
 * @param replyPump an LLEventPump on which postAndSuspend() will listen for a
 * reply. Pass either the LLEventPump& or its string name. The calling
 * coroutine will suspend until that reply arrives. (If you're concerned about a
 * reply that might not arrive, please see also LLEventTimeout.)
 * @param replyPumpNamePath specifies the location within @a event in which to
 * store <tt>replyPump.getName()</tt>. This is a strictly optional convenience
 * feature; obviously you can store the name in @a event "by hand" if desired.
 * @a replyPumpNamePath can be specified in any of four forms:
 * * @c isUndefined() (default-constructed LLSD object): do nothing. This is
 *   the default behavior if you omit @a replyPumpNamePath.
 * * @c isInteger(): @a event is an array. Store <tt>replyPump.getName()</tt>
 *   in <tt>event[replyPumpNamePath.asInteger()]</tt>.
 * * @c isString(): @a event is a map. Store <tt>replyPump.getName()</tt> in
 *   <tt>event[replyPumpNamePath.asString()]</tt>.
 * * @c isArray(): @a event has several levels of structure, e.g. map of
 *   maps, array of arrays, array of maps, map of arrays, ... Store
 *   <tt>replyPump.getName()</tt> in
 *   <tt>event[replyPumpNamePath[0]][replyPumpNamePath[1]]...</tt> In other
 *   words, examine each array entry in @a replyPumpNamePath in turn. If it's an
 *   <tt>LLSD::String</tt>, the current level of @a event is a map; step down to
 *   that map entry. If it's an <tt>LLSD::Integer</tt>, the current level of @a
 *   event is an array; step down to that array entry. The last array entry in
 *   @a replyPumpNamePath specifies the entry in the lowest-level structure in
 *   @a event into which to store <tt>replyPump.getName()</tt>.
 */
LLSD postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                 const LLEventPumpOrPumpName& replyPump, const LLSD& replyPumpNamePath=LLSD());

/// Wait for the next event on the specified LLEventPump. Pass either the
/// LLEventPump& or its string name.
inline
LLSD suspendUntilEventOn(const LLEventPumpOrPumpName& pump)
{
    // This is now a convenience wrapper for postAndSuspend().
    return postAndSuspend(LLSD(), LLEventPumpOrPumpName(), pump);
}

/// Like postAndSuspend(), but if we wait longer than @a timeout seconds,
/// stop waiting and return @a timeoutResult instead.
LLSD postAndSuspendWithTimeout(const LLSD& event,
                               const LLEventPumpOrPumpName& requestPump,
                               const LLEventPumpOrPumpName& replyPump,
                               const LLSD& replyPumpNamePath,
                               F32 timeout, const LLSD& timeoutResult);

/// Suspend the coroutine until an event is fired on the identified pump
/// or the timeout duration has elapsed.  If the timeout duration
/// elapses the specified LLSD is returned.
inline
LLSD suspendUntilEventOnWithTimeout(const LLEventPumpOrPumpName& suspendPumpOrName,
                                    F32 timeoutin, const LLSD &timeoutResult)
{
    return postAndSuspendWithTimeout(LLSD(),                  // event
                                     LLEventPumpOrPumpName(), // requestPump
                                     suspendPumpOrName,       // replyPump
                                     LLSD(),                  // replyPumpNamePath
                                     timeoutin,
                                     timeoutResult);
}

} // namespace llcoro

/**
 * The co_await forms of the primitives above (doc/clean/plan.md, "Coroutines"): the same names and semantics, for a
 * coroutine that returns an LL::Task. Each checks LLCoros::checkStop() first, sets the coroutine's status while it
 * waits, lets the first event win (a later event is not delivered and not consumed), and turns a status other than
 * "running" on "LLApp" into LLCoros::Stopping. An awaiter is not movable and unlinks every listener, timer and queue
 * entry in its destructor, so a destroyed frame is never resumed.
 *
 * A task must use these forms, never the blocking ones above: a blocking form blocks the hosting fibre, and a task is
 * not a fibre but runs on the main loop. The blocking forms abort when a task calls one.
 *
 * <tt>llcoro::co</tt> is the back end LL_CORO_NATIVE picks (llcoros.h): llcoro::native really suspends the task on
 * the executor, llcoro::bridge blocks the hosting fibre with the function above (nothing suspends at the C++20
 * level). Both are always built and tested. The change that removes the fibres makes llcoro::native the only one and
 * deletes the blocking functions above; callers then lose no more than the "co::".
 */
namespace llcoro::native
{

/// Waits for one event on a pump (optionally after posting a request, optionally with a timeout), or for a timeout
/// alone. Not movable: a coroutine obtains it as a temporary in a co_await expression.
class LL_COMMON_API EventAwaiter
{
public:
    /// postAndSuspend() and, with @a timeout, postAndSuspendWithTimeout()
    EventAwaiter(std::string callerName, LLSD event, LLEventPumpOrPumpName requestPump,
                 LLEventPumpOrPumpName replyPump, LLSD replyPumpNamePath,
                 std::optional<std::chrono::milliseconds> timeout, LLSD timeoutResult);
    /// suspendUntilTimeout(): wait on a pump nobody posts on
    EventAwaiter(std::unique_ptr<LLEventStream> bogusPump, std::chrono::milliseconds timeout);
    EventAwaiter(const EventAwaiter&) = delete;
    EventAwaiter& operator=(const EventAwaiter&) = delete;
    ~EventAwaiter();

    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    LLSD await_resume();

private:
    void completed();

    std::string mCallerName;
    // for suspendUntilTimeout(): the pump that mReplyPump names. Declared before the connections, so that they are
    // disconnected first.
    std::unique_ptr<LLEventStream> mOwnedPump;
    LLEventPumpOrPumpName mRequestPump, mReplyPump;
    LLSD mEvent, mReplyPumpNamePath, mTimeoutResult;
    std::optional<std::chrono::milliseconds> mTimeout;

    LL::Executor* mExecutor{nullptr};
    LL::CoroData* mRecord{nullptr};
    std::coroutine_handle<> mHandle;
    std::string mListenerName;
    LL::WaitClaim mClaim;
    LLSD mValue;
    std::exception_ptr mError;
    std::optional<LL::Executor::TimerKey> mTimer;
    std::optional<LLCoros::TempStatus> mStatus;
    bool mResumed{false};
    LLTempBoundListener mReplyConnection, mStopConnection;
};

/// Yield for one "mainloop" tick: the task runs again at the next pump.
class LL_COMMON_API SuspendAwaiter
{
public:
    SuspendAwaiter() = default;
    SuspendAwaiter(const SuspendAwaiter&) = delete;
    SuspendAwaiter& operator=(const SuspendAwaiter&) = delete;
    ~SuspendAwaiter();

    bool await_ready();
    void await_suspend(std::coroutine_handle<> handle);
    void await_resume();

private:
    LL::Executor* mExecutor{nullptr};
    std::coroutine_handle<> mHandle;
    std::optional<LLCoros::TempStatus> mStatus;
    bool mResumed{false};
};

/// co_await llcoro::native::suspend(); yields for one tick
[[nodiscard]] inline SuspendAwaiter suspend() { return {}; }

/// Wait for at least @a seconds
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilTimeout(float seconds);

/// Wait for the next "mainloop" event, once per frame
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilNextFrame();

/// LLSD reply = co_await llcoro::native::postAndSuspend(...)
[[nodiscard]] LL_COMMON_API EventAwaiter postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                                        const LLEventPumpOrPumpName& replyPump,
                                                        const LLSD& replyPumpNamePath = LLSD());

[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilEventOn(const LLEventPumpOrPumpName& pump);

[[nodiscard]] LL_COMMON_API EventAwaiter postAndSuspendWithTimeout(const LLSD& event,
                                                                   const LLEventPumpOrPumpName& requestPump,
                                                                   const LLEventPumpOrPumpName& replyPump,
                                                                   const LLSD& replyPumpNamePath, F32 timeout,
                                                                   const LLSD& timeoutResult);

[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilEventOnWithTimeout(const LLEventPumpOrPumpName& pump,
                                                                        F32 timeout, const LLSD& timeoutResult);

} // namespace llcoro::native

namespace llcoro::bridge
{

/// The fibre bridge: already "ready", and await_resume() calls the blocking function on the hosting fibre.
template<typename F>
class BridgeAwaiter
{
public:
    explicit BridgeAwaiter(F fn): mFn(std::move(fn)) {}
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    decltype(auto) await_resume() { return mFn(); }

private:
    F mFn;
};

[[nodiscard]] inline auto suspend()
{
    return BridgeAwaiter([] { llcoro::suspend(); });
}

[[nodiscard]] inline auto suspendUntilTimeout(float seconds)
{
    return BridgeAwaiter([seconds] { llcoro::suspendUntilTimeout(seconds); });
}

[[nodiscard]] inline auto suspendUntilNextFrame()
{
    return BridgeAwaiter([] { llcoro::suspendUntilNextFrame(); });
}

[[nodiscard]] inline auto postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                         const LLEventPumpOrPumpName& replyPump,
                                         const LLSD& replyPumpNamePath = LLSD())
{
    return BridgeAwaiter([=] { return llcoro::postAndSuspend(event, requestPump, replyPump, replyPumpNamePath); });
}

[[nodiscard]] inline auto suspendUntilEventOn(const LLEventPumpOrPumpName& pump)
{
    return BridgeAwaiter([=] { return llcoro::suspendUntilEventOn(pump); });
}

[[nodiscard]] inline auto postAndSuspendWithTimeout(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                                    const LLEventPumpOrPumpName& replyPump,
                                                    const LLSD& replyPumpNamePath, F32 timeout,
                                                    const LLSD& timeoutResult)
{
    return BridgeAwaiter([=]
    {
        return llcoro::postAndSuspendWithTimeout(event, requestPump, replyPump, replyPumpNamePath, timeout,
                                                 timeoutResult);
    });
}

[[nodiscard]] inline auto suspendUntilEventOnWithTimeout(const LLEventPumpOrPumpName& pump, F32 timeout,
                                                         const LLSD& timeoutResult)
{
    return BridgeAwaiter([=] { return llcoro::suspendUntilEventOnWithTimeout(pump, timeout, timeoutResult); });
}

} // namespace llcoro::bridge

namespace llcoro
{
#if LL_CORO_NATIVE
namespace co = native;
#else
namespace co = bridge;
#endif
} // namespace llcoro

/**
 * Certain event APIs require the name of an LLEventPump on which they should
 * post results. While it works to invent a distinct name and let
 * LLEventPumps::obtain() instantiate the LLEventPump as a "named singleton,"
 * in a certain sense it's more robust to instantiate a local LLEventPump and
 * provide its name instead. This class packages the following idiom:
 *
 * 1. Instantiate a local LLCoroEventPump, with an optional name prefix.
 * 2. Provide its actual name to the event API in question as the name of the
 *    reply LLEventPump.
 * 3. Initiate the request to the event API.
 * 4. Call your LLEventTempStream's suspend() method to suspend for the reply.
 * 5. Let the LLCoroEventPump go out of scope.
 */
class LL_COMMON_API LLCoroEventPump
{
public:
    LLCoroEventPump(const std::string& name="coro"):
        mPump(name, true)           // allow tweaking the pump instance name
    {}
    /// It's typical to request the LLEventPump name to direct an event API to
    /// send its response to this pump.
    std::string getName() const { return mPump.getName(); }
    /// Less typically, we'd request the pump itself for some reason.
    LLEventPump& getPump() { return mPump; }

    /**
     * Wait for an event on this LLEventPump.
     */
    LLSD suspend()
    {
        return llcoro::suspendUntilEventOn(mPump);
    }

    LLSD postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                     const LLSD& replyPumpNamePath=LLSD())
    {
        return llcoro::postAndSuspend(event, requestPump, mPump, replyPumpNamePath);
    }

private:
    LLEventStream mPump;
};

#endif /* ! defined(LL_LLEVENTCORO_H) */
