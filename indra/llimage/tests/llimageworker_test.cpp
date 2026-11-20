/**
 * @file llimageworker_test.cpp
 * @author Merov Linden
 * @date 2009-04-28
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
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

// Precompiled header: almost always required for newview cpp files
#include "linden_common.h"
// Class to test
#include "../llimageworker.h"
// For timer class
#include "../llcommon/lltimer.h"
// for lltrace class
#include "../llcommon/lltrace.h"
// Tut header
#include "../test/lltut.h"
// <Vulkanstorm> the pool's own parallelFor (USE_NEOJ2K's executor)
#include "../llcommon/workqueue.h"
#include "../llimagefpstate.h"
#include <atomic>
#include <vector>
#include <xmmintrin.h>

// -------------------------------------------------------------------------------------------
// Stubbing: Declarations required to link and run the class being tested
// Notes:
// * Add here stubbed implementation of the few classes and methods used in the class to be tested
// * Add as little as possible (let the link errors guide you)
// * Do not make any assumption as to how those classes or methods work (i.e. don't copy/paste code)
// * A simulator for a class can be implemented here. Please comment and document thoroughly.

LLImageBase::LLImageBase()
: mData(NULL),
mDataSize(0),
mWidth(0),
mHeight(0),
mComponents(0),
mBadBufferAllocation(false),
mAllowOverSize(false)
{
}
LLImageBase::~LLImageBase() {}
void LLImageBase::dump() { }
void LLImageBase::sanityCheck() { }
void LLImageBase::deleteData() { }
U8* LLImageBase::allocateData(S32 size) { return NULL; }
U8* LLImageBase::reallocateData(S32 size) { return NULL; }

LLImageRaw::LLImageRaw(U16 width, U16 height, S8 components) { }
LLImageRaw::~LLImageRaw() { }
void LLImageRaw::deleteData() { }
U8* LLImageRaw::allocateData(S32 size) { return NULL; }
U8* LLImageRaw::reallocateData(S32 size) { return NULL; }
const U8* LLImageBase::getData() const { return NULL; }
U8* LLImageBase::getData() { return NULL; }
const std::string& LLImage::getLastThreadError() { static std::string msg; return msg; }

// End Stubbing
// -------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------
// TUT
// -------------------------------------------------------------------------------------------

namespace tut
{
    // Test wrapper declarations

    // Note: We derive the responder class for 2 reasons:
    // 1. It's a pure virtual class and we can't compile without completed() being implemented
    // 2. We actually need a responder to test that the thread work test completed
    // We implement this making no assumption on what's done in the thread or worker
    // though, just that the responder's completed() method is called in the end.
    // Note on responders: responders are ref counted and *will* be deleted by the request they are
    // attached to when the queued request is deleted. The recommended way of using them is to
    // create them when creating a request, put a callback method in completed() and not rely on
    // anything to survive in the responder object once completed() has been called. Let the request
    // do the deletion and clean up itself.
    class responder_test : public LLImageDecodeThread::Responder
    {
        public:
            responder_test(bool* res)
            {
                done = res;
                *done = false;
            }
            virtual void completed(bool success, const std::string& error_message, LLImageRaw* raw, LLImageRaw* aux, U32 request_id)
            {
                *done = true;
            }
        private:
            // This is what can be thought of as the minimal implementation of a responder
            // Done will be switched to true when completed() is called and can be tested
            // outside the responder. A better way of doing this is to store a callback here.
            bool* done;
    };

    // Test wrapper declaration : decode thread
    struct imagedecodethread_test
    {
        // Instance to be tested
        LLImageDecodeThread* mThread;
        // Constructor and destructor of the test wrapper
        imagedecodethread_test()
        {
            mThread = NULL;
        }
        ~imagedecodethread_test()
        {
            delete mThread;
        }
    };

    // Tut templating thingamagic: test group, object and test instance
    typedef test_group<imagedecodethread_test> imagedecodethread_t;
    typedef imagedecodethread_t::object imagedecodethread_object_t;
    tut::imagedecodethread_t tut_imagedecodethread("LLImageDecodeThread");

    // ---------------------------------------------------------------------------------------
    // Test functions
    // Notes:
    // * Test as many as you possibly can without requiring a full blown simulation of everything
    // * The tests are executed in sequence so the test instance state may change between calls
    // * Remember that you cannot test private methods with tut
    // ---------------------------------------------------------------------------------------

    // ---------------------------------------------------------------------------------------
    // Test the LLImageDecodeThread interface
    // ---------------------------------------------------------------------------------------

    template<> template<>
    void imagedecodethread_object_t::test<1>()
    {
        // Test a *threaded* instance of the class
        mThread = new LLImageDecodeThread(true);
        ensure("LLImageDecodeThread: threaded constructor failed", mThread != NULL);
        // Insert something in the queue
        bool done = false;
        LLImageDecodeThread::handle_t decodeHandle = mThread->decodeImage(NULL, 0, false, new responder_test(&done));
        // Verifies we get back a valid handle
        ensure("LLImageDecodeThread:  threaded decodeImage(), returned handle is null", decodeHandle != 0);
        // Wait till the thread has time to handle the work order (though it doesn't do much per work order...)
        const U32 INCREMENT_TIME = 500;             // 500 milliseconds
        const U32 MAX_TIME = 20 * INCREMENT_TIME;   // Do the loop 20 times max, i.e. wait 10 seconds but no more
        U32 total_time = 0;
        while ((done == false) && (total_time < MAX_TIME))
        {
            ms_sleep(INCREMENT_TIME);
            total_time += INCREMENT_TIME;
        }
        // Verifies that the responder has now been called
        ensure("LLImageDecodeThread: threaded work unit not processed", done == true);
    }

    // <Vulkanstorm> parallelFor: the executor the J2C codec borrows (llimagej2cneo, USE_NEOJ2K).
    struct parallel_ctx
    {
        std::vector<std::atomic<U32>> hits;
        std::vector<U32> csr;
        explicit parallel_ctx(U32 n) : hits(n), csr(n, 0) {}
    };
    static void count_job(void* c, U32 i)
    {
        parallel_ctx* ctx = static_cast<parallel_ctx*>(c);
        ctx->hits[i].fetch_add(1);
        ctx->csr[i] = _mm_getcsr();
    }

    template<> template<>
    void imagedecodethread_object_t::test<2>()
    {
        set_test_name("parallelFor runs every job exactly once, with C code's floating-point state");
        mThread = new LLImageDecodeThread(true);
        ensure("the pool has workers", mThread->getWorkerCount() >= 1);
        ensure("an idle pool is idle", mThread->isIdle());
        ensure("the main thread is no pool request", LLImageDecodeThread::getCurrent() == nullptr);
        // the viewer's main thread runs with flush-to-zero on (LLVector4a::initClass): jobs must not see it
        const unsigned int saved = _mm_getcsr();
        _mm_setcsr(saved | 0x8000u | 0x0040u);
        for (U32 count : { 1u, 2u, 7u, 1000u })
        {
            parallel_ctx ctx(count);
            mThread->parallelFor(count, count_job, &ctx);
            for (U32 i = 0; i < count; ++i)
            {
                ensure_equals("job ran once", ctx.hits[i].load(), 1u);
                ensure("round to nearest, no flush-to-zero, no denormals-are-zero", (ctx.csr[i] & (0x6000u | 0x8000u | 0x0040u)) == 0);
            }
        }
        ensure("the caller's state is back", (_mm_getcsr() & (0x8000u | 0x0040u)) == (0x8000u | 0x0040u));
        _mm_setcsr(saved);
        mThread->parallelFor(0, count_job, nullptr); // no job, no call
    }

    template<> template<>
    void imagedecodethread_object_t::test<3>()
    {
        set_test_name("parallelFor from every worker at once cannot deadlock, and the closed pool still finishes");
        mThread = new LLImageDecodeThread(true);
        LL::WorkQueue::ptr_t queue = LL::WorkQueue::getInstance("ImageDecode");
        ensure("the pool's queue", queue != nullptr);
        const U32 workers = mThread->getWorkerCount();
        const U32 requests = workers * 3; // more than the workers: each one holds a worker while it loops
        std::atomic<U32> finished{0};
        std::atomic<U32> wrong{0};
        LLImageDecodeThread* pool = mThread;
        for (U32 r = 0; r < requests; ++r)
        {
            queue->post([pool, &finished, &wrong]()
            {
                parallel_ctx ctx(64);
                pool->parallelFor(64, count_job, &ctx);
                for (U32 i = 0; i < 64; ++i)
                {
                    if (ctx.hits[i].load() != 1u) { wrong.fetch_add(1); }
                }
                finished.fetch_add(1);
            });
        }
        const U32 INCREMENT_TIME = 100;
        U32 total_time = 0;
        while (finished.load() < requests && total_time < 20000)
        {
            ms_sleep(INCREMENT_TIME);
            total_time += INCREMENT_TIME;
        }
        ensure_equals("every request finished", finished.load(), requests);
        ensure_equals("no job ran twice or not at all", wrong.load(), 0u);
        // after shutdown the queue is closed: the caller runs every job itself
        mThread->shutdown();
        parallel_ctx ctx(10);
        mThread->parallelFor(10, count_job, &ctx);
        for (U32 i = 0; i < 10; ++i)
        {
            ensure_equals("job ran once after shutdown", ctx.hits[i].load(), 1u);
        }
    }
}
