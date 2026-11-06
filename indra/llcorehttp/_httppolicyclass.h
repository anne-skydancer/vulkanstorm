/**
 * @file _httppolicyclass.h
 * @brief Declarations for internal class defining policy class options.
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

#ifndef _LLCORE_HTTP_POLICY_CLASS_H_
#define _LLCORE_HTTP_POLICY_CLASS_H_


#include "httprequest.h"
#include <curl/curl.h> // <Vulkanstorm/> LIBCURL_VERSION_NUM


namespace LLCore
{

/// Options struct for per-class policy options.
///
/// Combines both raw blob data access with semantics-enforcing
/// set/get interfaces.  For internal operations by the worker
/// thread, just grab the setting directly from instance and test/use
/// as needed.  When attached to external APIs (the public API
/// options interfaces) the set/get methods are available to
/// enforce correct ranges, data types, contexts, etc. and suitable
/// status values are returned.
///
/// Threading:  Single-threaded.  In practice, init thread before
/// worker starts, worker thread after.
class HttpPolicyClass
{
public:
    HttpPolicyClass();
    ~HttpPolicyClass();

    HttpPolicyClass & operator=(const HttpPolicyClass &);
    HttpPolicyClass(const HttpPolicyClass &);

public:
    HttpStatus set(HttpRequest::EPolicyOption opt, long value);
    HttpStatus get(HttpRequest::EPolicyOption opt, long * value) const;

    // <Vulkanstorm> The requests the class may have in flight at once. With
    // HTTP/1.1 pipelining (libcurl before 7.62) a deep-queue class
    // (pipelining depth above 1) ran per-host limit x depth requests over
    // per-host limit connections. libcurl 7.62 and later have no
    // pipelining, and llcorehttp speaks HTTP/1.1 to every server, so such a
    // class runs one request per connection: the rest wait in llcorehttp's
    // ready queue, where no timeout runs, not in libcurl's, where the wait
    // for a connection would count against CURLOPT_CONNECTTIMEOUT. Other
    // classes run up to their connection limit. </Vulkanstorm>
    long activeLimit() const
    {
#if LIBCURL_VERSION_NUM >= 0x073e00 // 7.62.0
        return mPipelining > 1L ? mPerHostConnectionLimit : mConnectionLimit;
#else
        return mPipelining > 1L ? mPerHostConnectionLimit * mPipelining : mConnectionLimit;
#endif
    }

public:
    long                        mConnectionLimit;
    long                        mPerHostConnectionLimit;
    long                        mPipelining;
    long                        mThrottleRate;
};  // end class HttpPolicyClass

}  // end namespace LLCore

#endif // _LLCORE_HTTP_POLICY_CLASS_H_
