// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// Standalone checks of llshadowmethod.h (no viewer libraries):
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/newview indra/newview/tests/llshadowmethod_test.cpp
// The shadow method migration: an existing install follows its old PCSS checkbox exactly once (on is
// Stochastic, PCSS being removed), a fresh install is left to the feature table, the flag always ends up
// set, and an old preset file maps its checkbox to the method unless it already carries the method. A
// stored method 1 (the removed PCSS) and any unknown value read as Stochastic, never Legacy. The old light
// size setting moves to the new name once, at startup.

#include "llshadowmethod.h"

#include <cmath>
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
        static_assert(LLShadowMethod::LEGACY == 0 && LLShadowMethod::RETIRED_PCSS == 1 && LLShadowMethod::STOCHASTIC == 2);
        check(LLShadowMethod::fromPCSSFlag(true) == LLShadowMethod::STOCHASTIC, "flag on is Stochastic");
        check(LLShadowMethod::fromPCSSFlag(false) == LLShadowMethod::LEGACY, "flag off is Legacy");
        check(LLShadowMethod::sanitize(0) == 0, "sanitize 0");
        check(LLShadowMethod::sanitize(1) == 2, "a stored 1 (the removed PCSS) is Stochastic");
        check(LLShadowMethod::sanitize(2) == 2, "sanitize 2");
        check(LLShadowMethod::sanitize(3) == 2, "sanitize 3");
        check(LLShadowMethod::sanitize(4000000000u) == 2, "sanitize huge");
        check(!LLShadowMethod::usesStableCascades(0) && LLShadowMethod::usesStableCascades(2),
              "stable cascades for Stochastic, not Legacy");
        check(LLShadowMethod::usesStableCascades(LLShadowMethod::sanitize(1))
            && LLShadowMethod::usesStableCascades(LLShadowMethod::sanitize(3))
            && LLShadowMethod::usesStableCascades(LLShadowMethod::sanitize(4000000000u)), "a stored 1 or unknown value is a stable-cascade method");
        for (std::uint32_t v : { 0u, 1u, 2u, 3u, 4000000000u })
        {
            check(LLShadowMethod::sanitize(v) != LLShadowMethod::RETIRED_PCSS, "no value runs the removed method");
        }
        check(LLShadowMethod::sanitize(0) == LLShadowMethod::LEGACY, "Legacy stays a valid, selectable method");
        check(LLShadowMethod::DEFAULT == LLShadowMethod::STOCHASTIC, "unknown values read as the default, Stochastic");
        // Owner decision Q8 (2026-11-13): the method is chosen by the stored value alone (every device that
        // starts meets the OpenGL 4.6 floor). No run-time failure (an allocation or a shader build) is an input
        // here, so none can turn Stochastic into Legacy.
        check(LLShadowMethod::sanitize(LLShadowMethod::STOCHASTIC) == LLShadowMethod::STOCHASTIC, "Q8: Stochastic stays Stochastic");
    }

    void test_migration()
    {
        using namespace LLShadowMethod;
        for (bool pcss : { false, true })
        {
            const Migration existing = migrateOnStartup(false, false, pcss);
            check(existing.setMethod, "existing install sets the method");
            check(existing.method == (pcss ? STOCHASTIC : LEGACY), "existing install follows the checkbox");
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

    void test_light_size()
    {
        using namespace LLShadowMethod;
        LightSizeMigration m = migrateLightSize(true, true);
        check(!m.copyToNew && !m.resetOld, "nothing stored under the old name: nothing to do");
        m = migrateLightSize(true, false);
        check(!m.copyToNew && !m.resetOld, "only the new name stored: untouched");
        m = migrateLightSize(false, true);
        check(m.copyToNew && m.resetOld, "old value stored, new at its default: moves over, old reset");
        m = migrateLightSize(false, false);
        check(!m.copyToNew && m.resetOld, "both stored: the new one wins, the old one is reset");
        // idempotent: after the first run the old name is at its default
        m = migrateLightSize(true, false);
        check(!m.copyToNew && !m.resetOld, "a second run does nothing");
    }

    bool near(float a, float b, float tol) { return std::fabs(a - b) <= tol; }

    // the softness slider: ends, centre at exactly 0.23, monotone, value -> position -> value, position -> value -> position
    void test_softness()
    {
        using namespace LLShadowMethod;
        check(softnessFromPosition(0.0f) == SOFTNESS_MIN, "softness position 0 is 0.05");
        check(near(softnessFromPosition(1.0f), SOFTNESS_MAX, 1.0e-6f), "softness position 1 is 1.0");
        check(softnessFromPosition(0.5f) == SOFTNESS_CENTRE, "softness position 0.5 is exactly 0.23");
        check(softnessToPosition(SOFTNESS_CENTRE) == 0.5f, "softness 0.23 is exactly position 0.5");
        check(softnessToPosition(SOFTNESS_MIN) == 0.0f && near(softnessToPosition(SOFTNESS_MAX), 1.0f, 1.0e-6f), "softness ends map to positions 0 and 1");
        check(softnessFromPosition(-1.0f) == SOFTNESS_MIN && near(softnessFromPosition(2.0f), SOFTNESS_MAX, 1.0e-6f), "positions outside are clamped");
        check(softnessToPosition(0.0f) == 0.0f && near(softnessToPosition(5.0f), 1.0f, 1.0e-6f), "values outside are clamped");
        // each half is logarithmic: the quarter positions are the geometric means of the half's ends
        check(near(softnessFromPosition(0.25f), std::sqrt(SOFTNESS_MIN * SOFTNESS_CENTRE), 1.0e-6f), "the left half is logarithmic");
        check(near(softnessFromPosition(0.75f), std::sqrt(SOFTNESS_CENTRE * SOFTNESS_MAX), 1.0e-6f), "the right half is logarithmic");
        float prev = 0.0f;
        bool monotone = true, round_trip = true, round_trip_pos = true;
        for (int i = 0; i <= 1000; ++i)
        {
            const float pos = (float)i / 1000.0f;
            const float v = softnessFromPosition(pos);
            monotone = monotone && v > prev;
            prev = v;
            round_trip_pos = round_trip_pos && near(softnessToPosition(v), pos, 1.0e-5f);
            const float value = SOFTNESS_MIN + (SOFTNESS_MAX - SOFTNESS_MIN) * pos;
            round_trip = round_trip && near(softnessFromPosition(softnessToPosition(value)) / value, 1.0f, 1.0e-5f);
        }
        check(monotone, "softness is strictly increasing with the position");
        check(round_trip, "value -> position -> value round-trips");
        check(round_trip_pos, "position -> value -> position round-trips");
        // 0.01 steps of the slider are fine steps of the value (about 3.1 %: the check allows under 4 %), with no coarse jump
        float worst = 0.0f;
        for (int i = 0; i < 100; ++i)
        {
            worst = std::fmax(worst, softnessFromPosition((float)(i + 1) / 100.0f) / softnessFromPosition((float)i / 100.0f) - 1.0f);
        }
        check(worst < 0.04f, "a 0.01 slider step changes the value by under 4 %");
    }

    void test_rewrite()
    {
        using namespace LLShadowMethod;
        check(!needsRewrite(0) && !needsRewrite(2), "valid methods are not rewritten");
        check(needsRewrite(1) && needsRewrite(3) && needsRewrite(4000000000u), "the removed 1 and unknown values are rewritten");
        PresetMethod m = presetMethodAfterLoad({ false, false }, 2);
        check(!m.write, "a preset with a valid method writes nothing (no second shader reload)");
        m = presetMethodAfterLoad({ false, false }, 0);
        check(!m.write, "a Legacy preset writes nothing");
        m = presetMethodAfterLoad({ false, false }, 1);
        check(m.write && m.method == STOCHASTIC, "a preset storing 1 is rewritten as Stochastic");
        m = presetMethodAfterLoad({ false, false }, 3);
        check(m.write && m.method == STOCHASTIC, "a preset storing 3 is rewritten as Stochastic");
        m = presetMethodAfterLoad({ true, true }, 1);
        check(m.write && m.method == STOCHASTIC, "checkbox on with a stored 1: Stochastic, written once");
        m = presetMethodAfterLoad({ true, false }, 1);
        check(m.write && m.method == LEGACY, "checkbox off wins over a stored 1: Legacy");
        m = presetMethodAfterLoad({ true, true }, 0);
        check(m.write && m.method == STOCHASTIC, "checkbox on over a current 0");
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
    test_light_size();
    test_softness();
    test_rewrite();
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
