// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of the GPU reset classification (no viewer libraries, no GL):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/llrender indra/llrender/tests/llglreset_test.cpp
// The viewer polls glGetGraphicsResetStatus once a frame and closes on the first reset it sees, so two things must
// hold: every reset value is seen as a reset (never as a healthy context), and the first reset is kept when the
// driver reports NO_ERROR again after completing it.

#include "llglreset.h"

#include <cstdio>
#include <cstring>

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
}

int main()
{
    using namespace LLGLReset;

    // the four values the specification allows
    check(classify(NO_ERROR_STATUS) == Kind::NoReset, "NO_ERROR is no reset");
    check(classify(GUILTY_STATUS) == Kind::Guilty, "GUILTY_CONTEXT_RESET is guilty");
    check(classify(INNOCENT_STATUS) == Kind::Innocent, "INNOCENT_CONTEXT_RESET is innocent");
    check(classify(UNKNOWN_STATUS) == Kind::Unknown, "UNKNOWN_CONTEXT_RESET is unknown");

    // anything else from a driver is still a reset
    const uint32_t odd[] = { 0x0502u /* GL_INVALID_OPERATION */, 0x0507u /* GL_CONTEXT_LOST */, 0x8252u, 0x8256u,
                             1u, 0xFFFFFFFFu };
    for (uint32_t status : odd)
    {
        check(classify(status) == Kind::Unknown, "an unlisted non-zero value is an unknown reset");
    }

    // the first reset is kept
    check(latch(Kind::NoReset, NO_ERROR_STATUS) == Kind::NoReset, "no reset stays no reset");
    check(latch(Kind::NoReset, GUILTY_STATUS) == Kind::Guilty, "the first reset is taken");
    check(latch(Kind::Guilty, NO_ERROR_STATUS) == Kind::Guilty, "NO_ERROR after a completed reset keeps it");
    check(latch(Kind::Guilty, INNOCENT_STATUS) == Kind::Guilty, "a later reset of another kind keeps the first");
    check(latch(Kind::Innocent, GUILTY_STATUS) == Kind::Innocent, "and the other way round");
    check(latch(Kind::Unknown, NO_ERROR_STATUS) == Kind::Unknown, "an unknown reset is kept too");

    // a frame-by-frame sequence as the driver may report it: healthy, the reset while it is in progress (repeated),
    // then NO_ERROR once the driver has completed it
    {
        const uint32_t frames[] = { NO_ERROR_STATUS, NO_ERROR_STATUS, INNOCENT_STATUS, INNOCENT_STATUS,
                                    NO_ERROR_STATUS, NO_ERROR_STATUS };
        Kind seen = Kind::NoReset;
        int first = -1;
        for (int i = 0; i < (int)(sizeof(frames) / sizeof(frames[0])); ++i)
        {
            const Kind before = seen;
            seen = latch(seen, frames[i]);
            if (before == Kind::NoReset && seen != Kind::NoReset)
            {
                first = i;
            }
        }
        check(first == 2, "the reset is seen in the frame it is first reported");
        check(seen == Kind::Innocent, "and it is still the reset at the end");
    }

    // the log names: one per kind, all different
    const Kind kinds[] = { Kind::NoReset, Kind::Guilty, Kind::Innocent, Kind::Unknown };
    for (Kind a : kinds)
    {
        check(name(a) != nullptr && name(a)[0] != 0, "every kind has a name");
        check(cause(a) != nullptr && cause(a)[0] != 0, "every kind has a cause");
        for (Kind b : kinds)
        {
            if (a != b)
            {
                check(std::strcmp(name(a), name(b)) != 0, "names differ");
                check(std::strcmp(cause(a), cause(b)) != 0, "causes differ");
            }
        }
    }

    // usable at compile time (the viewer's checks of the GL values are static_asserts)
    static_assert(classify(GUILTY_STATUS) == Kind::Guilty);
    static_assert(latch(Kind::Unknown, NO_ERROR_STATUS) == Kind::Unknown);

    if (failures)
    {
        std::printf("%d check(s) failed\n", failures);
        return 1;
    }
    std::printf("GPU reset classification: all checks passed\n");
    return 0;
}
