// Renderer identity and normal-session admission. LGPL-2.1, like the viewer.
#pragma once
#include <string>

namespace VSRenderBackend
{
inline std::string normalize(const std::string& value)
{
    return value == "Vulkan" || value == "Zink" ? value : "OpenGL";
}
inline bool canStartSession(const std::string& value)
{
    // Diligent/diagnostic availability does not establish a usable UI/chat
    // session. Open this admission only with the integrated native lifecycle.
    return normalize(value) != "Vulkan";
}
}
