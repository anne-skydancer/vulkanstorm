/**
 * @file lltexturefetch.cpp
 * @brief Object which fetches textures from the cache and/or network
 *
 * $LicenseInfo:firstyear=2000&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2014, Linden Research, Inc.
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

#include "llviewerprecompiledheaders.h"

#include <iostream>
#include <map>
#include <algorithm>
#include <atomic>

#include "lltexturefetch.h"

#include "lldir.h"
#include "llhttpconstants.h"
#include "llimage.h"
#include "llimagej2c.h"
#include "llimageworker.h"
#include "llworkerthread.h"
#include "message.h"

#include "llagent.h"
#include "lltexturecache.h"
#include "llviewercontrol.h"
#include "llviewertexturelist.h"
#include "llviewertexture.h"
#include "llviewerregion.h"
#include "llviewerstats.h"
#include "llviewerstatsrecorder.h"
#include "llviewerthrottle.h"
#include "llviewerassetstats.h"
#include "llworld.h"
#include "llsdparam.h"
#include "llsdutil.h"
#include "llstartup.h"

#include "httprequest.h"
#include "httphandler.h"
#include "httpresponse.h"
#include "bufferarray.h"
#include "bufferstream.h"
#include "llcorehttputil.h"
#include "llhttpretrypolicy.h"
#include "fsassetblacklist.h" //For Asset blacklist
#include "llviewermenu.h"
#include "llviewernetwork.h" // <FS:Ansariel> OpenSim compatibility
#include "vkstorm_ffi.h"

LLTrace::CountStatHandle<F64> LLTextureFetch::sCacheHit("texture_cache_hit");
LLTrace::CountStatHandle<F64> LLTextureFetch::sCacheAttempt("texture_cache_attempt");
LLTrace::EventStatHandle<LLUnit<F32, LLUnits::Percent> > LLTextureFetch::sCacheHitRate("texture_cache_hits");

LLTrace::SampleStatHandle<F32Seconds> LLTextureFetch::sCacheReadLatency("texture_cache_read_latency");
LLTrace::SampleStatHandle<F32Seconds> LLTextureFetch::sTexDecodeLatency("texture_decode_latency");
LLTrace::SampleStatHandle<F32Seconds> LLTextureFetch::sCacheWriteLatency("texture_write_latency");
LLTrace::SampleStatHandle<F32Seconds> LLTextureFetch::sTexFetchLatency("texture_fetch_latency");

LLTextureFetchTester* LLTextureFetch::sTesterp = NULL ;
const std::string sTesterName("TextureFetchTester");

//////////////////////////////////////////////////////////////////////////////
//
// Introduction
//
// This is an attempt to document what's going on in here after-the-fact.
// It's a sincere attempt to be accurate but there will be mistakes.
//
//
// Purpose
//
// What is this module trying to do?  It accepts requests to load textures
// at a given priority and discard level and notifies the caller when done
// (successfully or not).  Additional constraints are:
//
// * Support a local texture cache.  Don't hit network when possible
//   to avoid it.
// * Use UDP or HTTP as directed or as fallback.  HTTP is tried when
//   not disabled and a URL is available.  UDP when a URL isn't
//   available or HTTP attempts fail.
// * Asynchronous (using threads).  Main thread is not to be blocked or
//   burdened.
// * High concurrency.  Many requests need to be in-flight and at various
//   stages of completion.
// * Tolerate frequent re-prioritizations of requests.  Priority is
//   a reflection of a camera's viewpoint and as that viewpoint changes,
//   objects and textures become more and less relevant and that is
//   expressed at this level by priority changes and request cancelations.
//
// The caller interfaces that fall out of the above and shape the
// implementation are:
// * createRequest - Load j2c image via UDP or HTTP at given discard level and priority
// * deleteRequest - Request removal of prior request
// * getRequestFinished - Test if request is finished returning data to caller
// * updateRequestPriority - Change priority of existing request
// * getFetchState - Retrieve progress on existing request
//
// Everything else in here is mostly plumbing, metrics and debug.
//
//
// The Work Queue
//
// The two central classes are LLTextureFetch and LLTextureFetchWorker.
// LLTextureFetch combines threading with a priority queue of work
// requests.  The priority queue is sorted by a U32 priority derived
// from the F32 priority in the APIs.  The *only* work request that
// receives service time by this thread is the highest priority
// request.  All others wait until it is complete or a dynamic priority
// change has re-ordered work.
//
// LLTextureFetchWorker implements the work request and is 1:1 with
// texture fetch requests.  Embedded in each is a state machine that
// walks it through the cache, HTTP, UDP, image decode and retry
// steps of texture acquisition.
//
//
// Threads
//
// Several threads are actively invoking code in this module.  They
// include:
//
// 1.  Tmain    Main thread of execution
// 2.  Ttf      LLTextureFetch's worker thread provided by LLQueuedThread
// 3.  Tcurl    LLCurl's worker thread (should disappear over time)
// 4.  Ttc      LLTextureCache's worker thread
// 5.  Tid      Image decoder's worker thread
// 6.  Thl      HTTP library's worker thread
//
//
// Mutexes/Condition Variables
//
// 1.  Mt       Mutex defined for LLThread's condition variable (base class of
//              LLTextureFetch)
// 2.  Ct       Condition variable for LLThread and used by lock/unlockData().
// 3.  Mwtd     Special LLWorkerThread mutex used for request deletion
//              operations (base class of LLTextureFetch)
// 4.  Mfq      LLTextureFetch's mutex covering request and command queue
//              data.
// 5.  Mfnq     LLTextureFetch's mutex covering udp and http request
//              queue data.
// 6.  Mwc      Mutex covering LLWorkerClass's members (base class of
//              LLTextureFetchWorker).  One per request.
// 7.  Mw       LLTextureFetchWorker's mutex.  One per request.
//
//
// Lock Ordering Rules
//
// Not an exhaustive list but shows the order of lock acquisition
// needed to prevent deadlocks.  'A < B' means acquire 'A' before
// acquiring 'B'.
//
// 1.    Mw < Mfnq
// (there are many more...)
//
//
// Method and Member Definitions
//
// With the above, we'll try to document what threads can call what
// methods (using T* for any), what locks must be held on entry and
// are taken out during execution and what data is covered by which
// lock (if any).  This latter category will be especially prone to
// error so be skeptical.
//
// A line like:  "// Locks:  M<xxx>" indicates a method that must
// be invoked by a caller holding the 'M<xxx>' lock.  Similarly,
// "// Threads:  T<xxx>" means that a caller should be running in
// the indicated thread.
//
// For data members, a trailing comment like "// M<xxx>" means that
// the data member is covered by the specified lock.  Absence of a
// comment can mean the member is unlocked or that I didn't bother
// to do the archaeology.  In the case of LLTextureFetchWorker,
// most data members added by the leaf class are actually covered
// by the Mw lock.  You may also see "// T<xxx>" which means that
// the member's usage is restricted to one thread (except for
// perhaps construction and destruction) and so explicit locking
// isn't used.
//
// In code, a trailing comment like "// [-+]M<xxx>" indicates a
// lock acquision or release point.
//
//
// Worker Lifecycle
//
// The threading and responder model makes it very likely that
// other components are holding on to a pointer to a worker request.
// So, uncoordinated deletions of requests is a guarantee of memory
// corruption in a short time.  So destroying a request involves
// invocations's of LLQueuedThread/LLWorkerThread's abort/stop
// logic that removes workers and puts them ona delete queue for
// 2-phase destruction.  That second phase is deferrable by calls
// to deleteOK() which only allow final destruction (via dtor)
// once deleteOK has determined that the request is in a safe
// state.
//
//
// Worker State Machine
//
// "doWork" will be executed for a given worker on its respective
// LLQueuedThread.  If doWork returns true, the worker is treated
// as completed.  If doWork returns false, the worker will be
// put on the back of the work queue at the start of the next iteration
// of the mainloop.  If a worker is waiting on a resource, it should
// return false as soon as possible and not block to avoid starving
// other workers of cpu cycles.
//



//////////////////////////////////////////////////////////////////////////////

// Tuning/Parameterization Constants
//
// The HTTP water marks, the open-ended Range: threshold (BUG-3323/SH-4375)
// and the capability retry limits live with the state machine in
// vkstorm-assets (fetch::mod).

//////////////////////////////////////////////////////////////////////////////
namespace
{
    // The NoOpDeletor is used when passing certain objects (the LLTextureFetchWorker)
    // in a smart pointer below for passage into
    // the LLCore::Http libararies. When the smart pointer is destroyed,  no
    // action will be taken since we do not in these cases want the object to
    // be destroyed at the end of the call.
    //
    // *NOTE$: Yes! It is "Deletor"
    // http://english.stackexchange.com/questions/4733/what-s-the-rule-for-adding-er-vs-or-when-nouning-a-verb
    // "delete" derives from Latin "deletus"
    void NoOpDeletor(LLCore::HttpHandler *)
    { /*NoOp*/ }
}


// Log scope
static const char * const LOG_TXT = "Texture";

// The worker's state machine (LLTextureFetchWorker::doWork and its
// callbacks) is vkstorm-assets (vkstorm-rust rung five, fetch::Worker),
// driven by this thread through a table of host callbacks that act on the
// C++ objects the machine does not own: the formatted and raw images, the
// cache handles, the decode thread, the HTTP request and its reply buffer,
// the statistics. See doc/rust_port.md.
class LLTextureFetchWorker : public LLWorkerClass, public LLCore::HttpHandler

{
    friend class LLTextureFetch;

private:
    class CacheReadResponder : public LLTextureCache::ReadResponder
    {
    public:

        // Threads:  Ttf
        CacheReadResponder(LLTextureFetch* fetcher, const LLUUID& id, LLImageFormatted* image)
            : mFetcher(fetcher), mID(id)
        {
            setImage(image);
        }

        // Threads:  Ttc
        virtual void completed(bool success)
        {
            LL_PROFILE_ZONE_SCOPED;
            LLTextureFetchWorker* worker = mFetcher->getWorker(mID);
            if (worker)
            {
                worker->callbackCacheRead(success, mFormattedImage, mImageSize, mImageLocal);
            }
        }
    private:
        LLTextureFetch* mFetcher;
        LLUUID mID;
    };

    class CacheWriteResponder : public LLTextureCache::WriteResponder
    {
    public:

        // Threads:  Ttf
        CacheWriteResponder(LLTextureFetch* fetcher, const LLUUID& id)
            : mFetcher(fetcher), mID(id)
        {
        }

        // Threads:  Ttc
        virtual void completed(bool success)
        {
            LL_PROFILE_ZONE_SCOPED;
            LLTextureFetchWorker* worker = mFetcher->getWorker(mID);
            if (worker)
            {
                worker->callbackCacheWrite(success);
            }
        }
    private:
        LLTextureFetch* mFetcher;
        LLUUID mID;
    };

    class DecodeResponder : public LLImageDecodeThread::Responder
    {
    public:

        // Threads:  Ttf
        DecodeResponder(LLTextureFetch* fetcher, const LLUUID& id, LLTextureFetchWorker* worker)
            : mFetcher(fetcher), mID(id)
        {
        }

        // Threads:  Tid
        virtual void completed(bool success, const std::string& error_message, LLImageRaw* raw, LLImageRaw* aux, U32 request_id)
        {
            LL_PROFILE_ZONE_SCOPED;
            LLTextureFetchWorker* worker = mFetcher->getWorker(mID);
            if (worker)
            {
                worker->callbackDecoded(success, error_message, raw, aux, request_id);
            }
        }
    private:
        LLTextureFetch* mFetcher;
        LLUUID mID;
    };

    struct Compare
    {
        // lhs < rhs
        bool operator()(const LLTextureFetchWorker* lhs, const LLTextureFetchWorker* rhs) const
        {
            // greater priority is "less"
            return lhs->mSortPriority > rhs->mSortPriority;
        }
    };

public:

    // Threads:  Ttf
    /*virtual*/ bool doWork(S32 param); // Called from LLWorkerThread::processRequest()

    // Threads:  Ttf
    /*virtual*/ void finishWork(S32 param, bool completed); // called from finishRequest() (WORK THREAD)

    // Threads:  Tmain
    /*virtual*/ bool deleteOK(); // called from update()

    ~LLTextureFetchWorker();

    // Threads:  Ttc
    void callbackCacheRead(bool success, LLImageFormatted* image,
                           S32 imagesize, bool islocal);

    // Threads:  Ttc
    void callbackCacheWrite(bool success);

    // Threads:  Tid
    void callbackDecoded(bool success, const std::string& error_message, LLImageRaw* raw, LLImageRaw* aux, S32 decode_id);

    // Locks:  Mw
    void setCanUseHTTP(bool can_use_http) { vks_tfetch_worker_set_can_use_http(mMachine, can_use_http ? 1 : 0); }
    bool getCanUseHTTP() const { VksTexFetchWorkerInfo info; getInfo(info); return info.can_use_http != 0; }

    void setUrl(const std::string& url) { vks_tfetch_worker_set_url(mMachine, url.data(), url.size()); }

    LLTextureFetch & getFetcher() { return *mFetcher; }

    // Inherited from LLCore::HttpHandler
    // Threads:  Ttf
    virtual void onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse * response);

    enum e_state // mState
    {
        // *NOTE:  Do not change the order/value of state variables, some code
        // depends upon specific ordering/adjacency.

        // NOTE: Affects LLTextureBar::draw in lltextureview.cpp (debug hack)
        INVALID = 0,
        INIT,
        LOAD_FROM_TEXTURE_CACHE,
        CACHE_POST,
        LOAD_FROM_NETWORK,
        LOAD_FROM_SIMULATOR, // <FS:Ansariel> OpenSim compatibility
        WAIT_HTTP_RESOURCE,             // Waiting for HTTP resources
        WAIT_HTTP_RESOURCE2,            // Waiting for HTTP resources
        SEND_HTTP_REQ,                  // Commit to sending as HTTP
        WAIT_HTTP_REQ,                  // Request sent, wait for completion
        DECODE_IMAGE,
        DECODE_IMAGE_UPDATE,
        WRITE_TO_CACHE,
        WAIT_ON_WRITE,
        DONE
    };

    // The machine's fields (a snapshot; take the work mutex for a
    // consistent one).
    void getInfo(VksTexFetchWorkerInfo& info) const { vks_tfetch_worker_info(mMachine, &info); }
    S32 getState() const { return vks_tfetch_worker_state(mMachine); }

protected:
    LLTextureFetchWorker(LLTextureFetch* fetcher, FTType f_type,
                         const std::string& url, const LLUUID& id, const LLHost& host,
                         F32 priority, S32 discard, S32 size);

private:

    // Threads:  Tmain
    /*virtual*/ void startWork(S32 param); // called from addWork() (MAIN THREAD)

    // Threads:  Tmain
    /*virtual*/ void endWork(S32 param, bool aborted); // called from doWork() (MAIN THREAD)

    // get the relative priority of this worker (should map to max virtual size)
    F32 getImagePriority() const;

    // Locks:  Mw
    void setImagePriority(F32 priority);

    // Locks:  Mw (ctor invokes without lock)
    void setDesiredDiscard(S32 discard, S32 size);

    void setState(e_state new_state);
    LLViewerRegion* getRegion();

#ifdef OPENSIM
    // <FS:Ansariel> OpenSim compatibility
    // Threads:  T*
    // Locks:  Mw
    bool insertPacket(S32 index, U8* data, S32 size);

    // Locks:  Mw
    void clearPackets();

    // Locks:  Mw
    void setupPacketData();
    // </FS:Ansariel>
#endif

    // Threads:  Ttf
    void recordTextureStart(bool is_http);

    // Threads:  Ttf
    void recordTextureDone(bool is_http, F64 byte_count);

    void lockWorkMutex() { mWorkMutex.lock(); }
    void unlockWorkMutex() { mWorkMutex.unlock(); }

    // Locks:  Mw
    void resetFormattedData();
    void releaseHttpBuffer();

    // The host callbacks the machine drives (Threads:  Ttf, Locks:  Mw).
    static void     hostEnv(void* ctx, VksTexFetchEnv* out);
    static void     hostRegion(void* ctx, VksTexFetchRegion* out);
    static void     hostReadCache(void* ctx, const char* local_file, size_t local_file_len, int32_t offset, int32_t size);
    static int32_t  hostCacheReadComplete(void* ctx);
    static int32_t  hostSendHttp(void* ctx, const VksTexFetchHttpGet* get);
    static void     hostDropRawImages(void* ctx);
    static uint32_t hostDecode(void* ctx, int32_t discard, int32_t needs_aux);
    static void     hostWriteCache(void* ctx, int32_t file_size, int32_t decoded_discard);
    static int32_t  hostCacheWriteComplete(void* ctx);
    static void     hostPrioritizeWrite(void* ctx);
    static void     hostRemoveFromCache(void* ctx);
    static void     hostResetFormattedData(void* ctx);
    static void     hostDropFormattedImage(void* ctx);
    static void     hostReleaseHttpBuffer(void* ctx);
    static int32_t  hostAssembleHttpReply(void* ctx, const char* url, size_t url_len, int32_t src_offset, int32_t cur_size, int32_t append_size, int32_t total_size);
    static void     hostAddToHttpQueue(void* ctx);
    static void     hostRecordTextureStart(void* ctx, int32_t is_http);
    static void     hostRecordTextureDone(void* ctx, int32_t is_http, double byte_count);
    static void     hostStatCacheAttempt(void* ctx);
    static void     hostStatCacheHit(void* ctx);
    static void     hostStatCacheHitRate(void* ctx, int32_t hit);
    static void     hostLog(void* ctx, int32_t level, int32_t once, const char* msg, size_t len);
    static void     hostAddToNetworkQueue(void* ctx);
    static void     hostRemoveFromNetworkQueue(void* ctx, int32_t cancel);
    static int32_t  hostAssemblePackets(void* ctx, int32_t first_packet, int32_t last_packet);

private:
    LLTextureFetch* mFetcher;
    VksTexFetchWorker* mMachine;
    VksTexFetchHost mHostTable;
    LLPointer<LLImageFormatted> mFormattedImage;
    LLPointer<LLImageRaw>       mRawImage,
                                mAuxImage;
    FTType mFTType;
    LLUUID mID;
    LLHost mHost;
    U8 mType;
    bool mInLocalCache;          // the machine's flag, mirrored for removeFromCache
    F32 mSortPriority;           // the image priority as last read for sorting
    LLCore::HttpStatus mGetStatus; // the last HTTP status, for getRequestFinished
    std::string mRegionAssetUrl; // the region's URLs handed to the machine
    std::string mRegionHttpUrl;
    LLTextureCache::handle_t    mCacheReadHandle,
                                mCacheWriteHandle;

    // Work Data
    LLMutex mWorkMutex;
#ifdef OPENSIM
    // <FS:Ansariel> OpenSim compatibility
    struct PacketData
    {
        PacketData(U8* data, S32 size)
        :   mData(data), mSize(size)
        {}
        ~PacketData() { clearData(); }
        void clearData() { delete[] mData; mData = NULL; }

        U8* mData;
        U32 mSize;
    };
    std::vector<PacketData*> mPackets;
    // </FS:Ansariel>
#endif

    LLViewerAssetStats::duration_t mMetricsStartTime;

    LLCore::HttpHandle      mHttpHandle;                // Handle of any active request
    LLCore::BufferArray *   mHttpBufferArray;           // Refcounted pointer to response data
    S32                     mHttpPolicyClass;
};

const std::set<S32> LOGGED_STATES = { LLTextureFetchWorker::LOAD_FROM_TEXTURE_CACHE, LLTextureFetchWorker::LOAD_FROM_NETWORK, LLTextureFetchWorker::LOAD_FROM_SIMULATOR, // <FS:Ansariel> OpenSim compatibility
                                        LLTextureFetchWorker::WAIT_HTTP_REQ, LLTextureFetchWorker::DECODE_IMAGE_UPDATE, LLTextureFetchWorker::WAIT_ON_WRITE };

//////////////////////////////////////////////////////////////////////////////

// Cross-thread messaging for asset metrics.

/**
 * @brief Base class for cross-thread requests made of the fetcher
 *
 * I believe the intent of the LLQueuedThread class was to
 * have these operations derived from LLQueuedThread::QueuedRequest
 * but the texture fetcher has elected to manage the queue
 * in its own manner.  So these are free-standing objects which are
 * managed in simple FIFO order on the mCommands queue of the
 * LLTextureFetch object.
 *
 * What each represents is a simple command sent from an
 * outside thread into the TextureFetch thread to be processed
 * in order and in a timely fashion (though not an absolute
 * higher priority than other operations of the thread).
 * Each operation derives a new class from the base customizing
 * members, constructors and the doWork() method to effect
 * the command.
 *
 * The flow is one-directional.  There are two global instances
 * of the LLViewerAssetStats collector, one for the main program's
 * thread pointed to by gViewerAssetStatsMain and one for the
 * TextureFetch thread pointed to by gViewerAssetStatsThread1.
 * Common operations has each thread recording metrics events
 * into the respective collector unconcerned with locking and
 * the state of any other thread.  But when the agent moves into
 * a different region or the metrics timer expires and a report
 * needs to be sent back to the grid, messaging across threads
 * is required to distribute data and perform global actions.
 * In pseudo-UML, it looks like:
 *
 * @verbatim
 *                       Main                 Thread1
 *                        .                      .
 *                        .                      .
 *                     +-----+                   .
 *                     | AM  |                   .
 *                     +--+--+                   .
 *      +-------+         |                      .
 *      | Main  |      +--+--+                   .
 *      |       |      | SRE |---.               .
 *      | Stats |      +-----+    \              .
 *      |       |         |        \  (uuid)  +-----+
 *      | Coll. |      +--+--+      `-------->| SR  |
 *      +-------+      | MSC |                +--+--+
 *         | ^         +-----+                   |
 *         | |  (uuid)  / .                   +-----+ (uuid)
 *         |  `--------'  .                   | MSC |---------.
 *         |              .                   +-----+         |
 *         |           +-----+                   .            v
 *         |           | TE  |                   .        +-------+
 *         |           +--+--+                   .        | Thd1  |
 *         |              |                      .        |       |
 *         |           +-----+                   .        | Stats |
 *          `--------->| RSC |                   .        |       |
 *                     +--+--+                   .        | Coll. |
 *                        |                      .        +-------+
 *                     +--+--+                   .            |
 *                     | SME |---.               .            |
 *                     +-----+    \              .            |
 *                        .        \ (clone)  +-----+         |
 *                        .         `-------->| SM  |         |
 *                        .                   +--+--+         |
 *                        .                      |            |
 *                        .                   +-----+         |
 *                        .                   | RSC |<--------'
 *                        .                   +-----+
 *                        .                      |
 *                        .                   +-----+
 *                        .                   | CP  |--> HTTP POST
 *                        .                   +-----+
 *                        .                      .
 *                        .                      .
 *
 * Key:
 *
 * SRE - Set Region Enqueued.  Enqueue a 'Set Region' command in
 *       the other thread providing the new UUID of the region.
 *       TFReqSetRegion carries the data.
 * SR  - Set Region.  New region UUID is sent to the thread-local
 *       collector.
 * SME - Send Metrics Enqueued.  Enqueue a 'Send Metrics' command
 *       including an ownership transfer of a cloned LLViewerAssetStats.
 *       TFReqSendMetrics carries the data.
 * SM  - Send Metrics.  Global metrics reporting operation.  Takes
 *       the cloned stats from the command, merges it with the
 *       thread's local stats, converts to LLSD and sends it on
 *       to the grid.
 * AM  - Agent Moved.  Agent has completed some sort of move to a
 *       new region.
 * TE  - Timer Expired.  Metrics timer has expired (on the order
 *       of 10 minutes).
 * CP  - CURL Post
 * MSC - Modify Stats Collector.  State change in the thread-local
 *       collector.  Typically a region change which affects the
 *       global pointers used to find the 'current stats'.
 * RSC - Read Stats Collector.  Extract collector data cloning it
 *       (i.e. deep copy) when necessary.
 * @endverbatim
 *
 */
class LLTextureFetch::TFRequest // : public LLQueuedThread::QueuedRequest
{
public:
    // Default ctors and assignment operator are correct.

    virtual ~TFRequest()
        {}

    // Patterned after QueuedRequest's method but expected behavior
    // is different.  Always expected to complete on the first call
    // and work dispatcher will assume the same and delete the
    // request after invocation.
    virtual bool doWork(LLTextureFetch * fetcher) = 0;
};

namespace
{

/**
 * @brief Implements a 'Set Region' cross-thread command.
 *
 * When an agent moves to a new region, subsequent metrics need
 * to be binned into a new or existing stats collection in 1:1
 * relationship with the region.  We communicate this region
 * change across the threads involved in the communication with
 * this message.
 *
 * Corresponds to LLTextureFetch::commandSetRegion()
 */
class TFReqSetRegion : public LLTextureFetch::TFRequest
{
public:
    TFReqSetRegion(U64 region_handle)
        : LLTextureFetch::TFRequest(),
          mRegionHandle(region_handle)
        {}
    TFReqSetRegion(const TFReqSetRegion&) = delete;
    TFReqSetRegion& operator=(const TFReqSetRegion&) = delete;

    virtual ~TFReqSetRegion() = default;

    virtual bool doWork(LLTextureFetch * fetcher);

public:
    const U64 mRegionHandle;
};


/**
 * @brief Implements a 'Send Metrics' cross-thread command.
 *
 * This is the big operation.  The main thread gathers metrics
 * for a period of minutes into LLViewerAssetStats and other
 * objects then makes a snapshot of the data by cloning the
 * collector.  This command transfers the clone, along with a few
 * additional arguments (UUIDs), handing ownership to the
 * TextureFetch thread.  It then merges its own data into the
 * cloned copy, converts to LLSD and kicks off an HTTP POST of
 * the resulting data to the currently active metrics collector.
 *
 * Corresponds to LLTextureFetch::commandSendMetrics()
 */
class TFReqSendMetrics : public LLTextureFetch::TFRequest
{
public:
    /**
     * Construct the 'Send Metrics' command to have the TextureFetch
     * thread add and log metrics data.
     *
     * @param   caps_url        URL of a "ViewerMetrics" Caps target
     *                          to receive the data.  Does not have to
     *                          be associated with a particular region.
     *
     * @param   session_id      UUID of the agent's session.
     *
     * @param   agent_id        UUID of the agent.  (Being pure here...)
     *
     * @param   main_stats      Pointer to a clone of the main thread's
     *                          LLViewerAssetStats data.  Thread1 takes
     *                          ownership of the copy and disposes of it
     *                          when done.
     */
    TFReqSendMetrics(const std::string & caps_url,
        const LLUUID & session_id,
        const LLUUID & agent_id,
        LLSD& stats_sd);

    TFReqSendMetrics(const TFReqSendMetrics&) = delete;
    TFReqSendMetrics& operator=(const TFReqSendMetrics&) = delete;

    virtual ~TFReqSendMetrics();

    virtual bool doWork(LLTextureFetch * fetcher);

public:
    const std::string mCapsURL;
    const LLUUID mSessionID;
    const LLUUID mAgentID;
    LLSD mStatsSD;

private:
    LLCore::HttpHandler::ptr_t  mHandler;
};

/*
 * Examines the merged viewer metrics report and if found to be too long,
 * will attempt to truncate it in some reasonable fashion.
 *
 * @param       max_regions     Limit of regions allowed in report.
 *
 * @param       metrics         Full, merged viewer metrics report.
 *
 * @returns     If data was truncated, returns true.
 */
bool truncate_viewer_metrics(int max_regions, LLSD & metrics);

} // end of anonymous namespace


//////////////////////////////////////////////////////////////////////////////

const char* sStateDescs[] = {
    "INVALID",
    "INIT",
    "LOAD_FROM_TEXTURE_CACHE",
    "CACHE_POST",
    "LOAD_FROM_NETWORK",
    "LOAD_FROM_SIMULATOR", // <FS:Ansariel> OpenSim compatibility
    "WAIT_HTTP_RESOURCE",
    "WAIT_HTTP_RESOURCE2",
    "SEND_HTTP_REQ",
    "WAIT_HTTP_REQ",
    "DECODE_IMAGE",
    "DECODE_IMAGE_UPDATE",
    "WRITE_TO_CACHE",
    "WAIT_ON_WRITE",
    "DONE"
};

// static
volatile bool LLTextureFetch::svMetricsDataBreak(true); // Start with a data break

namespace
{
    LLTextureFetchWorker* host_worker(void* ctx)
    {
        return static_cast<LLTextureFetchWorker*>(ctx);
    }
}

//////////////////////////////////////////////////////////////////////////////

// called from MAIN THREAD

LLTextureFetchWorker::LLTextureFetchWorker(LLTextureFetch* fetcher,
                                           FTType f_type, // Fetched image type
                                           const std::string& url, // Optional URL
                                           const LLUUID& id,    // Image UUID
                                           const LLHost& host,  // Simulator host
                                           F32 priority,        // Priority
                                           S32 discard,         // Desired discard
                                           S32 size)            // Desired size
    : LLWorkerClass(fetcher, "TextureFetch"),
      LLCore::HttpHandler(),
      mFetcher(fetcher),
      mMachine(NULL),
      mFTType(f_type),
      mID(id),
      mHost(host),
      mType(host.isOk() ? LLImageBase::TYPE_AVATAR_BAKE : LLImageBase::TYPE_NORMAL),
      mInLocalCache(false),
      mSortPriority(priority),
      mCacheReadHandle(LLTextureCache::nullHandle()),
      mCacheWriteHandle(LLTextureCache::nullHandle()),
      mWorkMutex(),
      mMetricsStartTime(0),
      mHttpHandle(LLCORE_HTTP_HANDLE_INVALID),
      mHttpBufferArray(NULL),
      mHttpPolicyClass(mFetcher->mHttpPolicyClass)
{
    // <FS:Ansariel> OpenSim compatibility: mCanUseNET = !isInSecondLife && url.empty()
    const bool in_second_life = LLGridManager::instance().isInSecondLife();
    mMachine = vks_tfetch_worker_new((int32_t)f_type, url.data(), url.size(), id.mData, host.isOk() ? 1 : 0, priority, in_second_life ? 1 : 0);
    llassert_always(mMachine);

    mHostTable.ctx = this;
    mHostTable.env = &hostEnv;
    mHostTable.region = &hostRegion;
    mHostTable.read_cache = &hostReadCache;
    mHostTable.cache_read_complete = &hostCacheReadComplete;
    mHostTable.send_http = &hostSendHttp;
    mHostTable.drop_raw_images = &hostDropRawImages;
    mHostTable.decode = &hostDecode;
    mHostTable.write_cache = &hostWriteCache;
    mHostTable.cache_write_complete = &hostCacheWriteComplete;
    mHostTable.prioritize_write = &hostPrioritizeWrite;
    mHostTable.remove_from_cache = &hostRemoveFromCache;
    mHostTable.reset_formatted_data = &hostResetFormattedData;
    mHostTable.drop_formatted_image = &hostDropFormattedImage;
    mHostTable.release_http_buffer = &hostReleaseHttpBuffer;
    mHostTable.assemble_http_reply = &hostAssembleHttpReply;
    mHostTable.add_to_http_queue = &hostAddToHttpQueue;
    mHostTable.record_texture_start = &hostRecordTextureStart;
    mHostTable.record_texture_done = &hostRecordTextureDone;
    mHostTable.stat_cache_attempt = &hostStatCacheAttempt;
    mHostTable.stat_cache_hit = &hostStatCacheHit;
    mHostTable.stat_cache_hit_rate = &hostStatCacheHitRate;
    mHostTable.log = &hostLog;
    mHostTable.add_to_network_queue = &hostAddToNetworkQueue;
    mHostTable.remove_from_network_queue = &hostRemoveFromNetworkQueue;
    mHostTable.assemble_packets = &hostAssemblePackets;

//  LL_INFOS(LOG_TXT) << "Create: " << mID << " mHost:" << host << " Discard=" << discard << LL_ENDL;
    if (!mFetcher->mDebugPause)
    {
        addWork(0);
    }
    setDesiredDiscard(discard, size);
}

LLTextureFetchWorker::~LLTextureFetchWorker()
{
//  LL_INFOS(LOG_TXT) << "Destroy: " << mID
//          << " Decoded=" << mDecodedDiscard
//          << " Requested=" << mRequestedDiscard
//          << " Desired=" << mDesiredDiscard << LL_ENDL;
    llassert_always(!haveWork());

    lockWorkMutex();                                                    // +Mw (should be useless)
    // Last-chance catchall to recover the resource, the waiter entry and
    // the statistics.
    const bool http_active = vks_tfetch_worker_on_destroy(mMachine, mFetcher->getMachine()) != 0;
    if (http_active)
    {
        // Issue a cancel on a live request...
        mFetcher->getHttpRequest().requestCancel(mHttpHandle, LLCore::HttpHandler::ptr_t());
    }
    if (mCacheReadHandle != LLTextureCache::nullHandle() && mFetcher->mTextureCache)
    {
        mFetcher->mTextureCache->readComplete(mCacheReadHandle, true);
    }
    if (mCacheWriteHandle != LLTextureCache::nullHandle() && mFetcher->mTextureCache)
    {
        mFetcher->mTextureCache->writeComplete(mCacheWriteHandle, true);
    }
    mFormattedImage = NULL;
#ifdef OPENSIM
    clearPackets(); // <FS:Ansariel> OpenSim compatibility
#endif
    releaseHttpBuffer();
    unlockWorkMutex();                                                  // -Mw
    mFetcher->removeFromHTTPQueue(mID, (S32Bytes)0);
    vks_tfetch_worker_free(mMachine);
    mMachine = NULL;
}

#ifdef OPENSIM
// <FS:Ansariel> OpenSim compatibility
// Locks:  Mw
void LLTextureFetchWorker::clearPackets()
{
    for_each(mPackets.begin(), mPackets.end(), DeletePointer());
    mPackets.clear();
}

// Locks:  Mw
void LLTextureFetchWorker::setupPacketData()
{
    S32 data_size = 0;
    if (mFormattedImage.notNull())
    {
        data_size = mFormattedImage->getDataSize();
    }
    // Only used for simulator requests
    const S32 r = vks_tfetch_worker_setup_packet_data(mMachine, data_size);
    if (r == 0)
    {
        LL_WARNS(LOG_TXT) << "Bad CACHED TEXTURE size: " << data_size << " removing." << LL_ENDL;
        hostRemoveFromCache(this);
        resetFormattedData();
        clearPackets();
    }
    else if (r < 0)
    {
        // This file was cached using HTTP so we have to refetch the first packet
        resetFormattedData();
        clearPackets();
    }
}
// </FS:Ansariel>
#endif

// Locks:  Mw (ctor invokes without lock)
void LLTextureFetchWorker::setDesiredDiscard(S32 discard, S32 size)
{
    const bool add_work = vks_tfetch_worker_set_desired_discard(mMachine, discard, size, haveWork() ? 1 : 0) != 0;
    if (add_work && !mFetcher->mDebugPause)
    {
        addWork(0);
    }
}

// Locks:  Mw
void LLTextureFetchWorker::setImagePriority(F32 priority)
{
    mSortPriority = priority;
    vks_tfetch_worker_set_image_priority(mMachine, priority); //should map to max virtual size, abort if zero
}

// Locks:  Mw
void LLTextureFetchWorker::resetFormattedData()
{
    releaseHttpBuffer();
    if (mFormattedImage.notNull())
    {
        mFormattedImage->deleteData();
    }
}

void LLTextureFetchWorker::releaseHttpBuffer()
{
    if (mHttpBufferArray)
    {
        mHttpBufferArray->release();
        mHttpBufferArray = NULL;
    }
}

F32 LLTextureFetchWorker::getImagePriority() const
{
    VksTexFetchWorkerInfo info;
    getInfo(info);
    return info.image_priority;
}

void LLTextureFetchWorker::setState(e_state new_state)
{
    vks_tfetch_worker_set_state(mMachine, (int32_t)new_state);
}

// Threads:  Tmain
void LLTextureFetchWorker::startWork(S32 param)
{
    llassert(mFormattedImage.isNull());
}

// Threads:  Ttf
bool LLTextureFetchWorker::doWork(S32 param)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE; // <FS:Beq/> Fix wrong category
    LLMutexLock lock(&mWorkMutex);                                      // +Mw
    return vks_tfetch_worker_do_work(mMachine, mFetcher->getMachine(), &mHostTable) != 0;
}                                                                       // -Mw

//////////////////////////////////////////////////////////////////////////////
// The host callbacks (Threads:  Ttf, under the work mutex of the worker
// the machine is running for). None may throw.

void LLTextureFetchWorker::hostEnv(void* ctx, VksTexFetchEnv* out)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    // Also used in llmeshrepository
    static LLCachedControl<bool> disable_range_req(gSavedSettings, "HttpRangeRequestsDisable", false);
    static LLCachedControl<bool> textures_decode_disabled(gSavedSettings, "TextureDecodeDisabled", false);
    memset(out, 0, sizeof(*out));
    out->quitting = w->mFetcher->isQuitting() ? 1 : 0;
    out->delete_requested = w->getFlags(LLWorkerClass::WCF_DELETE_REQUESTED) ? 1 : 0;
    out->debug_pause = w->mFetcher->mDebugPause ? 1 : 0;
    out->non_interactive = gNonInteractive ? 1 : 0;
    // <FS> Asset Blacklist
    out->blacklisted = FSAssetBlacklist::getInstance()->isBlacklisted(w->mID, LLAssetType::AT_TEXTURE) ? 1 : 0;
    if (w->mFormattedImage.notNull())
    {
        out->formatted_present = 1;
        out->formatted_size = w->mFormattedImage->getDataSize();
        out->formatted_discard = w->mFormattedImage->getDiscardLevel();
        out->formatted_is_j2c = (w->mFormattedImage->getCodec() == IMG_CODEC_J2C) ? 1 : 0;
        out->formatted_width = w->mFormattedImage->getWidth();
        out->formatted_height = w->mFormattedImage->getHeight();
    }
    out->can_load_from_cache = w->mFetcher->canLoadFromCache() ? 1 : 0;
    // <FS:Ansariel> Force HTTP features on SL
    out->use_http_textures = use_http_textures() ? 1 : 0;
    out->disconnected = gDisconnected ? 1 : 0;
    out->range_requests_disabled = disable_range_req ? 1 : 0;
    out->decode_disabled = textures_decode_disabled ? 1 : 0;
    if (w->mRawImage.notNull())
    {
        out->raw_width = w->mRawImage->getWidth();
        out->raw_height = w->mRawImage->getHeight();
    }
    out->texture_bandwidth = w->mFetcher->getTextureBandwidth();
    out->max_bandwidth = w->mFetcher->mMaxBandwidth;
}

void LLTextureFetchWorker::hostRegion(void* ctx, VksTexFetchRegion* out)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    LLViewerRegion* region = w->getRegion();
    if (!region)
    {
        out->present = 0;
        return;
    }
    out->present = 1;
    memcpy(out->id, region->getRegionID().mData, 16);
    w->mRegionAssetUrl = region->getViewerAssetUrl();
    // <FS:Ansariel> [UDP Assets]
    w->mRegionHttpUrl = region->getHttpUrl();
    out->viewer_asset_url = w->mRegionAssetUrl.data();
    out->viewer_asset_url_len = w->mRegionAssetUrl.size();
    out->http_url = w->mRegionHttpUrl.data();
    out->http_url_len = w->mRegionHttpUrl.size();
}

void LLTextureFetchWorker::hostReadCache(void* ctx, const char* local_file, size_t local_file_len, int32_t offset, int32_t size)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    CacheReadResponder* responder = new CacheReadResponder(w->mFetcher, w->mID, w->mFormattedImage);
    if (local_file)
    {
        // read file from local disk
        std::string filename(local_file, local_file_len);
        w->mCacheReadHandle = w->mFetcher->mTextureCache->readFromCache(filename, w->mID, offset, size, responder);
    }
    else
    {
        w->mCacheReadHandle = w->mFetcher->mTextureCache->readFromCache(w->mID, offset, size, responder);
    }
}

int32_t LLTextureFetchWorker::hostCacheReadComplete(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    // Make sure request is complete. *TODO: make this auto-complete
    if (w->mFetcher->mTextureCache->readComplete(w->mCacheReadHandle, false))
    {
        w->mCacheReadHandle = LLTextureCache::nullHandle();
        return 1;
    }
    return 0;
}

int32_t LLTextureFetchWorker::hostSendHttp(void* ctx, const VksTexFetchHttpGet* get)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    std::string url(get->url, get->url_len);
    w->mHttpHandle = LLCORE_HTTP_HANDLE_INVALID;
    w->mGetStatus = LLCore::HttpStatus();

    // Will call callbackHttpGet when curl request completes
    // Only server bake images use the returned headers currently, for getting retry-after field.
    LLCore::HttpOptions::ptr_t options = get->with_headers ? w->mFetcher->mHttpOptionsWithHeaders: w->mFetcher->mHttpOptions;
    if (get->no_range)
    {
        // 'Range:' requests may be disabled in which case all HTTP
        // texture fetches result in full fetches.  This can be used
        // by people with questionable ISPs or networking gear that
        // doesn't handle these well.
        w->mHttpHandle = w->mFetcher->mHttpRequest->requestGet(w->mHttpPolicyClass,
                                                               url,
                                                               options,
                                                               w->mFetcher->mHttpHeaders,
                                                               LLCore::HttpHandler::ptr_t(w, &NoOpDeletor));
    }
    else
    {
        w->mHttpHandle = w->mFetcher->mHttpRequest->requestGetByteRange(w->mHttpPolicyClass,
                                                                        url,
                                                                        get->offset,
                                                                        get->size,
                                                                        options,
                                                                        w->mFetcher->mHttpHeaders,
                                                                        LLCore::HttpHandler::ptr_t(w, &NoOpDeletor));
    }
    if (LLCORE_HTTP_HANDLE_INVALID == w->mHttpHandle)
    {
        LLCore::HttpStatus status(w->mFetcher->mHttpRequest->getStatus());
        LL_WARNS(LOG_TXT) << "HTTP GET request failed for " << w->mID
                          << ", Status: " << status.toTerseString()
                          << " Reason: '" << status.toString() << "'"
                          << LL_ENDL;
        return 0;
    }
    return 1;
}

void LLTextureFetchWorker::hostDropRawImages(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    w->mRawImage = NULL;
    w->mAuxImage = NULL;
}

uint32_t LLTextureFetchWorker::hostDecode(void* ctx, int32_t discard, int32_t needs_aux)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    // In case worked manages to request decode, be shut down,
    // then init and request decode again with first decode
    // still in progress, assign a sufficiently unique id
    return LLAppViewer::getImageDecodeThread()->decodeImage(w->mFormattedImage,
                                                           discard,
                                                           needs_aux != 0,
                                                           new DecodeResponder(w->mFetcher, w->mID, w));
}

void LLTextureFetchWorker::hostWriteCache(void* ctx, int32_t file_size, int32_t decoded_discard)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    LLImageDataSharedLock lock(w->mFormattedImage);
    S32 datasize = w->mFormattedImage->getDataSize();
    CacheWriteResponder* responder = new CacheWriteResponder(w->mFetcher, w->mID);
    // This call might be under work mutex, but mRawImage is not nessesary safe here.
    // If something retrieves it via getRequestFinished() and modifies, image won't
    // be protected by work mutex and won't be safe to use here nor in cache worker.
    // So make sure users of getRequestFinished() does not attempt to modify image while
    // fetcher is working
    w->mCacheWriteHandle = w->mFetcher->mTextureCache->writeToCache(w->mID,
                                                                    w->mFormattedImage->getData(), datasize,
                                                                    file_size, w->mRawImage, decoded_discard, responder);
}

int32_t LLTextureFetchWorker::hostCacheWriteComplete(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    if (w->mFetcher->mTextureCache->writeComplete(w->mCacheWriteHandle))
    {
        w->mCacheWriteHandle = LLTextureCache::nullHandle();
        return 1;
    }
    return 0;
}

void LLTextureFetchWorker::hostPrioritizeWrite(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    // We're waiting for this write to complete before we can receive more data
    // (we can't touch mFormattedImage until the write completes)
    // Prioritize the write
    w->mFetcher->mTextureCache->prioritizeWrite(w->mCacheWriteHandle);
}

void LLTextureFetchWorker::hostRemoveFromCache(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    if (!w->mInLocalCache)
    {
        w->mFetcher->mTextureCache->removeFromCache(w->mID);
    }
}

void LLTextureFetchWorker::hostResetFormattedData(void* ctx)
{
    host_worker(ctx)->resetFormattedData();
}

void LLTextureFetchWorker::hostDropFormattedImage(void* ctx)
{
    host_worker(ctx)->mFormattedImage = NULL;
}

void LLTextureFetchWorker::hostReleaseHttpBuffer(void* ctx)
{
    host_worker(ctx)->releaseHttpBuffer();
}

int32_t LLTextureFetchWorker::hostAssembleHttpReply(void* ctx, const char* url, size_t url_len, int32_t src_offset, int32_t cur_size, int32_t append_size, int32_t total_size)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    U8 * buffer = (U8 *)ll_aligned_malloc_16(total_size);
    if (!buffer)
    {
        // abort. If we have no space for packet, we have not enough space to decode image
        return 0;
    }

    if (w->mFormattedImage.isNull())
    {
        // For now, create formatted image based on extension
        std::string extension = gDirUtilp->getExtension(std::string(url, url_len));
        w->mFormattedImage = LLImageFormatted::createFromType(LLImageBase::getCodecFromExtension(extension));
        if (w->mFormattedImage.isNull())
        {
            w->mFormattedImage = new LLImageJ2C; // default
        }
    }

    LLImageDataLock lock(w->mFormattedImage);

    if (cur_size > 0)
    {
        // Copy previously collected data into buffer
        memcpy(buffer, w->mFormattedImage->getData(), cur_size);
    }
    w->mHttpBufferArray->read(src_offset, (char *) buffer + cur_size, append_size);

    // NOTE: setData releases current data and owns new data (buffer)
    w->mFormattedImage->setData(buffer, total_size);

    // Done with buffer array
    w->releaseHttpBuffer();
    return 1;
}

void LLTextureFetchWorker::hostAddToHttpQueue(void* ctx)
{
    LLTextureFetchWorker* w = host_worker(ctx);
    w->mFetcher->addToHTTPQueue(w->mID);
}

void LLTextureFetchWorker::hostRecordTextureStart(void* ctx, int32_t is_http)
{
    host_worker(ctx)->recordTextureStart(is_http != 0);
}

void LLTextureFetchWorker::hostRecordTextureDone(void* ctx, int32_t is_http, double byte_count)
{
    host_worker(ctx)->recordTextureDone(is_http != 0, byte_count);
}

void LLTextureFetchWorker::hostStatCacheAttempt(void* ctx)
{
    add(LLTextureFetch::sCacheAttempt, 1.0);
}

void LLTextureFetchWorker::hostStatCacheHit(void* ctx)
{
    add(LLTextureFetch::sCacheHit, 1.0);
}

void LLTextureFetchWorker::hostStatCacheHitRate(void* ctx, int32_t hit)
{
    record(LLTextureFetch::sCacheHitRate, LLUnits::Ratio::fromValue(hit ? 1 : 0));
}

void LLTextureFetchWorker::hostLog(void* ctx, int32_t level, int32_t once, const char* msg, size_t len)
{
    std::string text(msg, len);
    if (once)
    {
        // LL_INFOS_ONCE keyed on the message rather than the call site.
        static LLMutex seen_mutex;
        static std::set<std::string> seen;
        LLMutexLock lock(&seen_mutex);
        if (!seen.insert(text).second)
        {
            return;
        }
    }
    switch (level)
    {
    case 0:
        LL_DEBUGS(LOG_TXT) << text << LL_ENDL;
        break;
    case 1:
        LL_INFOS(LOG_TXT) << text << LL_ENDL;
        break;
    default:
        LL_WARNS(LOG_TXT) << text << LL_ENDL;
        break;
    }
}

void LLTextureFetchWorker::hostAddToNetworkQueue(void* ctx)
{
#ifdef OPENSIM
    LLTextureFetchWorker* w = host_worker(ctx);
    w->mFetcher->addToNetworkQueue(w);
#endif
}

void LLTextureFetchWorker::hostRemoveFromNetworkQueue(void* ctx, int32_t cancel)
{
#ifdef OPENSIM
    LLTextureFetchWorker* w = host_worker(ctx);
    w->mFetcher->removeFromNetworkQueue(w, cancel != 0);
#endif
}

int32_t LLTextureFetchWorker::hostAssemblePackets(void* ctx, int32_t first_packet, int32_t last_packet)
{
#ifdef OPENSIM
    LLTextureFetchWorker* w = host_worker(ctx);
    if (w->mFormattedImage.isNull())
    {
        return 0;
    }
    S32 cur_size = w->mFormattedImage->getDataSize();
    S32 buffer_size = cur_size;
    for (S32 i = first_packet; i <= last_packet; i++)
    {
        llassert_always((i >= 0) && (i < (S32)w->mPackets.size()));
        llassert_always(w->mPackets[i]);
        buffer_size += w->mPackets[i]->mSize;
    }
    /// We have new data
    U8* buffer = (U8*)ll_aligned_malloc_16(buffer_size);
    S32 offset = 0;
    if (cur_size > 0 && first_packet > 0)
    {
        memcpy(buffer, w->mFormattedImage->getData(), cur_size);
        offset = cur_size;
    }
    for (S32 i = first_packet; i <= last_packet; i++)
    {
        memcpy(buffer + offset, w->mPackets[i]->mData, w->mPackets[i]->mSize);
        offset += w->mPackets[i]->mSize;
    }
    // NOTE: setData releases current data
    w->mFormattedImage->setData(buffer, buffer_size);
    return buffer_size;
#else
    return 0;
#endif
}

//////////////////////////////////////////////////////////////////////////////

// Threads:  Ttf
// virtual
void LLTextureFetchWorker::onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse * response)
{
    LL_PROFILE_ZONE_SCOPED;
    static LLCachedControl<bool> log_to_viewer_log(gSavedSettings, "LogTextureDownloadsToViewerLog", false);
    static LLCachedControl<bool> log_to_sim(gSavedSettings, "LogTextureDownloadsToSimulator", false);
    static LLCachedControl<bool> log_texture_traffic(gSavedSettings, "LogTextureNetworkTraffic", false) ;

    LLMutexLock lock(&mWorkMutex);                                      // +Mw

    VksTexFetchWorkerInfo info;
    getInfo(info);

    if (log_to_viewer_log || log_to_sim)
    {
        mFetcher->mTextureInfo.setRequestStartTime(mID, mMetricsStartTime.value());
        mFetcher->mTextureInfo.setRequestType(mID, LLTextureInfoDetails::REQUEST_TYPE_HTTP);
        mFetcher->mTextureInfo.setRequestSize(mID, info.requested_size);
        mFetcher->mTextureInfo.setRequestOffset(mID, info.requested_offset);
        mFetcher->mTextureInfo.setRequestCompleteTimeAndLog(mID, LLTimer::getTotalTime());
    }

    static LLCachedControl<F32> fake_failure_rate(gSavedSettings, "TextureFetchFakeFailureRate", 0.0f);
    F32 rand_val = ll_frand();
    F32 rate = fake_failure_rate;
    if (mFTType == FTT_SERVER_BAKE && (fake_failure_rate > 0.0) && (rand_val < fake_failure_rate))
    {
        LL_WARNS(LOG_TXT) << mID << " for debugging, setting fake failure status for texture " << mID
                          << " (rand was " << rand_val << "/" << rate << ")" << LL_ENDL;
        response->setStatus(LLCore::HttpStatus(503));
    }
    LLCore::HttpStatus status(response->getStatus());
    mGetStatus = status;
    std::string reason(status.toString());

    // The retry policy of server bakes reads the Retry-After header.
    int32_t has_retry_after = 0;
    F32 retry_after = 0.f;
    if (!status && (mFTType == FTT_SERVER_BAKE))
    {
        const LLCore::HttpHeaders::ptr_t headers = response->getHeaders();
        if (headers)
        {
            const std::string *retry_value = headers->find(HTTP_IN_HEADER_RETRY_AFTER.c_str());
            if (retry_value && LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(*retry_value, retry_after))
            {
                has_retry_after = 1;
            }
        }
    }

    // A warning about partial (HTTP 206) data.  Some grid services
    // do *not* return a 'Content-Range' header in the response to
    // Range requests with a 206 status.  We're forced to assume
    // we get what we asked for in these cases until we can fix
    // the services.
    static const LLCore::HttpStatus par_status(HTTP_PARTIAL_CONTENT);
    const bool partial = (par_status == status);
    LLCore::BufferArray * body(response->getBody());
    const S32 body_size = body ? static_cast<S32>(body->size()) : 0;
    unsigned int range_offset(0), range_length(0), range_full(0);
    int32_t has_range = 0;
    if (partial)
    {
        response->getRange(&range_offset, &range_length, &range_full);
        has_range = 1;
    }

    const int64_t result = vks_tfetch_worker_http_completed(mMachine, mFetcher->getMachine(), &mHostTable,
                                                            status ? 1 : 0, status.getType(), reason.data(), reason.size(),
                                                            partial ? 1 : 0, body_size, has_range, range_offset, range_length, range_full,
                                                            has_retry_after, retry_after);
    if (result < 0)
    {
        // Retrying: back to LOAD_FROM_NETWORK after the policy's delay.
        mFetcher->removeFromHTTPQueue(mID, S32Bytes(0));
        return;
    }

    // The machine kept the body when it accepted the data (hold on to it
    // for the later copy).
    getInfo(info);
    if (info.http_buffer_present && !mHttpBufferArray && body)
    {
        body->addRef();
        mHttpBufferArray = body;
    }

    S32BytesImplicit data_size = (S32)result;

    if (log_texture_traffic && data_size > 0)
    {
        // one worker per multiple textures
        std::vector<LLViewerTexture*> textures;
        LLViewerTextureManager::findTextures(mID, textures);
        std::vector<LLViewerTexture*>::iterator iter = textures.begin();
        while (iter != textures.end())
        {
            LLViewerTexture* tex = *iter++;
            if (tex)
            {
                gTotalTextureBytesPerBoostLevel[tex->getBoostLevel()] += data_size;
            }
        }
    }

    mFetcher->removeFromHTTPQueue(mID, data_size);
}                                                                       // -Mw


// Threads:  Tmain
void LLTextureFetchWorker::endWork(S32 param, bool aborted)
{
    LL_PROFILE_ZONE_SCOPED;
    // LL::ThreadPool has no operation to cancel a particular work item
    vks_tfetch_worker_end_work(mMachine);
    mFormattedImage = NULL;
}

//////////////////////////////////////////////////////////////////////////////

// Threads:  Ttf

// virtual
void LLTextureFetchWorker::finishWork(S32 param, bool completed)
{
    LL_PROFILE_ZONE_SCOPED;
    // The following are required in case the work was aborted
    if (mCacheReadHandle != LLTextureCache::nullHandle())
    {
        mFetcher->mTextureCache->readComplete(mCacheReadHandle, true);
        mCacheReadHandle = LLTextureCache::nullHandle();
    }
    if (mCacheWriteHandle != LLTextureCache::nullHandle())
    {
        mFetcher->mTextureCache->writeComplete(mCacheWriteHandle, true);
        mCacheWriteHandle = LLTextureCache::nullHandle();
    }
}

// LLQueuedThread's update() method is asking if it's okay to
// delete this worker.  You'll notice we're not locking in here
// which is a slight concern.  Caller is expected to have made
// this request 'quiet' by whatever means...
//
// Threads:  Tmain

// virtual
bool LLTextureFetchWorker::deleteOK()
{
    bool delete_ok = vks_tfetch_worker_delete_ok_state(mMachine, mFetcher->getMachine(), haveWork() ? 1 : 0) != 0;

    // Allow any pending reads or writes to complete
    if (mCacheReadHandle != LLTextureCache::nullHandle())
    {
        if (!mFetcher->mTextureCache || mFetcher->mTextureCache->readComplete(mCacheReadHandle, true))
        {
            mCacheReadHandle = LLTextureCache::nullHandle();
        }
        else
        {
            delete_ok = false;
        }
    }
    if (mCacheWriteHandle != LLTextureCache::nullHandle())
    {
        if (!mFetcher->mTextureCache || mFetcher->mTextureCache->writeComplete(mCacheWriteHandle))
        {
            mCacheWriteHandle = LLTextureCache::nullHandle();
        }
        else
        {
            delete_ok = false;
        }
    }

    return delete_ok;
}

//////////////////////////////////////////////////////////////////////////////

// Threads:  Ttc
void LLTextureFetchWorker::callbackCacheRead(bool success, LLImageFormatted* image,
                                             S32 imagesize, bool islocal)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_TEXTURE;
    LLMutexLock lock(&mWorkMutex);                                      // +Mw
    if (getState() != LOAD_FROM_TEXTURE_CACHE)
    {
//      LL_WARNS(LOG_TXT) << "Read callback for " << mID << " with state = " << mState << LL_ENDL;
        return;
    }
    S32 data_size = 0;
    S32 codec = IMG_CODEC_INVALID;
    if (success)
    {
        llassert_always(imagesize >= 0);
        mFormattedImage = image;
        mInLocalCache = islocal;
        data_size = image->getDataSize();
        codec = image->getCodec();
    }
    vks_tfetch_worker_cache_read_done(mMachine, success ? 1 : 0, imagesize, data_size, codec, islocal ? 1 : 0);
}                                                                       // -Mw

// Threads:  Ttc
void LLTextureFetchWorker::callbackCacheWrite(bool success)
{
    LLMutexLock lock(&mWorkMutex);                                      // +Mw
    vks_tfetch_worker_cache_write_done(mMachine, success ? 1 : 0);
}                                                                       // -Mw

//////////////////////////////////////////////////////////////////////////////

// Threads:  Tid
void LLTextureFetchWorker::callbackDecoded(bool success, const std::string &error_message, LLImageRaw* raw, LLImageRaw* aux, S32 decode_id)
{
    LLMutexLock lock(&mWorkMutex);                                      // +Mw
    const S32 formatted_discard = mFormattedImage.notNull() ? mFormattedImage->getDiscardLevel() : -1;
    if (!vks_tfetch_worker_decode_done(mMachine, &mHostTable, success ? 1 : 0, formatted_discard, (uint32_t)decode_id))
    {
        return; // aborted or obsolete, ignore
    }
    llassert_always(mFormattedImage.notNull());
    if (success)
    {
        llassert_always(raw);
        mRawImage = raw;
        mAuxImage = aux;
        LL_DEBUGS(LOG_TXT) << mID << ": Decode Finished. Discard: " << formatted_discard
                           << " Raw Image: " << llformat("%dx%d",mRawImage->getWidth(),mRawImage->getHeight()) << LL_ENDL;
    }
    else
    {
        LL_WARNS(LOG_TXT) << "DECODE FAILED: " << mID << " Discard: " << formatted_discard << ", reason: " << error_message << LL_ENDL;
    }
//  LL_INFOS(LOG_TXT) << mID << " : DECODE COMPLETE " << LL_ENDL;
}                                                                       // -Mw

//////////////////////////////////////////////////////////////////////////////

// Threads:  Ttf
void LLTextureFetchWorker::recordTextureStart(bool is_http)
{
    if (! mMetricsStartTime.value())
    {
        mMetricsStartTime = LLViewerAssetStatsFF::get_timestamp();
    }
    LLViewerAssetStatsFF::record_enqueue(LLViewerAssetType::AT_TEXTURE,
                                                 is_http,
                                                 LLImageBase::TYPE_AVATAR_BAKE == mType);
}


// Threads:  Ttf
void LLTextureFetchWorker::recordTextureDone(bool is_http, F64 byte_count)
{
    if (mMetricsStartTime.value())
    {
        LLViewerAssetStatsFF::record_response(LLViewerAssetType::AT_TEXTURE,
                                              is_http,
                                              LLImageBase::TYPE_AVATAR_BAKE == mType,
                                              LLViewerAssetStatsFF::get_timestamp() - mMetricsStartTime,
                                              byte_count);
        mMetricsStartTime = (U32Seconds)0;
    }
    LLViewerAssetStatsFF::record_dequeue(LLViewerAssetType::AT_TEXTURE,
                                                 is_http,
                                                 LLImageBase::TYPE_AVATAR_BAKE == mType);
}


//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
// public

std::string LLTextureFetch::getStateString(S32 state)
{
    return vks_ffi_string([&](char* buf, size_t cap) { return vks_tfetch_state_string(state, buf, cap); });
}

LLTextureFetch::LLTextureFetch(LLTextureCache* cache, bool threaded, bool qa_mode)
    : LLWorkerThread("TextureFetch", threaded, true),
      mDebugCount(0),
      mDebugPause(false),
      mPacketCount(0), // <FS:Ansariel> OpenSim compatibility
      mBadPacketCount(0), // <FS:Ansariel> OpenSim compatibility
      mQueueMutex(),
      mNetworkQueueMutex(),
      mTextureCache(cache),
      mTextureBandwidth(0),
      mHTTPTextureBits(0),
      mTotalHTTPRequests(0),
      mQAMode(qa_mode),
      mHttpRequest(NULL),
      mHttpOptions(),
      mHttpOptionsWithHeaders(),
      mHttpHeaders(),
      mHttpPolicyClass(LLCore::HttpRequest::DEFAULT_POLICY_ID),
      mHttpMetricsHeaders(),
      mHttpMetricsPolicyClass(LLCore::HttpRequest::DEFAULT_POLICY_ID),
      mMachine(NULL),
      mFetchSource(LLTextureFetch::FROM_ALL),
      mOriginFetchSource(LLTextureFetch::FROM_ALL),
      mTextureInfoMainThread(false)
{
    mMaxBandwidth = LLViewerThrottle::getMaxBandwidthKbps();
    mTextureInfo.setLogging(true);

    LLAppCoreHttp & app_core_http(LLAppViewer::instance()->getAppCoreHttp());
    mHttpRequest = new LLCore::HttpRequest;
    mHttpOptions  = std::make_shared<LLCore::HttpOptions>();
    mHttpOptionsWithHeaders = std::make_shared<LLCore::HttpOptions>();
    mHttpOptionsWithHeaders->setWantHeaders(true);
    mHttpHeaders = std::make_shared<LLCore::HttpHeaders>();
    mHttpHeaders->append(HTTP_OUT_HEADER_ACCEPT, HTTP_CONTENT_IMAGE_X_J2C);
    mHttpPolicyClass = app_core_http.getPolicy(LLAppCoreHttp::AP_TEXTURE);
    mHttpMetricsHeaders = std::make_shared<LLCore::HttpHeaders>();
    mHttpMetricsHeaders->append(HTTP_OUT_HEADER_CONTENT_TYPE, HTTP_CONTENT_LLSD_XML);
    mHttpMetricsPolicyClass = app_core_http.getPolicy(LLAppCoreHttp::AP_REPORTING);
    mMachine = vks_tfetch_fetcher_new();
    llassert_always(mMachine);

    // If that test log has ben requested but not yet created, create it
    if (LLMetricPerformanceTesterBasic::isMetricLogRequested(sTesterName) && !LLMetricPerformanceTesterBasic::getTester(sTesterName))
    {
        sTesterp = new LLTextureFetchTester() ;
        if (!sTesterp->isValid())
        {
            delete sTesterp;
            sTesterp = NULL;
        }
    }
}

LLTextureFetch::~LLTextureFetch()
{
    clearDeleteList();

    while (! mCommands.empty())
    {
        TFRequest * req(mCommands.front());
        mCommands.erase(mCommands.begin());
        delete req;
    }

    vks_tfetch_fetcher_cancel_waiters(mMachine);

    delete mHttpRequest;
    mHttpRequest = NULL;

    // ~LLQueuedThread() called here; the workers it deletes still use the
    // machine, so the machine goes last (see shutDownTextureCacheThread).
    vks_tfetch_fetcher_free(mMachine);
    mMachine = NULL;
}

S32 LLTextureFetch::createRequest(FTType f_type, const std::string& url, const LLUUID& id, const LLHost& host, F32 priority,
    S32 w, S32 h, S32 c, S32 desired_discard, bool needs_aux, bool can_use_http)
{
    LL_PROFILE_ZONE_SCOPED;
    if (mDebugPause)
    {
        return CREATE_REQUEST_ERROR_DEFAULT;
    }

    if (f_type == FTT_SERVER_BAKE)
    {
        LL_DEBUGS("Avatar") << " requesting " << id << " " << w << "x" << h << " discard " << desired_discard << " type " << f_type << LL_ENDL;
    }
    LLTextureFetchWorker* worker = getWorker(id);
    if (worker)
    {
        if (worker->mHost != host)
        {
            LL_WARNS(LOG_TXT) << "LLTextureFetch::createRequest " << id << " called with multiple hosts: "
                << host << " != " << worker->mHost << LL_ENDL;
            removeRequest(worker, true);
            worker = NULL;
            return CREATE_REQUEST_ERROR_MHOSTS;
        }
    }

    // The desired size and discard: full requests for non-J2C URLs and
    // discard 0, the byte budget when the dimensions are known, else the
    // smallest amount at the lowest resolution.
    S32 desired_size;
    std::string exten = gDirUtilp->getExtension(url);
    const bool url_is_non_j2c = !url.empty() && !exten.empty() && (LLImageBase::getCodecFromExtension(exten) != IMG_CODEC_J2C);
    vks_tfetch_desired_size((int32_t)f_type, url.empty() ? 0 : 1, url_is_non_j2c ? 1 : 0, w, h, c, desired_discard, &desired_size, &desired_discard);

    if (worker)
    {
        if (worker->wasAborted())
        {
            return CREATE_REQUEST_ERROR_ABORTED; // need to wait for previous aborted request to complete
        }
        worker->lockWorkMutex();                                        // +Mw
        VksTexFetchWorkerInfo info;
        worker->getInfo(info);
        if (vks_tfetch_already_done(info.state == LLTextureFetchWorker::DONE ? 1 : 0, info.desired_size, info.desired_discard, desired_size, desired_discard))
        {
            worker->unlockWorkMutex();                                  // -Mw

            return CREATE_REQUEST_ERROR_TRANSITION; // similar request has finished, failed or is in a transitional state
        }
        vks_tfetch_worker_inc_active(worker->mMachine);
        vks_tfetch_worker_set_needs_aux(worker->mMachine, needs_aux ? 1 : 0);
        worker->setImagePriority(priority);
        worker->setDesiredDiscard(desired_discard, desired_size);
        worker->setCanUseHTTP(can_use_http);

        //MAINT-4184 url is always empty.  Do not set with it.

        if (!worker->haveWork())
        {
            worker->setState(LLTextureFetchWorker::INIT);
            worker->unlockWorkMutex();                                  // -Mw

            worker->addWork(0);
        }
        else
        {
            worker->unlockWorkMutex();                                  // -Mw
        }
    }
    else
    {
        worker = new LLTextureFetchWorker(this, f_type, url, id, host, priority, desired_discard, desired_size);
        lockQueue();                                                    // +Mfq
        mRequestMap[id] = worker;
        unlockQueue();                                                  // -Mfq

        worker->lockWorkMutex();                                        // +Mw
        vks_tfetch_worker_inc_active(worker->mMachine);
        vks_tfetch_worker_set_needs_aux(worker->mMachine, needs_aux ? 1 : 0);
        worker->setCanUseHTTP(can_use_http);
        worker->unlockWorkMutex();                                      // -Mw
    }

    LL_DEBUGS(LOG_TXT) << "REQUESTED: " << id << " f_type " << fttype_to_string(f_type)
        << " Discard: " << desired_discard << " size " << desired_size << LL_ENDL;
    return desired_discard;
}


// <FS:Ansariel> OpenSim compatibility
// Threads:  T* (but Ttf in practice)

// protected
void LLTextureFetch::addToNetworkQueue(LLTextureFetchWorker* worker)
{
    LL_PROFILE_ZONE_SCOPED;
    lockQueue();                                                        // +Mfq
    bool in_request_map = (mRequestMap.find(worker->mID) != mRequestMap.end()) ;
    unlockQueue();                                                      // -Mfq

    LLMutexLock lock(&mNetworkQueueMutex);                              // +Mfnq
    if (in_request_map)
    {
        // only add to the queue if in the request map
        // i.e. a delete has not been requested
        mNetworkQueue.insert(worker->mID);
    }
    for (cancel_queue_t::iterator iter1 = mCancelQueue.begin();
         iter1 != mCancelQueue.end(); ++iter1)
    {
        iter1->second.erase(worker->mID);
    }
}                                                                       // -Mfnq

// Threads:  T*
void LLTextureFetch::removeFromNetworkQueue(LLTextureFetchWorker* worker, bool cancel)
{
    LL_PROFILE_ZONE_SCOPED;
    LLMutexLock lock(&mNetworkQueueMutex);                              // +Mfnq
    size_t erased = mNetworkQueue.erase(worker->mID);
    if (cancel && erased > 0)
    {
        mCancelQueue[worker->mHost].insert(worker->mID);
    }
}                                                                       // -Mfnq
// </FS:Ansariel>

// Threads:  T*
//
// protected
void LLTextureFetch::addToHTTPQueue(const LLUUID& id)
{
    LL_PROFILE_ZONE_SCOPED;
    LLMutexLock lock(&mNetworkQueueMutex);                              // +Mfnq
    mHTTPTextureQueue.insert(id);
    mTotalHTTPRequests++;
}                                                                       // -Mfnq

// Threads:  T*
void LLTextureFetch::removeFromHTTPQueue(const LLUUID& id, S32Bytes received_size)
{
    LL_PROFILE_ZONE_SCOPED;
    LLMutexLock lock(&mNetworkQueueMutex);                              // +Mfnq
    mHTTPTextureQueue.erase(id);
    mHTTPTextureBits += received_size; // Approximate - does not include header bits
}                                                                       // -Mfnq

// NB:  If you change deleteRequest() you should probably make
// parallel changes in removeRequest().  They're functionally
// identical with only argument variations.
//
// Threads:  T*
void LLTextureFetch::deleteRequest(const LLUUID& id, bool cancel)
{
    LL_PROFILE_ZONE_SCOPED;
    lockQueue();                                                        // +Mfq
    LLTextureFetchWorker* worker = getWorkerAfterLock(id);
    if (worker)
    {
        size_t erased_1 = mRequestMap.erase(worker->mID);
        unlockQueue();                                                  // -Mfq

        llassert_always(erased_1 > 0) ;
        removeFromNetworkQueue(worker, cancel); // <FS:Ansariel> OpenSim compatibility
        llassert_always(!(worker->getFlags(LLWorkerClass::WCF_DELETE_REQUESTED))) ;

        worker->scheduleDelete();
    }
    else
    {
        unlockQueue();                                                  // -Mfq
    }
}

// NB:  If you change removeRequest() you should probably make
// parallel changes in deleteRequest().  They're functionally
// identical with only argument variations.
//
// Threads:  T*
void LLTextureFetch::removeRequest(LLTextureFetchWorker* worker, bool cancel)
{
    LL_PROFILE_ZONE_SCOPED;
    if(!worker)
    {
        return;
    }

    lockQueue();                                                        // +Mfq
    size_t erased_1 = mRequestMap.erase(worker->mID);
    unlockQueue();                                                      // -Mfq

    llassert_always(erased_1 > 0) ;
    removeFromNetworkQueue(worker, cancel); // <FS:Ansariel> OpenSim compatibility
    llassert_always(!(worker->getFlags(LLWorkerClass::WCF_DELETE_REQUESTED))) ;

    worker->scheduleDelete();
}

void LLTextureFetch::deleteAllRequests()
{
    while(1)
    {
        lockQueue();
        if(mRequestMap.empty())
        {
            unlockQueue() ;
            break;
        }

        LLTextureFetchWorker* worker = mRequestMap.begin()->second;
        unlockQueue() ;

        removeRequest(worker, true);
    }
}

// Threads:  T*
S32 LLTextureFetch::getNumRequests()
{
    lockQueue();                                                        // +Mfq
    S32 size = (S32)mRequestMap.size();
    unlockQueue();                                                      // -Mfq

    return size;
}

// Threads:  T*
S32 LLTextureFetch::getNumHTTPRequests()
{
    mNetworkQueueMutex.lock();                                          // +Mfq
    S32 size = (S32)mHTTPTextureQueue.size();
    mNetworkQueueMutex.unlock();                                        // -Mfq

    return size;
}

// Threads:  T*
U32 LLTextureFetch::getTotalNumHTTPRequests()
{
    mNetworkQueueMutex.lock();                                          // +Mfq
    U32 size = mTotalHTTPRequests;
    mNetworkQueueMutex.unlock();                                        // -Mfq

    return size;
}

// call lockQueue() first!
// Threads:  T*
// Locks:  Mfq
LLTextureFetchWorker* LLTextureFetch::getWorkerAfterLock(const LLUUID& id)
{
    LL_PROFILE_ZONE_SCOPED;
    LLTextureFetchWorker* res = NULL;
    map_t::iterator iter = mRequestMap.find(id);
    if (iter != mRequestMap.end())
    {
        res = iter->second;
    }
    return res;
}

// Threads:  T*
LLTextureFetchWorker* LLTextureFetch::getWorker(const LLUUID& id)
{
    LLMutexLock lock(&mQueueMutex);                                     // +Mfq

    return getWorkerAfterLock(id);
}                                                                       // -Mfq


// Threads:  T*
bool LLTextureFetch::getRequestFinished(const LLUUID& id, S32& discard_level, S32& worker_state,
                                        LLPointer<LLImageRaw>& raw, LLPointer<LLImageRaw>& aux,
                                        LLCore::HttpStatus& last_http_get_status)
{
    LL_PROFILE_ZONE_SCOPED;
    bool res = false;
    LLTextureFetchWorker* worker = getWorker(id);
    if (worker)
    {
        VksTexFetchWorkerInfo info;
        worker->getInfo(info);
        worker_state = info.state;
        if (worker->wasAborted())
        {
            res = true;
        }
        else if (!worker->haveWork())
        {
            // Should only happen if we set mDebugPause...
            if (!mDebugPause)
            {
//              LL_WARNS(LOG_TXT) << "Adding work for inactive worker: " << id << LL_ENDL;
                worker->addWork(0);
            }
        }
        else if (worker->checkWork())
        {
            F32 decode_time;
            F32 fetch_time;
            F32 cache_read_time;
            F32 cache_write_time;
            S32 file_size;
            std::map<S32, F32> logged_state_timers;
            F32 skipped_states_time;
            worker->lockWorkMutex();                                    // +Mw
            worker->getInfo(info);
            last_http_get_status = worker->mGetStatus;
            discard_level = info.decoded_discard;
            raw = worker->mRawImage;
            aux = worker->mAuxImage;

            decode_time = info.decode_time;
            fetch_time = info.fetch_time;
            cache_read_time = info.cache_read_time;
            cache_write_time = info.cache_write_time;
            file_size = info.file_size;
            for (S32 state : LOGGED_STATES)
            {
                logged_state_timers[state] = info.state_timers[state];
            }
            skipped_states_time = info.skipped_states_time;
            vks_tfetch_worker_reset_timers(worker->mMachine);
            res = true;
            LL_DEBUGS(LOG_TXT) << id << ": Request Finished. State: " << info.state << " Discard: " << discard_level << LL_ENDL;
            worker->unlockWorkMutex();                                  // -Mw

            sample(sTexDecodeLatency, decode_time);
            sample(sTexFetchLatency, fetch_time);
            sample(sCacheReadLatency, cache_read_time);
            sample(sCacheWriteLatency, cache_write_time);

            static LLCachedControl<F32> min_time_to_log(gSavedSettings, "TextureFetchMinTimeToLog", 2.f);
            if (fetch_time > min_time_to_log)
            {
                //LL_INFOS() << "fetch_time: " << fetch_time << " cache_read_time: " << cache_read_time << " decode_time: " << decode_time << " cache_write_time: " << cache_write_time << LL_ENDL;

                LLTextureFetchTester* tester = (LLTextureFetchTester*)LLMetricPerformanceTesterBasic::getTester(sTesterName);
                if (tester)
                {
                    tester->updateStats(logged_state_timers, fetch_time, skipped_states_time, file_size) ;
                }
            }
        }
        else
        {
            worker->lockWorkMutex();                                    // +Mw
            worker->getInfo(info);
            if ((info.decoded_discard >= 0) &&
                (info.decoded_discard < discard_level || discard_level < 0) &&
                (info.state >= LLTextureFetchWorker::WAIT_ON_WRITE))
            {
                // Not finished, but data is ready
                discard_level = info.decoded_discard;
                raw = worker->mRawImage;
                aux = worker->mAuxImage;
            }
            worker->unlockWorkMutex();                                  // -Mw
        }
    }
    else
    {
        worker_state = 0;
        res = true;
    }
    return res;
}

// Threads:  T*
bool LLTextureFetch::updateRequestPriority(const LLUUID& id, F32 priority)
{
    LL_PROFILE_ZONE_SCOPED;
    mRequestQueue.tryPost([=, this]()
        {
            LLTextureFetchWorker* worker = getWorker(id);
            if (worker)
            {
                worker->lockWorkMutex();                                        // +Mw
                worker->setImagePriority(priority);
                worker->unlockWorkMutex();                                      // -Mw
            }
        });

    return true;
}

// Replicates and expands upon the base class's
// getPending() implementation.  getPending() and
// runCondition() replicate one another's logic to
// an extent and are sometimes used for the same
// function (deciding whether or not to sleep/pause
// a thread).  So the implementations need to stay
// in step, at least until this can be refactored and
// the redundancy eliminated.
//
// Threads:  T*

//virtual
size_t LLTextureFetch::getPending()
{
    LL_PROFILE_ZONE_SCOPED;
    size_t res;
    lockData();                                                         // +Ct
    {
        LLMutexLock lock(&mQueueMutex);                                 // +Mfq

        res = mRequestQueue.size();
        res += mCommands.size();
    }                                                                   // -Mfq
    unlockData();                                                       // -Ct
    return res;
}

// Locks:  Ct
// virtual
bool LLTextureFetch::runCondition()
{
    // Caller is holding the lock on LLThread's condition variable.

    // LLQueuedThread, unlike its base class LLThread, makes this a
    // private method which is unfortunate.  I want to use it directly
    // but I'm going to have to re-implement the logic here (or change
    // declarations, which I don't want to do right now).
    //
    // Changes here may need to be reflected in getPending().

    bool have_no_commands(false);
    {
        LLMutexLock lock(&mQueueMutex);                                 // +Mfq

        have_no_commands = mCommands.empty();
    }                                                                   // -Mfq

    return ! (have_no_commands
              && (mRequestQueue.size() == 0 && mIdleThread));       // From base class
}

//////////////////////////////////////////////////////////////////////////////

// Threads:  Ttf
void LLTextureFetch::commonUpdate()
{
    LL_PROFILE_ZONE_SCOPED;
    // Update low/high water levels based on pipelining.  We pick
    // up setting eventually, so the semaphore/request level can
    // fall outside the [0..HIGH_WATER] range.  Expect that.
    vks_tfetch_fetcher_set_pipelined(mMachine, LLAppViewer::instance()->getAppCoreHttp().isPipelined(LLAppCoreHttp::AP_TEXTURE) ? 1 : 0);

    // Release waiters
    releaseHttpWaiters();

    // Run a cross-thread command, if any.
    cmdDoWork();

    // Deliver all completion notifications
    LLCore::HttpStatus status = mHttpRequest->update(0);
    if (! status)
    {
        LL_INFOS_ONCE(LOG_TXT) << "Problem during HTTP servicing.  Reason:  "
                               << status.toString()
                               << LL_ENDL;
    }
}


// Threads:  Tmain

//virtual
size_t LLTextureFetch::update(F32 max_time_ms)
{
    LL_PROFILE_ZONE_SCOPED;

    {
        mNetworkQueueMutex.lock();                                      // +Mfnq
        mMaxBandwidth = LLViewerThrottle::getMaxBandwidthKbps();

        add(LLStatViewer::TEXTURE_NETWORK_DATA_RECEIVED, mHTTPTextureBits);
        mHTTPTextureBits = (U32Bits)0;

        mNetworkQueueMutex.unlock();                                    // -Mfnq
    }

    // The debugging aids the texture console sets.
    vks_tfetch_fetcher_set_debug(mMachine, mDebugPause ? 1 : 0, mDebugID.mData);

    size_t res = LLWorkerThread::update(max_time_ms);

#ifdef OPENSIM
    // <FS:Ansariel> OpenSim compatibility
    if (!mDebugPause)
    {
        // this is the startup state when send_complete_agent_movement() message is sent.
        // Before this, the RequestImages message sent by sendRequestListToSimulators
        // won't work so don't bother trying
        if (LLStartUp::getStartupState() > STATE_AGENT_SEND)
        {
            sendRequestListToSimulators();
        }
    }
    // </FS:Ansariel>
#endif

    if (!mThreaded)
    {
        commonUpdate();
    }

    return res;
}

// called in the MAIN thread after the TextureCacheThread shuts down.
//
// Threads:  Tmain
void LLTextureFetch::shutDownTextureCacheThread()
{
    if(mTextureCache)
    {
        llassert_always(mTextureCache->isQuitting() || mTextureCache->isStopped()) ;
        mTextureCache = NULL ;
    }
}

// Threads:  Ttf
void LLTextureFetch::startThread()
{
    mTextureInfo.startRecording();
}

// Threads:  Ttf
void LLTextureFetch::endThread()
{
    U32 cache_reads = 0, cache_writes = 0, res_waits = 0;
    getStateStats(&cache_reads, &cache_writes, &res_waits);
    LL_INFOS(LOG_TXT) << "CacheReads:  " << cache_reads
                      << ", CacheWrites:  " << cache_writes
                      << ", ResWaits:  " << res_waits
                      << ", TotalHTTPReq:  " << getTotalNumHTTPRequests()
                      << LL_ENDL;

    mTextureInfo.stopRecording();
}

// Threads:  Ttf
void LLTextureFetch::threadedUpdate()
{
    LL_PROFILE_ZONE_SCOPED;
    llassert_always(mHttpRequest);

    commonUpdate();
}

#ifdef OPENSIM
// <FS:Ansariel> OpenSim compatibility
//////////////////////////////////////////////////////////////////////////////

// Threads:  Tmain
void LLTextureFetch::sendRequestListToSimulators()
{
    LL_PROFILE_ZONE_SCOPED;
    // All requests
    const F32 REQUEST_DELTA_TIME = 0.10f; // 10 fps

    // Sim requests
    const S32 IMAGES_PER_REQUEST = 50;
    const F32 SIM_LAZY_FLUSH_TIMEOUT = 10.0f; // temp
    const F32 MIN_REQUEST_TIME = 1.0f;
    const F32 MIN_DELTA_PRIORITY = 1000.f;

    // Periodically, gather the list of textures that need data from the network
    // And send the requests out to the simulators
    static LLFrameTimer timer;
    if (timer.getElapsedTimeF32() < REQUEST_DELTA_TIME)
    {
        return;
    }
    timer.reset();

    // Send requests
    typedef std::set<LLTextureFetchWorker*,LLTextureFetchWorker::Compare> request_list_t;
    typedef std::map< LLHost, request_list_t > work_request_map_t;
    work_request_map_t requests;
    {
        LLMutexLock lock2(&mNetworkQueueMutex);                         // +Mfnq
        for (queue_t::iterator iter = mNetworkQueue.begin(); iter != mNetworkQueue.end(); )
        {
            queue_t::iterator curiter = iter++;
            LLTextureFetchWorker* req = getWorker(*curiter);
            if (!req)
            {
                mNetworkQueue.erase(curiter);
                continue; // paranoia
            }
            VksTexFetchWorkerInfo info;
            req->getInfo(info);
            if ((info.state != LLTextureFetchWorker::LOAD_FROM_NETWORK) &&
                (info.state != LLTextureFetchWorker::LOAD_FROM_SIMULATOR))
            {
                // We already received our URL, remove from the queue
                LL_WARNS(LOG_TXT) << "Worker: " << req->mID << " in mNetworkQueue but in wrong state: " << info.state << LL_ENDL;
                mNetworkQueue.erase(curiter);
                continue;
            }
            if (req->mID == mDebugID)
            {
                mDebugCount++; // for setting breakpoints
            }
            if (info.sent_request == 2 /* SENT_SIM */ &&
                info.total_packets > 0 &&
                info.last_packet >= info.total_packets-1)
            {
                // We have all the packets... make sure this is high priority
                continue;
            }
            F32 elapsed = vks_tfetch_worker_requested_delta_secs(req->mMachine);
            {
                F32 delta_priority = llabs(info.requested_priority - info.image_priority);
                if ((info.sim_requested_discard != info.desired_discard) ||
                    (delta_priority > MIN_DELTA_PRIORITY && elapsed >= MIN_REQUEST_TIME) ||
                    (elapsed >= SIM_LAZY_FLUSH_TIMEOUT))
                {
                    req->mSortPriority = info.image_priority;
                    requests[req->mHost].insert(req);
                }
            }
        }
    }                                                                   // -Mfnq

    for (work_request_map_t::iterator iter1 = requests.begin();
         iter1 != requests.end(); ++iter1)
    {
        LLHost host = iter1->first;
        // invalid host = use agent host
        if (host.isInvalid())
        {
            host = gAgent.getRegionHost();
        }

        S32 sim_request_count = 0;

        for (request_list_t::iterator iter2 = iter1->second.begin();
             iter2 != iter1->second.end(); ++iter2)
        {
            LLTextureFetchWorker* req = *iter2;
            if (gMessageSystem)
            {
                VksTexFetchWorkerInfo info;
                req->getInfo(info);
                if (info.sent_request != 2 /* SENT_SIM */)
                {
                    // Initialize packet data based on data read from cache
                    req->lockWorkMutex();                               // +Mw
                    req->setupPacketData();
                    req->getInfo(info);
                    req->unlockWorkMutex();                             // -Mw
                }
                if (0 == sim_request_count)
                {
                    gMessageSystem->newMessageFast(_PREHASH_RequestImage);
                    gMessageSystem->nextBlockFast(_PREHASH_AgentData);
                    gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
                    gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
                }
                S32 packet = info.last_packet + 1;
                //LL_INFOS() << req->mID << ": " << req->mImagePriority << LL_ENDL;
                gMessageSystem->nextBlockFast(_PREHASH_RequestImage);
                gMessageSystem->addUUIDFast(_PREHASH_Image, req->mID);
                gMessageSystem->addS8Fast(_PREHASH_DiscardLevel, (S8)info.desired_discard);
                gMessageSystem->addF32Fast(_PREHASH_DownloadPriority, info.image_priority);
                gMessageSystem->addU32Fast(_PREHASH_Packet, packet);
                gMessageSystem->addU8Fast(_PREHASH_Type, req->mType);
//              LL_INFOS(LOG_TXT) << "IMAGE REQUEST: " << req->mID << " Discard: " << req->mDesiredDiscard
//                      << " Packet: " << packet << " Priority: " << req->mImagePriority << LL_ENDL;

                static LLCachedControl<bool> log_to_viewer_log(gSavedSettings,"LogTextureDownloadsToViewerLog", false);
                static LLCachedControl<bool> log_to_sim(gSavedSettings,"LogTextureDownloadsToSimulator", false);
                if (log_to_viewer_log || log_to_sim)
                {
                    mTextureInfo.setRequestStartTime(req->mID, LLTimer::getTotalTime());
                    mTextureInfo.setRequestOffset(req->mID, 0);
                    mTextureInfo.setRequestSize(req->mID, 0);
                    mTextureInfo.setRequestType(req->mID, LLTextureInfoDetails::REQUEST_TYPE_UDP);
                }

                req->lockWorkMutex();                                   // +Mw
                vks_tfetch_worker_mark_sent_sim(req->mMachine);
                req->unlockWorkMutex();                                 // -Mw
                sim_request_count++;
                if (sim_request_count >= IMAGES_PER_REQUEST)
                {
//                  LL_INFOS(LOG_TXT) << "REQUESTING " << sim_request_count << " IMAGES FROM HOST: " << host.getIPString() << LL_ENDL;

                    gMessageSystem->sendSemiReliable(host, NULL, NULL);
                    sim_request_count = 0;
                }
            }
        }
        if (gMessageSystem && sim_request_count > 0 && sim_request_count < IMAGES_PER_REQUEST)
        {
//          LL_INFOS(LOG_TXT) << "REQUESTING " << sim_request_count << " IMAGES FROM HOST: " << host.getIPString() << LL_ENDL;
            gMessageSystem->sendSemiReliable(host, NULL, NULL);
            sim_request_count = 0;
        }
    }

    // Send cancelations
    {
        LLMutexLock lock2(&mNetworkQueueMutex);                         // +Mfnq
        if (gMessageSystem && !mCancelQueue.empty())
        {
            for (cancel_queue_t::iterator iter1 = mCancelQueue.begin();
                 iter1 != mCancelQueue.end(); ++iter1)
            {
                LLHost host = iter1->first;
                if (host.isInvalid())
                {
                    host = gAgent.getRegionHost();
                }
                S32 request_count = 0;
                for (queue_t::iterator iter2 = iter1->second.begin();
                     iter2 != iter1->second.end(); ++iter2)
                {
                    if (0 == request_count)
                    {
                        gMessageSystem->newMessageFast(_PREHASH_RequestImage);
                        gMessageSystem->nextBlockFast(_PREHASH_AgentData);
                        gMessageSystem->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
                        gMessageSystem->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
                    }
                    gMessageSystem->nextBlockFast(_PREHASH_RequestImage);
                    gMessageSystem->addUUIDFast(_PREHASH_Image, *iter2);
                    gMessageSystem->addS8Fast(_PREHASH_DiscardLevel, -1);
                    gMessageSystem->addF32Fast(_PREHASH_DownloadPriority, 0);
                    gMessageSystem->addU32Fast(_PREHASH_Packet, 0);
                    gMessageSystem->addU8Fast(_PREHASH_Type, 0);
//              LL_INFOS(LOG_TXT) << "CANCELING IMAGE REQUEST: " << (*iter2) << LL_ENDL;

                    request_count++;
                    if (request_count >= IMAGES_PER_REQUEST)
                    {
                        gMessageSystem->sendSemiReliable(host, NULL, NULL);
                        request_count = 0;
                    }
                }
                if (request_count > 0 && request_count < IMAGES_PER_REQUEST)
                {
                    gMessageSystem->sendSemiReliable(host, NULL, NULL);
                }
            }
            mCancelQueue.clear();
        }
    }                                                                   // -Mfnq
}

//////////////////////////////////////////////////////////////////////////////

// Threads:  T*
// Locks:  Mw
bool LLTextureFetchWorker::insertPacket(S32 index, U8* data, S32 size)
{
    LL_PROFILE_ZONE_SCOPED;
    if (!vks_tfetch_worker_insert_packet(mMachine, index, (uint32_t)size))
    {
        // out of range, badly sized or a duplicate
        return false;
    }
    if (index >= (S32)mPackets.size())
    {
        mPackets.resize(index+1, (PacketData*)NULL); // initializes v to NULL pointers
    }
    mPackets[index] = new PacketData(data, size);
    return true;
}
// </FS:Ansariel>
#endif

LLViewerRegion* LLTextureFetchWorker::getRegion()
{
    LLViewerRegion* region = NULL;
    if (mHost.isInvalid())
    {
        region = gAgent.getRegion();
    }
    else if (LLWorld::instanceExists())
    {
        region = LLWorld::getInstance()->getRegion(mHost);
    }
    return region;
}

// <FS:Ansariel> OpenSim compatibility
// Threads:  T*
bool LLTextureFetch::isFromLocalCache(const LLUUID& id)
{
    bool from_cache = false ;

    LLTextureFetchWorker* worker = getWorker(id);
    if (worker)
    {
        worker->lockWorkMutex();                                        // +Mw
        VksTexFetchWorkerInfo info;
        worker->getInfo(info);
        from_cache = info.in_local_cache != 0;
        worker->unlockWorkMutex();                                      // -Mw
    }

    return from_cache ;
}

S32 LLTextureFetch::getFetchState(const LLUUID& id)
{
    S32 state = LLTextureFetchWorker::INVALID;
    LLTextureFetchWorker* worker = getWorker(id);
    if (worker && worker->haveWork())
    {
        state = worker->getState();
    }

    return state;
}

// Threads:  T*
S32 LLTextureFetch::getFetchState(const LLUUID& id, F32& data_progress_p, F32& requested_priority_p,
                                  U32& fetch_priority_p, F32& fetch_dtime_p, F32& request_dtime_p, bool& can_use_http)
{
    LL_PROFILE_ZONE_SCOPED;
    S32 state = LLTextureFetchWorker::INVALID;
    F32 data_progress = 0.0f;
    F32 requested_priority = 0.0f;
    F32 fetch_dtime = 999999.f;
    F32 request_dtime = 999999.f;
    U32 fetch_priority = 0;

    LLTextureFetchWorker* worker = getWorker(id);
    if (worker && worker->haveWork())
    {
        worker->lockWorkMutex();                                        // +Mw
        VksTexFetchWorkerInfo info;
        worker->getInfo(info);
        state = info.state;
        fetch_dtime = vks_tfetch_worker_fetch_delta_secs(worker->mMachine);
        request_dtime = vks_tfetch_worker_requested_delta_secs(worker->mMachine);
        const bool formatted = worker->mFormattedImage.notNull();
        data_progress = vks_tfetch_worker_data_progress(worker->mMachine, formatted ? 1 : 0, formatted ? worker->mFormattedImage->getDataSize() : 0);
        requested_priority = vks_tfetch_worker_requested_priority_for_state(worker->mMachine);
        fetch_priority = (U32)info.image_priority;
        can_use_http = info.can_use_http != 0;
        worker->unlockWorkMutex();                                      // -Mw
    }
    data_progress_p = data_progress;
    requested_priority_p = requested_priority;
    fetch_priority_p = fetch_priority;
    fetch_dtime_p = fetch_dtime;
    request_dtime_p = request_dtime;
    return state;
}

// Threads:  T*
S32 LLTextureFetch::getLastFetchState(const LLUUID& id, S32& requested_discard, S32& decoded_discard, bool& decoded)
{
    LL_PROFILE_ZONE_SCOPED;
    S32 state = LLTextureFetchWorker::INVALID;

    LLTextureFetchWorker* worker = getWorker(id);
    if (worker) // Don't check haveWork, intent is to get whatever is in the worker
    {
        worker->lockWorkMutex();                                        // +Mw
        VksTexFetchWorkerInfo info;
        worker->getInfo(info);
        state = info.state;
        requested_discard = info.desired_discard;
        decoded_discard = info.decoded_discard;
        decoded = info.decoded != 0;
        worker->unlockWorkMutex();                                      // -Mw
    }
    return state;
}

// Threads:  T*
S32 LLTextureFetch::getLastRawImage(const LLUUID& id,
    LLPointer<LLImageRaw>& raw, LLPointer<LLImageRaw>& aux)
{
    LL_PROFILE_ZONE_SCOPED;
    S32 decoded_discard = -1;
    LLTextureFetchWorker* worker = getWorker(id);
    if (worker && !worker->haveWork())
    {
            worker->lockWorkMutex();                                    // +Mw
            VksTexFetchWorkerInfo info;
            worker->getInfo(info);
            if (info.decoded_discard >= 0)
            {
                raw = worker->mRawImage;
                aux = worker->mAuxImage;
                decoded_discard = info.decoded_discard;
            }
            worker->unlockWorkMutex();                                  // -Mw
    }
    return decoded_discard;
}

// <FS:Ansariel> OpenSim compatibility
// Threads:  T*
bool LLTextureFetch::receiveImageHeader(const LLHost& host, const LLUUID& id, U8 codec, U16 packets, U32 totalbytes,
                                        U16 data_size, U8* data)
{
    LL_PROFILE_ZONE_SCOPED;
#ifdef OPENSIM
    LLTextureFetchWorker* worker = getWorker(id);
    bool res = true;

    ++mPacketCount;

    VksTexFetchWorkerInfo info;
    if (worker)
    {
        worker->getInfo(info);
    }
    if (!worker)
    {
//      LL_WARNS(LOG_TXT) << "Received header for non active worker: " << id << LL_ENDL;
        res = false;
    }
    else if (info.state != LLTextureFetchWorker::LOAD_FROM_NETWORK ||
             info.sent_request != 2 /* SENT_SIM */)
    {
//      LL_WARNS(LOG_TXT) << "receiveImageHeader for worker: " << id
//              << " in state: " << LLTextureFetchWorker::sStateDescs[worker->mState]
//              << " sent: " << worker->mSentRequest << LL_ENDL;
        res = false;
    }
    else if (info.last_packet != -1)
    {
        // check to see if we've gotten this packet before
//      LL_WARNS(LOG_TXT) << "Received duplicate header for: " << id << LL_ENDL;
        res = false;
    }
    else if (!data_size)
    {
//      LL_WARNS(LOG_TXT) << "Img: " << id << ":" << " Empty Image Header" << LL_ENDL;
        res = false;
    }
    if (!res)
    {
        mNetworkQueueMutex.lock();                                      // +Mfnq
        ++mBadPacketCount;
        mCancelQueue[host].insert(id);
        mNetworkQueueMutex.unlock();                                    // -Mfnq
        return false;
    }

    LLViewerStatsRecorder::instance().textureFetch();

    worker->lockWorkMutex();


    //  Copy header data into image object
    vks_tfetch_worker_receive_header(worker->mMachine, codec, packets, (S32)totalbytes);
    llassert_always(totalbytes > 0);
    llassert_always(data_size == FIRST_PACKET_SIZE || data_size == (S32)totalbytes);
    res = worker->insertPacket(0, data, data_size);
    worker->setState(LLTextureFetchWorker::LOAD_FROM_SIMULATOR);
    worker->unlockWorkMutex();                                          // -Mw
    return res;
#else
    // No UDP texture transfer without the OpenSim build.
    ++mPacketCount;
    mNetworkQueueMutex.lock();                                          // +Mfnq
    ++mBadPacketCount;
    mNetworkQueueMutex.unlock();                                        // -Mfnq
    return false;
#endif
}


// Threads:  T*
bool LLTextureFetch::receiveImagePacket(const LLHost& host, const LLUUID& id, U16 packet_num, U16 data_size, U8* data)
{
    LL_PROFILE_ZONE_SCOPED;
#ifdef OPENSIM
    LLTextureFetchWorker* worker = getWorker(id);
    bool res = true;

    ++mPacketCount;

    VksTexFetchWorkerInfo info;
    if (worker)
    {
        worker->getInfo(info);
    }
    if (!worker)
    {
//      LL_WARNS(LOG_TXT) << "Received packet " << packet_num << " for non active worker: " << id << LL_ENDL;
        res = false;
    }
    else if (info.last_packet == -1)
    {
//      LL_WARNS(LOG_TXT) << "Received packet " << packet_num << " before header for: " << id << LL_ENDL;
        res = false;
    }
    else if (!data_size)
    {
//      LL_WARNS(LOG_TXT) << "Img: " << id << ":" << " Empty Image Header" << LL_ENDL;
        res = false;
    }
    if (!res)
    {
        mNetworkQueueMutex.lock();                                      // +Mfnq
        ++mBadPacketCount;
        mCancelQueue[host].insert(id);
        mNetworkQueueMutex.unlock();                                    // -Mfnq
        return false;
    }

    LLViewerStatsRecorder::instance().textureFetch();

    worker->lockWorkMutex();


    res = worker->insertPacket(packet_num, data, data_size);
    worker->getInfo(info);

    if ((info.state == LLTextureFetchWorker::LOAD_FROM_SIMULATOR) ||
        (info.state == LLTextureFetchWorker::LOAD_FROM_NETWORK))
    {
        worker->setState(LLTextureFetchWorker::LOAD_FROM_SIMULATOR);
    }
    else
    {
//      LL_WARNS(LOG_TXT) << "receiveImagePacket " << packet_num << "/" << worker->mLastPacket << " for worker: " << id
//              << " in state: " << LLTextureFetchWorker::sStateDescs[worker->mState] << LL_ENDL;
        removeFromNetworkQueue(worker, true); // failsafe
    }

    if (packet_num >= (info.total_packets - 1))
    {
        static LLCachedControl<bool> log_to_viewer_log(gSavedSettings,"LogTextureDownloadsToViewerLog", false);
        static LLCachedControl<bool> log_to_sim(gSavedSettings,"LogTextureDownloadsToSimulator", false);

        if (log_to_viewer_log || log_to_sim)
        {
            U64Microseconds timeNow = LLTimer::getTotalTime();
            mTextureInfoMainThread.setRequestSize(id, info.file_size);
            mTextureInfoMainThread.setRequestCompleteTimeAndLog(id, timeNow);
        }
    }
    worker->unlockWorkMutex();                                          // -Mw

    return res;
#else
    // No UDP texture transfer without the OpenSim build.
    ++mPacketCount;
    mNetworkQueueMutex.lock();                                          // +Mfnq
    ++mBadPacketCount;
    mNetworkQueueMutex.unlock();                                        // -Mfnq
    return false;
#endif
}
// </FS:Ansariel>

//////////////////////////////////////////////////////////////////////////////

// Threads:  T*
void LLTextureFetch::dump()
{
    LL_INFOS(LOG_TXT) << "LLTextureFetch ACTIVE_HTTP:" << LL_ENDL;
    for (queue_t::const_iterator iter(mHTTPTextureQueue.begin());
         mHTTPTextureQueue.end() != iter;
         ++iter)
    {
        LL_INFOS(LOG_TXT) << " ID: " << (*iter) << LL_ENDL;
    }

    LL_INFOS(LOG_TXT) << "LLTextureFetch WAIT_HTTP_RESOURCE: " << getHttpWaitersCount() << " waiters" << LL_ENDL;
}

//////////////////////////////////////////////////////////////////////////////

// HTTP Resource Waiting Methods (the semaphore and the waiter set live in
// the machine, under its lock)

// Threads:  Ttf
void LLTextureFetch::addHttpWaiter(const LLUUID & tid)
{
    vks_tfetch_fetcher_add_waiter(mMachine, tid.mData);
}

// Threads:  Ttf
void LLTextureFetch::removeHttpWaiter(const LLUUID & tid)
{
    vks_tfetch_fetcher_remove_waiter(mMachine, tid.mData);
}

// Threads:  T*
bool LLTextureFetch::isHttpWaiter(const LLUUID & tid)
{
    return vks_tfetch_fetcher_is_waiter(mMachine, tid.mData) != 0;
}

// Release as many requests as permitted from the WAIT_HTTP_RESOURCE2
// state to the SEND_HTTP_REQ state based on their current priority.
//
// This data structures and code associated with this looks a bit
// indirect and naive but it's done in the name of safety.  An
// ordered container may become invalid from time to time due to
// priority changes caused by actions in other threads.  State itself
// could also suffer the same fate with canceled operations.  Even
// done this way, I'm not fully trusting we're truly safe.  This
// module is due for a major refactoring and we'll deal with it then.
//
// Threads:  Ttf
// Locks:  -Mw (must not hold any worker when called)
void LLTextureFetch::releaseHttpWaiters()
{
    LL_PROFILE_ZONE_SCOPED;
    // Quickly make a copy of all the LLUIDs.  Get off the
    // mutex as early as possible.
    const size_t cap = (size_t)llmax(getHttpWaitersCount(), 0) + 16;
    std::vector<U8> ids(cap * 16);
    int32_t needed = 0;
    const size_t count = vks_tfetch_fetcher_waiters_to_release(mMachine, &needed, ids.data(), cap);
    if (needed <= 0 || count == 0)
    {
        return;
    }

    // Now lookup the UUUIDs to find valid requests and sort
    // them in priority order, highest to lowest.  We're going
    // to modify priority later as a side-effect of releasing
    // these objects.  That, in turn, would violate the partial
    // ordering assumption of std::set, std::map, etc. so we
    // don't use those containers.  We use a vector and an explicit
    // sort to keep the containers valid later.
    typedef std::vector<LLTextureFetchWorker *> worker_list_t;
    worker_list_t tids2;
    std::vector<F32> priorities;

    tids2.reserve(count);
    priorities.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        LLUUID tid;
        memcpy(tid.mData, ids.data() + i * 16, 16);
        LLTextureFetchWorker * worker(getWorker(tid));
        if (worker)
        {
            tids2.push_back(worker);
            priorities.push_back(worker->getImagePriority());
        }
        else
        {
            // If worker isn't found, this should be due to a request
            // for deletion.  We signal our recognition that this
            // uuid shouldn't be used for resource waiting anymore by
            // erasing it from the resource waiter list.  That allows
            // deleteOK to do final deletion on the worker.
            removeHttpWaiter(tid);
        }
    }

    // Sort into priority order, if necessary and only as much as needed
    std::vector<size_t> order(tids2.size());
    vks_tfetch_release_order(priorities.data(), priorities.size(), needed, order.data());

    // Release workers up to the high water mark.  Since we aren't
    // holding any locks at this point, we can be in competition
    // with other callers.  Do defensive things like getting
    // refreshed counts of requests and checking if someone else
    // has moved any worker state around....
    for (size_t i = 0; i < order.size(); ++i)
    {
        LLTextureFetchWorker * worker(tids2[order[i]]);

        worker->lockWorkMutex();                                        // +Mw
        const int32_t release = vks_tfetch_worker_try_release(worker->mMachine, mMachine);
        if (release == 1)
        {
            // Not in expected state, remove it, try the next one
            const S32 state = worker->getState();
            worker->unlockWorkMutex();                                  // -Mw
            LL_WARNS(LOG_TXT) << "Resource-waited texture " << worker->mID
                              << " in unexpected state:  " << state
                              << ".  Removing from wait list."
                              << LL_ENDL;
            removeHttpWaiter(worker->mID);
            continue;
        }
        if (release == 2)
        {
            // Out of active slots, quit
            worker->unlockWorkMutex();                                  // -Mw
            break;
        }
        worker->unlockWorkMutex();                                      // -Mw

        removeHttpWaiter(worker->mID);
    }
}

// Threads:  T*
void LLTextureFetch::cancelHttpWaiters()
{
    vks_tfetch_fetcher_cancel_waiters(mMachine);
}

// Threads:  T*
int LLTextureFetch::getHttpWaitersCount()
{
    return vks_tfetch_fetcher_waiters_count(mMachine);
}


// Threads:  T*
void LLTextureFetch::updateStateStats(U32 cache_read, U32 cache_write, U32 res_wait)
{
    vks_tfetch_fetcher_update_state_stats(mMachine, cache_read, cache_write, res_wait);
}


// Threads:  T*
void LLTextureFetch::getStateStats(U32 * cache_read, U32 * cache_write, U32 * res_wait)
{
    vks_tfetch_fetcher_get_state_stats(mMachine, cache_read, cache_write, res_wait);
}

//////////////////////////////////////////////////////////////////////////////

// cross-thread command methods

// Threads:  T*
void LLTextureFetch::commandSetRegion(U64 region_handle)
{
    TFReqSetRegion * req = new TFReqSetRegion(region_handle);

    cmdEnqueue(req);
}

// Threads:  T*
void LLTextureFetch::commandSendMetrics(const std::string & caps_url,
                                        const LLUUID & session_id,
                                        const LLUUID & agent_id,
                                        LLSD& stats_sd)
{
    TFReqSendMetrics * req = new TFReqSendMetrics(caps_url, session_id, agent_id, stats_sd);

    cmdEnqueue(req);
}

// Threads:  T*
void LLTextureFetch::commandDataBreak()
{
    // The pedantically correct way to implement this is to create a command
    // request object in the above fashion and enqueue it.  However, this is
    // simple data of an advisorial not operational nature and this case
    // of shared-write access is tolerable.

    LLTextureFetch::svMetricsDataBreak = true;
}

// Threads:  T*
void LLTextureFetch::cmdEnqueue(TFRequest * req)
{
    LL_PROFILE_ZONE_SCOPED;
    lockQueue();                                                        // +Mfq
    mCommands.push_back(req);
    unlockQueue();                                                      // -Mfq

    unpause();
}

// Threads:  T*
LLTextureFetch::TFRequest * LLTextureFetch::cmdDequeue()
{
    LL_PROFILE_ZONE_SCOPED;
    TFRequest * ret = 0;

    lockQueue();                                                        // +Mfq
    if (! mCommands.empty())
    {
        ret = mCommands.front();
        mCommands.erase(mCommands.begin());
    }
    unlockQueue();                                                      // -Mfq

    return ret;
}

// Threads:  Ttf
void LLTextureFetch::cmdDoWork()
{
    LL_PROFILE_ZONE_SCOPED;
    if (mDebugPause)
    {
        return;  // debug: don't do any work
    }

    TFRequest * req = cmdDequeue();
    if (req)
    {
        // One request per pass should really be enough for this.
        req->doWork(this);
        delete req;
    }
}

//////////////////////////////////////////////////////////////////////////////

// Private (anonymous) class methods implementing the command scheme.

namespace
{


// Example of a simple notification handler for metrics
// delivery notification.  Earlier versions of the code used
// a Responder that tried harder to detect delivery breaks
// but it really isn't that important.  If someone wants to
// revisit that effort, here is a place to start.
class AssetReportHandler : public LLCore::HttpHandler
{
public:

    // Threads:  Ttf
    virtual void onCompleted(LLCore::HttpHandle handle, LLCore::HttpResponse * response)
    {
        LLCore::HttpStatus status(response->getStatus());

        if (status)
        {
            LL_DEBUGS(LOG_TXT) << "Successfully delivered asset metrics to grid."
                               << LL_ENDL;
        }
        else
        {
            LL_WARNS(LOG_TXT) << "Error delivering asset metrics to grid.  Status:  "
                              << status.toTerseString()
                              << ", Reason:  " << status.toString() << LL_ENDL;
        }
    }
}; // end class AssetReportHandler

/**
 * Implements the 'Set Region' command.
 *
 * Thread:  Thread1 (TextureFetch)
 */
bool
TFReqSetRegion::doWork(LLTextureFetch *)
{
    LLViewerAssetStatsFF::set_region(mRegionHandle);

    return true;
}

TFReqSendMetrics::TFReqSendMetrics(const std::string & caps_url,
                                   const LLUUID & session_id,
                                   const LLUUID & agent_id,
                                   LLSD& stats_sd):
    LLTextureFetch::TFRequest(),
    mCapsURL(caps_url),
    mSessionID(session_id),
    mAgentID(agent_id),
    mStatsSD(stats_sd),
    mHandler(std::make_shared<AssetReportHandler>())
{}


TFReqSendMetrics::~TFReqSendMetrics()
{
}


/**
 * Implements the 'Send Metrics' command.  Takes over
 * ownership of the passed LLViewerAssetStats pointer.
 *
 * Thread:  Thread1 (TextureFetch)
 */
bool
TFReqSendMetrics::doWork(LLTextureFetch * fetcher)
{
    LL_PROFILE_ZONE_SCOPED;

    //if (! gViewerAssetStatsThread1)
    //  return true;

    static std::atomic<bool> reporting_started(false);
    static std::atomic<S32> report_sequence(0);

    // In mStatsSD, we have a copy we own of the LLSD representation
    // of the asset stats. Add some additional fields and ship it off.

    static const S32 metrics_data_version = 2;

    bool initial_report = !reporting_started.load();
    mStatsSD["session_id"] = mSessionID;
    mStatsSD["agent_id"] = mAgentID;
    mStatsSD["message"] = "ViewerAssetMetrics";
    mStatsSD["sequence"] = report_sequence.load();
    mStatsSD["initial"] = initial_report;
    mStatsSD["version"] = metrics_data_version;
    mStatsSD["break"] = static_cast<bool>(LLTextureFetch::svMetricsDataBreak);

    // Update sequence number
    if (S32_MAX == report_sequence.fetch_add(1))
    {
        report_sequence.store(0);
    }

    reporting_started.store(true);

    // Limit the size of the stats report if necessary.

    mStatsSD["truncated"] = truncate_viewer_metrics(10, mStatsSD);

    if (gSavedSettings.getBOOL("QAModeMetrics"))
    {
        dump_sequential_xml("metric_asset_stats",mStatsSD);
    }

    if (! mCapsURL.empty())
    {
        // Don't care about handle, this is a fire-and-forget operation.
        LLCoreHttpUtil::requestPostWithLLSD(&fetcher->getHttpRequest(),
                                            fetcher->getMetricsPolicyClass(),
                                            mCapsURL,
                                            mStatsSD,
                                            LLCore::HttpOptions::ptr_t(),
                                            fetcher->getMetricsHeaders(),
                                            mHandler);
        LLTextureFetch::svMetricsDataBreak = false;
    }
    else
    {
        LLTextureFetch::svMetricsDataBreak = true;
    }

    // In QA mode, Metrics submode, log the result for ease of testing
    if (fetcher->isQAMode())
    {
        LL_INFOS(LOG_TXT) << "ViewerAssetMetrics as submitted\n" << ll_pretty_print_sd(mStatsSD) << LL_ENDL;
    }

    return true;
}


bool
truncate_viewer_metrics(int max_regions, LLSD & metrics)
{
    static const LLSD::String reg_tag("regions");
    static const LLSD::String duration_tag("duration");

    LLSD & reg_map(metrics[reg_tag]);
    if (reg_map.size() <= max_regions)
    {
        return false;
    }

    // Build map of region hashes ordered by duration
    typedef std::multimap<LLSD::Real, int> reg_ordered_list_t;
    reg_ordered_list_t regions_by_duration;

    int ind(0);
    LLSD::array_const_iterator it_end(reg_map.endArray());
    for (LLSD::array_const_iterator it(reg_map.beginArray()); it_end != it; ++it, ++ind)
    {
        LLSD::Real duration = (*it)[duration_tag].asReal();
        regions_by_duration.insert(reg_ordered_list_t::value_type(duration, ind));
    }

    // Build a replacement regions array with the longest-persistence regions
    LLSD new_region(LLSD::emptyArray());
    reg_ordered_list_t::const_reverse_iterator it2_end(regions_by_duration.rend());
    reg_ordered_list_t::const_reverse_iterator it2(regions_by_duration.rbegin());
    for (int i(0); i < max_regions && it2_end != it2; ++i, ++it2)
    {
        new_region.append(reg_map[it2->second]);
    }
    reg_map = new_region;

    return true;
}

} // end of anonymous namespace

LLTextureFetchTester::LLTextureFetchTester() : LLMetricPerformanceTesterBasic(sTesterName)
{
    mTextureFetchTime = 0;
    mSkippedStatesTime = 0;
    mFileSize = 0;
}

LLTextureFetchTester::~LLTextureFetchTester()
{
    outputTestResults();
    LLTextureFetch::sTesterp = NULL;
}

//virtual
void LLTextureFetchTester::outputTestRecord(LLSD *sd)
{
    std::string currentLabel = getCurrentLabelName();

    (*sd)[currentLabel]["Texture Fetch Time"]   = (LLSD::Real)mTextureFetchTime;
    (*sd)[currentLabel]["File Size"]            = (LLSD::Integer)mFileSize;
    (*sd)[currentLabel]["Skipped States Time"]  = (LLSD::String)llformat("%.6f", mSkippedStatesTime);

    for(auto i : LOGGED_STATES)
    {
        (*sd)[currentLabel][sStateDescs[i]] = mStateTimersMap[i];
    }
}

void LLTextureFetchTester::updateStats(const std::map<S32, F32> state_timers, const F32 fetch_time, const F32 skipped_states_time, const S32 file_size)
{
    mTextureFetchTime = fetch_time;
    mStateTimersMap = state_timers;
    mFileSize = file_size;
    mSkippedStatesTime = skipped_states_time;
    outputTestResults();
}

