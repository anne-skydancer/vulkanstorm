/**
 * @file llimageworker.h
 * @brief Object for managing images and their textures.
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
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

#ifndef LL_LLIMAGEWORKER_H
#define LL_LLIMAGEWORKER_H

#include "llimage.h"
#include "llpointer.h"
#include "threadpool_fwd.h"

#include <atomic>

class LLImageDecodeThread
{
public:
    class Responder : public LLThreadSafeRefCount
    {
    protected:
        virtual ~Responder();
    public:
        virtual void completed(bool success, const std::string& error_message, LLImageRaw* raw, LLImageRaw* aux, U32 request_id) = 0;
    };

public:
    LLImageDecodeThread(bool threaded = true);
    virtual ~LLImageDecodeThread();

    // meant to resemble LLQueuedThread::handle_t
    typedef U32 handle_t;
    handle_t decodeImage(const LLPointer<LLImageFormatted>& image,
                         S32 discard, bool needs_aux,
                         const LLPointer<Responder>& responder);
    size_t getPending();
    size_t update(F32 max_time_ms);
    S32 getTotalDecodeCount() { return mDecodeCount; }
    void shutdown();

    // <Vulkanstorm> Cores for one image's own threads (the J2C codec lends them to neo-j2k, USE_NEOJ2K). Busy or
    // idle is decided for each decode: the pool is idle when fewer decode requests are in flight (waiting in the
    // queue or running) than it has workers, so at least one worker is free for a helper. Then a decode may split
    // its stages over the pool; otherwise it runs on its own thread.
    typedef void (*job_fn)(void* job_ctx, U32 index);
    // The pool whose decode request the calling thread is running, or null on every other thread (the main
    // thread, the mesh upload thread, a bake). Only a pool thread inside a request may borrow the pool, so the
    // pool outlives every borrower: shutdown() joins the workers.
    static LLImageDecodeThread* getCurrent();
    // True when fewer decode requests are in flight (waiting or running, the caller's own included) than the pool has
    // workers. One atomic load, no lock.
    bool isIdle() const;
    // How many jobs the pool runs at once, the calling thread included.
    U32 getWorkerCount() const;
    // Runs job(job_ctx, i) once for every i < count, on the calling thread and on idle workers, and returns
    // when every job has returned. The calling thread claims jobs itself, so it finishes the loop alone when
    // every worker is busy or the pool is closed: it waits only for jobs a worker has already started. Jobs
    // never wait for one another. They run with C code's floating-point state (llimagefpstate.h). At most as many
    // helper tasks are posted as there are free workers (workers minus requests in flight), so a busy pool gets none.
    void parallelFor(U32 count, job_fn job, void* job_ctx);

private:
    // As of SL-17483, LLImageDecodeThread is no longer itself an
    // LLQueuedThread - instead this is the API by which we submit work to the
    // "ImageDecode" ThreadPool.
    std::unique_ptr<LL::ThreadPool> mThreadPool;
    LLAtomicU32 mDecodeCount;
    std::atomic<U32> mInFlight{0}; // <Vulkanstorm> decode requests posted and not yet finished (waiting or running)
    U32 mWorkers = 0;              // <Vulkanstorm> the pool's thread count, fixed once started
};

#endif
