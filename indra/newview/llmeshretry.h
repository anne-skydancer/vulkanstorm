// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// The retry rule of the mesh repository for the requests that have no other way to ask again: the header of a mesh
// and the physics shape and decomposition of a mesh.  Pure functions only (no viewer libraries), so that a
// standalone test can check them, see indra/newview/tests/llmeshretry_test.cpp.
//
// Rule (owner, 2026-11-28): an ask that fails with a transient failure is asked again 3 times, about 5 s apart,
// then given up for the session with one warning in the log (no notice on screen).  A final failure is not asked
// again at all.

#ifndef LL_LLMESHRETRY_H
#define LL_LLMESHRETRY_H

namespace LLMeshRetry
{
    constexpr unsigned MAX_REASKS = 3;          // asks after the first one
    constexpr double REASK_SECONDS = 5.0;       // between two asks
    constexpr double REASK_JITTER = 0.2;        // +-20 % of the wait

    // The HTTP answers that are final: the simulator has no (usable) data and will not have it a moment later.
    // 404 (not found), 410 (gone), and any other 4xx that is not in the transient list below.
    constexpr bool isFinalHttpCode(int code)
    {
        return code >= 400 && code <= 498 && code != 403 && code != 408 && code != 425 && code != 429;
    }

    // The HTTP answers that may pass: 403, 408 (timeout), 425 (too early), 429 (too many requests), and the
    // 499..599 that HttpStatus::isRetryable() lists (5xx and the special 499).
    constexpr bool isTransientHttpCode(int code)
    {
        return code == 403 || code == 408 || code == 425 || code == 429 || (code >= 499 && code <= 599);
    }

    // The class of an HTTP answer for the baseline counters of the quiescent log line; one scheme, no overlap:
    //   Http429   429 (too many requests), counted apart because it is the rate-limit signal
    //   Transient the other transient answers (403, 408, 425, 499 and 500..599, so 5xx is in here)
    //   Final     the final 4xx (400..498 without 403, 408, 425, 429)
    //   Other     every other number (below 400, 600 and above)
    // Failures that are not an HTTP status (curl, llcorehttp) are classed Transport by the caller.
    enum class HttpClass { Http429, Transient, Final, Other };

    constexpr HttpClass classifyHttpCode(int code)
    {
        return code == 429 ? HttpClass::Http429
             : isTransientHttpCode(code) ? HttpClass::Transient
             : isFinalHttpCode(code) ? HttpClass::Final
             : HttpClass::Other;
    }

    // failures: the consecutive failures of the ask so far, this one included (the first failure is 1)
    constexpr bool mayReask(unsigned failures)
    {
        return failures >= 1 && failures <= MAX_REASKS;
    }

    // unit: a random number in [0, 1]
    constexpr double reaskDelay(double unit)
    {
        return REASK_SECONDS * (1.0 + REASK_JITTER * (2.0 * unit - 1.0));
    }
}

#endif // LL_LLMESHRETRY_H
