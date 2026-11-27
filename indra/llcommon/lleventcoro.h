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

/**
 * The suspension primitives of a coroutine, for a coroutine that returns an LL::Task: <tt>co_await</tt> each one.
 * Each checks LLCoros::checkStop() first, sets the coroutine's status while it waits, lets the first event win (a
 * later event is not delivered and not consumed), and turns a status other than "running" on "LLApp" into
 * LLCoros::Stopping. An awaiter is not movable and unlinks every listener, timer and queue entry in its destructor,
 * so a destroyed frame is never resumed.
 *
 * A task runs on the main loop's executor, so nothing may block it: use these, never a blocking wait.
 */
namespace llcoro
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

/**
 * Yield control from a coroutine for one "mainloop" tick: <tt>co_await llcoro::suspend();</tt> The task runs again at
 * the next pump. If your coroutine runs without suspending for nontrivial time, sprinkle in calls to this function to
 * avoid stalling the rest of the viewer processing.
 */
[[nodiscard]] inline SuspendAwaiter suspend() { return {}; }

/// Yield control from a coroutine for at least @a seconds
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilTimeout(float seconds);

/// Yield control from a coroutine until the next mainloop's newFrame event.
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilNextFrame();

/**
 * Post specified LLSD event on the specified LLEventPump, then suspend for a
 * response on specified other LLEventPump. This is more than mere
 * convenience: the difference between this function and the sequence
 * @code
 * requestPump.post(myEvent);
 * LLSD reply = co_await suspendUntilEventOn(replyPump);
 * @endcode
 * is that the sequence above fails if the reply is posted immediately on
 * @a replyPump, that is, before <tt>requestPump.post()</tt> returns. In the
 * sequence above, the running coroutine isn't even listening on @a replyPump
 * until <tt>requestPump.post()</tt> returns and @c suspendUntilEventOn() is
 * entered. Therefore, the coroutine completely misses an immediate reply
 * event, making it suspend indefinitely.
 *
 * By contrast, postAndSuspend() listens on the @a replyPump @em before posting
 * the specified LLSD event on the specified @a requestPump (when the
 * returned awaiter is co_awaited), and a reply delivered during that post
 * completes the wait without suspending.
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
[[nodiscard]] LL_COMMON_API EventAwaiter postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                                        const LLEventPumpOrPumpName& replyPump,
                                                        const LLSD& replyPumpNamePath = LLSD());

/// Wait for the next event on the specified LLEventPump. Pass either the
/// LLEventPump& or its string name.
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilEventOn(const LLEventPumpOrPumpName& pump);

/// Like postAndSuspend(), but if we wait longer than @a timeout seconds,
/// stop waiting and return @a timeoutResult instead.
[[nodiscard]] LL_COMMON_API EventAwaiter postAndSuspendWithTimeout(const LLSD& event,
                                                                   const LLEventPumpOrPumpName& requestPump,
                                                                   const LLEventPumpOrPumpName& replyPump,
                                                                   const LLSD& replyPumpNamePath, F32 timeout,
                                                                   const LLSD& timeoutResult);

/// Suspend the coroutine until an event is fired on the identified pump
/// or the timeout duration has elapsed.  If the timeout duration
/// elapses the specified LLSD is returned.
[[nodiscard]] LL_COMMON_API EventAwaiter suspendUntilEventOnWithTimeout(const LLEventPumpOrPumpName& pump,
                                                                        F32 timeout, const LLSD& timeoutResult);

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
     * Wait for an event on this LLEventPump: <tt>LLSD reply = co_await waiter.suspend();</tt>
     */
    [[nodiscard]] llcoro::EventAwaiter suspend()
    {
        return llcoro::suspendUntilEventOn(mPump);
    }

    [[nodiscard]] llcoro::EventAwaiter postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                                      const LLSD& replyPumpNamePath=LLSD())
    {
        return llcoro::postAndSuspend(event, requestPump, mPump, replyPumpNamePath);
    }

private:
    LLEventStream mPump;
};

#endif /* ! defined(LL_LLEVENTCORO_H) */
