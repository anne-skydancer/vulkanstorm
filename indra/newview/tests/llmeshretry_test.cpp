// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llmeshretry.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llmeshretry_test.cpp
// The status classifiers of the mesh repository (final answer, transient answer) and the retry rule (3 asks
// after the first, 5 s apart with 20 % jitter).  Transport errors are transient through
// HttpStatus::isRetryable(), which this standalone test does not link; the classifiers here cover HTTP codes.

#include "llmeshretry.h"

#include <cstdio>

namespace
{
    int failures = 0;

    void check(bool cond, const char* what)
    {
        if (!cond)
        {
            std::printf("FAIL %s\n", what);
            ++failures;
        }
    }

    void check_code(int code)
    {
        const bool is_final = LLMeshRetry::isFinalHttpCode(code);
        const bool transient = LLMeshRetry::isTransientHttpCode(code);
        check(!(is_final && transient), "a code is never final and transient at once");
        if (code == 404 || code == 410)
        {
            check(is_final && !transient, "404 and 410 are final");
        }
        else if (code == 403 || code == 408 || code == 425 || code == 429)
        {
            check(!is_final && transient, "403, 408, 425 and 429 are transient");
        }
        else if (code >= 499 && code <= 599)
        {
            check(!is_final && transient, "499 and 5xx are transient");
        }
        else if (code >= 400 && code <= 498)
        {
            check(is_final && !transient, "any other 4xx is final");
        }
        else
        {
            check(!is_final && !transient, "a code outside 4xx and 5xx is neither");
        }
    }
}

int main()
{
    for (int code = 100; code <= 699; ++code)
    {
        check_code(code);
    }
    check(LLMeshRetry::isFinalHttpCode(400) && LLMeshRetry::isFinalHttpCode(401) && LLMeshRetry::isFinalHttpCode(416),
          "400, 401 and 416 are final");
    check(LLMeshRetry::isTransientHttpCode(500) && LLMeshRetry::isTransientHttpCode(503)
          && LLMeshRetry::isTransientHttpCode(499), "500, 503 and 499 are transient");

    // the class of an answer for the baseline counters (boundaries); transport errors are not HTTP statuses and are
    // classed by the repository from HttpStatus, which this test does not link
    using C = LLMeshRetry::HttpClass;
    const struct { int code; C cls; } classes[] = {
        {399, C::Other}, {400, C::Final}, {403, C::Transient}, {404, C::Final}, {408, C::Transient},
        {410, C::Final}, {425, C::Transient}, {429, C::Http429}, {499, C::Transient}, {500, C::Transient},
        {599, C::Transient}, {600, C::Other}, {999, C::Other}, {200, C::Other}, {498, C::Final}};
    for (const auto& c : classes)
    {
        check(LLMeshRetry::classifyHttpCode(c.code) == c.cls, "classifyHttpCode boundary");
    }

    // 3 asks after the first one: failures 1, 2, 3 are asked again, the 4th is given up
    check(!LLMeshRetry::mayReask(0), "no failure, no ask");
    check(LLMeshRetry::mayReask(1) && LLMeshRetry::mayReask(2) && LLMeshRetry::mayReask(3), "failures 1 to 3 ask again");
    check(!LLMeshRetry::mayReask(4) && !LLMeshRetry::mayReask(100), "the 4th failure gives up");

    // the wait: 5 s, plus or minus 20 %
    check(LLMeshRetry::reaskDelay(0.5) > 4.999 && LLMeshRetry::reaskDelay(0.5) < 5.001, "middle of the jitter is 5 s");
    check(LLMeshRetry::reaskDelay(0.0) > 3.999 && LLMeshRetry::reaskDelay(0.0) < 4.001, "low end is 4 s");
    check(LLMeshRetry::reaskDelay(1.0) > 5.999 && LLMeshRetry::reaskDelay(1.0) < 6.001, "high end is 6 s");

    if (failures == 0)
    {
        std::printf("llmeshretry_test: all checks passed\n");
        return 0;
    }
    std::printf("llmeshretry_test: %d check(s) failed\n", failures);
    return 1;
}
