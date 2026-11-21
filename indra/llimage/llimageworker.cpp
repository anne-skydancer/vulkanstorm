/**
 * @file llimageworker.cpp
 * @brief Base class for images.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "linden_common.h"

#include "llimageworker.h"
#include "llimagedxt.h"
#include "threadpool.h"
#include "llimagefpstate.h"

#include <algorithm>
#include <memory>

/*--------------------------------------------------------------------------*/
class ImageRequest
{
public:
    ImageRequest(const LLPointer<LLImageFormatted>& image,
                 S32 discard,
                 bool needs_aux,
                 const LLPointer<LLImageDecodeThread::Responder>& responder,
                 U32 request_id);
    virtual ~ImageRequest();

    /*virtual*/ bool processRequest();
    /*virtual*/ void finishRequest(bool completed);

private:
    // LLPointers stored in ImageRequest MUST be LLPointer instances rather
    // than references: we need to increment the refcount when storing these.
    // input
    LLPointer<LLImageFormatted> mFormattedImage;
    S32 mDiscardLevel;
    U32 mRequestId;
    bool mNeedsAux;
    // output
    LLPointer<LLImageRaw> mDecodedImageRaw;
    LLPointer<LLImageRaw> mDecodedImageAux;
    bool mDecodedRaw;
    bool mDecodedAux;
    LLPointer<LLImageDecodeThread::Responder> mResponder;
    std::string mErrorString;};


//----------------------------------------------------------------------------

namespace
{
    // The pool whose decode request this thread is running (LLImageDecodeThread::getCurrent).
    thread_local LLImageDecodeThread* sCurrentPool = nullptr;

    // One parallelFor call's fork-join, shared by the caller and by the helper tasks it posts. A helper task
    // that starts after the caller has returned finds no job left and touches only this block, which the task
    // keeps alive.
    struct ForkJoin
    {
        ForkJoin(U32 count, LLImageDecodeThread::job_fn job, void* ctx) : mCount(count), mJob(job), mCtx(ctx) {}

        // Claims and runs jobs until none is left. Each run takes the floating-point state C code expects: the
        // main thread has flush-to-zero on (LLVector4a::initClass) and a pool thread is not trusted to have
        // anything in particular.
        void run()
        {
            LLImageFPScope fp;
            for (U32 i = mNext.fetch_add(1, std::memory_order_relaxed); i < mCount;
                 i = mNext.fetch_add(1, std::memory_order_relaxed))
            {
                mJob(mCtx, i);
                // release: whoever sees this count sees the job's writes
                if (mDone.fetch_add(1, std::memory_order_acq_rel) + 1 == mCount)
                {
                    mDone.notify_all();
                }
            }
        }

        // Returns when every job has returned, those that other threads claimed included.
        void wait()
        {
            for (U32 done = mDone.load(std::memory_order_acquire); done < mCount;
                 done = mDone.load(std::memory_order_acquire))
            {
                mDone.wait(done, std::memory_order_acquire);
            }
        }

        const U32 mCount;
        LLImageDecodeThread::job_fn const mJob;
        void* const mCtx;
        std::atomic<U32> mNext{0};
        std::atomic<U32> mDone{0};
    };
}

// MAIN THREAD
LLImageDecodeThread::LLImageDecodeThread(bool /*threaded*/)
    : mDecodeCount(0)
{
    mThreadPool = std::make_unique<LL::ThreadPool>("ImageDecode", 8);
    mThreadPool->start();
    mWorkers = (U32)mThreadPool->getWidth(); // the threads it started; fixed from here on
}

//virtual
LLImageDecodeThread::~LLImageDecodeThread()
{}

// MAIN THREAD
// virtual
size_t LLImageDecodeThread::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    return getPending();
}

size_t LLImageDecodeThread::getPending()
{
    return mThreadPool->getQueue().size();
}

LLImageDecodeThread::handle_t LLImageDecodeThread::decodeImage(
    const LLPointer<LLImageFormatted>& image,
    S32 discard,
    bool needs_aux,
    const LLPointer<LLImageDecodeThread::Responder>& responder)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    U32 decode_id = ++mDecodeCount;
    if (decode_id == 0)
        decode_id = ++mDecodeCount;

    // Instantiate the ImageRequest right in the lambda, why not?
    mInFlight.fetch_add(1, std::memory_order_relaxed);
    bool posted = mThreadPool->getQueue().post(
        [this, req = ImageRequest(image, discard, needs_aux, responder, decode_id)]
        () mutable
        {
            // in flight from the post to the end of the request, whatever the request does
            struct Finished
            {
                std::atomic<U32>& count;
                ~Finished()
                {
                    count.fetch_sub(1, std::memory_order_relaxed);
                    sCurrentPool = nullptr;
                }
            } finished{mInFlight};
            sCurrentPool = this;
            auto done = req.processRequest();
            req.finishRequest(done);
        });
    if (! posted)
    {
        mInFlight.fetch_sub(1, std::memory_order_relaxed);
        LL_DEBUGS() << "Tried to start decoding on shutdown" << LL_ENDL;
        return 0;
    }

    return decode_id;
}

void LLImageDecodeThread::shutdown()
{
    mThreadPool->close();
}

// static
LLImageDecodeThread* LLImageDecodeThread::getCurrent()
{
    return sCurrentPool;
}

bool LLImageDecodeThread::isIdle() const
{
    return mInFlight.load(std::memory_order_relaxed) < mWorkers;
}

U32 LLImageDecodeThread::getWorkerCount() const
{
    return mWorkers;
}

void LLImageDecodeThread::parallelFor(U32 count, job_fn job, void* job_ctx)
{
    if (count == 0)
    {
        return;
    }
    auto state = std::make_shared<ForkJoin>(count, job, job_ctx);
    // One helper task for each job the calling thread cannot take, and at most one for each free worker: the
    // workers minus the requests in flight (the caller's own, when it is a pool thread, among them). A busy pool
    // gets none, so its queue is not filled with tasks that find nothing to do. A task that the closed queue
    // refuses, or that no worker gets to, costs nothing: the caller claims what is left.
    const U32 in_flight = mInFlight.load(std::memory_order_relaxed);
    const U32 free_workers = mWorkers > in_flight ? mWorkers - in_flight : 0;
    const U32 helpers = std::min<U32>(count - 1, free_workers);
    for (U32 i = 0; i < helpers; ++i)
    {
        if (!mThreadPool->getQueue().post([state]() { state->run(); }))
        {
            break;
        }
    }
    state->run();
    state->wait();
}

LLImageDecodeThread::Responder::~Responder()
{
}

//----------------------------------------------------------------------------

ImageRequest::ImageRequest(const LLPointer<LLImageFormatted>& image,
                           S32 discard,
                           bool needs_aux,
                           const LLPointer<LLImageDecodeThread::Responder>& responder,
                           U32 request_id)
    : mFormattedImage(image),
      mDiscardLevel(discard),
      mNeedsAux(needs_aux),
      mDecodedRaw(false),
      mDecodedAux(false),
      mResponder(responder),
      mRequestId(request_id)
{
}

ImageRequest::~ImageRequest()
{
    mDecodedImageRaw = NULL;
    mDecodedImageAux = NULL;
    mFormattedImage = NULL;
}

//----------------------------------------------------------------------------


// Returns true when done, whether or not decode was successful.
bool ImageRequest::processRequest()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;

    if (mFormattedImage.isNull())
        return true;

    const F32 decode_time_slice = 0.f; //disable time slicing
    bool done = true;

    LLImageDataLock lockFormatted(mFormattedImage);
    LLImageDataLock lockDecodedRaw(mDecodedImageRaw);
    LLImageDataLock lockDecodedAux(mDecodedImageAux);

    if (!mDecodedRaw)
    {
        // Decode primary channels
        if (mDecodedImageRaw.isNull())
        {
            // parse formatted header
            if (!mFormattedImage->updateData())
            {
                return true; // done (failed)
            }
            if ((mFormattedImage->getWidth() * mFormattedImage->getHeight() * mFormattedImage->getComponents()) == 0)
            {
                return true; // done (failed)
            }
            if (mDiscardLevel >= 0)
            {
                mFormattedImage->setDiscardLevel(mDiscardLevel);
            }
            mDecodedImageRaw = new LLImageRaw(mFormattedImage->getWidth(),
                                              mFormattedImage->getHeight(),
                                              mFormattedImage->getComponents());
        }

        // <FS:ND> Probably out of memory crash
        // done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice);
        if( mDecodedImageRaw->getData() )
            done = mFormattedImage->decode(mDecodedImageRaw, decode_time_slice);
        else
        {
            LL_WARNS() << "No memory for LLImageRaw of size " << (U32)mFormattedImage->getWidth() << "x" << (U32)mFormattedImage->getHeight() << "x"
                       << (U32)mFormattedImage->getComponents() << LL_ENDL;
            done = false;
        }
        // </FS:ND>

        // some decoders are removing data when task is complete and there were errors
        mDecodedRaw = done && mDecodedImageRaw->getData();

        // Pick up errors from decoding
        mErrorString = LLImage::getLastThreadError();
    }
    if (done && mNeedsAux && !mDecodedAux && mFormattedImage.notNull())
    {
        // Decode aux channel
        if (!mDecodedImageAux)
        {
            mDecodedImageAux = new LLImageRaw(mFormattedImage->getWidth(),
                                              mFormattedImage->getHeight(),
                                              1);
        }
        done = mFormattedImage->decodeChannels(mDecodedImageAux, decode_time_slice, 4, 4);
        mDecodedAux = done && mDecodedImageAux->getData();

        // Pick up errors from decoding
        mErrorString = LLImage::getLastThreadError();
    }

    return done;
}

void ImageRequest::finishRequest(bool completed)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    if (mResponder.notNull())
    {
        bool success = completed && mDecodedRaw && (!mNeedsAux || mDecodedAux);
        mResponder->completed(success, mErrorString, mDecodedImageRaw, mDecodedImageAux, mRequestId);
    }
    // Will automatically be deleted
}
