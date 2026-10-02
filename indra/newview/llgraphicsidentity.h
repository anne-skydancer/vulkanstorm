#pragma once

#include <string>
#include <initializer_list>

namespace LLGraphicsIdentity
{
// Compare the adapter name, not the GL driver's vendor/wrapper. Keep unknown
// formats verbatim so that an unrecognized hardware change still triggers a reset.
inline std::string adapter(std::string gpu)
{
    const std::string zink = " zink Vulkan ";
    const auto wrapper = gpu.find(zink);
    if (wrapper != std::string::npos)
    {
        const auto begin = gpu.find('(', wrapper + zink.size());
        const auto end = gpu.rfind(" (");
        if (begin == std::string::npos || end == std::string::npos || end <= begin)
            return gpu;
        gpu = gpu.substr(begin + 1, end - begin - 1);
    }
    else
    {
        for (const auto* vendor : {"ATI Technologies Inc. ", "NVIDIA Corporation ", "AMD ", "Mesa "})
        {
            const std::string prefix(vendor);
            if (gpu.compare(0, prefix.size(), prefix) == 0)
            {
                gpu.erase(0, prefix.size());
                break;
            }
        }
    }
    const std::string pcie = "/PCIe/SSE2";
    if (gpu.size() >= pcie.size() && gpu.compare(gpu.size() - pcie.size(), pcie.size(), pcie) == 0)
        gpu.resize(gpu.size() - pcie.size());
    // Mesa's native Radeon driver appends architecture/driver details.
    if (gpu.compare(0, 11, "AMD Radeon ") == 0)
    {
        const auto details = gpu.find(" (");
        if (details != std::string::npos) gpu.resize(details);
    }
    return gpu;
}

inline bool changed(const std::string& previousGPU, const std::string& currentGPU,
                    const std::string& previousFamily, bool vulkan)
{
    const std::string family = vulkan ? "Vulkan" : "OpenGL";
    if (!previousFamily.empty() && previousFamily != family) return true;
    // Old settings did not record the renderer family. Preserve their raw
    // comparison for Vulkan; normalize only the shared OpenGL renderer.
    return vulkan ? previousGPU != currentGPU
                  : adapter(previousGPU) != adapter(currentGPU);
}
}
