/**
 * @file _httplibcurl.cpp
 * @brief Internal definitions of the Http libcurl thread
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
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

#include "_httplibcurl.h"

#include "httpheaders.h"
#include "bufferarray.h"
#include "bufferstream.h" // <Vulkanstorm/> sequential reads for the reply decoding
#include "_httpoprequest.h"
#include "_httppolicy.h"

#include "llhttpconstants.h"

// <Vulkanstorm> gzip/deflate reply decoding (HttpLibcurl::completeRequest)
#ifdef LL_USESYSTEMLIBS
# include <zlib.h>
#else
# include "zlib-ng/zlib.h"
#endif
#include <algorithm>
#include <cctype>
#include <cstring>
// </Vulkanstorm>

namespace
{

// Error testing and reporting for libcurl status codes
void check_curl_multi_code(CURLMcode code);
void check_curl_multi_code(CURLMcode code, int curl_setopt_option);

// This is a template because different 'option' values require different
// types for 'ARG'. Just pass them through unchanged (by value).
template <typename ARG>
void check_curl_multi_setopt(CURLM* handle, CURLMoption option, ARG argument)
{
    CURLMcode code = curl_multi_setopt(handle, option, argument);
    check_curl_multi_code(code, option);
}

static const char * const LOG_CORE("CoreHttp");

} // end anonymous namespace


#if LIBCURL_VERSION_NUM >= 0x075300 // 7.83.0, the only users (HttpLibcurl::completeRequest)
// <Vulkanstorm> Undo a gzip or deflate Content-Encoding as libcurl 7.54.1
// did with CURLOPT_ENCODING "" (lib/http.c, lib/content_encoding.c), with
// two of its defects fixed: it lost output still pending in zlib when the
// last input filled its 16 KiB buffer, and it dropped every gzip member
// after the first.
//  - The value is matched as 7.54.1 did: leading white space skipped, then
//    a case-insensitive prefix, so only the first coding of a list counts
//    ("gzip, identity" is gzip): "identity", "deflate", "gzip", "x-gzip".
//    Anything else, the asset CDN's "binary/octet-stream" among it, leaves
//    the body as it came.
//  - gzip uses MAX_WBITS + 32 (a gzip or zlib wrapper), deflate MAX_WBITS;
//    for either, data that does not start a valid stream is retried as a raw
//    deflate stream, 7.54.1's fallback for servers that send no header. As
//    there, only a format error before any output counts: 7.54.1 restarted
//    on Z_DATA_ERROR only until inflate() had once returned Z_OK. A stream
//    that fails later (corrupt data, a bad CRC-32 or Adler-32, no memory) is
//    refused, never raw-decoded, which could turn a corrupt zlib stream
//    into an unrelated body. 7.54.1 re-armed the restart for every network
//    read, so a checksum error in a later read could still restart; here it
//    is armed for the first inflate() call of a reply only.
//  - Every member of a concatenation is decoded; bytes after a complete
//    stream that do not start another member are ignored; a truncated
//    stream keeps what decoded. Data that does not decode is refused
//    (CURLE_BAD_CONTENT_ENCODING) and the original body kept.
namespace
{
enum class ReplyCoding { NONE, GZIP, DEFLATE };

ReplyCoding reply_coding(const char * value)
{
    while (*value && isspace(static_cast<unsigned char>(*value)))
    {
        ++value;
    }
    // case-insensitive prefix test, as libcurl's checkprefix()
    auto prefix = [value](const char * p)
    {
        for (size_t i(0); p[i]; ++i)
        {
            if (tolower(static_cast<unsigned char>(value[i])) != p[i])
            {
                return false;   // also stops at the end of `value` (NUL never matches)
            }
        }
        return true;
    };
    if (prefix("identity"))
    {
        return ReplyCoding::NONE;
    }
    if (prefix("deflate"))
    {
        return ReplyCoding::DEFLATE;
    }
    if (prefix("gzip") || prefix("x-gzip"))
    {
        return ReplyCoding::GZIP;
    }
    return ReplyCoding::NONE;
}

// Inflates `in` into a new BufferArray, reading it through
// BufferArrayStreamBuf (sequential, no block search per chunk) in 16 KiB
// chunks. Returns NULL if the data does not decode or the output cannot be
// stored; `not_a_stream` is then true if the data failed as a format error
// at its start, before any output (the raw-deflate fallback's case).
LLCore::BufferArray * inflate_reply(LLCore::BufferArray * in, int window_bits, bool & not_a_stream)
{
    not_a_stream = false;
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, window_bits) != Z_OK)
    {
        return NULL;
    }
    LLCore::BufferArray * out(new LLCore::BufferArray());
    LLCore::BufferArrayStreamBuf src(in);
    char inbuf[16384];
    char outbuf[16384];
    bool ok(true), ended(false), next_member(false), input_done(false), full(false), first_call(true);
    while (ok)
    {
        if (z.avail_in == 0 && !input_done)
        {
            const std::streamsize n(src.sgetn(inbuf, sizeof(inbuf)));
            if (n <= 0)
            {
                input_done = true;
            }
            else
            {
                z.next_in = reinterpret_cast<Bytef *>(inbuf);
                z.avail_in = static_cast<uInt>(n);
            }
        }
        if (z.avail_in == 0 && input_done && (ended || !full))
        {
            break;              // all input used and no output pending (a truncated stream keeps what decoded)
        }
        if (ended)
        {
            // more input after a complete stream: the next gzip member
            if (inflateReset(&z) != Z_OK)
            {
                ok = false;
                break;
            }
            ended = false;
            next_member = true;
        }
        z.next_out = reinterpret_cast<Bytef *>(outbuf);
        z.avail_out = sizeof(outbuf);
        const int ret(inflate(&z, Z_NO_FLUSH));
        const size_t produced(sizeof(outbuf) - z.avail_out);
        full = (z.avail_out == 0);
        if (first_call && ret == Z_DATA_ERROR && produced == 0)
        {
            not_a_stream = true;    // bad header (or first block) before any output
        }
        first_call = false;
        if (ret == Z_DATA_ERROR && next_member && produced == 0)
        {
            break;              // trailing bytes after a complete stream are not another member: ignored
        }
        if (ret == Z_BUF_ERROR && produced == 0 && z.avail_in == 0)
        {
            if (input_done)
            {
                break;          // nothing more to give zlib
            }
            continue;
        }
        if (ret != Z_OK && ret != Z_STREAM_END)
        {
            ok = false;
            break;
        }
        if (produced)
        {
            next_member = false;
        }
        if (produced && out->append(outbuf, produced) != produced)
        {
            ok = false;         // BufferArray could not allocate
            break;
        }
        ended = (ret == Z_STREAM_END);
    }
    inflateEnd(&z);
    if (!ok)
    {
        out->release();
        return NULL;
    }
    return out;
}

// 1: decoded into `body`; 0: not an encoding to undo; -1: bad encoded data.
int decode_reply(const char * encoding, LLCore::BufferArray * & body)
{
    const ReplyCoding coding(reply_coding(encoding));
    if (coding == ReplyCoding::NONE)
    {
        return 0;
    }
    if (!body || !body->size())
    {
        return 1;
    }
    bool not_a_stream(false);
    LLCore::BufferArray * decoded(inflate_reply(body, coding == ReplyCoding::GZIP ? MAX_WBITS + 32 : MAX_WBITS,
                                                not_a_stream));
    if (!decoded && not_a_stream)
    {
        decoded = inflate_reply(body, -MAX_WBITS, not_a_stream);   // 7.54.1's fallback: no zlib or gzip header
    }
    if (!decoded)
    {
        return -1;
    }
    body->release();
    body = decoded;
    return 1;
}
} // anonymous namespace
// </Vulkanstorm>
#endif

namespace LLCore
{


HttpLibcurl::HttpLibcurl(HttpService * service)
    : mService(service),
      mHandleCache(),
      mPolicyCount(0),
      mMultiHandles(NULL),
      mActiveHandles(NULL)
{}


HttpLibcurl::~HttpLibcurl()
{
    shutdown();

    mService = NULL;
}


// Transport shutdown, on the worker thread once it was asked to stop
// (HttpService::threadRun() leaves its loop, then HttpService::shutdown()).
// ~HttpLibcurl calls it again on the main thread after the worker was joined;
// by then mActiveOps is empty and every multi handle is NULL, so that call
// does nothing.
//
// Every transfer still active is removed from its multi handle
// (curl_multi_remove_handle) and delivered to its handler as canceled; the
// handles are then recycled and each multi handle is cleaned up. That is how
// libcurl closes its own transfers and connections. Nothing is killed and no
// thread is abandoned: the worker is always joined, and every call below ends
// by itself within the bound stated for it.
//
// Source of the libcurl claims below: the curl 8.23.0 source tree (the version
// and SHA-256 recorded in packages/LICENSES/curl-buildinfo.json, as
// unpacked from the release tarball), file:line in its lib/ directory.
//
// libcurl is entered from these places, all on the HTTP worker thread:
//  - addOp()/prepareRequest(): curl_easy_setopt() and curl_multi_add_handle()
//    (starts the transfer; the name lookup is queued to the resolver pool,
//    nothing waits for it). Memory only.
//  - cancel()/cancelRequest() and the loop in this function:
//    curl_multi_remove_handle(). It does not wait for a lookup: for a transfer
//    whose lookup is queued it only clears that queued item, one that already
//    started keeps running on its pool thread (vdns/asyn-thrdd.c:290-302).
//  - completeRequest(): curl_multi_info_read() and curl_multi_remove_handle().
//  - processTransport(): curl_multi_perform(), see below.
//  - this function: curl_multi_cleanup(), see below.
//
// Every call that can block the HTTP worker thread, and what ends it:
//  - waiting for a request (HttpRequestQueue::fetchAll): condition variable,
//    woken by HttpRequestQueue::stopQueue(), which ~HttpService calls after
//    setting the exit flag; the check and the wait are under one mutex.
//  - sleeping between polls (ms_sleep(HTTP_SERVICE_LOOP_SLEEP_NORMAL_MS) in
//    HttpService::threadRun()): 2 ms.
//  - HttpOpSpin mode 0 (test and diagnostic only): polls the exit flag every
//    10 ms.
//  - curl_multi_perform(): never waits for the network (non-blocking sockets,
//    threaded resolver), but it does local work: reading the CA bundle file
//    for a new TLS context, and the callbacks below. Our callbacks are local
//    and short: write/read/seek/header (memory only), debug (logging), and the
//    SSL context and certificate verification callbacks. The certificate
//    callback enters the viewer (LLAppCoreHttp::sslVerify: gSecAPIHandler, the
//    local certificate store file and OpenSSL chain checks; no network and no
//    lock shared with another thread, apart from the logging mutex); it is
//    skipped once the exit flag is set (HttpOpRequest::sslCertVerifyCallback),
//    so at most one verification in progress delays the stop, bounded by its
//    CPU and file work.
//  - proxy setup (LLProxy::applyProxySettings, in prepareRequest): takes
//    LLProxy::mProxyMutex, which the main thread holds only to copy settings.
//    It contacts nothing: libcurl does the proxy connection and handshake
//    inside curl_multi_perform().
//  - policy and queue work (HttpPolicy::processReadyQueue, HttpService::
//    processRequestQueue): in-memory queues; operations dequeued after the
//    exit flag is set are not staged but cancelled (a request is answered as
//    canceled on the reply queue).
//  - name lookups: libcurl's threaded resolver runs getaddrinfo() on its own
//    pool threads, so the HTTP worker never waits for one while transferring,
//    and a transfer's lookup is limited by its CURLOPT_CONNECTTIMEOUT
//    (default 30 s, HttpOptions::setTimeout()), after which the transfer
//    fails and is removed. The one wait is curl_multi_cleanup() below: it
//    calls Curl_async_thrdd_multi_destroy(multi, !multi->quick_exit)
//    (multi.c:3033), which, as CURLMOPT_QUICK_EXIT is not set, destroys the
//    resolver queue and pool with join (asyn-thrdd.c:511-521,
//    thrdqueue.c:244-250) and waits until every pool thread has ended
//    (thrdpool.c:357-385, Curl_cond_wait on the slots). So a getaddrinfo()
//    that has started, for this multi handle, is waited for; only lookups
//    still queued and not started are dropped. getaddrinfo() cannot be
//    interrupted; its bound is the operating system's resolver timeout,
//    which this code does not control. Lookups in progress at stop are rare
//    (a request issued just before quitting, to a name whose server does not
//    answer), and each multi handle is cleaned up in turn.
//    CURLMOPT_QUICK_EXIT would make libcurl detach those threads instead
//    (thrdpool.c:370-379); it is deliberately not used.
//  - connection shutdown at curl_multi_cleanup(): best effort with a 0 ms
//    timeout. The connections are moved to the shutdown list and
//    Curl_cshutdn_destroy() runs cshutdn_terminate_all() with timeout_ms = 0
//    (cshutdn.c:421-441; conncache.c:260-285 for the pool being discarded), so
//    a TLS close_notify or similar is attempted once without waiting and the
//    sockets are then closed. HTTP and HTTPS have no blocking disconnect
//    handler.
//  - TLS: handshakes and closes are non-blocking inside curl_multi_perform();
//    the close at shutdown is the best-effort attempt above.
//  - curl_easy_cleanup() (HandleCache destructor): handles outside any multi
//    handle own no connection and no lookup; memory only.
//  - the reply queue and HttpOperation notifiers: short mutexes, no waits on
//    the consumer.
void HttpLibcurl::shutdown()
{
    while (! mActiveOps.empty())
    {
        HttpOpRequest::ptr_t op(* mActiveOps.begin());
        mActiveOps.erase(mActiveOps.begin());

        cancelRequest(op);
    }

    if (mMultiHandles)
    {
        for (unsigned int policy_class(0); policy_class < mPolicyCount; ++policy_class)
        {
            if (mMultiHandles[policy_class])
            {
                curl_multi_cleanup(mMultiHandles[policy_class]);
                mMultiHandles[policy_class] = 0;
            }
        }

        delete [] mMultiHandles;
        mMultiHandles = NULL;

        delete [] mActiveHandles;
        mActiveHandles = NULL;
        mActiveHosts.clear(); // <Vulkanstorm/>
    }

    mPolicyCount = 0;
}


void HttpLibcurl::start(int policy_count)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    llassert_always(policy_count <= HTTP_POLICY_CLASS_LIMIT);
    llassert_always(! mMultiHandles);                   // One-time call only

    mPolicyCount = policy_count;
    mMultiHandles = new CURLM * [mPolicyCount];
    mActiveHandles = new int [mPolicyCount];
    mActiveHosts.assign(mPolicyCount, std::map<std::string, int>()); // <Vulkanstorm/>

    for (unsigned int policy_class(0); policy_class < mPolicyCount; ++policy_class)
    {
        if (NULL == (mMultiHandles[policy_class] = curl_multi_init()))
        {
            LL_ERRS(LOG_CORE) << "Failed to allocate multi handle in libcurl."
                              << LL_ENDL;
        }
        mActiveHandles[policy_class] = 0;
        policyUpdated(policy_class);
    }
}


// Give libcurl some cycles, invoke it's callbacks, process
// completed requests finalizing or issuing retries as needed.
//
// If active list goes empty *and* we didn't queue any
// requests for retry, we return a request for a hard
// sleep otherwise ask for a normal polling interval.
HttpService::ELoopSpeed HttpLibcurl::processTransport()
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    HttpService::ELoopSpeed ret(HttpService::REQUEST_SLEEP);

    // Give libcurl some cycles to do I/O & callbacks
    for (unsigned int policy_class(0); policy_class < mPolicyCount; ++policy_class)
    {
        if (! mMultiHandles[policy_class])
        {
            // No handle, nothing to do.
            continue;
        }
        if (! mActiveHandles[policy_class])
        {
            continue;
        }

        int running(0);
        CURLMcode status(CURLM_CALL_MULTI_PERFORM);
        do
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_NETWORK("httppt - curl_multi_perform");
            running = 0;
            status = curl_multi_perform(mMultiHandles[policy_class], &running);
        }
        while (0 != running && CURLM_CALL_MULTI_PERFORM == status);

        // Run completion on anything done
        CURLMsg * msg(NULL);
        int msgs_in_queue(0);
        {
            LL_PROFILE_ZONE_NAMED_CATEGORY_NETWORK("httppt - curl_multi_info_read");
            while ((msg = curl_multi_info_read(mMultiHandles[policy_class], &msgs_in_queue)))
            {
                if (CURLMSG_DONE == msg->msg)
                {
                    CURL* handle(msg->easy_handle);
                    CURLcode result(msg->data.result);

                    completeRequest(mMultiHandles[policy_class], handle, result);
                    handle = NULL;                  // No longer valid on return
                    ret = HttpService::NORMAL;      // If anything completes, we may have a free slot.
                                                    // Turning around quickly reduces connection gap by 7-10mS.
                }
                else if (CURLMSG_NONE == msg->msg)
                {
                    // Ignore this... it shouldn't mean anything.
                    ;
                }
                else
                {
                    LL_WARNS_ONCE(LOG_CORE) << "Unexpected message from libcurl.  Msg code:  "
                        << msg->msg
                        << LL_ENDL;
                }
                msgs_in_queue = 0;
            }
        }
    }

    if (! mActiveOps.empty())
    {
        ret = HttpService::NORMAL;
    }
    return ret;
}


// Caller has provided us with a ref count on op.
void HttpLibcurl::addOp(const HttpOpRequest::ptr_t &op)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    llassert_always(op->mReqPolicy < mPolicyCount);
    llassert_always(mMultiHandles[op->mReqPolicy] != NULL);

    // Create standard handle
    if (! op->prepareRequest(mService))
    {
        // Couldn't issue request, fail with notification
        // *TODO:  Need failure path
        return;
    }

    // Make the request live
    CURLMcode code;
    code = curl_multi_add_handle(mMultiHandles[op->mReqPolicy], op->mCurlHandle);
    if (CURLM_OK != code)
    {
        // *TODO:  Better cleanup and recovery but not much we can do here.
        check_curl_multi_code(code);
        return;
    }
    op->mCurlActive = true;
    mActiveOps.insert(op);
    ++mActiveHandles[op->mReqPolicy];
    ++mActiveHosts[op->mReqPolicy][op->mReqHost]; // <Vulkanstorm/>

    if (op->mTracing > HTTP_TRACE_OFF)
    {
        HttpPolicy & policy(mService->getPolicy());

        LL_INFOS(LOG_CORE) << "TRACE, ToActiveQueue, Handle:  "
                            << op->getHandle()
                            << ", Actives:  " << mActiveOps.size()
                            << ", Readies:  " << policy.getReadyCount(op->mReqPolicy)
                            << LL_ENDL;
    }
}


// Implements the transport part of any cancel operation.
// See if the handle is an active operation and if so,
// use the more complicated transport-based cancellation
// method to kill the request.
bool HttpLibcurl::cancel(HttpHandle handle)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    HttpOpRequest::ptr_t op = HttpOpRequest::fromHandle<HttpOpRequest>(handle);
    active_set_t::iterator it(mActiveOps.find(op));
    if (mActiveOps.end() == it)
    {
        return false;
    }

    // Cancel request
    cancelRequest(op);

    // Drop references
    mActiveOps.erase(it);
    --mActiveHandles[op->mReqPolicy];
    releaseHost(op->mReqPolicy, op->mReqHost); // <Vulkanstorm/>

    return true;
}


// *NOTE:  cancelRequest logic parallels completeRequest logic.
// Keep them synchronized as necessary.  Caller is expected to
// remove the op from the active list and release the op *after*
// calling this method.  It must be called first to deliver the
// op to the reply queue with refcount intact.
void HttpLibcurl::cancelRequest(const HttpOpRequest::ptr_t &op)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    // Deactivate request
    op->mCurlActive = false;

    // Detach from multi and recycle handle
    curl_multi_remove_handle(mMultiHandles[op->mReqPolicy], op->mCurlHandle);
    mHandleCache.freeHandle(op->mCurlHandle);
    op->mCurlHandle = NULL;

    // Tracing
    if (op->mTracing > HTTP_TRACE_OFF)
    {
        LL_INFOS(LOG_CORE) << "TRACE, RequestCanceled, Handle:  "
                           << op->getHandle()
                           << ", Status:  " << op->mStatus.toTerseString()
                           << LL_ENDL;
    }

    // Cancel op and deliver for notification
    op->cancel();
}


// *NOTE:  cancelRequest logic parallels completeRequest logic.
// Keep them synchronized as necessary.
bool HttpLibcurl::completeRequest(CURLM * multi_handle, CURL * handle, CURLcode status)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    HttpHandle ophandle(NULL);

    CURLcode ccode(CURLE_OK);

    ccode = curl_easy_getinfo(handle, CURLINFO_PRIVATE, &ophandle);
    if (ccode)
    {
        LL_WARNS(LOG_CORE) << "libcurl error: " << ccode << " Unable to retrieve operation handle from CURL handle" << LL_ENDL;
        return false;
    }
    HttpOpRequest::ptr_t op(HttpOpRequest::fromHandle<HttpOpRequest>(ophandle));

    if (!op)
    {
        LL_WARNS() << "Unable to locate operation by handle. May have expired!" << LL_ENDL;
        return false;
    }

    if (handle != op->mCurlHandle || ! op->mCurlActive)
    {
        LL_WARNS(LOG_CORE) << "libcurl handle and HttpOpRequest handle in disagreement or inactive request."
                           << "  Handle:  " << static_cast<HttpHandle>(handle)
                           << LL_ENDL;
        return false;
    }

    active_set_t::iterator it(mActiveOps.find(op));
    if (mActiveOps.end() == it)
    {
        LL_WARNS(LOG_CORE) << "libcurl completion for request not on active list.  Continuing."
                           << "  Handle:  " << static_cast<HttpHandle>(handle)
                           << LL_ENDL;
        return false;
    }

    // Deactivate request
    mActiveOps.erase(it);
    --mActiveHandles[op->mReqPolicy];
    releaseHost(op->mReqPolicy, op->mReqHost); // <Vulkanstorm/>
    op->mCurlActive = false;

    // Set final status of request if it hasn't failed by other mechanisms yet
    if (op->mStatus)
    {
        op->mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, status);
    }
    if (op->mStatus)
    {
        // note: CURLINFO_RESPONSE_CODE requires a long - https://curl.haxx.se/libcurl/c/CURLINFO_RESPONSE_CODE.html
        long http_status(HTTP_OK);

        if (handle)
        {
            ccode = curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &http_status);
            if (ccode == CURLE_OK)
            {
                if (http_status >= 100 && http_status <= 999)
                {
                    char * cont_type(NULL);
                    ccode = curl_easy_getinfo(handle, CURLINFO_CONTENT_TYPE, &cont_type);
                    if (ccode == CURLE_OK)
                    {
                        if (cont_type)
                        {
                            op->mReplyConType = cont_type;
                        }
                    }
                    else
                    {
                        LL_WARNS(LOG_CORE) << "CURL error:" << ccode << " Attempting to get content type." << LL_ENDL;
                    }
                    op->mStatus = HttpStatus(http_status);
                }
                else
                {
                    LL_WARNS(LOG_CORE) << "Invalid HTTP response code ("
                        << http_status << ") received from server."
                        << LL_ENDL;
                    op->mStatus = HttpStatus(HttpStatus::LLCORE, HE_INVALID_HTTP_STATUS);
                }
            }
            else
            {
                op->mStatus = HttpStatus(HttpStatus::LLCORE, HE_INVALID_HTTP_STATUS);
            }
        }
        else
        {
            LL_WARNS(LOG_CORE) << "Attempt to retrieve status from NULL handle!" << LL_ENDL;
        }
    }

#if LIBCURL_VERSION_NUM >= 0x075300
    // <Vulkanstorm> the reply decoding libcurl 7.54.1 did, see decode_reply()
    if (op->mDecodeReply && handle)
    {
        // the last Content-Encoding line of the final response, as 7.54.1
        // read every line and kept the last
        struct curl_header * encoding(NULL);
        if (curl_easy_header(handle, "Content-Encoding", 0, CURLH_HEADER, -1, &encoding) == CURLHE_OK
            && encoding && encoding->amount > 1)
        {
            curl_easy_header(handle, "Content-Encoding", encoding->amount - 1, CURLH_HEADER, -1, &encoding);
        }
        if (encoding && encoding->value
            && decode_reply(encoding->value, op->mReplyBody) < 0)
        {
            LL_WARNS(LOG_CORE) << "Could not undo Content-Encoding " << encoding->value
                               << ", url " << op->mReqURL << LL_ENDL;
            op->mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_BAD_CONTENT_ENCODING);
        }
    }
    // </Vulkanstorm>
#endif

    // <Vulkanstorm> Firestorm's X-LL-URL / Content-Range check for HTTP/1.1
    // pipelining going out of sync is gone with the pipelining (curl 8), but
    // its offset test stays: a 206 that does not start where the request
    // asked is refused, with the retryable HE_INV_CONTENT_RANGE_HDR and
    // nothing switched off. lltexturefetch skips its realignment for a reply
    // at offset 0 and would append a wrong one to the data it has. A 206
    // without a usable Content-Range reads as offset 0 here, so it is refused
    // for a request at a later offset, as Firestorm's check did, although
    // lltexturefetch and the mesh repository would take it as the asked
    // range (LL's fallback for services that scrub headers). A longer reply
    // at the asked offset is delivered (BUG-2295).
    if (op->mStatus == HttpStatus(HTTP_PARTIAL_CONTENT)
        && op->mReqOffset != op->mReplyOffset)
    {
        LL_WARNS(LOG_CORE) << "Partial content at offset " << op->mReplyOffset
                           << " for a request at offset " << op->mReqOffset
                           << ", url " << op->mReqURL << LL_ENDL;
        op->mStatus = HttpStatus(HttpStatus::LLCORE, HE_INV_CONTENT_RANGE_HDR);
    }
    // </Vulkanstorm>

    if (multi_handle && handle)
    {
        // Detach from multi and recycle handle
        curl_multi_remove_handle(multi_handle, handle);
        mHandleCache.freeHandle(op->mCurlHandle);
    }
    else
    {
        LL_WARNS(LOG_CORE) << "Curl multi_handle or handle is NULL on remove! multi:"
            << std::hex << multi_handle << " h:" << std::hex << handle << std::dec << LL_ENDL;
    }

    op->mCurlHandle = NULL;

    // Tracing
    if (op->mTracing > HTTP_TRACE_OFF)
    {
        LL_INFOS(LOG_CORE) << "TRACE, RequestComplete, Handle:  "
                            << op->getHandle()
                            << ", Status:  " << op->mStatus.toTerseString()
                            << LL_ENDL;
    }

    // Dispatch to next stage
    HttpPolicy & policy(mService->getPolicy());
    bool still_active(policy.stageAfterCompletion(op));

    return still_active;
}


int HttpLibcurl::getActiveCount() const
{
    return static_cast<int>(mActiveOps.size());
}


// <Vulkanstorm>
int HttpLibcurl::getActiveCountForHost(unsigned int policy_class, const std::string & host) const
{
    if (policy_class >= mActiveHosts.size())
    {
        return 0;
    }
    const std::map<std::string, int> & hosts(mActiveHosts[policy_class]);
    const std::map<std::string, int>::const_iterator it(hosts.find(host));
    return it == hosts.end() ? 0 : it->second;
}

void HttpLibcurl::releaseHost(unsigned int policy_class, const std::string & host)
{
    if (policy_class >= mActiveHosts.size())
    {
        return;
    }
    std::map<std::string, int> & hosts(mActiveHosts[policy_class]);
    std::map<std::string, int>::iterator it(hosts.find(host));
    if (it != hosts.end() && --it->second <= 0)
    {
        hosts.erase(it);
    }
}
// </Vulkanstorm>

int HttpLibcurl::getActiveCountInClass(unsigned int policy_class) const
{
    llassert_always(policy_class < mPolicyCount);

    return mActiveHandles ? mActiveHandles[policy_class] : 0;
}

void HttpLibcurl::policyUpdated(unsigned int policy_class)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    if (policy_class < 0 || policy_class >= mPolicyCount || ! mMultiHandles)
    {
        return;
    }

    // <Vulkanstorm> Options apply at once. The stall that let a class go
    // idle first protected libcurl 7.37, which corrupted memory when
    // HTTP/1.1 pipelining was switched off under active requests; libcurl
    // 7.62 and later have no HTTP/1.1 pipelining to switch.
    //
    // A deep-queue class keeps the connections it had with HTTP/1.1
    // pipelining: up to its per-host limit to a host and its connection
    // limit in all. With libcurl 7.62+ it runs one request per connection
    // (HttpPolicyClass::activeLimit() and hostLimit(), admitted per host by
    // HttpPolicy::processReadyQueue); the rest wait in llcorehttp's ready
    // queue, as they waited in a pipeline before.
    HttpPolicy & policy(mService->getPolicy());
    const HttpPolicyClass & options(policy.getClassOptions(policy_class));
    CURLM * multi_handle(mMultiHandles[policy_class]);

    if (options.mPipelining > 1)
    {
#if LIBCURL_VERSION_NUM < 0x073e00
        // libcurl before 7.62 (the darwin64 package, 7.54.1): HTTP/1.1
        // pipelining as before, matching HttpPolicyClass::activeLimit()'s
        // per-host limit x depth for that version. (The stall that let the
        // class go idle before switching pipelining off is not restored.)
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_PIPELINING,
                                1L);
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_PIPELINE_LENGTH,
                                long(options.mPipelining));
#else
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_PIPELINING,
                                CURLPIPE_MULTIPLEX);
#endif
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_HOST_CONNECTIONS,
                                long(options.mPerHostConnectionLimit));
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_TOTAL_CONNECTIONS,
                                long(options.mConnectionLimit));
#if LIBCURL_VERSION_NUM >= 0x074300 // 7.67.0
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_CONCURRENT_STREAMS,
                                options.mPipelining);
#endif
    }
    else
    {
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_PIPELINING,
                                CURLPIPE_NOTHING);
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_HOST_CONNECTIONS,
                                0L);
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_MAX_TOTAL_CONNECTIONS,
                                long(options.mConnectionLimit));
    }
    // </Vulkanstorm>
}

// ---------------------------------------
// HttpLibcurl::HandleCache
// ---------------------------------------

HttpLibcurl::HandleCache::HandleCache()
    : mHandleTemplate(NULL)
{
    mCache.reserve(50);
}


HttpLibcurl::HandleCache::~HandleCache()
{
    if (mHandleTemplate)
    {
        curl_easy_cleanup(mHandleTemplate);
        mHandleTemplate = NULL;
    }

    for (handle_cache_t::iterator it(mCache.begin()); mCache.end() != it; ++it)
    {
        curl_easy_cleanup(*it);
    }
    mCache.clear();
}


CURL * HttpLibcurl::HandleCache::getHandle()
{
    CURL * ret(NULL);

    if (! mCache.empty())
    {
        // Fastest path to handle
        ret = mCache.back();
        mCache.pop_back();
    }
    else if (mHandleTemplate)
    {
        // Still fast path
        ret = curl_easy_duphandle(mHandleTemplate);
    }
    else
    {
        // When all else fails
        ret = curl_easy_init();
    }

    return ret;
}


void HttpLibcurl::HandleCache::freeHandle(CURL * handle)
{
    if (! handle)
    {
        return;
    }

    curl_easy_reset(handle);
    if (! mHandleTemplate)
    {
        // Save the first freed handle as a template.
        mHandleTemplate = handle;
    }
    else
    {
        // Otherwise add it to the cache
        if (mCache.size() >= mCache.capacity())
        {
            mCache.reserve(mCache.capacity() + 50);
        }
        mCache.push_back(handle);
    }
}


// ---------------------------------------
// Free functions
// ---------------------------------------


struct curl_slist * append_headers_to_slist(const HttpHeaders::ptr_t &headers, struct curl_slist * slist)
{
    const HttpHeaders::const_iterator end(headers->end());
    for (HttpHeaders::const_iterator it(headers->begin()); end != it; ++it)
    {
        static const char sep[] = ": ";
        std::string header;
        header.reserve((*it).first.size() + (*it).second.size() + sizeof(sep));
        header.append((*it).first);
        header.append(sep);
        header.append((*it).second);

        slist = curl_slist_append(slist, header.c_str());
    }
    return slist;
}


}  // end namespace LLCore


namespace
{

void check_curl_multi_code(CURLMcode code, int curl_setopt_option)
{
    if (CURLM_OK != code)
    {
        LL_WARNS(LOG_CORE) << "libcurl multi error detected:  " << curl_multi_strerror(code)
                           << ", curl_multi_setopt option:  " << curl_setopt_option
                           << LL_ENDL;
    }
}


void check_curl_multi_code(CURLMcode code)
{
    if (CURLM_OK != code)
    {
        LL_WARNS(LOG_CORE) << "libcurl multi error detected:  " << curl_multi_strerror(code)
                           << LL_ENDL;
    }
}

}  // end anonymous namespace
