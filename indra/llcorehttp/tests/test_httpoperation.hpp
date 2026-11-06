/**
 * @file test_httpoperation.hpp
 * @brief unit tests for the LLCore::HttpOperation-derived classes
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
#ifndef TEST_LLCORE_HTTP_OPERATION_H_
#define TEST_LLCORE_HTTP_OPERATION_H_

#include "_httpoperation.h"
#include "_httpoprequest.h" // <Vulkanstorm/> admissionKey()
#include "httphandler.h"

#include <iostream>


using namespace LLCoreInt;


namespace
{

class TestHandler : public LLCore::HttpHandler
{
public:
    virtual void onCompleted(HttpHandle, HttpResponse *)
        {
            std::cout << "TestHandler::onCompleted() invoked" << std::endl;
        }

};


}  // end namespace anonymous


namespace tut
{
    struct HttpOperationTestData
    {
        // the test objects inherit from this so the member functions and variables
        // can be referenced directly inside of the test functions.
    };

    typedef test_group<HttpOperationTestData> HttpOperationTestGroupType;
    typedef HttpOperationTestGroupType::object HttpOperationTestObjectType;
    HttpOperationTestGroupType HttpOperationTestGroup("HttpOperation Tests");

    template <> template <>
    void HttpOperationTestObjectType::test<1>()
    {
        set_test_name("HttpOpNull construction");

        // create a new ref counted object with an implicit reference
        HttpOperation::ptr_t op (new HttpOpNull());
        ensure(op.use_count() == 1);

        // release the implicit reference, causing the object to be released
        op.reset();
    }

    template <> template <>
    void HttpOperationTestObjectType::test<2>()
    {
        set_test_name("HttpOpNull construction with handlers");

        // Get some handlers
        LLCore::HttpHandler::ptr_t h1 (new TestHandler());

        // create a new ref counted object with an implicit reference
        HttpOperation::ptr_t op (new HttpOpNull());

        // Add the handlers
        op->setReplyPath(LLCore::HttpOperation::HttpReplyQueuePtr_t(), h1);

        // Check ref count
        ensure(op.use_count() == 1);

        // release the reference, releasing the operation but
        // not the handlers.
        op.reset();

        // release the handlers
        h1.reset();
    }

    // <Vulkanstorm>
    template <> template <>
    void HttpOperationTestObjectType::test<3>()
    {
        set_test_name("HttpOpRequest admission key: host and effective port, the HTTP proxy for plain http");

        using LLCore::HttpOpRequest;
        const std::string none;
        // one key for every spelling of one host and port
        ensure_equals("https default port", HttpOpRequest::admissionKey("https://cdn.example/a/b?c", none), std::string("cdn.example:443"));
        ensure_equals("https explicit port", HttpOpRequest::admissionKey("https://cdn.example:443/x", none), std::string("cdn.example:443"));
        ensure_equals("case and userinfo", HttpOpRequest::admissionKey("HTTPS://user:pw@CDN.Example:0443", none), std::string("cdn.example:443"));
        ensure_equals("http default port", HttpOpRequest::admissionKey("http://cdn.example?q", none), std::string("cdn.example:80"));
        ensure_equals("other port", HttpOpRequest::admissionKey("http://cdn.example:8002/#f", none), std::string("cdn.example:8002"));
        ensure_equals("IPv6 literal", HttpOpRequest::admissionKey("http://[::1]:8000/x", none), std::string("[::1]:8000"));
        ensure_equals("IPv6 default port", HttpOpRequest::admissionKey("https://[::1]/x", none), std::string("[::1]:443"));
        // plain http through an HTTP proxy is the proxy's connection
        ensure_equals("http via proxy", HttpOpRequest::admissionKey("http://a.example/x", "10.0.0.1:3128"), std::string("proxy 10.0.0.1:3128"));
        ensure_equals("second origin, same proxy", HttpOpRequest::admissionKey("http://b.example:8080/x", "http://10.0.0.1:3128"),
                      std::string("proxy 10.0.0.1:3128"));
        ensure_equals("proxy default port", HttpOpRequest::admissionKey("http://a.example/x", "Proxy.Example"), std::string("proxy proxy.example:1080"));
        ensure_equals("https proxy default port", HttpOpRequest::admissionKey("http://a.example/x", "https://proxy.example"),
                      std::string("proxy proxy.example:443"));
        // https is tunnelled to the origin; a SOCKS proxy relays to it
        ensure_equals("https via proxy", HttpOpRequest::admissionKey("https://a.example/x", "10.0.0.1:3128"), std::string("a.example:443"));
        ensure_equals("SOCKS proxy", HttpOpRequest::admissionKey("http://a.example/x", "socks5h://10.0.0.1"), std::string("a.example:80"));
    }
    // </Vulkanstorm>
}

#endif  // TEST_LLCORE_HTTP_OPERATION_H_
