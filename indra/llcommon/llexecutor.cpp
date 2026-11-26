// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file   llexecutor.cpp
 * @brief  The main-thread executor that runs LL::Task coroutines.
 */

// Precompiled header
#include "linden_common.h"
// associated header
#include "llexecutor.h"
// other Linden headers
#include "llcoros.h"
#include "llerror.h"
#include "llexception.h"
#include "lltimer.h"
#include "stringize.h"

namespace
{
// The executor and the record of the task that is running on this thread, set while the executor resumes a task.
thread_local LL::Executor* sExecutor = nullptr;
thread_local LL::CoroData* sRecord = nullptr;
} // anonymous namespace

namespace LL
{

/*****************************************************************************
*   CoroData
*****************************************************************************/
CoroData::CoroData(const std::string& name):
    LLInstanceTracker<CoroData, std::string>(name),
    mName(name),
    mCreationTime(LLTimer::getTotalSeconds())
{
}

CoroData::CoroData(int n):
    // This constructor is used for the thread_local instance belonging to the default coroutine on each thread. We
    // must give each one a different LLInstanceTracker key because LLInstanceTracker's map spans all threads, but we
    // want the default coroutine on each thread to have the empty string as its visible name because some consumers
    // test for that.
    LLInstanceTracker<CoroData, std::string>("main" + stringize(n)),
    mCreationTime(LLTimer::getTotalSeconds())
{
}

CoroData::~CoroData()
{
    for (auto& [key, slot] : mLocals)
    {
        slot.deleter(slot.value);
    }
}

void* CoroData::localGet(const void* key) const
{
    auto found = mLocals.find(key);
    return found == mLocals.end() ? nullptr : found->second.value;
}

void CoroData::localErase(const void* key) noexcept
{
    auto found = mLocals.find(key);
    if (found != mLocals.end())
    {
        const Slot old = found->second;
        mLocals.erase(found);
        old.deleter(old.value);
    }
}

void CoroData::localSet(const void* key, void* value, void (*deleter)(void*))
{
    auto found = mLocals.find(key);
    if (found != mLocals.end())
    {
        if (found->second.value == value)
        {
            return;
        }
        // move the old slot out first: its deleter may run code that touches this record
        Slot old = found->second;
        if (value)
        {
            found->second = Slot{ .value = value, .deleter = deleter };
        }
        else
        {
            mLocals.erase(found);
        }
        old.deleter(old.value);
    }
    else if (value)
    {
        mLocals.emplace(key, Slot{ .value = value, .deleter = deleter });
    }
}

/*****************************************************************************
*   Executor
*****************************************************************************/
struct Executor::Scope
{
    Scope(Executor* executor, CoroData* record): mPrevExecutor(sExecutor), mPrevRecord(sRecord)
    {
        sExecutor = executor;
        sRecord = record;
    }
    ~Scope()
    {
        sExecutor = mPrevExecutor;
        sRecord = mPrevRecord;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

    Executor* mPrevExecutor;
    CoroData* mPrevRecord;
};

Executor::Executor()
{
    // TIMERS stays in the queue for the life of the executor: it moves back behind what it adds every time it runs.
    mReady.push_back(Entry{ .kind = Entry::Kind::Timers, .handle = {}, .record = nullptr });
}

// NOLINTNEXTLINE(bugprone-exception-escape): shutdown() only logs and destroys frames; nothing it calls throws
Executor::~Executor()
{
    if (! mLive.empty())
    {
        shutdown(0);
    }
}

// static
Executor* Executor::current()
{
    return sExecutor;
}

// static
CoroData* Executor::currentRecord()
{
    return sRecord;
}

void Executor::bindOwner()
{
    std::thread::id none;
    mOwner.compare_exchange_strong(none, std::this_thread::get_id());
}

bool Executor::onOwnerThread() const
{
    return mOwner.load() == std::this_thread::get_id();
}

void Executor::start(CoroData* record)
{
    bindOwner();
    llassert_always(onOwnerThread());
    mLive.push_back(record);
    resume(Entry{ .kind = Entry::Kind::Task, .handle = record->mRoot, .record = record });
}

void Executor::retire(CoroData* record)
{
    std::erase(mLive, record);
}

void Executor::resume(const Entry& entry)
{
    Scope scope(this, entry.record);
    entry.handle.resume();
}

void Executor::pump()
{
    bindOwner();
    llassert_always(onOwnerThread());
    llassert_always(! mPumping);
    mPumping = true;
    struct Reset
    {
        bool& flag;
        ~Reset() { flag = false; }
    } reset{ mPumping };

    // Everything that becomes ready from now on lies behind MAIN and runs at the next pump.
    mReady.push_back(Entry{ .kind = Entry::Kind::Main, .handle = {}, .record = nullptr });
    for (;;)
    {
        Entry entry = mReady.front();
        mReady.pop_front();
        switch (entry.kind)
        {
        case Entry::Kind::Main:
            return;

        case Entry::Kind::Timers:
        {
            // Boost's dispatcher fibre: wake-ups from other threads first, then the timers that have expired, in
            // deadline order; then itself, behind what it just added.
            std::vector<Entry> remote;
            {
                std::scoped_lock lock(mRemoteMutex);
                remote.swap(mRemote);
            }
            mReady.insert(mReady.end(), remote.begin(), remote.end());

            const Clock::time_point now = Clock::now();
            while (! mTimers.empty() && mTimers.begin()->first.first <= now)
            {
                auto node = mTimers.extract(mTimers.begin());
                const Timer& timer = node.mapped();
                // the task may have been woken by an event in the meantime: then the claim is gone
                if (timer.claim->claim())
                {
                    timer.claim->mTimedOut = true;
                    mReady.push_back(Entry{ .kind = Entry::Kind::Task, .handle = timer.handle, .record = timer.record });
                }
            }
            mReady.push_back(entry);
            break;
        }

        case Entry::Kind::Task:
            // an empty handle is an entry forget() cancelled
            if (entry.handle)
            {
                resume(entry);
            }
            break;
        }
    }
}

void Executor::wake(std::coroutine_handle<> handle, CoroData* record)
{
    const Entry entry{ .kind = Entry::Kind::Task, .handle = handle, .record = record };
    if (onOwnerThread())
    {
        mReady.push_back(entry);
    }
    else
    {
        std::scoped_lock lock(mRemoteMutex);
        mRemote.push_back(entry);
    }
}

Executor::TimerKey Executor::addTimer(Clock::duration delay, std::coroutine_handle<> handle, CoroData* record,
                                      WaitClaim* claim)
{
    llassert(onOwnerThread());
    TimerKey key{ Clock::now() + delay, ++mTimerSeq };
    mTimers.emplace(key, Timer{ .handle = handle, .record = record, .claim = claim });
    return key;
}

void Executor::cancelTimer(const TimerKey& key)
{
    llassert(onOwnerThread());
    mTimers.erase(key);
}

void Executor::forget(std::coroutine_handle<> handle)
{
    llassert(onOwnerThread());
    for (Entry& entry : mReady)
    {
        if (entry.kind == Entry::Kind::Task && entry.handle == handle)
        {
            entry.handle = {};
        }
    }
    std::scoped_lock lock(mRemoteMutex);
    for (Entry& entry : mRemote)
    {
        if (entry.handle == handle)
        {
            entry.handle = {};
        }
    }
}

std::size_t Executor::shutdown(int maxPumps)
{
    for (int count = 0; count < maxPumps && ! mLive.empty(); ++count)
    {
        pump();
    }
    if (mLive.empty())
    {
        return 0;
    }

    std::vector<CoroData*> survivors;
    survivors.swap(mLive);
    for (const CoroData* record : survivors)
    {
        LL_WARNS("LLCoros") << "coroutine " << record->mName << " (" << record->mStatus
                            << ") is still waiting at shutdown: destroying it" << LL_ENDL;
    }
    for (CoroData* record : survivors)
    {
        // Destroying the root frame destroys the task chain below it, innermost first; each leaf awaiter unlinks
        // itself. The record goes with the root frame.
        const std::coroutine_handle<> root = record->mRoot;
        Scope scope(this, record);
        root.destroy();
    }
    return survivors.size();
}

void Executor::handleUncaught(CoroData& record, const std::exception_ptr& error)
{
    const std::string& name(record.mName);
    try
    {
        std::rethrow_exception(error);
    }
    catch (const LLCoros::Stop& exc)
    {
        LL_INFOS("LLCoros") << "coroutine " << name << " terminating because " << exc.what() << LL_ENDL;
    }
    catch (const LLContinueError&)
    {
        // Any uncaught exception derived from LLContinueError is logged here. This coroutine terminates but the rest
        // of the viewer carries on.
        LOG_UNHANDLED_EXCEPTION(STRINGIZE("coroutine " << name));
    }
    catch (...)
    {
        // Stash any OTHER kind of uncaught exception to be rethrown by the main loop after the pump.
        LL_WARNS("LLCoros") << "Capturing uncaught exception in coroutine " << name << LL_ENDL;
        mUncaught.push(Uncaught{ .name = name, .error = error });
    }
}

void Executor::rethrow()
{
    if (! mUncaught.empty())
    {
        Uncaught front = std::move(mUncaught.front());
        mUncaught.pop();
        LL_WARNS("LLCoros") << "Rethrowing exception from coroutine " << front.name << LL_ENDL;
        std::rethrow_exception(front.error);
    }
}

} // namespace LL
