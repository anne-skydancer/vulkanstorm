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
#if VS_NATIVE_VULKAN
    return true;
#else
    return normalize(value) != "Vulkan";
#endif
}
}
