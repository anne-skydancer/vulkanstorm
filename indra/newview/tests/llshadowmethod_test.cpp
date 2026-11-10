// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowmethod.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowmethod_test.cpp
// The shadow method migration: an existing install follows its old PCSS checkbox exactly once, a
// fresh install is left to the feature table, the flag always ends up set, and an old preset file
// maps its checkbox to the method unless it already carries the method.

#include "llshadowmethod.h"

#include <cstdio>
#include <initializer_list>

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

    void test_values()
    {
        static_assert(LLShadowMethod::LEGACY == 0 && LLShadowMethod::PCSS == 1);
        check(LLShadowMethod::fromPCSSFlag(true) == LLShadowMethod::PCSS, "flag on is PCSS");
        check(LLShadowMethod::fromPCSSFlag(false) == LLShadowMethod::LEGACY, "flag off is Legacy");
        check(LLShadowMethod::sanitize(0) == 0, "sanitize 0");
        check(LLShadowMethod::sanitize(1) == 1, "sanitize 1");
        check(LLShadowMethod::sanitize(2) == 1, "sanitize 2");
        check(LLShadowMethod::sanitize(4000000000u) == 1, "sanitize huge");
    }

    void test_migration()
    {
        using namespace LLShadowMethod;
        for (bool pcss : { false, true })
        {
            const Migration existing = migrateOnStartup(false, false, pcss);
            check(existing.setMethod, "existing install sets the method");
            check(existing.method == (pcss ? PCSS : LEGACY), "existing install follows the checkbox");
            check(existing.markMigrated, "existing install marks the flag");

            const Migration fresh = migrateOnStartup(false, true, pcss);
            check(!fresh.setMethod, "fresh install keeps the feature table value");
            check(fresh.markMigrated, "fresh install marks the flag");

            for (bool first_run : { false, true })
            {
                const Migration done = migrateOnStartup(true, first_run, pcss);
                check(!done.setMethod && !done.markMigrated, "a migrated install is never touched again");
            }
        }
    }

    void test_presets()
    {
        using namespace LLShadowMethod;
        check(presetNeedsMapping(true, false), "old preset maps");
        check(!presetNeedsMapping(true, true), "new preset with both wins");
        check(!presetNeedsMapping(false, true), "new preset");
        check(!presetNeedsMapping(false, false), "preset without shadow settings");
    }
}

int main()
{
    test_values();
    test_migration();
    test_presets();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("llshadowmethod: all checks passed\n");
    return 0;
}
