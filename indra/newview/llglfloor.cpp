/**
 * @file llglfloor.cpp
 * @brief The OpenGL 4.6 floor: refuse to start below it, with a plain message
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Vulkanstorm Viewer Source Code
 * Copyright (C) 2026, Anne Skydancer
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, version 3 of the License or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See LICENSE and
 * NOTICE.md in the source tree.
 */

#include "llviewerprecompiledheaders.h"

#include "llglfloor.h"

#include "llappviewer.h"
#include "llgl.h"
#include "llglheaders.h"
#include "llwindow.h"

namespace
{
    std::string gl_string(GLenum name)
    {
        // glGetString gives NULL when there is no context or the enum is unknown to the context
        return ll_safe_string((const char*)glGetString(name));
    }

    void drain_gl_errors()
    {
        for (int i = 0; i < 16 && glGetError() != GL_NO_ERROR; ++i)
        {
        }
    }
}

void LLGLFloor::enforce(LLWindow* window)
{
    const S32 major = gGLManager.mDriverVersionMajor;
    const S32 minor = gGLManager.mDriverVersionMinor;

    // The raw strings, not the upper-cased members: they are what the user sees in the box. The version
    // string is the one parse_gl_version stored (empty when the driver gave none).
    const std::string version = gGLManager.mGLVersionString;
    const std::string renderer = gl_string(GL_RENDERER);
    const std::string vendor = gl_string(GL_VENDOR);
    const std::string glsl = gl_string(GL_SHADING_LANGUAGE_VERSION);

    // The profile mask exists from GL 3.2; on an older context the enum is invalid and the error is dropped
    GLint profile_mask = 0;
    if (major > 3 || (major == 3 && minor >= 2))
    {
        glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    }
    drain_gl_errors();
    const char* profile = (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) ? "core" : "compatibility";

    if (meets(major, minor))
    {
        LL_INFOS("RenderInit") << "OpenGL floor " << REQUIRED_MAJOR << "." << REQUIRED_MINOR << " met: '" << version << "' on '"
                               << renderer << "' (" << vendor << ", GLSL " << glsl << ", " << profile << " context)" << LL_ENDL;
        return;
    }

    gGLManager.mHasRequirements = false;

    LL_WARNS("RenderInit") << "OpenGL " << REQUIRED_MAJOR << "." << REQUIRED_MINOR << " is required; this device offers OpenGL '"
                           << version << "' on '" << renderer << "' (" << vendor << ", GLSL " << glsl << ", " << profile
                           << " context). Vulkanstorm does not start." << LL_ENDL;

    // Close the window first: on Windows this joins the window thread and deletes the GL context, so the box
    // has no parent window and nothing of the viewer is left on screen. OSMessageBox hides the splash screen.
    LLWindowManager::destroyWindow(window);
    OSMessageBox(message(version, renderer), "Vulkanstorm", OSMB_OK);
    LLSplashScreen::hide(); // OSMessageBox shows the splash again after the box closes

    // Exit as the failed-window path does (fastQuit removes the exec marker, so the next start is not counted as a crash).
    // Not a return through init(): the worker pools started by initThreads are idle but alive, and main()
    // returning with them running can crash in the static destructors, which looks like the crash this avoids.
    LLAppViewer::instance()->fastQuit(1);
}
