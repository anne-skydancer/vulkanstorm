// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llshadowmethod.h
 * @brief The shadow method setting (RenderShadowMethod: 0 = Legacy, 2 = Stochastic; 1 is unused) and the pure
 *        rules that carry the retired settings over: the RenderShadowPCSS checkbox, a stored method 1 (the
 *        removed PCSS method) and RenderShadowPCSSLightSize (now RenderShadowLightSize).
 *
 * Renderer-free (no GL, no viewer types): LLAppViewer::initConfiguration uses migrateOnStartup and
 * migrateLightSize once per install, LLPresetsManager::loadPreset uses readPreset for graphic preset
 * files written before the switch, and indra/newview/tests/llshadowmethod_test.cpp checks them.
 */

#ifndef LL_LLSHADOWMETHOD_H
#define LL_LLSHADOWMETHOD_H

#include <cstdint>

namespace LLShadowMethod
{
    constexpr std::uint32_t LEGACY = 0;  // Firestorm 7.2.5 cascades and filtering
    // 1 was PCSS, removed (archive tag archive/pcss/2026-11-11). The value stays unused, so 2 keeps meaning
    // Stochastic in stored settings and presets; a stored 1 reads as Stochastic (sanitize).
    constexpr std::uint32_t RETIRED_PCSS = 1;
    constexpr std::uint32_t STOCHASTIC = 2; // stable cascades, 8 rays per pixel to the light's disc, temporal accumulation

    // The method a retired RenderShadowPCSS value stands for: soft shadows on is Stochastic.
    constexpr std::uint32_t fromPCSSFlag(bool pcss) { return pcss ? STOCHASTIC : LEGACY; }

    // The method a stored value stands for when it is neither Legacy nor Stochastic: the retired PCSS (1)
    // and any hand-edited value. Never Legacy; effective() turns it into Legacy on a device without Stochastic.
    constexpr std::uint32_t DEFAULT = STOCHASTIC;

    // Maps a stored value to a valid method: 0 and 2 as they are, 1 (PCSS) and anything else to DEFAULT.
    constexpr std::uint32_t sanitize(std::uint32_t method) { return (method == LEGACY || method == STOCHASTIC) ? method : DEFAULT; }

    // The method a device runs for a stored value: sanitized, and Legacy for Stochastic on a device
    // that cannot run it (OpenGL below 4.0: LLStochasticEngine::deviceSupported). A capability choice
    // at load, never written back, so the stored choice survives a GPU upgrade; not a run-time fallback.
    constexpr std::uint32_t effective(std::uint32_t method, bool stochastic_supported)
    {
        const std::uint32_t m = sanitize(method);
        return (m == STOCHASTIC && !stochastic_supported) ? LEGACY : m;
    }

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
    // line is applied. An existing install takes the method from its RenderShadowPCSS checkbox
    // (on is Stochastic, since PCSS is gone);
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
    // before the switch: on becomes Stochastic, off Legacy. A file with the new setting wins.
    constexpr bool presetNeedsMapping(bool file_has_old_flag, bool file_has_method)
    {
        return file_has_old_flag && !file_has_method;
    }

    // RenderShadowPCSSLightSize became RenderShadowLightSize. Run once at startup, after the user settings
    // were read: a stored old value moves to the new setting unless the new one was already set (it wins),
    // and the old one is always reset, so the migration is idempotent.
    struct LightSizeMigration
    {
        bool copyToNew;   // set RenderShadowLightSize to the old value
        bool resetOld;    // reset RenderShadowPCSSLightSize to its default (it is no longer saved)
    };

    constexpr LightSizeMigration migrateLightSize(bool old_is_default, bool new_is_default)
    {
        if (old_is_default)
        {
            return { false, false };
        }
        return { new_is_default, true };
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

    // True when a stored value is not a valid method (the removed 1, or a hand-edited one): it is written back
    // sanitized, and only then, so a valid choice causes no second shader reload.
    constexpr bool needsRewrite(std::uint32_t method) { return sanitize(method) != method; }

    struct PresetMethod
    {
        bool write;            // set RenderShadowMethod after loading the preset
        std::uint32_t method;
    };

    // What loadPreset writes once the file is loaded: the old checkbox's method first (an old file has no
    // RenderShadowMethod, so 'loaded' is then the current value), then a sanitized value only if the result needs it.
    constexpr PresetMethod presetMethodAfterLoad(const PresetMapping& mapping, std::uint32_t loaded)
    {
        const std::uint32_t method = mapping.apply ? fromPCSSFlag(mapping.pcss) : loaded;
        return { mapping.apply || needsRewrite(method), sanitize(method) };
    }
}

#endif  // LL_LLSHADOWMETHOD_H
