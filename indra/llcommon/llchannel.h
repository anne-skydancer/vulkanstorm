// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   llchannel.h
 * @brief  LL::Channel<T>: a bounded queue that tasks wait on, replacing boost::fibers::buffered_channel.
 *
 * The semantics the coprocedure pools rely on, kept:
 * - try_push() never blocks: it hands the value to the first waiting consumer, or queues it, or fails because the
 *   channel is full or closed.
 * - consumers wait first come, first served; a push wakes the first one.
 * - pop_wait_for() does not suspend when a value is queued (or the channel is closed and empty), and gives up after
 *   its timeout.
 * - close() wakes every waiting consumer; values still queued can be popped afterwards, then pops report closed.
 *
 * The channel may be pushed to and closed from any thread (a waiting task is woken through Executor::wake()); a
 * consumer is a task on the executor's thread. The channel's lock is not held while a value is destroyed by the
 * consumer, only while one is moved.
 */

#if ! defined(LL_LLCHANNEL_H)
#define LL_LLCHANNEL_H

#include "llerror.h"
#include "llexecutor.h"

#include <chrono>
#include <coroutine>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace LL
{

/// The result of Channel::pop_wait_for(), named as Boost's channel_op_status was
enum class ChannelStatus
{
    success,
    full,
    closed,
    timeout
};

template<typename T>
class Channel
{
public:
    explicit Channel(std::size_t capacity): mCapacity(capacity) {}
    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;
    // Consumers must be gone before the channel: close() it and let them finish, or shut the executor down first.
    ~Channel() = default;

    /// Hand @a value to a waiting consumer or queue it. success, full or closed. Any thread.
    ChannelStatus try_push(T value)
    {
        std::unique_lock lock(mMutex);
        if (mClosed)
        {
            return ChannelStatus::closed;
        }
        while (! mWaiters.empty())
        {
            Waiter* waiter = mWaiters.front();
            mWaiters.pop_front();
            waiter->mLinked = false;
            // A waiter whose timer won the claim is left to unlink itself and tell its task; try the next one.
            if (waiter->mClaim.claim())
            {
                waiter->mValue.emplace(std::move(value));
                waiter->mStatus = ChannelStatus::success;
                lock.unlock();
                waiter->wake();
                return ChannelStatus::success;
            }
        }
        if (mItems.size() >= mCapacity)
        {
            return ChannelStatus::full;
        }
        mItems.push_back(std::move(value));
        return ChannelStatus::success;
    }

    /// No more pushes; every waiting consumer wakes with closed. Queued values stay poppable. Any thread.
    void close()
    {
        std::deque<Waiter*> waiters;
        {
            std::lock_guard lock(mMutex);
            mClosed = true;
            waiters.swap(mWaiters);
            for (Waiter* waiter : waiters)
            {
                waiter->mLinked = false;
            }
        }
        // wake outside the lock, in the order they waited
        for (Waiter* waiter : waiters)
        {
            if (waiter->mClaim.claim())
            {
                waiter->mStatus = ChannelStatus::closed;
                waiter->wake();
            }
        }
    }

    /// What co_await channel.pop_wait_for(...) gives: the status, and the value when it is success.
    struct Popped
    {
        ChannelStatus status;
        std::optional<T> value;
    };

    class Waiter
    {
    public:
        Waiter(Channel& channel, std::chrono::milliseconds timeout): mChannel(channel), mTimeout(timeout) {}
        Waiter(const Waiter&) = delete;
        Waiter& operator=(const Waiter&) = delete;
        ~Waiter()
        {
            // destroyed while waiting (shutdown, or a discarded Task): leave the channel and the executor
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
            unlink();
        }

        bool await_ready() const noexcept { return false; }

        // Decides under the channel's lock, so that a push cannot slip between "empty" and "waiting".
        bool await_suspend(std::coroutine_handle<> handle)
        {
            mExecutor = Executor::current();
            mRecord = Executor::currentRecord();
            llassert_always_msg(mExecutor && mRecord, "co_await outside of a task: Channel::pop_wait_for()");
            {
                std::lock_guard lock(mChannel.mMutex);
                if (! mChannel.mItems.empty())
                {
                    mValue.emplace(std::move(mChannel.mItems.front()));
                    mChannel.mItems.pop_front();
                    mStatus = ChannelStatus::success;
                    return false;
                }
                if (mChannel.mClosed)
                {
                    mStatus = ChannelStatus::closed;
                    return false;
                }
                mHandle = handle;
                mChannel.mWaiters.push_back(this);
                mLinked = true;
            }
            // From here a push may claim us and wake the handle at once: it only queues, and we run on the
            // executor's thread, so the task cannot resume before we return.
            mTimer = mExecutor->addTimer(mTimeout, handle, mRecord, &mClaim);
            return true;
        }

        Popped await_resume()
        {
            mResumed = true;
            if (mTimer)
            {
                mExecutor->cancelTimer(*mTimer);
                mTimer.reset();
            }
            if (mClaim.mTimedOut)
            {
                unlink();
                return Popped{ ChannelStatus::timeout, std::nullopt };
            }
            return Popped{ mStatus, std::move(mValue) };
        }

    private:
        friend Channel;

        // From any thread. The timer, if it has not fired yet, can no longer claim the wait, so the task is made ready
        // exactly once; await_resume() removes the timer entry.
        void wake() { mExecutor->wake(mHandle, mRecord); }

        void unlink()
        {
            std::lock_guard lock(mChannel.mMutex);
            if (mLinked)
            {
                std::erase(mChannel.mWaiters, this);
                mLinked = false;
            }
        }

        Channel& mChannel;
        std::chrono::milliseconds mTimeout;
        Executor* mExecutor{nullptr};
        CoroData* mRecord{nullptr};
        std::coroutine_handle<> mHandle;
        WaitClaim mClaim;
        std::optional<Executor::TimerKey> mTimer;
        std::optional<T> mValue;
        ChannelStatus mStatus{ChannelStatus::timeout};
        bool mLinked{false};
        bool mResumed{false};
    };

    /// Wait up to @a timeout for a value: <tt>auto [status, value] = co_await channel.pop_wait_for(10s);</tt>
    [[nodiscard]] Waiter pop_wait_for(std::chrono::milliseconds timeout) { return Waiter(*this, timeout); }

private:
    std::mutex mMutex;
    std::deque<T> mItems;
    std::deque<Waiter*> mWaiters;
    const std::size_t mCapacity;
    bool mClosed{false};
};

} // namespace LL

#endif /* ! defined(LL_LLCHANNEL_H) */
