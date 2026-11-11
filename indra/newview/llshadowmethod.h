// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llshadowmethod.h
 * @brief The shadow method setting (RenderShadowMethod: 0 = Legacy, 1 = PCSS, 2 = Stochastic) and the pure rules
 *        that carry the old RenderShadowPCSS checkbox over to it.
 *
 * Renderer-free (no GL, no viewer types): LLAppViewer::initConfiguration uses migrateOnStartup
 * once per install, LLPresetsManager::loadPreset uses presetNeedsMapping for graphic preset
 * files written before the switch, and indra/newview/tests/llshadowmethod_test.cpp checks both.
 */

#ifndef LL_LLSHADOWMETHOD_H
#define LL_LLSHADOWMETHOD_H

#include <cstdint>

namespace LLShadowMethod
{
    constexpr std::uint32_t LEGACY = 0;  // Firestorm 7.2.5 cascades and filtering
    constexpr std::uint32_t PCSS = 1;    // stable cascades, soft shadows, the later shadow stages
    constexpr std::uint32_t STOCHASTIC = 2; // stable cascades, 8 rays per pixel to the light's disc, temporal accumulation

    // The method a retired RenderShadowPCSS value stands for.
    constexpr std::uint32_t fromPCSSFlag(bool pcss) { return pcss ? PCSS : LEGACY; }

    // Clamps a stored value (a hand-edited settings file) to a valid method. An unknown value is
    // PCSS, as before Stochastic existed (an older viewer reading 2 gives PCSS the same way).
    constexpr std::uint32_t sanitize(std::uint32_t method) { return method > STOCHASTIC ? PCSS : method; }

    // The methods that fit the sun cascades as stable, world-anchored squares (llshadowcascadefit.h):
    // every method but Legacy. Takes a sanitized value.
    constexpr bool usesStableCascades(std::uint32_t method) { return method != LEGACY; }

    struct Migration
    {
        bool setMethod;       // write 'method' to RenderShadowMethod
        std::uint32_t method;
        bool markMigrated;    // set RenderShadowMethodMigrated
    };

    // One-time migration at startup, after the user settings were read and before the command
    // line is applied. An existing install takes the method from its RenderShadowPCSS checkbox;
    // a fresh install gets the feature table's value at window creation instead. The flag is set
    // in every case, so the migration never runs again and never touches a later choice.
    constexpr Migration migrateOnStartup(bool already_migrated, bool first_run, bool old_pcss_flag)
    {
        if (already_migrated)
        {
            return { false, LEGACY, false };
        }
        if (first_run)
        {
            return { false, LEGACY, true };
        }
        return { true, fromPCSSFlag(old_pcss_flag), true };
    }

    // A graphic preset file that carries the old checkbox but not the new setting is a preset from
    // before the switch: on becomes PCSS, off Legacy. A file with the new setting wins.
    constexpr bool presetNeedsMapping(bool file_has_old_flag, bool file_has_method)
    {
        return file_has_old_flag && !file_has_method;
    }

    struct PresetMapping
    {
        bool apply;  // set RenderShadowMethod after loading the preset
        bool pcss;   // the old checkbox's value in the file
    };

    // The read path of LLPresetsManager::loadPreset: 'settings' is the preset file parsed as an
    // LLSD map {name: {Comment, Type, Value}} (anything with has(name) and [name]["Value"].asBoolean());
    // 'parsed' is false when the file could not be opened or parsed, which maps nothing.
    template <class Settings>
    PresetMapping readPreset(bool parsed, const Settings& settings)
    {
        if (!parsed || !presetNeedsMapping(settings.has("RenderShadowPCSS"), settings.has("RenderShadowMethod")))
        {
            return { false, false };
        }
        return { true, settings["RenderShadowPCSS"]["Value"].asBoolean() };
    }
}

#endif  // LL_LLSHADOWMETHOD_H
