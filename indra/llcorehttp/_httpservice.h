/**
 * @file _httpservice.h
 * @brief Internal declarations for the HTTP service state
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

#ifndef _LLCORE_HTTP_SERVICE_H_
#define _LLCORE_HTTP_SERVICE_H_

#include "vkstorm_ffi.h"

namespace LLCore
{

/// The HTTP service itself (request queue, service thread, policy classes,
/// retry queue, libcurl transport and reply queues) is Rust: vkstorm-http,
/// reached through vks_http_* (vkstorm-rust rung four). What remains here
/// is the state query HttpRequest and the tests use.
class HttpService
{
public:
    enum EState
    {
        NOT_INITIALIZED = -1,
        INITIALIZED,            ///< createService() has been called
        RUNNING,                ///< thread created and running
        STOPPED                 ///< thread has committed to exiting
    };

    static EState getState()
        {
            return static_cast<EState>(vks_http_service_state());
        }

    /// True once the service thread has stopped (after requestStopThread).
    static bool isStopped()
        {
            return STOPPED == getState();
        }
};

}  // end namespace LLCore

#endif // _LLCORE_HTTP_SERVICE_H_
