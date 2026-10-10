// Viewer-owned native Vulkan device, swapchain and ordered UI submission. LGPL-2.1.
#pragma once
#include <functional>
#include "vsuirenderer.h"
#include <memory>
#include <string>
namespace Diligent { struct GraphicsAdapterInfo; }
class LLWindow;
class LLSD;
class VSUIResources;
class VSVulkanContext
{
public:
    // The borrowed native window outlives this owner. Normal execution uses
    // loader discovery; ICD selection belongs to the process/test environment.
    VSVulkanContext(LLWindow&, bool validation);
    ~VSVulkanContext();
    VSVulkanContext(const VSVulkanContext&)              = delete;
    VSVulkanContext&   operator=(const VSVulkanContext&) = delete;
    VSUIResources&     resources();
    bool               present(float dpi, const std::function<void()>& draw);
    using FrameObserver = std::function<void(Diligent::IRenderDevice*, Diligent::IDeviceContext*, Diligent::ITextureView*, unsigned, unsigned, float, const std::vector<VSUIRenderer::Packet>&)>;
    void setFrameObserver(FrameObserver);
    void               wait();
    // Cached facts from the selected device; safe for About/crash reports.
    LLSD rendererInfo() const;
    const std::string& adapter() const;
    const Diligent::GraphicsAdapterInfo& adapterInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
