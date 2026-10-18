// Viewer-owned native Vulkan device, swapchain and ordered UI submission. LGPL-2.1.
#pragma once
#include "vsgraphicssettings.h"
#include <functional>
#include "vsuirenderer.h"
#include "vsworldrenderer.h"
#include "vsscenemonitor.h"
#include <memory>
#include <string>
namespace Diligent { struct GraphicsAdapterInfo; }
class LLWindow;
class LLSD;
class LLImageRaw;
class VSUIResources;
class VSWorldScene;
class VSVulkanContext
{
public:
    // The borrowed native window outlives this owner. Normal execution uses
    // loader discovery; ICD selection belongs to the process/test environment.
    VSVulkanContext(LLWindow&, bool validation);
    // Explicit diagnostic replay only; this never qualifies presentation.
    static bool headlessReplay();
    ~VSVulkanContext();
    VSVulkanContext(const VSVulkanContext&)              = delete;
    VSVulkanContext&   operator=(const VSVulkanContext&) = delete;
    VSUIResources&     resources();
    VSWorldScene&      world();
    bool               present(float dpi, const std::function<void()>& draw);
    using FrameObserver = std::function<void(Diligent::IRenderDevice*, Diligent::IDeviceContext*, Diligent::ITextureView*, unsigned, unsigned, float, const std::vector<VSUIRenderer::Packet>&)>;
    void setFrameObserver(FrameObserver);
    using SceneObserver = std::function<void(Diligent::IRenderDevice*, Diligent::IDeviceContext*, Diligent::ITextureView*, unsigned, unsigned)>;
    void setSceneObserver(SceneObserver);
    enum class SnapshotLayer { Color, Depth, Depth24 };
    // Render the actual scene into a dedicated target, without presenting or
    // replacing diagnostic observers. Output keeps LLImageRaw's bottom-first rows.
    bool snapshot(LLImageRaw&, unsigned width, unsigned height, float dpi,
                  bool showUI, bool showHUD, SnapshotLayer, const std::function<void()>& draw);
    // Independent preview depth/geometry ownership, on the same native device.
    // Closing a producer retires its renderer only after its actual fences.
    std::shared_ptr<VSWorldRenderer> createPreviewRenderer();
    VSUIRenderer::Image renderPreview(VSWorldRenderer&, unsigned width, unsigned height,
        const VSWorldRenderer::View&, const std::vector<VSWorldRenderer::Packet>&,
        const std::array<float,4>& background);
    void               wait();
    // Cached facts from the selected device; safe for About/crash reports.
    LLSD rendererInfo() const;
    const std::string& adapter() const;
    VSGraphicsCapabilities graphicsCapabilities() const;
    VSWorldRenderer::Statistics worldStatistics() const;
    VSUIRenderer::Statistics uiStatistics() const;
    struct DebugPixel { unsigned x=0,y=0; std::array<std::uint8_t,4> rgba{}; };
    // Bottom-left raw window coordinates, like the reference color inspector.
    // Returns a completed prior frame sample while requesting the next sample.
    std::optional<DebugPixel> inspectPixel(unsigned x,unsigned y);
    void resetPixelInspection();
    bool compareSceneMonitor(float pixelRatio,float tolerance);
    std::optional<VSSceneMonitor::Result> sceneMonitorResult();
    VSUIRenderer::Image sceneMonitorImage() const;
    void resetSceneMonitor();


    const Diligent::GraphicsAdapterInfo& adapterInfo() const;

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
