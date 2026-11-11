/**
 * @file test_httprequest.hpp
 * @brief unit tests for the LLCore::HttpRequest class
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
#ifndef TEST_LLCORE_HTTP_REQUEST_H_
#define TEST_LLCORE_HTTP_REQUEST_H_

#include "httprequest.h"
#include "bufferarray.h"
#include "httphandler.h"
#include "httpheaders.h"
#include "httpresponse.h"
#include "httpoptions.h"
#include "_httpservice.h"
#include "_httprequestqueue.h"
#include "_thread.h"

#include <curl/curl.h>
#include <boost/regex.hpp>
#include <sstream>
#include <chrono>
#include <iostream>

#include "llcorehttp_test.h"


using namespace LLCoreInt;

// spin/sleep waiting times for client/server exchange tests
//
// These are now fairly generous to try to get around timeout
// ('reasonable time') failures during execution on a heavily-
// loaded system where the unit test is in competition with
// other programs.
static const int LOOP_SLEEP_INTERVAL(10000);
static const int LOOP_COUNT_SHORT(500);         // 5-second dwell time
static const int LOOP_COUNT_LONG(3000);         // 30-second dwell time

namespace
{

#if defined(WIN32)

void usleep(unsigned long usec);

#endif

}

namespace tut
{

typedef std::vector<std::pair<boost::regex, boost::regex> > regex_container_t;

struct HttpRequestTestData
{
    // the test objects inherit from this so the member functions and variables
    // can be referenced directly inside of the test functions.
    int             mHandlerCalls;
    HttpStatus      mStatus;
};

class TestHandler2 : public LLCore::HttpHandler
{
public:
    TestHandler2(HttpRequestTestData * state,
                 const std::string & name)
        : mState(state),
          mName(name),
          mExpectHandle(LLCORE_HTTP_HANDLE_INVALID)
        {}

    virtual void onCompleted(HttpHandle handle, HttpResponse * response)
        {
            if (LLCORE_HTTP_HANDLE_INVALID != mExpectHandle)
            {
                ensure("Expected handle received in handler", mExpectHandle == handle);
            }
            ensure("Handler got a response", NULL != response);
            if (response && mState)
            {
                const HttpStatus actual_status(response->getStatus());
                std::ostringstream test;
                test << "Expected HttpStatus received in response.  Wanted:  "
                     << mState->mStatus.toHex() << " Received:  " << actual_status.toHex();
                ensure(test.str().c_str(), actual_status == mState->mStatus);
            }
            if (mState)
            {
                mState->mHandlerCalls++;
            }
            if (! mHeadersRequired.empty() || ! mHeadersDisallowed.empty() || ! mHeaderCounts.empty())
            {
                ensure("Response required with header check", response != NULL);
                HttpHeaders::ptr_t header(response->getHeaders());  // Will not hold onto this
                ensure("Some quantity of headers returned", header != NULL);

                for (size_t i(0); i < mHeaderCounts.size(); ++i)
                {
                    int found(0);
                    for (HttpHeaders::const_iterator iter(header->begin()); header->end() != iter; ++iter)
                    {
                        if (boost::regex_match((*iter).first, mHeaderCounts[i].first))
                        {
                            ++found;
                        }
                    }
                    std::ostringstream str;
                    str << "Header count #" << i << " " << mHeaderCounts[i].first
                        << ": wanted " << mHeaderCounts[i].second << ", found " << found;
                    ensure(str.str(), found == mHeaderCounts[i].second);
                }

                if (! mHeadersRequired.empty())
                {
                    for (int i(0); i < mHeadersRequired.size(); ++i)
                    {
                        bool found = false;
                        for (HttpHeaders::const_iterator iter(header->begin());
                             header->end() != iter;
                             ++iter)
                        {
                            // std::cerr << "Header: " << (*iter).first
                            //        << ": " << (*iter).second << std::endl;

                            if (boost::regex_match((*iter).first,
                                                   mHeadersRequired[i].first) &&
                                boost::regex_match((*iter).second,
                                                   mHeadersRequired[i].second))
                            {
                                found = true;
                                break;
                            }
                        }
                        std::ostringstream str;
                        str << "Required header #" << i << " "
                            << mHeadersRequired[i].first << "=" << mHeadersRequired[i].second
                            << " not found in response";
                        ensure(str.str(), found);
                    }
                }

                if (! mHeadersDisallowed.empty())
                {
                    for (int i(0); i < mHeadersDisallowed.size(); ++i)
                    {
                        for (HttpHeaders::const_iterator iter(header->begin());
                             header->end() != iter;
                             ++iter)
                        {
                            if (boost::regex_match((*iter).first,
                                                   mHeadersDisallowed[i].first) &&
                                boost::regex_match((*iter).second,
                                                   mHeadersDisallowed[i].second))
                            {
                                std::ostringstream str;
                                str << "Disallowed header #" << i << " "
                                    << mHeadersDisallowed[i].first << "=" << mHeadersDisallowed[i].second
                                    << " found in response";
                                ensure(str.str(), false);
                            }
                        }
                    }
                }
            }

            if (! mCheckContentType.empty())
            {
                ensure("Response required with content type check", response != NULL);
                std::string con_type(response->getContentType());
                ensure("Content-Type as expected (" + mCheckContentType + ")",
                       mCheckContentType == con_type);
            }

            // std::cout << "TestHandler2::onCompleted() invoked" << std::endl;
        }

    HttpRequestTestData * mState;
    std::string mName;
    HttpHandle mExpectHandle;
    std::string mCheckContentType;
    regex_container_t mHeadersRequired;
    regex_container_t mHeadersDisallowed;
    // Header names (regex) that must appear exactly this many times
    std::vector<std::pair<boost::regex, int> > mHeaderCounts;
};

typedef test_group<HttpRequestTestData> HttpRequestTestGroupType;
typedef HttpRequestTestGroupType::object HttpRequestTestObjectType;
HttpRequestTestGroupType HttpRequestTestGroup("HttpRequest Tests");

template <> template <>
void HttpRequestTestObjectType::test<1>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest construction");

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // release the request object
        delete req;
        req = NULL;

        HttpRequest::destroyService();
    }
    catch (...)
    {
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<2>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest and Null Op queued");

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a NoOp
        HttpHandle handle = req->requestNoOp(LLCore::HttpHandler::ptr_t());
        ensure("Request issued", handle != LLCORE_HTTP_HANDLE_INVALID);

        // release the request object
        delete req;
        req = NULL;

        // Request queue should have two references:  global singleton & service object
        ensure("Two references to request queue", 2 == HttpRequestQueue::instanceOf()->getRefCount());

        // Okay, tear it down
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

namespace
{
    void NoOpDeletor(LLCore::HttpHandler *) { }
}

template <> template <>
void HttpRequestTestObjectType::test<3>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest NoOp + Stop execution");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a NoOp
        HttpHandle handle = req->requestNoOp(handlerp);
        ensure("Valid handle returned for first request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<4>()
{
    ScopedCurlInit ready;

    set_test_name("2 HttpRequest instances, one thread");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    TestHandler2 handler1(this, "handler1");
    TestHandler2 handler2(this, "handler2");

    LLCore::HttpHandler::ptr_t handler1p(&handler1, NoOpDeletor);
    LLCore::HttpHandler::ptr_t handler2p(&handler2, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req1 = NULL;
    HttpRequest * req2 = NULL;

    try
    {

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req1 = new HttpRequest();
        req2 = new HttpRequest();

        // Issue some NoOps
        HttpHandle handle = req1->requestNoOp(handler1p);
        ensure("Valid handle returned for first request", handle != LLCORE_HTTP_HANDLE_INVALID);
        handler1.mExpectHandle = handle;

        handle = req2->requestNoOp(handler2p);
        ensure("Valid handle returned for first request", handle != LLCORE_HTTP_HANDLE_INVALID);
        handler2.mExpectHandle = handle;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 2)
        {
            req1->update(1000000);
            req2->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 2);

        // Okay, request a shutdown of the servicing thread
        handle = req2->requestStopThread(handler2p);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);
        handler2.mExpectHandle = handle;

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 3)
        {
            req1->update(1000000);
            req2->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 3);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req1;
        req1 = NULL;
        delete req2;
        req2 = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 3 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req1);
        delete req1;
        delete req2;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<5>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest Spin (soft) + NoOp + destroy with running thread");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a Spin
        HttpHandle handle = req->requestSpin(1);
        ensure("Valid handle returned for spin request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Issue a NoOp
        handle = req->requestNoOp(handlerp);
        ensure("Valid handle returned for no-op request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("NoOp notification received", mHandlerCalls == 1);

        // release the request object
        delete req;
        req = NULL;

        // Shut down service with the worker still running (it is
        // soft-spinning): destroyService() stops and joins it.
        HttpRequest::destroyService();
        ensure("No HttpThread left alive", HttpThread::liveCount() == 0);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<6>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest hard Spin + NoOp + cooperative stop");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a Spin
        HttpHandle handle = req->requestSpin(0);        // Hard spin
        ensure("Valid handle returned for spin request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Issue a NoOp
        handle = req->requestNoOp(handlerp);
        ensure("Valid handle returned for no-op request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_SHORT);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("No notifications received", mHandlerCalls == 0);

        // release the request object
        delete req;
        req = NULL;

        // Shut down service while the worker is in the hard spin: the
        // spin ends when the service is asked to stop, so this must
        // return promptly and leave no thread behind.
        const auto t0(std::chrono::steady_clock::now());
        HttpRequest::destroyService();
        const auto elapsed(std::chrono::steady_clock::now() - t0);
        ensure("destroyService() returned in under 5 s", elapsed < std::chrono::seconds(5));
        ensure("No HttpThread left alive", HttpThread::liveCount() == 0);

        // The library must be usable again afterwards.
        mHandlerCalls = 0;
        HttpRequest::createService();
        HttpRequest::startThread();
        req = new HttpRequest();
        handle = req->requestNoOp(handlerp);
        ensure("Valid handle returned for second NoOp", handle != LLCORE_HTTP_HANDLE_INVALID);
        count = 0;
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second NoOp notification received", mHandlerCalls == 1);
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for stop request", handle != LLCORE_HTTP_HANDLE_INVALID);
        count = 0;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Stop notification received", mHandlerCalls == 2);
        delete req;
        req = NULL;
        HttpRequest::destroyService();
        ensure("No HttpThread left alive after restart", HttpThread::liveCount() == 0);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<7>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest GET to dead port + Stop execution");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");

    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setRetries(1);            // Don't try for too long - default retries take about 18S

        // Issue a GET that can't connect
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_COULDNT_CONNECT);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     "http://127.0.0.1:2/nothing/here",
                                                     0,
                                                     0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<8>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest GET to real service");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<9>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest GET with Range: header to real service");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                     0,
                                                     HttpOptions::ptr_t(),
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<10>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest PUT to real service");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    BufferArray * body = new BufferArray;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        static const char * body_text("Now is the time for all good men...");
        body->append(body_text, strlen(body_text));
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            body,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // Lose the request body
        body->release();
        body = NULL;

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        if (body)
        {
            body->release();
        }
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<11>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest POST to real service");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    BufferArray * body = new BufferArray;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        static const char * body_text("Now is the time for all good men...");
        body->append(body_text, strlen(body_text));
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base,
                                             body,
                                             HttpOptions::ptr_t(),
                                             HttpHeaders::ptr_t(),
                                             handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // Lose the request body
        body->release();
        body = NULL;

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        if (body)
        {
            body->release();
        }
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<12>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest GET with some tracing");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Enable tracing
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::DEFAULT_POLICY_ID, 2, NULL);

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                     0,
                                                     HttpOptions::ptr_t(),
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<13>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest GET with returned headers");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    handler.mHeadersRequired.reserve(20);               // Avoid memory leak test failure
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Enable tracing
        HttpRequest::setStaticPolicyOption(HttpRequest::PO_TRACE, HttpRequest::DEFAULT_POLICY_ID, 2, NULL);

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setWantHeaders(true);

        // Issue a GET that succeeds
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(boost::regex("X-LL-Special", boost::regex::icase),
                                          boost::regex(".*", boost::regex::icase)));
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                 0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // release options
        opts.reset();

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


template <> template <>
void HttpRequestTestObjectType::test<14>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest GET timeout");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    std::string url_base(get_base_url() + "/sleep/");   // path to a 5-second sleep
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions);
        opts->setRetries(0);            // Don't retry
        opts->setTimeout(2);

        // Issue a GET that sleeps
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_OPERATION_TIMEDOUT);
        HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                     url_base,
                                                     0,
                                                     0,
                                                     opts,
                                                     HttpHeaders::ptr_t(),
                                                     handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// Test retrieval of Content-Type/Content-Encoding headers
template <> template <>
void HttpRequestTestObjectType::test<15>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("HttpRequest GET with Content-Type");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);

    // Load and clear the string setting to preload std::string object
    // for memory return tests.
    handler.mCheckContentType = "application/llsd+xml";
    handler.mCheckContentType.clear();
    mHandlerCalls = 0;

    HttpRequest * req = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        handler.mCheckContentType = "application/llsd+xml";
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mCheckContentType.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();

        ensure("Two handler calls on the way out", 2 == mHandlerCalls);
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on GET requests
template <> template <>
void HttpRequestTestObjectType::test<16>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest GET");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // Issue a GET that *can* connect
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            options,
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Do a texture-style fetch
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("Accept", "image/x-j2c");

        mStatus = HttpStatus(200);
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("image/x-j2c", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("\\W*X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                          url_base + "reflect/",
                                          0,
                                          47,
                                          options,
                                          headers,
                                          handlerp);
        ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 2);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 3)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 3);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on POST requests
template <> template <>
void HttpRequestTestObjectType::test<17>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest POST");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // And a buffer array
        const char * msg("It was the best of times, it was the worst of times.");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default POST
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-length", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("application/x-www-form-urlencoded", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-expect", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer_encoding", boost::regex::icase),
                boost::regex(".*chunked.*", boost::regex::icase)));
        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base + "reflect/",
                                             ba,
                                             options,
                                             HttpHeaders::ptr_t(),
                                             handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on PUT requests
template <> template <>
void HttpRequestTestObjectType::test<18>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest PUT");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // And a buffer array
        const char * msg("It was the best of times, it was the worst of times.");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default PUT
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-length", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-expect", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*chunked.*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));

        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            ba,
                                            options,
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on GET requests with overrides
template <> template <>
void HttpRequestTestObjectType::test<19>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest GET with header overrides");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("Keep-Alive", "120");
        headers->append("Accept-encoding", "deflate");
        headers->append("Accept", "text/plain");

        // Issue a GET with modified headers
        mStatus = HttpStatus(200);
        // The caller set Keep-Alive but not Connection: no default Connection
        // header is sent either (the two defaults go together).
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("text/plain", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("deflate", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("120", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("300", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            options,
                                            headers,
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        options.reset();
        headers.reset();

        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on POST requests with overrides
template <> template <>
void HttpRequestTestObjectType::test<20>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest POST with header overrides");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders());
        headers->append("keep-Alive", "120");
        headers->append("Accept", "text/html");
        headers->append("content-type", "application/llsd+xml");
        headers->append("cache-control", "no-store");

        // And a buffer array
        const char * msg("<xml><llsd><string>It was the best of times, it was the worst of times.</string></llsd></xml>");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default POST
        mStatus = HttpStatus(200);
        // The caller set Keep-Alive but not Connection: no default Connection
        // header is sent either (the two defaults go together).
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("text/html", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("120", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-length", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("application/llsd\\+xml", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex("no-store", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("application/x-www-form-urlencoded", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("300", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-expect", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));

        HttpHandle handle = req->requestPost(HttpRequest::DEFAULT_POLICY_ID,
                                             url_base + "reflect/",
                                             ba,
                                             options,
                                             headers,
                                             handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test header generation on PUT requests with overrides
template <> template <>
void HttpRequestTestObjectType::test<21>()
{
    ScopedCurlInit ready;

    // Warmup boost::regex to pre-alloc memory for memory size tests
    boost::regex warmup("askldjflasdj;f", boost::regex::icase);
    boost::regex_match("akl;sjflajfk;ajsk", warmup);

    std::string url_base(get_base_url());

    set_test_name("Header generation for HttpRequest PUT with header overrides");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;
    BufferArray * ba = NULL;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        // headers
        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("content-type", "text/plain");
        headers->append("content-type", "text/html");
        headers->append("content-type", "application/llsd+xml");

        // And a buffer array
        const char * msg("<xml><llsd><string>It was the best of times, it was the worst of times.</string></llsd></xml>");
        ba = new BufferArray;
        ba->append(msg, strlen(msg));

        // Issue a default PUT
        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept", boost::regex::icase),
                boost::regex("\\*/\\*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-accept-encoding", boost::regex::icase),
                boost::regex("((gzip|deflate),\\s*)+(gzip|deflate)", boost::regex::icase))); // close enough
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-host", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-length", boost::regex::icase),
                boost::regex("\\d+", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("application/llsd\\+xml", boost::regex::icase)));
        // The three content-type appends above are all sent, in the order
        // appended, as three separate Content-Type header lines, and the test
        // server reflects each as its own X-Reflect-content-type header.
        // Each must be present; their relative order is not asserted.
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("text/plain", boost::regex::icase)));
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-type", boost::regex::icase),
                boost::regex("text/html", boost::regex::icase)));

        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-cache-control", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-pragma", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-range", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-referer", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-content-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-expect", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-transfer-encoding", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        HttpHandle handle = req->requestPut(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            ba,
                                            options,
                                            headers,
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);
        ba->release();
        ba = NULL;

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);


        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 2);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options & headers
        options.reset();
        headers.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        if (ba)
        {
            ba->release();
            ba = NULL;
        }
        options.reset();
        headers.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// BUG-2295 Tests - Content-Range header received but no body
template <> template <>
void HttpRequestTestObjectType::test<22>()
{
    ScopedCurlInit ready;

    std::string url_base(get_base_url());
    // std::cerr << "Base:  "  << url_base << std::endl;

    set_test_name("BUG-2295");

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpOptions::ptr_t options;
    HttpRequest * req = NULL;

    try
    {
        // options set
        options = HttpOptions::ptr_t(new HttpOptions());
        options->setRetries(1);         // Partial_File is retryable and can timeout in here
        options->setDNSCacheTimeout(30);

        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        // ======================================
        // Issue bug2295 GETs that will get a 206
        // ======================================
        mStatus = HttpStatus(206);
        static const int test_count(3);
        for (int i(0); i < test_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < test_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time - ms1", count < limit);
        ensure("One handler invocation for each request - ms1", mHandlerCalls == test_count);

        // ======================================
        // Issue bug2295 GETs that will get a libcurl 18 (PARTIAL_FILE)
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_PARTIAL_FILE);
        static const int test2_count(1);
        for (int i(0); i < test2_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/00000012/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test2_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time - ms2", count < limit);
        ensure("One handler invocation for each request - ms2", mHandlerCalls == test2_count);

        // ======================================
        // Issue bug2295 GETs that will get an llcorehttp HE_INV_CONTENT_RANGE_HDR status
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::LLCORE, HE_INV_CONTENT_RANGE_HDR);
        static const int test3_count(1);
        for (int i(0); i < test3_count; ++i)
        {
            char buffer[128];
            snprintf(buffer, sizeof(buffer), "/bug2295/inv_cont_range/%d/", i);
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + buffer,
                                                         0,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test3_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time - ms3", count < limit);
        ensure("One handler invocation for each request - ms3", mHandlerCalls == test3_count);

        // ======================================
        // <Vulkanstorm> A 206 that starts elsewhere than asked gets
        // HE_INV_CONTENT_RANGE_HDR: the peer answers bytes 0-75 to a request
        // at offset 10. (A longer reply at the asked offset, above, is
        // delivered.) </Vulkanstorm>
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::LLCORE, HE_INV_CONTENT_RANGE_HDR);
        static const int test4_count(1);
        for (int i(0); i < test4_count; ++i)
        {
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + "/bug2295/0/",
                                                         10,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test4_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time - ms4", count < limit);
        ensure("One handler invocation for each request - ms4", mHandlerCalls == test4_count);

        // ======================================
        // <Vulkanstorm> So does a 206 without Content-Range to a request at
        // offset 10: it reads as offset 0. </Vulkanstorm>
        // ======================================
        mHandlerCalls = 0;
        mStatus = HttpStatus(HttpStatus::LLCORE, HE_INV_CONTENT_RANGE_HDR);
        static const int test5_count(1);
        for (int i(0); i < test5_count; ++i)
        {
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url_base + "/bug2295/no_range/",
                                                         10,
                                                         25,
                                                         options,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);
            ensure("Valid handle returned for ranged request", handle != LLCORE_HTTP_HANDLE_INVALID);
        }

        // Run the notification pump.
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < test5_count)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time - ms5", count < limit);
        ensure("One handler invocation for each request - ms5", mHandlerCalls == test5_count);

        // ======================================
        // Okay, request a shutdown of the servicing thread
        // ======================================
        mStatus = HttpStatus();
        mHandlerCalls = 0;
        HttpHandle handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Shutdown request executed in reasonable time", count < limit);
        ensure("Shutdown handler invocation", mHandlerCalls == 1);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options
        options.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

template <> template <>
void HttpRequestTestObjectType::test<23>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest GET 503s with 'Retry-After'");

    // This tests mainly that the code doesn't fall over if
    // various well- and mis-formed Retry-After headers are
    // sent along with the response.  Direct inspection of
    // the parsing result isn't supported.

    // Handler can be stack-allocated *if* there are no dangling
    // references to it after completion of this method.
    // Create before memory record as the string copy will bump numbers.
    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    std::string url_base(get_base_url() + "/503/"); // path to 503 generators
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t opts;

    try
    {
        // Get singletons created
        HttpRequest::createService();

        // Start threading early so that thread memory is invariant
        // over the test.
        HttpRequest::startThread();

        // create a new ref counted object with an implicit reference
        req = new HttpRequest();

        opts = HttpOptions::ptr_t(new HttpOptions());
        opts->setRetries(1);            // Retry once only
        opts->setUseRetryAfter(true);   // Try to parse the retry-after header

        // Issue a GET that 503s with valid retry-after
        mStatus = HttpStatus(503);
        int url_limit(6);
        for (int i(0); i < url_limit; ++i)
        {
            std::ostringstream url;
            url << url_base << i << "/";
            HttpHandle handle = req->requestGetByteRange(HttpRequest::DEFAULT_POLICY_ID,
                                                         url.str(),
                                                         0,
                                                         0,
                                                         opts,
                                                         HttpHeaders::ptr_t(),
                                                         handlerp);

            std::ostringstream testtag;
            testtag << "Valid handle returned for 503 request #" << i;
            ensure(testtag.str(), handle != LLCORE_HTTP_HANDLE_INVALID);
        }


        // Run the notification pump.
        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < url_limit)
        {
            req->update(0);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == url_limit);

        // Okay, request a shutdown of the servicing thread
        mStatus = HttpStatus();
        mHandlerCalls = 0;
        HttpHandle handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        // Run the notification pump again
        count = 0;
        limit = LOOP_COUNT_LONG;
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second request executed in reasonable time", count < limit);
        ensure("Second handler invocation", mHandlerCalls == 1);

        // See that we actually shutdown the thread
        count = 0;
        limit = LOOP_COUNT_SHORT;
        while (count++ < limit && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread actually stopped running", HttpService::isStopped());

        // release options
        opts.reset();

        // release the request object
        delete req;
        req = NULL;

        // Shut down service
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        opts.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// <Vulkanstorm> Content-Encoding as libcurl 7.54.1 handled it: gzip and
// deflate (zlib or raw) are undone, any other value (the asset CDN's
// "binary/octet-stream") passes through untouched, and gzip data that does
// not decode fails with CURLE_BAD_CONTENT_ENCODING, as does a zlib or gzip
// stream that fails after its header (no raw-deflate retry then).
namespace
{
class EncodingHandler : public LLCore::HttpHandler
{
public:
    virtual void onCompleted(HttpHandle, HttpResponse * response)
        {
            mStatus = response->getStatus();
            mBody.clear();
            BufferArray * body(response->getBody());
            if (body && body->size())
            {
                mBody.resize(body->size());
                body->read(0, &mBody[0], mBody.size());
            }
            ++mCalls;
        }
    HttpStatus mStatus;
    std::string mBody;
    int mCalls = 0;
};
}

template <> template <>
void HttpRequestTestObjectType::test<24>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest GET with gzip (gzip or zlib wrapper, several members, trailing bytes), deflate and unknown Content-Encoding");

    std::string text;
    for (int i(0); i < 40; ++i)
    {
        text += "Vulkanstorm content encoding test body. ";
    }

    EncodingHandler handler;
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    HttpRequest * req = NULL;

    try
    {
        HttpRequest::createService();
        HttpRequest::startThread();
        req = new HttpRequest();

        struct Case { const char * path; bool ok; bool decoded; };
        const Case cases[] = {
            { "/encoding/gzip/", true, true },
            { "/encoding/gzipzlib/", true, true },
#if LIBCURL_VERSION_NUM >= 0x075300 // the viewer's decoder; libcurl 7.54.1's stops at the first member's end
            { "/encoding/gzipmulti/", true, true },
#endif
            { "/encoding/gziptrail/", true, true },
            { "/encoding/gzipraw/", true, true },
            { "/encoding/gziplist/", true, true },
            { "/encoding/deflate/", true, true },
            { "/encoding/rawdeflate/", true, true },
            { "/encoding/binary/", true, false },
            { "/encoding/none/", true, false },
            { "/encoding/badgzip/", false, false },
#if LIBCURL_VERSION_NUM >= 0x075300 // the viewer's decoder: no raw-deflate retry after a failure past the header
            { "/encoding/deflatebadsum/", false, false },
            { "/encoding/gzipbadcrc/", false, false },
            { "/encoding/zlibtwinbad/", false, false },
#endif
        };
        for (const Case & c : cases)
        {
            handler.mCalls = 0;
            HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                                get_base_url() + c.path,
                                                HttpOptions::ptr_t(),
                                                HttpHeaders::ptr_t(),
                                                handlerp);
            ensure(std::string("Valid handle for ") + c.path, handle != LLCORE_HTTP_HANDLE_INVALID);
            int count(0);
            while (count++ < LOOP_COUNT_LONG && handler.mCalls < 1)
            {
                req->update(1000000);
                usleep(LOOP_SLEEP_INTERVAL);
            }
            ensure(std::string("Reply in time for ") + c.path, handler.mCalls == 1);
            if (c.ok)
            {
                ensure(std::string("Success for ") + c.path, bool(handler.mStatus));
                ensure_equals(std::string("Body for ") + c.path, handler.mBody, text);
            }
            else
            {
                ensure(std::string("Bad encoding refused for ") + c.path,
                       handler.mStatus == HttpStatus(HttpStatus::EXT_CURL_EASY, CURLE_BAD_CONTENT_ENCODING));
            }
        }

        // zlib output still pending when the input runs out must not be lost;
        // a valid zlib stream that also reads as raw deflate decodes as zlib
        const std::string twin(std::string("\x01\x00\xfe\xffX\x01\x00\x00\xff\xff", 10) + std::string(65524, 'A'));
        for (const auto & big : { std::make_pair("/encoding/rawbig/", std::string(16400, 'A')),
                                  std::make_pair("/encoding/gzipbig/", std::string(70000, 'A')),
                                  std::make_pair("/encoding/zlibtwin/", twin) })
        {
            handler.mCalls = 0;
            HttpHandle bh = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            get_base_url() + big.first,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
            ensure(std::string("Valid handle for ") + big.first, bh != LLCORE_HTTP_HANDLE_INVALID);
            int count(0);
            while (count++ < LOOP_COUNT_LONG && handler.mCalls < 1)
            {
                req->update(1000000);
                usleep(LOOP_SLEEP_INTERVAL);
            }
            ensure(std::string("Reply in time for ") + big.first, handler.mCalls == 1);
            ensure(std::string("Success for ") + big.first, bool(handler.mStatus));
            ensure_equals(std::string("Body size for ") + big.first, handler.mBody.size(), big.second.size());
            ensure(std::string("Body for ") + big.first, handler.mBody == big.second);
        }

        handler.mCalls = 0;
        HttpHandle handle = req->requestStopThread(handlerp);
        ensure("Valid handle for stop", handle != LLCORE_HTTP_HANDLE_INVALID);
        int count(0);
        while (count++ < LOOP_COUNT_LONG && handler.mCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        count = 0;
        while (count++ < LOOP_COUNT_SHORT && ! HttpService::isStopped())
        {
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Thread stopped", HttpService::isStopped());
        delete req;
        req = NULL;
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}


// Test that a caller-supplied Connection header replaces both default
// Connection/Keep-alive headers and is not duplicated
template <> template <>
void HttpRequestTestObjectType::test<25>()
{
    ScopedCurlInit ready;

    set_test_name("Header generation for HttpRequest GET with caller Connection: close");

    std::string url_base(get_base_url());

    TestHandler2 handler(this, "handler");
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    mHandlerCalls = 0;

    HttpRequest * req = NULL;
    HttpOptions::ptr_t options;
    HttpHeaders::ptr_t headers;

    try
    {
        HttpRequest::createService();
        HttpRequest::startThread();
        req = new HttpRequest();

        options = HttpOptions::ptr_t(new HttpOptions());
        options->setWantHeaders(true);

        headers = HttpHeaders::ptr_t(new HttpHeaders);
        headers->append("connection", "close");

        mStatus = HttpStatus(200);
        handler.mHeadersRequired.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("close", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-connection", boost::regex::icase),
                boost::regex("keep-alive", boost::regex::icase)));
        handler.mHeadersDisallowed.push_back(
            regex_container_t::value_type(
                boost::regex("X-Reflect-keep-alive", boost::regex::icase),
                boost::regex(".*", boost::regex::icase)));
        // exactly one Connection header: no duplicate default
        handler.mHeaderCounts.push_back(
            std::make_pair(boost::regex("X-Reflect-connection", boost::regex::icase), 1));

        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url_base + "reflect/",
                                            options,
                                            headers,
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);

        int count(0);
        int limit(LOOP_COUNT_LONG);
        while (count++ < limit && mHandlerCalls < 1)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Request executed in reasonable time", count < limit);
        ensure("One handler invocation for request", mHandlerCalls == 1);

        mStatus = HttpStatus();
        handler.mHeadersRequired.clear();
        handler.mHeadersDisallowed.clear();
        handler.mHeaderCounts.clear();
        handle = req->requestStopThread(handlerp);
        ensure("Valid handle returned for second request", handle != LLCORE_HTTP_HANDLE_INVALID);

        count = 0;
        while (count++ < limit && mHandlerCalls < 2)
        {
            req->update(1000000);
            usleep(LOOP_SLEEP_INTERVAL);
        }
        ensure("Second handler invocation", mHandlerCalls == 2);

        options.reset();
        headers.reset();
        delete req;
        req = NULL;
        HttpRequest::destroyService();
    }
    catch (...)
    {
        stop_thread(req);
        options.reset();
        headers.reset();
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}

// Quit while a request is stuck in the network: the worker must end by
// itself and be joined, and destroyService() must return, without any forced
// stop. The request is answered as canceled, or as failed if it ended first.
namespace
{
class TestHandlerQuit : public LLCore::HttpHandler
{
public:
    virtual void onCompleted(HttpHandle, HttpResponse * response)
    {
        ++mCalls;
        if (response)
        {
            mStatus = response->getStatus();
        }
    }
    int mCalls = 0;
    HttpStatus mStatus;
};

// Reads the peer's /hanghits/ answer: the number of /hang/ requests it received.
class TestHandlerHits : public LLCore::HttpHandler
{
public:
    virtual void onCompleted(HttpHandle, HttpResponse * response)
    {
        mHits = -1;
        BufferArray * body(response ? response->getBody() : NULL);
        if (body && body->size())
        {
            std::string text(body->size(), '\0');
            body->read(0, &text[0], text.size());
            mHits = atoi(text.c_str());
        }
        ++mCalls;
    }
    int mCalls = 0;
    int mHits = -1;
};

// Timing notes go to stdout only when LL_TEST_VERBOSE is set.
void note_time(const char * what, std::chrono::milliseconds elapsed)
{
    if (getenv("LL_TEST_VERBOSE"))
    {
        std::cout << "[" << what << " took " << elapsed.count() << " ms] " << std::endl;
    }
}

// Issues a GET to `url`, destroys the service, and returns how long
// destroyService() took.
//  wait_for_peer: the peer counts requests to /hang/; wait until it received
//    this one, so the request is known to be in the transport and at the
//    server, then check it is still unanswered. Otherwise the worker gets a
//    short dwell to start on the request, nothing more is assumed about where
//    it is (a lookup that fails at once has already completed).
// `req` stays alive across destroyService(): one update() afterwards must
// deliver exactly one handler call, canceled if the request was still pending.
std::chrono::milliseconds quit_during_request(const std::string & url, bool wait_for_peer)
{
    HttpRequest * req = NULL;
    TestHandlerQuit handler;
    LLCore::HttpHandler::ptr_t handlerp(&handler, NoOpDeletor);
    TestHandlerHits hits;
    LLCore::HttpHandler::ptr_t hitsp(&hits, NoOpDeletor);
    try
    {
        HttpRequest::createService();
        HttpRequest::startThread();
        req = new HttpRequest();

        const std::string hits_url(get_base_url() + "hanghits/");
        int baseline(0);
        if (wait_for_peer)
        {
            hits.mCalls = 0;
            ensure("Valid handle for the hit count request",
                   req->requestGet(HttpRequest::DEFAULT_POLICY_ID, hits_url,
                                   HttpOptions::ptr_t(), HttpHeaders::ptr_t(), hitsp)
                   != LLCORE_HTTP_HANDLE_INVALID);
            for (int i(0); i < LOOP_COUNT_LONG && hits.mCalls < 1; ++i)
            {
                req->update(1000000);
                usleep(LOOP_SLEEP_INTERVAL);
            }
            ensure("Hit count read", hits.mCalls == 1 && hits.mHits >= 0);
            baseline = hits.mHits;
        }

        HttpHandle handle = req->requestGet(HttpRequest::DEFAULT_POLICY_ID,
                                            url,
                                            HttpOptions::ptr_t(),
                                            HttpHeaders::ptr_t(),
                                            handlerp);
        ensure("Valid handle returned for get request", handle != LLCORE_HTTP_HANDLE_INVALID);

        if (wait_for_peer)
        {
            // Ask the peer, over the same service, until it has seen the request.
            int seen(baseline);
            for (int i(0); i < LOOP_COUNT_LONG && seen <= baseline; ++i)
            {
                hits.mCalls = 0;
                req->requestGet(HttpRequest::DEFAULT_POLICY_ID, hits_url,
                                HttpOptions::ptr_t(), HttpHeaders::ptr_t(), hitsp);
                for (int j(0); j < 100 && hits.mCalls < 1; ++j)
                {
                    req->update(1000000);
                    usleep(LOOP_SLEEP_INTERVAL);
                }
                seen = (hits.mCalls == 1) ? hits.mHits : baseline;
            }
            ensure("The peer received the request", seen > baseline);
            ensure("The request is still unanswered", handler.mCalls == 0);
        }
        else
        {
            for (int i(0); i < 30; ++i)
            {
                req->update(1000000);
                usleep(LOOP_SLEEP_INTERVAL);
            }
        }

        const auto t0(std::chrono::steady_clock::now());
        HttpRequest::destroyService();
        const auto elapsed(std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::steady_clock::now() - t0));
        ensure("No HttpThread left alive", HttpThread::liveCount() == 0);

        // The reply queue outlives the worker: the request is answered now.
        req->update(1000000);
        ensure("One handler call for the request", handler.mCalls == 1);
        if (wait_for_peer)
        {
            ensure("The request was canceled",
                   handler.mStatus == HttpStatus(HttpStatus::LLCORE, HE_OP_CANCELED));
        }
        delete req;
        req = NULL;
        return elapsed;
    }
    catch (...)
    {
        stop_thread(req);
        delete req;
        HttpRequest::destroyService();
        throw;
    }
}
}

// A server that accepts the connection and never answers
template <> template <>
void HttpRequestTestObjectType::test<26>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest quit during a request the server never answers");

    const auto elapsed(quit_during_request(get_base_url() + "hang/", true));
    note_time("destroyService with a hung server", elapsed);
    ensure("destroyService() returned in under 5 s", elapsed < std::chrono::seconds(5));
}

// Quitting while a name lookup may be in progress never hangs. The name is
// under .invalid (RFC 6761: it never resolves). Where a lookup takes a while
// it is still running at quit and libcurl's cleanup joins it (see
// HttpLibcurl::shutdown()); where it fails at once the request has completed
// before the quit. Both are fine: the test only asserts that the quit returns
// within the resolver's bound, that the worker is joined, and that the
// request got exactly one answer. It says nothing about where the request was.
template <> template <>
void HttpRequestTestObjectType::test<27>()
{
    ScopedCurlInit ready;

    set_test_name("HttpRequest quitting during a name lookup never hangs");

    const auto elapsed(quit_during_request("http://vkstorm-stuck-lookup-probe.invalid/", false));
    note_time("destroyService during a name lookup", elapsed);
    ensure("destroyService() returned within the resolver bound (60 s)", elapsed < std::chrono::seconds(60));
}
// </Vulkanstorm>

}  // end namespace tut

namespace
{

#if defined(WIN32)

void usleep(unsigned long usec)
{
    Sleep((DWORD) (usec / 1000UL));
}

#endif

}

#endif  // TEST_LLCORE_HTTP_REQUEST_H_
