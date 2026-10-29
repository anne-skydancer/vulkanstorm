/**
 * @file llhttpretrypolicy.h
 * @brief Header for a retry policy class intended for use with http responders.
 *
 * $LicenseInfo:firstyear=2013&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2013, Linden Research, Inc.
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
#include "llhttpretrypolicy.h"

#include "vkstorm_ffi.h"

LLAdaptiveRetryPolicy::LLAdaptiveRetryPolicy(F32 min_delay, F32 max_delay, F32 backoff_factor, U32 max_retries, bool retry_on_4xx):
    mPolicy(vks_retry_policy_new(min_delay, max_delay, backoff_factor, max_retries, retry_on_4xx ? 1 : 0))
{
}

LLAdaptiveRetryPolicy::~LLAdaptiveRetryPolicy()
{
    vks_retry_policy_free(mPolicy);
}

void LLAdaptiveRetryPolicy::reset()
{
    vks_retry_policy_reset(mPolicy);
}

bool LLAdaptiveRetryPolicy::getRetryAfter(const LLSD& headers, F32& retry_header_time)
{
    return (headers.has(HTTP_IN_HEADER_RETRY_AFTER)
            && getSecondsUntilRetryAfter(headers[HTTP_IN_HEADER_RETRY_AFTER].asStringRef(), retry_header_time));
}

bool LLAdaptiveRetryPolicy::getRetryAfter(const LLCore::HttpHeaders::ptr_t &headers, F32& retry_header_time)
{
    if (headers)
    {
        const std::string *retry_value = headers->find(HTTP_IN_HEADER_RETRY_AFTER.c_str());
        if (retry_value &&
            getSecondsUntilRetryAfter(*retry_value, retry_header_time))
        {
            return true;
        }
    }
    return false;
}

void LLAdaptiveRetryPolicy::onSuccess()
{
    vks_retry_policy_on_success(mPolicy);
}

void LLAdaptiveRetryPolicy::onFailure(S32 status, const LLSD& headers)
{
    F32 retry_header_time{0};
    bool has_retry_header_time = getRetryAfter(headers,retry_header_time);
    onFailureCommon(status, has_retry_header_time, retry_header_time);
}

void LLAdaptiveRetryPolicy::onFailure(const LLCore::HttpResponse *response)
{
    F32 retry_header_time{0};
    const LLCore::HttpHeaders::ptr_t headers = response->getHeaders();
    bool has_retry_header_time = getRetryAfter(headers,retry_header_time);
    onFailureCommon(response->getStatus().getType(), has_retry_header_time, retry_header_time);
}

void LLAdaptiveRetryPolicy::onFailureCommon(S32 status, bool has_retry_header_time, F32 retry_header_time)
{
    vks_retry_policy_on_failure(mPolicy, status, has_retry_header_time ? 1 : 0, retry_header_time);
}

bool LLAdaptiveRetryPolicy::shouldRetry(F32& seconds_to_wait) const
{
    return vks_retry_policy_should_retry(mPolicy, &seconds_to_wait) != 0;
}

// Parses 'Retry-After' header contents and returns seconds until retry should occur.
/*static*/
bool LLAdaptiveRetryPolicy::getSecondsUntilRetryAfter(const std::string& retry_after, F32& seconds_to_wait)
{
    // The header is read here and the policy takes the seconds: the number
    // of seconds through strtod, an RFC 1123 date through libcurl's date
    // parser, as before the port.
    // Examples of Retry-After headers:
    // Retry-After: Fri, 31 Dec 1999 23:59:59 GMT
    // Retry-After: 120

    // Check for number of seconds version, first:
    char* end = 0;
    // Parse as double
    double seconds = std::strtod(retry_after.c_str(), &end);
    if (end != 0 && *end == 0)
    {
        // Successful parse
        seconds_to_wait = (F32)seconds;
        return true;
    }

    // Parse rfc1123 date.
    time_t date = curl_getdate(retry_after.c_str(), NULL);
    if (-1 == date) return false;

    seconds_to_wait = (F32)((F64)date - LLTimer::getTotalSeconds());

    return true;
}
