/**
 * @file _thread.h
 * @brief thread type abstraction
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012, Linden Research, Inc.
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

#ifndef LLCOREINT_THREAD_H_
#define LLCOREINT_THREAD_H_

#include "linden_common.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <thread>

#include "_refcounted.h"

namespace LLCoreInt
{

class HttpThread
{
public:
    HttpThread() = delete;                              // Not defined
    HttpThread(const HttpThread&) = delete;             // Not defined
    void operator=(const HttpThread &) = delete;        // Not defined

private:
    void run()
    { // THREAD CONTEXT
        // run the thread function
        mThreadFunc(this);
        // the thread function has returned: timedJoin() can now succeed
        mDone.set_value();
    } // THREAD CONTEXT

public:
    /// Constructs a thread object for concurrent execution but does
    /// not start running.  Caller receives on refcount on the thread
    /// instance.  If the thread is started, another will be taken
    /// out for the exit handler.
    explicit HttpThread(std::function<void (HttpThread *)> threadFunc)
          : mThreadFunc(threadFunc)
    {
        // this creates a std thread that will call HttpThread::run on this instance
        // and pass it the threadfunc callable...
        std::function<void()> f = std::bind(&HttpThread::run, this);

        mDoneFuture = mDone.get_future();
        mThread = std::make_unique<std::thread>(f);
        // Counted only once the thread exists: a constructor that throws
        // never runs the destructor, so counting earlier would leak a count.
        ++sLiveCount;
    }

    ~HttpThread()
    {
        --sLiveCount;
    }

    inline void join()
    {
        mThread->join();
    }

    /// Waits up to @a millis for the thread function to return and, if it
    /// did, joins the thread.  Returns true when the thread has been joined.
    inline bool timedJoin(S32 millis)
    {
        if (mDoneFuture.wait_for(std::chrono::milliseconds(millis)) != std::future_status::ready)
        {
            return false;
        }
        mThread->join();
        return true;
    }

    inline bool joinable() const
    {
        return mThread->joinable();
    }

    inline void detach()
    {
        mThread->detach();
    }

    /// Number of HttpThread objects currently alive (tests use this to
    /// check that nothing leaks).
    static int liveCount()
    {
        return sLiveCount.load();
    }

private:
    static inline std::atomic<int> sLiveCount{0};

    std::function<void(HttpThread *)> mThreadFunc;
    std::promise<void> mDone;
    std::future<void> mDoneFuture;
    std::unique_ptr<std::thread> mThread;
}; // end class HttpThread

} // end namespace LLCoreInt

#endif // LLCOREINT_THREAD_H_


