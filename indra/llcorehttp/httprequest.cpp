/**
 * @file httprequest.cpp
 * @brief Implementation of the HTTPRequest class on the Rust HTTP service
 *
 * $LicenseInfo:firstyear=2012&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2012-2013, Linden Research, Inc.
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

// The service behind this class (request queue, service thread, policy
// classes, retry queue, libcurl transport, reply queues) is Rust:
// vkstorm-http, reached through vks_http_* (vkstorm-rust rung four). This
// file is the C++ side of the boundary: it hands requests over, drives the
// reply queue from update(), and supplies the viewer's facilities the
// service asks for (logging, statistics, LLProxy, the certificate
// verification callback, thread registration).

#include "httprequest.h"

#include "linden_common.h"      // Modifies curl/curl.h interfaces
#include <curl/curl.h>
#include <openssl/ssl.h>
#include <openssl/x509_vfy.h>

#include "httphandler.h"
#include "httpresponse.h"
#include "bufferarray.h"
#include "httpheaders.h"
#include "httpoptions.h"
#include "httpstats.h"
#include "_httpservice.h"
#include "_mutex.h"
#include "llproxy.h"
#include "lltimer.h"
#include "llthread.h"
#include "indra_constants.h"    // APP_NAME, the User-Agent
#include "vkstorm_ffi.h"

#include <map>
#include <vector>

namespace
{
bool has_inited(false);

// The viewer's certificate verification callback (PO_SSL_VERIFY_CALLBACK),
// kept here; the service only knows whether one is set.
LLCore::HttpRequest::policyCallback_t sSslCallback;

// The full status (message and certificate data) the verification callback
// produced for a request, keyed by its handle, until its reply is delivered.
LLCoreInt::HttpMutex sSslStatusMutex;
std::map<uint64_t, LLCore::HttpStatus> sSslStatus;

inline LLCore::HttpHandle toHandle(uint64_t h)
{
    return reinterpret_cast<LLCore::HttpHandle>(static_cast<uintptr_t>(h));
}

inline uint64_t fromHandle(LLCore::HttpHandle h)
{
    return static_cast<uint64_t>(reinterpret_cast<uintptr_t>(h));
}

inline LLCore::HttpStatus toStatus(const VksHttpStatus& s)
{
    return LLCore::HttpStatus(static_cast<LLCore::HttpStatus::type_enum_t>(s.type), static_cast<short>(s.code));
}

// ---- The service's hooks into the viewer -----------------------------------

void hook_log(void*, int32_t level, const char* text, size_t len)
{
    const std::string msg(text, len);
    switch (level)
    {
    case 0:
        LL_DEBUGS("CoreHttp") << msg << LL_ENDL;
        break;
    case 1:
        LL_INFOS("CoreHttp") << msg << LL_ENDL;
        break;
    default:
        LL_WARNS("CoreHttp") << msg << LL_ENDL;
        break;
    }
}

void hook_stat(void*, int32_t kind, uint64_t value)
{
    if (!LLCore::HTTPStats::instanceExists())
    {
        return;
    }
    switch (kind)
    {
    case 0:
        LLCore::HTTPStats::instance().recordDataDown(static_cast<size_t>(value));
        break;
    case 1:
        LLCore::HTTPStats::instance().recordDataUp(static_cast<size_t>(value));
        break;
    case 2:
        LLCore::HTTPStats::instance().recordResultCode(static_cast<S32>(value));
        break;
    default:
        break;
    }
}

void hook_apply_proxy(void*, void* curl)
{
    // The viewer's thread-safe proxy settings, as applied to every handle
    // before (LLProxy is not getInstance()'d from the service thread).
    LLProxy::applyProxySettings(static_cast<CURL*>(curl));
}

// OpenSSL's certificate verification, routed to the viewer's callback with
// the request's URL and handler. The decision goes back to the service in
// the request's SSL info; the full status is kept for the reply.
int cert_verify_callback(X509_STORE_CTX* ctx, void* param)
{
    VksHttpSslInfo* info = static_cast<VksHttpSslInfo*>(param);
    if (!sSslCallback)
    {
        return 1;
    }
    LLCore::HttpHandler::ptr_t handler;
    if (info->user)
    {
        handler = *static_cast<LLCore::HttpHandler::ptr_t*>(info->user);
    }
    const std::string url(info->url ? info->url : "");
    LLCore::HttpStatus status;
    try
    {
        status = sSslCallback(url, handler, ctx);
    }
    catch (...)
    {
        LL_WARNS("CoreHttp") << "Exception in the SSL verification callback for " << url << LL_ENDL;
        status = LLCore::HttpStatus(LLCore::HttpStatus::EXT_CURL_EASY, CURLE_SSL_CACERT);
    }
    info->result_type = status.getType();
    info->result_code = status.getStatus();
    if (!status)
    {
        LLCoreInt::HttpScopedLock lock(sSslStatusMutex);
        sSslStatus[info->handle] = status;
        return 0;
    }
    return 1;
}

void hook_ssl_ctx(void*, void* sslctx, VksHttpSslInfo* info)
{
    SSL_CTX* ctx = static_cast<SSL_CTX*>(sslctx);
    if (info->verify_peer)
    {
        // verification for ssl certs
        SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER, NULL);
    }
    else
    {
        // disable any default verification for server certs
        // Ex: setting urls (assume non-SL) for parcel media in LLFloaterURLEntry
        SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);
    }
    SSL_CTX_set_cert_verify_callback(ctx, cert_verify_callback, info);
}

void hook_thread_start(void*)
{
    LL_PROFILER_SET_THREAD_NAME("HttpService");
    LLThread::registerThreadID();
}

void hook_thread_stop(void*)
{
}

void hook_release_user(void*, void* user)
{
    delete static_cast<LLCore::HttpHandler::ptr_t*>(user);
}

VksHttpHooks make_hooks()
{
    VksHttpHooks hooks;
    hooks.ctx = NULL;
    hooks.log = hook_log;
    hooks.stat = hook_stat;
    hooks.apply_proxy = hook_apply_proxy;
    hooks.ssl_ctx = hook_ssl_ctx;
    hooks.thread_start = hook_thread_start;
    hooks.thread_stop = hook_thread_stop;
    hooks.release_user = hook_release_user;
    return hooks;
}

// The status of a delivered reply: the service's (type, code), or, when the
// certificate verification failed this request with that very status, the
// full status it produced (message and certificate data included).
LLCore::HttpStatus replyStatus(const VksHttpReply* reply, uint64_t handle)
{
    LLCore::HttpStatus status(toStatus(vks_http_reply_status(reply)));
    if (vks_http_reply_kind(reply) == 0)
    {
        LLCoreInt::HttpScopedLock lock(sSslStatusMutex);
        std::map<uint64_t, LLCore::HttpStatus>::iterator it(sSslStatus.find(handle));
        if (it != sSslStatus.end())
        {
            if (!status && it->second == status)
            {
                status = it->second;
            }
            sSslStatus.erase(it);
        }
    }
    return status;
}

void fill_options(VksHttpOptions& out, const LLCore::HttpOptions& in)
{
    out.want_headers = in.getWantHeaders() ? 1 : 0;
    out.trace = in.getTrace();
    out.timeout = in.getTimeout();
    out.transfer_timeout = in.getTransferTimeout();
    out.retries = in.getRetries();
    out.min_backoff = in.getMinBackoff();
    out.max_backoff = in.getMaxBackoff();
    out.use_retry_after = in.getUseRetryAfter() ? 1 : 0;
    out.follow_redirects = in.getFollowRedirects() ? 1 : 0;
    out.verify_peer = in.getSSLVerifyPeer() ? 1 : 0;
    out.verify_host = in.getSSLVerifyHost() ? 1 : 0;
    out.dns_cache_timeout = in.getDNSCacheTimeout();
    out.headers_only = in.getHeadersOnly() ? 1 : 0;
    out.last_modified = static_cast<int64_t>(in.getLastModified());
}

} // end anonymous namespace

namespace LLCore
{

// ====================================
// HttpRequest Implementation
// ====================================

HttpRequest::HttpRequest()
    : mReplyQueue(vks_http_reply_queue_new())
{
    HTTPStats::instance().recordHTTPRequest();
}

HttpRequest::~HttpRequest()
{
    if (mReplyQueue)
    {
        // Whatever was never delivered goes with the instance, handlers
        // included, as before.
        while (VksHttpReply* reply = vks_http_reply_fetch(mReplyQueue))
        {
            delete static_cast<HttpHandler::ptr_t*>(vks_http_reply_user(reply));
            vks_http_reply_free(reply);
        }
        vks_http_reply_queue_free(mReplyQueue);
        mReplyQueue = NULL;
    }
}

// ====================================
// Policy Methods
// ====================================

HttpRequest::policy_t HttpRequest::createPolicyClass()
{
    if (HttpService::RUNNING == HttpService::getState())
    {
        return 0;
    }
    return vks_http_policy_class_create();
}

HttpStatus HttpRequest::setStaticPolicyOption(EPolicyOption opt, policy_t pclass,
                                              long value, long * ret_value)
{
    if (HttpService::RUNNING == HttpService::getState())
    {
        return HttpStatus(HttpStatus::LLCORE, HE_OPT_NOT_DYNAMIC);
    }
    int64_t ret(0);
    const HttpStatus status(toStatus(vks_http_policy_set_long(opt, pclass, value, &ret)));
    if (status && ret_value)
    {
        *ret_value = static_cast<long>(ret);
    }
    return status;
}

HttpStatus HttpRequest::setStaticPolicyOption(EPolicyOption opt, policy_t pclass,
                                              const std::string & value, std::string * ret_value)
{
    if (HttpService::RUNNING == HttpService::getState())
    {
        return HttpStatus(HttpStatus::LLCORE, HE_OPT_NOT_DYNAMIC);
    }
    HttpStatus status(toStatus(vks_http_policy_set_string(opt, pclass, value.data(), value.size())));
    if (status && ret_value)
    {
        std::vector<char> buf(value.size() + 1);
        size_t len(0);
        status = toStatus(vks_http_policy_get_string(opt, pclass, buf.data(), buf.size(), &len));
        if (status)
        {
            ret_value->assign(buf.data(), (std::min)(len, buf.size()));
        }
    }
    return status;
}

HttpStatus HttpRequest::setStaticPolicyOption(EPolicyOption opt, policy_t pclass, policyCallback_t value, policyCallback_t * ret_value)
{
    if (HttpService::RUNNING == HttpService::getState())
    {
        return HttpStatus(HttpStatus::LLCORE, HE_OPT_NOT_DYNAMIC);
    }
    const HttpStatus status(toStatus(vks_http_policy_set_callback(opt, pclass, value ? 1 : 0)));
    if (status)
    {
        sSslCallback = value;
        if (ret_value)
        {
            *ret_value = sSslCallback;
        }
    }
    return status;
}

HttpHandle HttpRequest::setPolicyOption(EPolicyOption opt, policy_t pclass,
                                        long value, HttpHandler::ptr_t handler)
{
    HttpHandler::ptr_t* user = handler ? new HttpHandler::ptr_t(handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_set_long(mReplyQueue, opt, pclass, value, user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

HttpHandle HttpRequest::setPolicyOption(EPolicyOption opt, policy_t pclass,
                                        const std::string & value, HttpHandler::ptr_t handler)
{
    HttpHandler::ptr_t* user = handler ? new HttpHandler::ptr_t(handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_set_string(mReplyQueue, opt, pclass, value.data(), value.size(), user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

// ====================================
// Request Methods
// ====================================

HttpStatus HttpRequest::getStatus() const
{
    return mLastReqStatus;
}

HttpHandle HttpRequest::submit(policy_t policy_id,
                               int method,
                               const std::string & url,
                               BufferArray * body,
                               size_t offset,
                               size_t len,
                               const HttpOptions::ptr_t & options,
                               const HttpHeaders::ptr_t & headers,
                               const HttpHandler::ptr_t & handler)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    std::vector<VksHttpHeader> hdrs;
    if (headers)
    {
        hdrs.reserve(headers->size());
        for (HttpHeaders::const_iterator it(headers->begin()); it != headers->end(); ++it)
        {
            VksHttpHeader h;
            h.name = it->first.data();
            h.name_len = it->first.size();
            h.value = it->second.data();
            h.value_len = it->second.size();
            hdrs.push_back(h);
        }
    }
    VksHttpOptions opts;
    if (options)
    {
        fill_options(opts, *options);
    }
    std::string bytes;
    if (body && body->size())
    {
        bytes.resize(body->size());
        body->read(0, &bytes[0], bytes.size());
    }
    VksHttpRequestDesc desc;
    desc.policy = policy_id;
    desc.method = method;
    desc.url = url.data();
    desc.url_len = url.size();
    desc.body = reinterpret_cast<const uint8_t*>(bytes.data());
    desc.body_len = bytes.size();
    desc.has_body = body ? 1 : 0;
    desc.offset = offset;
    desc.length = len;
    desc.headers = hdrs.empty() ? NULL : hdrs.data();
    desc.header_count = hdrs.size();
    desc.options = options ? &opts : NULL;

    HttpHandler::ptr_t* user = handler ? new HttpHandler::ptr_t(handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_submit(mReplyQueue, &desc, user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

HttpHandle HttpRequest::requestGet(policy_t policy_id,
                                   const std::string & url,
                                   const HttpOptions::ptr_t & options,
                                   const HttpHeaders::ptr_t & headers,
                                   HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 0, url, NULL, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestGetByteRange(policy_t policy_id,
                                            const std::string & url,
                                            size_t offset,
                                            size_t len,
                                            const HttpOptions::ptr_t & options,
                                            const HttpHeaders::ptr_t & headers,
                                            HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 0, url, NULL, offset, len, options, headers, user_handler);
}

HttpHandle HttpRequest::requestPost(policy_t policy_id,
                                    const std::string & url,
                                    BufferArray * body,
                                    const HttpOptions::ptr_t & options,
                                    const HttpHeaders::ptr_t & headers,
                                    HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 1, url, body, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestPut(policy_t policy_id,
                                   const std::string & url,
                                   BufferArray * body,
                                   const HttpOptions::ptr_t & options,
                                   const HttpHeaders::ptr_t & headers,
                                   HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 2, url, body, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestDelete(policy_t policy_id,
    const std::string & url,
    const HttpOptions::ptr_t & options,
    const HttpHeaders::ptr_t & headers,
    HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 3, url, NULL, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestPatch(policy_t policy_id,
    const std::string & url,
    BufferArray * body,
    const HttpOptions::ptr_t & options,
    const HttpHeaders::ptr_t & headers,
    HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 4, url, body, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestCopy(policy_t policy_id,
    const std::string & url,
    const HttpOptions::ptr_t & options,
    const HttpHeaders::ptr_t & headers,
    HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 5, url, NULL, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestMove(policy_t policy_id,
    const std::string & url,
    const HttpOptions::ptr_t & options,
    const HttpHeaders::ptr_t & headers,
    HttpHandler::ptr_t user_handler)
{
    return submit(policy_id, 6, url, NULL, 0, 0, options, headers, user_handler);
}

HttpHandle HttpRequest::requestNoOp(HttpHandler::ptr_t user_handler)
{
    HttpHandler::ptr_t* user = user_handler ? new HttpHandler::ptr_t(user_handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_noop(mReplyQueue, user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

// Delivers one completed operation to its handler
// (HttpOperation::visitNotifier / HttpOpRequest::visitNotifier).
void HttpRequest::deliver(void* reply_ptr)
{
    LL_PROFILE_ZONE_SCOPED_CATEGORY_NETWORK;
    VksHttpReply* reply = static_cast<VksHttpReply*>(reply_ptr);
    HttpHandler::ptr_t* user = static_cast<HttpHandler::ptr_t*>(vks_http_reply_user(reply));
    const uint64_t handle(vks_http_reply_handle(reply));
    if (user && *user)
    {
        HttpResponse * response = new HttpResponse();
        response->setStatus(replyStatus(reply, handle));
        if (vks_http_reply_kind(reply) == 0)
        {
            size_t len(0);
            const uint8_t* bytes(vks_http_reply_body(reply, &len));
            if (len)
            {
                BufferArray * ba = new BufferArray();
                ba->append(bytes, len);
                response->setBody(ba);
                ba->release();
            }
            if (vks_http_reply_has_headers(reply))
            {
                HttpHeaders::ptr_t headers(std::make_shared<HttpHeaders>());
                const size_t count(vks_http_reply_header_count(reply));
                for (size_t i = 0; i < count; ++i)
                {
                    VksHttpHeader h;
                    if (vks_http_reply_header(reply, i, &h))
                    {
                        headers->append(std::string(h.name, h.name_len), std::string(h.value, h.value_len));
                    }
                }
                response->setHeaders(headers);
            }
            size_t url_len(0), method_len(0), ct_len(0);
            const char* url(vks_http_reply_url(reply, &url_len));
            const char* method(vks_http_reply_method(reply, &method_len));
            const char* ct(vks_http_reply_content_type(reply, &ct_len));
            response->setRequestURL(std::string(url, url_len));
            response->setRequestMethod(std::string(method, method_len));
            uint64_t offset(0), length(0), full(0);
            if (vks_http_reply_range(reply, &offset, &length, &full))
            {
                response->setRange(static_cast<unsigned int>(offset), static_cast<unsigned int>(length), static_cast<unsigned int>(full));
            }
            response->setContentType(std::string(ct, ct_len));
            response->setRetries(vks_http_reply_retries(reply), vks_http_reply_retries_503(reply));
            HttpResponse::TransferStats::ptr_t stats = std::make_shared<HttpResponse::TransferStats>();
            vks_http_reply_stats(reply, &stats->mSizeDownload, &stats->mTotalTime, &stats->mSpeedDownload);
            response->setTransferStats(stats);
        }
        (*user)->onCompleted(toHandle(handle), response);
        response->release();
    }
    else
    {
        // No handler: a request's SSL status, if any, is not needed.
        replyStatus(reply, handle);
    }
    delete user;
    vks_http_reply_free(reply);
}

HttpStatus HttpRequest::update(long usecs)
{
    if (usecs)
    {
        const HttpTime limit(totalTime() + HttpTime(usecs));
        while (limit >= totalTime())
        {
            VksHttpReply* reply = vks_http_reply_fetch(mReplyQueue);
            if (!reply)
            {
                break;
            }
            deliver(reply);
        }
    }
    else
    {
        // Same as above, just no time limit
        while (VksHttpReply* reply = vks_http_reply_fetch(mReplyQueue))
        {
            deliver(reply);
        }
    }
    return HttpStatus();
}

// ====================================
// Request Management Methods
// ====================================

HttpHandle HttpRequest::requestCancel(HttpHandle request, HttpHandler::ptr_t user_handler)
{
    HttpHandler::ptr_t* user = user_handler ? new HttpHandler::ptr_t(user_handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_cancel(mReplyQueue, fromHandle(request), user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

// ====================================
// Utility Methods
// ====================================

HttpStatus HttpRequest::createService()
{
    HttpStatus status;
    if (! has_inited)
    {
        const VksHttpHooks hooks(make_hooks());
        if (!vks_http_service_create(&hooks, APP_NAME.c_str()))
        {
            return HttpStatus(HttpStatus::LLCORE, HE_BAD_ALLOC);
        }
        HTTPStats::createInstance();
        has_inited = true;
    }
    return status;
}

HttpStatus HttpRequest::destroyService()
{
    HttpStatus status;
    if (has_inited)
    {
        // The service first: it may still run its thread, which records
        // statistics, for up to a second.
        vks_http_service_destroy();
        HTTPStats::deleteSingleton();
        {
            LLCoreInt::HttpScopedLock lock(sSslStatusMutex);
            sSslStatus.clear();
        }
        sSslCallback = nullptr;
        has_inited = false;
    }
    return status;
}

HttpStatus HttpRequest::startThread()
{
    HttpStatus status;
    vks_http_service_start_thread();
    return status;
}

HttpHandle HttpRequest::requestStopThread(HttpHandler::ptr_t user_handler)
{
    HttpHandler::ptr_t* user = user_handler ? new HttpHandler::ptr_t(user_handler) : NULL;
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_stop(mReplyQueue, user, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

HttpHandle HttpRequest::requestSpin(int mode)
{
    VksHttpStatus status;
    const uint64_t handle(vks_http_request_spin(mReplyQueue, mode, &status));
    mLastReqStatus = toStatus(status);
    return handle ? toHandle(handle) : LLCORE_HTTP_HANDLE_INVALID;
}

}   // end namespace LLCore
