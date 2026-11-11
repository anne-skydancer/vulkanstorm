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
#include <map>
#include <string>

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
        static_assert(LLShadowMethod::LEGACY == 0 && LLShadowMethod::PCSS == 1 && LLShadowMethod::STOCHASTIC == 2);
        check(LLShadowMethod::fromPCSSFlag(true) == LLShadowMethod::PCSS, "flag on is PCSS");
        check(LLShadowMethod::fromPCSSFlag(false) == LLShadowMethod::LEGACY, "flag off is Legacy");
        check(LLShadowMethod::sanitize(0) == 0, "sanitize 0");
        check(LLShadowMethod::sanitize(1) == 1, "sanitize 1");
        check(LLShadowMethod::sanitize(2) == 2, "sanitize 2");
        check(LLShadowMethod::sanitize(3) == 1, "sanitize 3");
        check(LLShadowMethod::sanitize(4000000000u) == 1, "sanitize huge");
        check(!LLShadowMethod::usesStableCascades(0) && LLShadowMethod::usesStableCascades(1)
            && LLShadowMethod::usesStableCascades(2), "stable cascades for every method but Legacy");
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

    // A stand-in for the LLSD map of a parsed preset file: {name: {Comment, Type, Value}}
    struct Node
    {
        std::map<std::string, Node> children;
        bool value = false;
        bool has(const char* key) const { return children.count(key) != 0; }
        const Node& operator[](const char* key) const
        {
            static const Node empty;
            const auto it = children.find(key);
            return it == children.end() ? empty : it->second;
        }
        bool asBoolean() const { return value; }
    };

    Node setting(bool value)
    {
        Node n;
        n.children["Value"].value = value;
        return n;
    }

    void test_preset_file()
    {
        using namespace LLShadowMethod;
        Node old_on, old_off, both, only_new, no_value;
        old_on.children["RenderShadowPCSS"] = setting(true);
        old_off.children["RenderShadowPCSS"] = setting(false);
        both.children["RenderShadowPCSS"] = setting(true);
        both.children["RenderShadowMethod"] = setting(false);
        only_new.children["RenderShadowMethod"] = setting(true);
        no_value.children["RenderShadowPCSS"] = Node();   // an entry without a Value reads as off

        PresetMapping m = readPreset(true, old_on);
        check(m.apply && m.pcss, "old preset, checkbox on");
        m = readPreset(true, old_off);
        check(m.apply && !m.pcss, "old preset, checkbox off");
        check(!readPreset(true, both).apply, "preset with both keys keeps the method");
        check(!readPreset(true, only_new).apply, "new preset");
        check(!readPreset(true, Node()).apply, "preset without shadow settings");
        m = readPreset(true, no_value);
        check(m.apply && !m.pcss, "old preset entry without a value");
        check(!readPreset(false, old_on).apply, "unreadable or malformed file maps nothing");
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
    test_preset_file();
    if (failures != 0)
    {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::printf("llshadowmethod: all checks passed\n");
    return 0;
}
