// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
//
// What a glGetGraphicsResetStatus result means (OpenGL 4.6 core, section 2.3.2 "Graphics Reset Recovery").
// No GL headers here, so tests/llglreset_test.cpp checks it on its own:
//   g++ -std=c++20 -Wall -Wextra -Werror -I indra/llrender indra/llrender/tests/llglreset_test.cpp
// The GL values below are checked against the GL headers in llgl.cpp. The viewer's policy for a reset (log,
// message box, clean close, nothing changed for the next start) is in doc/gl/gpu_reset.md.
#pragma once

#include <cstdint>

namespace LLGLReset
{
    // The values of glGetGraphicsResetStatus.
    constexpr uint32_t NO_ERROR_STATUS = 0;      // GL_NO_ERROR
    constexpr uint32_t GUILTY_STATUS = 0x8253;   // GL_GUILTY_CONTEXT_RESET
    constexpr uint32_t INNOCENT_STATUS = 0x8254; // GL_INNOCENT_CONTEXT_RESET
    constexpr uint32_t UNKNOWN_STATUS = 0x8255;  // GL_UNKNOWN_CONTEXT_RESET

    // NoReset, not None: X11's X.h defines None as a macro, and the Linux GL headers include it.
    enum class Kind : uint8_t
    {
        NoReset,  // no reset
        Guilty,   // the reset is attributable to this context: its own GPU work ran past the driver's timeout
        Innocent, // the reset is not attributable to this context: another context's work, or the driver itself
        Unknown,  // a reset whose cause is unknown
    };

    // Any value other than NO_ERROR is a reset. The specification allows only the three reset values; anything else
    // from a driver is still a reset and is kept as Unknown, so it is never mistaken for a healthy context.
    constexpr Kind classify(uint32_t status)
    {
        switch (status)
        {
            case NO_ERROR_STATUS: return Kind::NoReset;
            case GUILTY_STATUS:   return Kind::Guilty;
            case INNOCENT_STATUS: return Kind::Innocent;
            default:              return Kind::Unknown; // UNKNOWN_STATUS, and any value the specification does not list
        }
    }

    // The first reset is kept. The GL reports a reset until the driver has completed it and NO_ERROR again after
    // that, but the context stays lost ("Recovery requires creating a new context and recreating all relevant
    // state from the lost context"), so a later NO_ERROR, or a later reset of another kind, changes nothing.
    constexpr Kind latch(Kind seen, uint32_t status)
    {
        return seen != Kind::NoReset ? seen : classify(status);
    }

    // For the log.
    constexpr const char* name(Kind kind)
    {
        switch (kind)
        {
            case Kind::Guilty:   return "guilty";
            case Kind::Innocent: return "innocent";
            case Kind::Unknown:  return "unknown";
            case Kind::NoReset:  break;
        }
        return "none";
    }

    constexpr const char* cause(Kind kind)
    {
        switch (kind)
        {
            case Kind::Guilty:   return "the viewer's own GPU work ran past the driver's timeout";
            case Kind::Innocent: return "the driver reset the GPU for work that was not the viewer's";
            case Kind::Unknown:  return "the driver reset the GPU for a reason it does not report";
            case Kind::NoReset:  break;
        }
        return "no reset";
    }
}
