// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026, Anne Skydancer (Vulkanstorm)
/**
 * @file llglfloor.h
 * @brief The OpenGL 4.6 floor: the version the viewer needs, and the message shown below it.
 *
 * Vulkanstorm needs OpenGL 4.6 on every line. A device below it gets a plain message in an operating
 * system box before any window of the viewer's own is shown, and the viewer closes (llglfloor.cpp).
 *
 * The rule and the text are renderer-free (no GL, no viewer types), so
 * indra/newview/tests/llglfloor_test.cpp checks them without the viewer libraries.
 */

#ifndef LL_LLGLFLOOR_H
#define LL_LLGLFLOOR_H

#include <string>

class LLWindow;

namespace LLGLFloor
{
    constexpr int REQUIRED_MAJOR = 4;
    constexpr int REQUIRED_MINOR = 6;

    // The integer version parsed from GL_VERSION ("<major>.<minor>[.<release>] <vendor>"), as
    // LLGLManager::mDriverVersionMajor/Minor hold it. A context that gave no version string reads 0.0.
    constexpr bool meets(int major, int minor)
    {
        return major > REQUIRED_MAJOR || (major == REQUIRED_MAJOR && minor >= REQUIRED_MINOR);
    }

    // The text of the box. The title is "Vulkanstorm". An empty field reads "unknown".
    inline std::string message(const std::string& gl_version, const std::string& renderer)
    {
        return std::string("Your graphics card or its driver is too old for Vulkanstorm.\n\n")
            + "Vulkanstorm needs OpenGL 4.6. This computer has:\n"
            + "    Graphics: " + (renderer.empty() ? std::string("unknown") : renderer) + "\n"
            + "    OpenGL:   " + (gl_version.empty() ? std::string("unknown") : gl_version) + "\n\n"
            + "Installing the latest graphics driver from NVIDIA, AMD or Intel (on Linux, a current Mesa or\n"
            + "the vendor's driver) may fix this. On a laptop with two graphics chips, set Vulkanstorm to\n"
            + "use the high-performance one. Otherwise this computer cannot run Vulkanstorm.\n\n"
            + "Vulkanstorm will now close. This is not a crash.";
    }

    // Call once the window and its GL context exist and before anything is drawn (LLViewerWindow's
    // constructor, right after the window is created). Logs the GL device. At or above the floor it returns.
    // Below it: closes the window, shows message() in an OS message box and exits the process
    // (LLAppViewer::fastQuit(1)), so it never returns.
    void enforce(LLWindow* window);
}

#endif // LL_LLGLFLOOR_H
