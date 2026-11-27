/**
 * @file   lleventcoro.cpp
 * @author Nat Goodspeed
 * @date   2009-04-29
 * @brief  Implementation for lleventcoro.
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

// Precompiled header
#include "linden_common.h"
// associated header
#include "lleventcoro.h"
// STL headers
#include <chrono>
#include <exception>
// std headers
// external library headers
// other Linden headers
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llerror.h"
#include "llcoros.h"
#include "stringize.h"

namespace
{

/**
 * suspendUntilEventOn() permits a coroutine to temporarily listen on an
 * LLEventPump any number of times. We don't really want to have to ask
 * the caller to label each such call with a distinct string; the whole
 * point of suspendUntilEventOn() is to present a nice sequential interface to
 * the underlying LLEventPump-with-named-listeners machinery. So we'll use
 * LLEventPump::inventName() to generate a distinct name for each
 * temporary listener. On the other hand, because a given coroutine might
 * call suspendUntilEventOn() any number of times, we don't really want to
 * consume an arbitrary number of generated inventName()s: that namespace,
 * though large, is nonetheless finite. So we memoize an invented name for
 * each distinct coroutine instance.
 */
std::string listenerNameForCoro()
{
    // If this coroutine was launched by LLCoros::launch(), find that name.
    std::string name(LLCoros::getName());
    if (! name.empty())
    {
        return name;
    }
    // this is the first time we've been called for this coroutine instance
    name = LLEventPump::inventName("coro");
    LL_INFOS("LLEventCoro") << "listenerNameForCoro(): inventing coro name '"
                            << name << "'" << LL_ENDL;
    return name;
}

/**
 * Implement behavior described for postAndSuspend()'s @a replyPumpNamePath
 * parameter:
 *
 * * If <tt>path.isUndefined()</tt>, do nothing.
 * * If <tt>path.isString()</tt>, @a dest is an LLSD map: store @a value
 *   into <tt>dest[path.asString()]</tt>.
 * * If <tt>path.isInteger()</tt>, @a dest is an LLSD array: store @a
 *   value into <tt>dest[path.asInteger()]</tt>.
 * * If <tt>path.isArray()</tt>, iteratively apply the rules above to step
 *   down through the structure of @a dest. The last array entry in @a
 *   path specifies the entry in the lowest-level structure in @a dest
 *   into which to store @a value.
 *
 * @note
 * In the degenerate case in which @a path is an empty array, @a dest will
 * @em become @a value rather than @em containing it.
 */
void storeToLLSDPath(LLSD& dest, const LLSD& path, const LLSD& value)
{
    if (path.isUndefined())
    {
        // no-op case
        return;
    }

    // Drill down to where we should store 'value'.
    llsd::drill_ref(dest, path) = value;
}

} // anonymous

/*****************************************************************************
*   awaiters
*****************************************************************************/
namespace llcoro
{

EventAwaiter::EventAwaiter(std::string callerName, LLSD event, LLEventPumpOrPumpName requestPump,
                           LLEventPumpOrPumpName replyPump, LLSD replyPumpNamePath,
                           std::optional<std::chrono::milliseconds> timeout, LLSD timeoutResult):
    mCallerName(std::move(callerName)),
    mRequestPump(std::move(requestPump)),
    mReplyPump(std::move(replyPump)),
    mEvent(std::move(event)),
    mReplyPumpNamePath(std::move(replyPumpNamePath)),
    mTimeoutResult(std::move(timeoutResult)),
    mTimeout(timeout)
{
}

EventAwaiter::EventAwaiter(std::unique_ptr<LLEventStream> bogusPump, std::chrono::milliseconds timeout):
    mCallerName("suspendUntilTimeout()"),
    mOwnedPump(std::move(bogusPump)),
    mReplyPump(*mOwnedPump),
    mTimeout(timeout)
{
}

EventAwaiter::~EventAwaiter()
{
    // The frame is destroyed while waiting (shutdown, or a discarded Task): leave nothing that can resume it. The
    // listeners disconnect with the members.
    if (mExecutor)
    {
        if (mTimer)
        {
            mExecutor->cancelTimer(*mTimer);
        }
        if (mHandle && ! mResumed)
        {
            mExecutor->forget(mHandle);
        }
    }
}

bool EventAwaiter::await_ready()
{
    // Before we get any farther -- should we be stopping instead of suspending?
    LLCoros::checkStop();
    mExecutor = LL::Executor::current();
    mRecord = LL::Executor::currentRecord();
    llassert_always_msg(mExecutor && mRecord, ("co_await outside of a task: " + mCallerName).c_str());

    // The consuming attribute for THIS coroutine, the one that is about to suspend: the listener below runs in the
    // context of whoever posts the event.
    const bool consuming(LLCoros::get_consuming());
    mListenerName = listenerNameForCoro();
    llassert_always_msg(mReplyPump, ("replyPump required for " + mCallerName).c_str());
    LLEventPump& replyPump(mReplyPump.getPump());

    // The relative order of the two listen() calls below would only matter if "LLApp" were an LLEventMailDrop. But if
    // we ever go there, we'd want to notice the pending LLApp status first.
    mStopConnection = LLEventPumps::instance().obtain("LLApp").listen(
        mListenerName,
        [this](const LLSD& status)
        {
            llassert_always(mExecutor->onOwnerThread()); // listeners of the event awaiters run on the executor's thread
            // anything except "running" should wake up the waiting coroutine
            const auto& statsd = status["status"];
            if (statsd.asString() != "running")
            {
                LL_DEBUGS("lleventcoro") << mListenerName << " spotted status " << statsd << ", throwing Stopping"
                                         << LL_ENDL;
                if (mClaim.claim())
                {
                    mError = std::make_exception_ptr(LLCoros::Stopping("status " + statsd.asString()));
                    completed();
                }
                else
                {
                    LL_WARNS("lleventcoro") << mListenerName
                                            << " couldn't throw Stopping because the wait is already over" << LL_ENDL;
                }
            }
            // do not consume -- every listener must see status
            return false;
        });
    mReplyConnection = replyPump.listen(
        mListenerName,
        [this, consuming](const LLSD& result)
        {
            llassert_always(mExecutor->onOwnerThread()); // listeners of the event awaiters run on the executor's thread
            if (! mClaim.claim())
            {
                // the first event won; this one is neither delivered nor consumed
                LL_DEBUGS("lleventcoro") << "wait already over in '" << mListenerName << "'" << LL_ENDL;
                return false;
            }
            mValue = result;
            completed();
            // We did manage to propagate the result value to the waiter. If we're supposed to indicate that we've
            // consumed it, do so.
            return consuming;
        });

    // skip the "post" part if requestPump is default-constructed
    if (mRequestPump)
    {
        LLEventPump& requestPump(mRequestPump.getPump());
        // If replyPumpNamePath is non-empty, store the replyPump name in the request event.
        LLSD modevent(mEvent);
        storeToLLSDPath(modevent, mReplyPumpNamePath, replyPump.getName());
        LL_DEBUGS("lleventcoro") << mCallerName << ": coroutine " << mListenerName << " posting to "
                                 << requestPump.getName() << LL_ENDL;
        // *NOTE:Mani - Removed because modevent could contain user's hashed passwd.
        requestPump.post(modevent);
    }
    LL_DEBUGS("lleventcoro") << mCallerName << ": coroutine " << mListenerName << " about to wait on LLEventPump "
                             << replyPump.getName() << LL_ENDL;
    // A reply delivered while we posted the request completes the wait without suspending.
    return mClaim.mClaimed.load();
}

void EventAwaiter::await_suspend(std::coroutine_handle<> handle)
{
    mHandle = handle;
    if (mTimeout)
    {
        mStatus.emplace(STRINGIZE("waiting for " << mReplyPump.getPump().getName() << " for "
                                  << static_cast<F32>(mTimeout->count()) / 1000.f << "s"));
        mTimer = mExecutor->addTimer(*mTimeout, handle, mRecord, &mClaim);
    }
    else
    {
        mStatus.emplace(STRINGIZE("waiting for " << mReplyPump.getPump().getName()));
    }
}

LLSD EventAwaiter::await_resume()
{
    mResumed = true;
    mStatus.reset();
    if (mTimer)
    {
        mExecutor->cancelTimer(*mTimer);
        mTimer.reset();
    }
    // disconnect now rather than when the awaiter goes away
    mReplyConnection.disconnect();
    mStopConnection.disconnect();
    if (mError)
    {
        std::rethrow_exception(mError);
    }
    if (mClaim.mTimedOut)
    {
        LL_DEBUGS("lleventcoro") << mCallerName << ": coroutine " << mListenerName << " timed out after "
                                 << mTimeout.value_or(std::chrono::milliseconds(0)).count() << "ms, resuming with "
                                 << mTimeoutResult << LL_ENDL;
        return mTimeoutResult;
    }
    LL_DEBUGS("lleventcoro") << mCallerName << ": coroutine " << mListenerName << " resuming with " << mValue
                             << LL_ENDL;
    return mValue;
}

void EventAwaiter::completed()
{
    // The winner of the claim has stored the outcome: make the task ready. The timer, if any, stays until await_resume()
    // or the destructor cancels it; if it fires first it finds the claim taken and does nothing.
    // no handle yet: the wait ended while await_ready() posted the request, and the task never suspends
    if (mHandle)
    {
        mExecutor->wake(mHandle, mRecord);
    }
}

SuspendAwaiter::~SuspendAwaiter()
{
    if (mExecutor && mHandle && ! mResumed)
    {
        mExecutor->forget(mHandle);
    }
}

bool SuspendAwaiter::await_ready()
{
    LLCoros::checkStop();
    mExecutor = LL::Executor::current();
    llassert_always_msg(mExecutor, "co_await outside of a task: suspend()");
    return false;
}

void SuspendAwaiter::await_suspend(std::coroutine_handle<> handle)
{
    mHandle = handle;
    mStatus.emplace("waiting one tick");
    // the ready queue's back: the task runs again at the next pump
    mExecutor->wake(handle, LL::Executor::currentRecord());
}

void SuspendAwaiter::await_resume()
{
    mResumed = true;
    mStatus.reset();
}

EventAwaiter suspendUntilTimeout(float seconds)
{
    // Wait on a pump on which nobody posts, so that the "LLApp" listener ends the wait with
    // Stopping at shutdown. Timeout is the NORMAL case for this call. Don't make the pump static: that would force
    // instantiation of the LLEventPumps registry at static initialization time. DO allow tweaking the name for
    // uniqueness, this definitely gets re-entered on multiple coroutines.
    return { std::make_unique<LLEventStream>("xyzzy", true),
             std::chrono::milliseconds(static_cast<long>(seconds * 1000)) };
}

EventAwaiter suspendUntilNextFrame()
{
    // Once per frame we get mainloop.post(newFrame); the event data is ignored.
    return suspendUntilEventOn(LLEventPumpOrPumpName("mainloop"));
}

EventAwaiter postAndSuspend(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                            const LLEventPumpOrPumpName& replyPump, const LLSD& replyPumpNamePath)
{
    return { "postAndSuspend()", event, requestPump, replyPump, replyPumpNamePath, std::nullopt, LLSD() };
}

EventAwaiter suspendUntilEventOn(const LLEventPumpOrPumpName& pump)
{
    return postAndSuspend(LLSD(), LLEventPumpOrPumpName(), pump);
}

EventAwaiter postAndSuspendWithTimeout(const LLSD& event, const LLEventPumpOrPumpName& requestPump,
                                       const LLEventPumpOrPumpName& replyPump, const LLSD& replyPumpNamePath,
                                       F32 timeout, const LLSD& timeoutResult)
{
    // The fact that we accept non-integer seconds means we should probably use granularity finer than one second.
    // However, given the overhead of the rest of our processing, it seems silly to use granularity finer than a
    // millisecond.
    return { "postAndSuspendWithTimeout()", event, requestPump, replyPump, replyPumpNamePath,
             std::chrono::milliseconds(static_cast<long>(timeout * 1000)), timeoutResult };
}

EventAwaiter suspendUntilEventOnWithTimeout(const LLEventPumpOrPumpName& pump, F32 timeout,
                                            const LLSD& timeoutResult)
{
    return postAndSuspendWithTimeout(LLSD(), LLEventPumpOrPumpName(), pump, LLSD(), timeout, timeoutResult);
}

} // namespace llcoro
