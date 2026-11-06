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


// <Vulkanstorm> Undo a gzip or deflate Content-Encoding, as libcurl 7.54.1
// did with CURLOPT_ENCODING "" (see HttpOpRequest::prepareRequest). Only
// those two (and x-gzip) are decoded; any other value, the asset CDN's
// "binary/octet-stream" or "identity" among them, leaves the body as it
// came, again as 7.54.1 did. deflate is the zlib format, with a fallback to
// a raw deflate stream as libcurl had. Returns false if the data cannot be
// decoded (libcurl's CURLE_BAD_CONTENT_ENCODING).
namespace
{
// Inflates `in` into a new BufferArray, reading and writing in chunks so
// that no whole copy of either side is made. Successive gzip members (a
// valid concatenation) are all decoded. Returns NULL if the data does not
// decode or the output cannot be stored.
LLCore::BufferArray * inflate_reply(LLCore::BufferArray * in, int window_bits)
{
    z_stream z;
    memset(&z, 0, sizeof(z));
    if (inflateInit2(&z, window_bits) != Z_OK)
    {
        return NULL;
    }
    LLCore::BufferArray * out(new LLCore::BufferArray());
    char inbuf[16384];
    char outbuf[16384];
    size_t pos(0);
    bool ok(true), ended(false), next_member(false);
    while (ok)
    {
        if (z.avail_in == 0)
        {
            const size_t n(in->read(pos, inbuf, sizeof(inbuf)));
            if (n == 0)
            {
                break;          // all input used (a truncated stream keeps what decoded, as libcurl did)
            }
            pos += n;
            z.next_in = reinterpret_cast<Bytef *>(inbuf);
            z.avail_in = static_cast<uInt>(n);
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
        if (ret == Z_DATA_ERROR && next_member && produced == 0)
        {
            break;              // trailing bytes after a complete stream are not another member: ignored
        }
        if (ret != Z_OK && ret != Z_STREAM_END && !(ret == Z_BUF_ERROR && produced == 0 && z.avail_in == 0))
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
int decode_reply(const std::string & encoding, LLCore::BufferArray * & body)
{
    std::string enc(encoding);
    std::transform(enc.begin(), enc.end(), enc.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    const size_t first(enc.find_first_not_of(" \t"));
    const size_t last(enc.find_last_not_of(" \t"));
    enc = first == std::string::npos ? std::string() : enc.substr(first, last - first + 1);
    const bool gzip(enc == "gzip" || enc == "x-gzip");
    if (!gzip && enc != "deflate")
    {
        return 0;
    }
    if (!body || !body->size())
    {
        return 1;
    }
    // gzip as libcurl 7.54.1 decoded it: MAX_WBITS + 32 detects a gzip or
    // zlib wrapper. deflate: zlib, then a raw stream as libcurl's fallback.
    LLCore::BufferArray * decoded(inflate_reply(body, gzip ? MAX_WBITS + 32 : MAX_WBITS));
    if (!decoded && !gzip)
    {
        decoded = inflate_reply(body, -MAX_WBITS);
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
        struct curl_header * encoding(NULL);
        if (curl_easy_header(handle, "Content-Encoding", 0, CURLH_HEADER, -1, &encoding) == CURLHE_OK
            && encoding && encoding->value
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
    // limit in all. It runs one request per connection
    // (HttpPolicyClass::activeLimit()); the rest wait in llcorehttp's ready
    // queue, as they waited in a pipeline before.
    HttpPolicy & policy(mService->getPolicy());
    const HttpPolicyClass & options(policy.getClassOptions(policy_class));
    CURLM * multi_handle(mMultiHandles[policy_class]);

    if (options.mPipelining > 1)
    {
        check_curl_multi_setopt(multi_handle,
                                CURLMOPT_PIPELINING,
                                CURLPIPE_MULTIPLEX);
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
