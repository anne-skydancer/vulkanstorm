// Native Vulkan ownership for the normal viewer window. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "llperfstats.h"
#if VS_VULKAN_DIAGNOSTICS
#include "vsparitybaseline.h"
#include "vsparityscene.h"
#endif
#include "vsvulkancontext.h"
#include "llwindow.h"
#include "llviewercontrol.h"
#include "vsuirenderer.h"
#include "vsuiresources.h"
#include "vsworldrenderer.h"
#include "vsworldscene.h"
#include "vsenvironmentstate.h"
#include "llenvironment.h"
#include "vsparcelpresentation.h"
#include "vsselectionlightpresentation.h"
#include "vsbeaconpresentation.h"
#include "vstrackerpresentation.h"
#include "llscenemonitor.h"
#include "llviewerwindow.h"
#include "llhudtext.h"
#include "llhudicon.h"
#include "llhudnametag.h"
#include "llagent.h"
#include "llregionhandle.h"
#include "rlveffects.h"
#include "rlvactions.h"
#include "pipeline.h"
#include "vsnativesession.h"
#include "vsparticleeffects.h"
#include "lldrawable.h"
#include "llagentcamera.h"
#include "llviewercamera.h"
#include "llviewertexture.h"
#include "llframetimer.h"
#include "llmemory.h"
#include "llimage.h"
#include "llfocusmgr.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <vulkan/vulkan.h>
#include <DiligentCore/Graphics/GraphicsEngineVulkan/interface/RenderDeviceVk.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <stdexcept>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <iterator>
#include <limits>
#include <vector>
#if LL_WINDOWS
#include <dxgi1_2.h>
#include <wrl/client.h>
#endif
#if !LL_WINDOWS
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib-xcb.h>
#endif
namespace
{
void require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
// Count heaps, not memory types: several types can alias the same heap.
// CPU-visible device-local heaps (Resizable BAR/UMA) still belong here.
double deviceLocalMemoryMegabytes(const VkPhysicalDeviceMemoryProperties& memory)
{
    double bytes = 0;
    for (uint32_t i = 0; i < memory.memoryHeapCount; ++i)
        if (memory.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
            bytes += static_cast<double>(memory.memoryHeaps[i].size);
    return bytes / (1024.0 * 1024.0);
}
std::string graphicsDriverVersion(const VkPhysicalDeviceProperties& properties,
                                  const VkPhysicalDeviceDriverProperties& driver)
{
    // Prefer the installed driver's own human-readable release/build string.
    // apiVersion and conformanceVersion describe different things.
    const auto end = std::find(std::begin(driver.driverInfo), std::end(driver.driverInfo), '\0');
    if (end != std::begin(driver.driverInfo)) return std::string(std::begin(driver.driverInfo), end);
    const uint32_t v = properties.driverVersion;
    if (driver.driverID == VK_DRIVER_ID_NVIDIA_PROPRIETARY ||
        (!driver.driverID && properties.vendorID == 0x10de))
        return llformat("%u.%u.%u.%u", (v >> 22) & 0x3ff, (v >> 14) & 0xff,
                        (v >> 6) & 0xff, v & 0x3f);
    if (driver.driverID == VK_DRIVER_ID_INTEL_PROPRIETARY_WINDOWS
#if LL_WINDOWS
        || (!driver.driverID && properties.vendorID == 0x8086)
#endif
       )
        return llformat("%u.%u", v >> 14, v & 0x3fff);
    return llformat("%u.%u.%u", VK_VERSION_MAJOR(v), VK_VERSION_MINOR(v), VK_VERSION_PATCH(v));
}
VkPhysicalDeviceDriverProperties queryDriverProperties(Diligent::IRenderDeviceVk& device)
{
    VkPhysicalDeviceDriverProperties driver{};
    driver.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    // The normal native device requires Vulkan 1.1. Do not invoke a core 1.1
    // entry point on an older instance if that requirement changes.
    if (device.GetVkVersion() < VK_API_VERSION_1_1) return driver;
    const auto physical = device.GetVkPhysicalDevice();
    uint32_t count = 0;
    bool supported = device.GetVkVersion() >= VK_API_VERSION_1_2;
    if (!supported && vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr) == VK_SUCCESS)
    {
        std::vector<VkExtensionProperties> extensions(count);
        const auto result = vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data());
        if (result == VK_SUCCESS)
            supported = std::any_of(extensions.begin(), extensions.begin() + count, [](const auto& extension)
            { return std::strcmp(extension.extensionName, VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME) == 0; });
    }
    if (supported)
    {
        VkPhysicalDeviceProperties2 properties{};
        properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        properties.pNext = &driver;
        vkGetPhysicalDeviceProperties2(physical, &properties);
    }
    return driver;
}
#if LL_WINDOWS
std::string windowsGraphicsDriverVersion(Diligent::IRenderDeviceVk& device)
{
    if (device.GetVkVersion() < VK_API_VERSION_1_1) return {};
    VkPhysicalDeviceIDProperties identity{};
    identity.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
    VkPhysicalDeviceProperties2 properties{};
    properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties.pNext = &identity;
    vkGetPhysicalDeviceProperties2(device.GetVkPhysicalDevice(), &properties);
    if (!identity.deviceLUIDValid) return {};
    // Match the selected Vulkan device exactly, not the first display or the
    // first adapter of a vendor. No Direct3D device or GL context is created.
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(factory.GetAddressOf())))) return {};
    for (UINT i = 0; ; ++i)
    {
        Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;
        if (FAILED(factory->EnumAdapters1(i, adapter.GetAddressOf()))) return {};
        DXGI_ADAPTER_DESC1 description{};
        if (FAILED(adapter->GetDesc1(&description)) ||
            std::memcmp(&description.AdapterLuid, identity.deviceLUID, VK_LUID_SIZE) != 0) continue;
        LARGE_INTEGER version{};
        if (FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) return {};
        const auto value = static_cast<uint64_t>(version.QuadPart);
        if (!value) return {};
        return llformat("%u.%u.%u.%u", unsigned((value >> 48) & 0xffff),
            unsigned((value >> 32) & 0xffff), unsigned((value >> 16) & 0xffff), unsigned(value & 0xffff));
    }
}
#endif
} // namespace
struct VSVulkanContext::Impl
{
    LLWindow&                                         window;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice>  device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain>     swapchain;
    Diligent::RefCntAutoPtr<Diligent::ITexture>       replayTarget;
    std::unique_ptr<VSUIRenderer>                     renderer;
    std::unique_ptr<VSUIResources>                    resources;
    std::unique_ptr<VSSelectionLightPresentation> selectionLightPresentation;
    std::unique_ptr<VSParcelPresentation> parcelPresentation;
    std::unique_ptr<VSBeaconPresentation> beaconPresentation;
    std::unique_ptr<VSTrackerPresentation> trackerPresentation;
    std::unique_ptr<VSWorldRenderer>                  worldRenderer;
    std::vector<std::shared_ptr<VSWorldRenderer>> previewRenderers;
    std::unique_ptr<VSWorldScene>                     worldScene;
    std::unique_ptr<VSSceneMonitor> sceneMonitor;
    std::optional<VSSceneMonitor::Result> sceneMonitorCompleted;
    std::string                                       adapter;
    LLSD renderer_info;
    double lastTextureMemoryUpdate=0;
#if VS_VULKAN_DIAGNOSTICS
    const bool profileFrames = std::getenv("VS_VULKAN_FRAME_PROFILE") != nullptr;
    using ProfileClock = std::chrono::steady_clock;
    ProfileClock::time_point profileEpoch = ProfileClock::now();
    unsigned profileCount = 0;
    double profilePackets = 0, profileWorld = 0, profileUI = 0, profilePresent = 0;
#endif
    FrameObserver observer;
    SceneObserver sceneObserver;
    Diligent::RefCntAutoPtr<Diligent::ITexture> debugPixelTexture;
    Diligent::RefCntAutoPtr<Diligent::IFence> debugPixelFence;
    Diligent::TEXTURE_FORMAT debugPixelFormat=Diligent::TEX_FORMAT_UNKNOWN;
    U64 debugPixelSequence=0,debugPixelCompletion=0;
    bool debugPixelDiscard=false;
    std::array<unsigned,2> debugPixelSubmitted{};
    std::optional<std::array<unsigned,2>> debugPixelRequest;
    std::optional<DebugPixel> debugPixelResult;

#if !LL_WINDOWS
    Display* display = nullptr; // Borrowed from the SDL window, including retirement.
#endif
    Impl(LLWindow& w, bool validation) : window(w)
    {
        using namespace Diligent;
        try
        {
            auto* factory = LoadAndGetEngineFactoryVk();
            require(factory != nullptr, "Native Vulkan factory is unavailable");
            Uint32 count = 0;
            factory->EnumerateAdapters({ 1, 1 }, count, nullptr);
            require(count != 0, "No Vulkan adapter is available");
            std::vector<GraphicsAdapterInfo> adapters(count);
            factory->EnumerateAdapters({ 1, 1 }, count, adapters.data());
            EngineVkCreateInfo info{};
            info.Features.TextureCompressionBC=DEVICE_FEATURE_STATE_OPTIONAL;
            info.Features.WireframeFill=DEVICE_FEATURE_STATE_OPTIONAL;
            info.Features.DurationQueries=DEVICE_FEATURE_STATE_OPTIONAL;
            info.Features.OcclusionQueries=DEVICE_FEATURE_STATE_OPTIONAL;
            info.EnableValidation = validation;
            info.AdapterId        = 0;
            const char* layer     = "VK_LAYER_KHRONOS_validation";
            if (validation)
            {
                info.InstanceLayerCount   = 1;
                info.ppInstanceLayerNames = &layer;
            }
            for (Uint32 i = 1; i < count; ++i)
                if (adapters[i].Type > adapters[info.AdapterId].Type)
                    info.AdapterId = i;
            factory->CreateDeviceAndContextsVk(info, &device, &context);
            require(device && context, "Native Vulkan device/context creation failed");
            const auto& actual = device->GetAdapterInfo();
            const auto& selected = adapters[info.AdapterId];
            require(actual.VendorId == selected.VendorId && actual.DeviceId == selected.DeviceId &&
                std::string(actual.Description) == selected.Description, "Native Vulkan selected an unexpected adapter");
            adapter = actual.Description;
            RefCntAutoPtr<IRenderDeviceVk> native_device(device, IID_RenderDeviceVk);
            require(native_device != nullptr, "Native device does not expose Vulkan properties");
            VkPhysicalDeviceProperties properties{};
            vkGetPhysicalDeviceProperties(native_device->GetVkPhysicalDevice(), &properties);
            renderer_info["GRAPHICS_CARD"] = adapter;
            renderer_info["VALIDATION_ENABLED"] = validation;
            renderer_info["RENDERING_API"] = "Vulkan";
            renderer_info["RENDERING_API_VERSION"] = llformat("%u.%u.%u",
                VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion),
                VK_VERSION_PATCH(properties.apiVersion));
            std::string driver_version;
#if LL_WINDOWS
            driver_version = windowsGraphicsDriverVersion(*native_device);
#endif
            if (driver_version.empty()) driver_version = graphicsDriverVersion(properties, queryDriverProperties(*native_device));
            renderer_info["GRAPHICS_DRIVER_VERSION"] = driver_version;
            VkPhysicalDeviceMemoryProperties memory{};
            vkGetPhysicalDeviceMemoryProperties(native_device->GetVkPhysicalDevice(), &memory);
            const double megabytes = deviceLocalMemoryMegabytes(memory);
            if (megabytes > 0)
            {
                renderer_info["GRAPHICS_CARD_MEMORY"] = megabytes;
                renderer_info["GRAPHICS_CARD_MEMORY_DETECTED"] = megabytes;
            }
            LL_INFOS("NativeVulkan") << "Machine Vulkan adapter: " << adapter << LL_ENDL;
            renderer_info["HEADLESS_REPLAY"] = VSVulkanContext::headlessReplay();
            if (VSVulkanContext::headlessReplay())
            {
                LLCoordWindow size;
                require(window.getSize(&size) && size.mX>0 && size.mY>0,"Headless replay extent is invalid");
                resizeReplayTarget(size.mX,size.mY);
            }
            else
            {
            NativeWindow native{};
#if LL_WINDOWS
            native.hWnd = window.getPlatformWindow();
#else
            SDL_SysWMinfo wm{};
            SDL_VERSION(&wm.version);
            require(SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window.getPlatformWindow()), &wm) && wm.subsystem == SDL_SYSWM_X11,
                    "Native Vulkan requires an SDL2/X11 window");
            display               = wm.info.x11.display;
            native.WindowId       = wm.info.x11.window;
            native.pDisplay       = display;
            native.pXCBConnection = XGetXCBConnection(display);
            require(native.pXCBConnection != nullptr, "SDL2/X11 XCB connection is unavailable");
#endif
            LLCoordWindow size;
            require(window.getSize(&size) && size.mX > 0 && size.mY > 0, "Initial native viewer extent is invalid");
            SwapChainDesc desc{};
            desc.Width             = size.mX;
            desc.Height            = size.mY;
            desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
            desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
            desc.Usage             = SWAP_CHAIN_USAGE_RENDER_TARGET | SWAP_CHAIN_USAGE_COPY_SOURCE;
            factory->CreateSwapChainVk(device, context, desc, native, &swapchain);
            require(swapchain != nullptr, "Native viewer swapchain creation failed");
            }
            renderer  = std::make_unique<VSUIRenderer>(device, context);
            resources = std::make_unique<VSUIResources>(*renderer);
            worldRenderer = std::make_unique<VSWorldRenderer>(device, context);
            worldScene = std::make_unique<VSWorldScene>(*worldRenderer, *resources);
            selectionLightPresentation=std::make_unique<VSSelectionLightPresentation>(*worldRenderer,*worldScene);
            parcelPresentation=std::make_unique<VSParcelPresentation>(*worldRenderer,*resources,*worldScene);
            beaconPresentation=std::make_unique<VSBeaconPresentation>(*worldRenderer,*worldScene);
            trackerPresentation=std::make_unique<VSTrackerPresentation>(*worldRenderer,*worldScene);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    void retirePreviews()
    {
        for (const auto& preview : previewRenderers) preview->retire();
        std::erase_if(previewRenderers, [](const auto& preview)
        { return preview.use_count() == 1 && !preview->pending(); });
    }
    void cleanup()
    {
        if (context)
        {
            context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->WaitForIdle();
        }
        sceneMonitor.reset();sceneMonitorCompleted.reset();
        selectionLightPresentation.reset();
        parcelPresentation.reset();
        worldScene.reset();
        previewRenderers.clear(); // The real context idle boundary is above.
        if (worldRenderer) { worldRenderer->retire(); worldRenderer.reset(); }
        resources.reset();
        if (renderer)
        {
            renderer->retire();
            renderer.reset();
        }
#if !LL_WINDOWS
        if (display)
            XSync(display, False);
#endif
        swapchain.Release();replayTarget.Release();
#if !LL_WINDOWS
        if (display)
            XSync(display, False);
#endif
        debugPixelTexture.Release();debugPixelFence.Release();
        context.Release();
        device.Release();
    }
    ~Impl() { cleanup(); }
    void resizeReplayTarget(unsigned width,unsigned height)
    {
        using namespace Diligent;
        TextureDesc desc{};desc.Name="Headless native viewer replay target";
        desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=width;desc.Height=height;
        desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_RENDER_TARGET;
        device->CreateTexture(desc,nullptr,&replayTarget);
        require(replayTarget!=nullptr,"Headless native replay target allocation failed");
    }
    void updateTextureMemory()
    {
        VSTextureMemory::Settings settings;
        settings.detectedMiB=renderer_info["GRAPHICS_CARD_MEMORY_DETECTED"].asReal();
        settings.overrideMiB=gSavedSettings.getBOOL("FSOverrideVRAMDetection")?gSavedSettings.getU32("FSForcedVideoMemory"):0;
        settings.divisor=gSavedSettings.getU32("RenderTextureVRAMDivisor");
        settings.manual=gSavedSettings.getBOOL("FSLimitTextureVRAMUsage");
        settings.manualMiB=gSavedSettings.getU32("RenderMaxVRAMBudget");
        settings.minimumTargetMiB=MIN_VRAM_BUDGET;
        settings.lowIncrement=gSavedSettings.getF32("RenderLowMemMinDiscardIncrement");
        settings.highDecrement=gSavedSettings.getF32("RenderHighMemMinDiscardDecrement");
        settings.minimumFreeSystemMiB=gSavedSettings.getU32("RenderMinFreeMainMemoryThreshold");
        settings.minimizedSeconds=gSavedSettings.getF32("TextureDiscardMinimizedTime");
        settings.backgroundSeconds=gSavedSettings.getF32("TextureDiscardBackgroundedTime");
        VSTextureMemory::Input input;
        input.now=LLFrameTimer::getTotalSeconds();input.seconds=lastTextureMemoryUpdate?std::max(input.now-lastTextureMemoryUpdate,0.0):0;
        lastTextureMemoryUpdate=input.now;
        input.geometryBytes=worldRenderer->statistics().residentMeshBytes;
        input.systemLow=LLViewerTexture::isSystemMemoryLow();
        input.systemFactor=LLMemory::getSystemMemoryBudgetFactor();
        input.freeSystemMiB=LLMemory::getAvailableMemKB().valueInUnits<LLUnits::Megabytes>();
        // A headless diagnostic emulates an active view, not a minimized viewer.
        input.background=window.getMinimized() ||
            (!VSVulkanContext::headlessReplay() && (!window.getVisible() || !gFocusMgr.getAppHasFocus()));
        input.minimized=window.getMinimized() && input.background;
        resources->updateTextureMemory(settings,input);
    }

    struct Snapshot
    {
        Diligent::RefCntAutoPtr<Diligent::ITexture> target;
        SnapshotLayer layer = SnapshotLayer::Color;
        bool showUI = false, showHUD = false, complete = true;
        std::vector<U8> depth;
    };
    bool readTexture(Diligent::ITexture* source, std::vector<U8>& bytes)
    {
        using namespace Diligent;
        if (!source) return false;
        auto desc = source->GetDesc();
        if (desc.Format != TEX_FORMAT_RGBA8_UNORM && desc.Format != TEX_FORMAT_D32_FLOAT) return false;
        desc.Name = "Viewer snapshot readback";
        desc.BindFlags = BIND_NONE;
        desc.Usage = USAGE_STAGING;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;
        device->CreateTexture(desc, nullptr, &staging);
        if (!staging) return false;
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};
        copy.pSrcTexture = source; copy.pDstTexture = staging;
        copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        context->WaitForIdle();
        MappedTextureSubresource mapped{};
        context->MapTextureSubresource(staging, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
        if (!mapped.pData) return false;
        try
        {
            const std::size_t stride = std::size_t(desc.Width) * 4;
            bytes.resize(stride * desc.Height);
            for (unsigned row = 0; row < desc.Height; ++row)
                std::memcpy(bytes.data() + std::size_t(desc.Height - row - 1) * stride,
                            static_cast<const U8*>(mapped.pData) + row * mapped.Stride, stride);
        }
        catch (...)
        {
            context->UnmapTextureSubresource(staging, 0, 0);
            throw;
        }
        context->UnmapTextureSubresource(staging, 0, 0);
        return true;
    }

    void updateDebugPixel(Diligent::ITexture* source)
    {
        using namespace Diligent;
        if (debugPixelCompletion && debugPixelFence->GetCompletedValue() >= debugPixelCompletion)
        {
            MappedTextureSubresource mapped{};
            context->MapTextureSubresource(debugPixelTexture,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
            if (mapped.pData)
            {
                DebugPixel pixel;
                pixel.x=debugPixelSubmitted[0];pixel.y=debugPixelSubmitted[1];
                std::copy_n(static_cast<const U8*>(mapped.pData),4,pixel.rgba.begin());
                if(debugPixelFormat==TEX_FORMAT_BGRA8_UNORM || debugPixelFormat==TEX_FORMAT_BGRA8_UNORM_SRGB)
                    std::swap(pixel.rgba[0],pixel.rgba[2]);
                context->UnmapTextureSubresource(debugPixelTexture,0,0);
                if(!debugPixelDiscard)debugPixelResult=pixel;
            }
            debugPixelCompletion=0;
        }
        // At most one four-byte readback is in flight, only while the ordinary
        // debug color control requests it. Never wait for the graphics queue.
        if(debugPixelCompletion || !debugPixelRequest || !source)return;
        const auto request=*debugPixelRequest;debugPixelRequest.reset();
        const auto& desc=source->GetDesc();
        if(request[0]>=desc.Width || request[1]>=desc.Height)return;
        if(desc.Format!=TEX_FORMAT_RGBA8_UNORM && desc.Format!=TEX_FORMAT_RGBA8_UNORM_SRGB &&
           desc.Format!=TEX_FORMAT_BGRA8_UNORM && desc.Format!=TEX_FORMAT_BGRA8_UNORM_SRGB)return;
        if(!debugPixelFence)
        {
            FenceDesc fence{};fence.Name="Viewer pixel inspection";
            device->CreateFence(fence,&debugPixelFence);
            if(!debugPixelFence)return;
        }
        if(!debugPixelTexture || debugPixelFormat!=desc.Format)
        {
            TextureDesc staging{};staging.Name="Viewer pixel inspection readback";
            staging.Type=RESOURCE_DIM_TEX_2D;staging.Width=staging.Height=1;
            staging.Format=desc.Format;staging.Usage=USAGE_STAGING;staging.CPUAccessFlags=CPU_ACCESS_READ;
            device->CreateTexture(staging,nullptr,&debugPixelTexture);
            if(!debugPixelTexture)return;
            debugPixelFormat=desc.Format;
        }
        Box region{request[0],request[0]+1,desc.Height-1-request[1],desc.Height-request[1],0,1};
        CopyTextureAttribs copy{};copy.pSrcTexture=source;copy.pSrcBox=&region;copy.pDstTexture=debugPixelTexture;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->CopyTexture(copy);
        debugPixelSubmitted=request;debugPixelDiscard=false;debugPixelCompletion=++debugPixelSequence;
        context->EnqueueSignal(debugPixelFence,debugPixelCompletion);
    }

    bool present(float dpi, const std::function<void()>& draw, Snapshot* snapshot = nullptr)
    {
        using namespace Diligent;
#if VS_VULKAN_DIAGNOSTICS
        double packetMilliseconds = 0, worldMilliseconds = 0;
        unsigned terrainPackets = 0, scenePackets = 0;
        const bool profileFrame = profileFrames && !snapshot;
#endif
        updateTextureMemory();
        if(!snapshot && sceneMonitor)
            if(const auto completed=sceneMonitor->poll())sceneMonitorCompleted=completed;
        LLCoordWindow size;
        require(window.getSize(&size), "Cannot query native viewer extent");
        if (snapshot)
        {
            size.mX = snapshot->target->GetDesc().Width;
            size.mY = snapshot->target->GetDesc().Height;
        }
        if (size.mX <= 0 || size.mY <= 0 || (!snapshot && window.getMinimized()))
        {
            // Coarsening and completion do not depend on producing a visible
            // frame. Keep immutable old images until replacements are admitted.
            if(resources->textureMemoryPolicy().bias>1.9) worldScene->refreshInactiveImages();
            renderer->flushUploads();context->Flush();
            renderer->retire();worldRenderer->retire();
            return false;
        }
        if (!snapshot && replayTarget && (replayTarget->GetDesc().Width!=unsigned(size.mX) || replayTarget->GetDesc().Height!=unsigned(size.mY)))
        {
            context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->WaitForIdle();resizeReplayTarget(size.mX,size.mY);
        }
        if (!snapshot && swapchain && (swapchain->GetDesc().Width != unsigned(size.mX) || swapchain->GetDesc().Height != unsigned(size.mY)))
        {
            context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            swapchain->Resize(size.mX, size.mY);
        }
        require(snapshot || !swapchain || (swapchain->GetDesc().Width == unsigned(size.mX) && swapchain->GetDesc().Height == unsigned(size.mY)),
                "Native swapchain resize did not publish the client extent");
        auto* target = snapshot ? snapshot->target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET) :
            swapchain ? swapchain->GetCurrentBackBufferRTV() : replayTarget->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        require(target != nullptr, "Native viewer backbuffer is unavailable");
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float color[]{ 16.f / 255, 32.f / 255, 48.f / 255, 1 };
        context->ClearRenderTarget(target, color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
#if VS_VULKAN_DIAGNOSTICS
        if (!snapshot) vs_parity_baseline_before_render();
#endif
#if VS_VULKAN_DIAGNOSTICS
        if (!snapshot && vs_parity_scene_active())
        {
            const auto start=std::chrono::steady_clock::now();
            const auto packetStart=std::chrono::steady_clock::now();
            auto packets=worldScene->packets(vs_parity_scene_eye(),32,size.mY);
            packets.erase(std::remove_if(packets.begin(),packets.end(),[](const auto& p)
                { return p.background || p.alpha; }),packets.end());
            LLSD timing;timing["packet_build_cpu_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-packetStart).count();
            worldRenderer->draw(target,size.mX,size.mY,vs_parity_scene_view(size.mX,size.mY),packets);
            timing["scene_and_packet_submit_cpu_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            const auto stats=worldRenderer->statistics();
            timing["draw_calls"]=F64(stats.draws);timing["mesh_upload_bytes"]=F64(stats.meshUploadBytes);
            timing["uniform_buffer_allocations"]=F64(stats.constantBuffersCreated);timing["binding_allocations"]=F64(stats.bindingsCreated);
            timing["constant_upload_bytes"]=F64(stats.constantUploadBytes);timing["resource_commits"]=F64(stats.resourceCommits);
            timing["skinning_workload_avatars"]=0;vs_parity_baseline_metrics(timing);
        }
        else
#endif
        if (const auto session=VSNativeSession::active(); session && session->phase()==VSNativeSession::Phase::Connected)
        {
            auto camera=session->cameraState();
            if(snapshot)
            {
                // Snapshot controllers position the shared camera explicitly,
                // including all six panorama faces; normal frames retain the live camera owner.
                const auto* reference=LLViewerCamera::getInstance();
                camera.center=LLVector3(gAgent.getPosGlobalFromAgent(reference->getOrigin())-from_region_handle(session->handle()));
                camera.at=reference->getAtAxis();camera.up=reference->getUpAxis();camera.left=reference->getLeftAxis();
            }
            const LLVector3 eye=camera.center;
            const float distance=LLViewerCamera::getInstance()->getFar();
            auto view    = VSWorldScene::cameraView(eye, camera.at, camera.up, size.mX, size.mY, distance,
                LLViewerCamera::getInstance()->getView());
            view.hudZoom = gAgentCamera.mHUDCurZoom;
            view.wireframe = gUseWireframe;
            view.glow = gSavedSettings.getBOOL("RenderGlow");
            if(!snapshot)view.timingFrame=LLDrawable::getCurrentFrame();
            view.environment=vs_environment_state(eye,camera.at,camera.up,float(size.mX)/size.mY,
                LLViewerCamera::getInstance()->getView(),LLEnvironment::instance().getWaterHeight());
            if(view.environment.values[0][0]>.5f) {
                const auto direction=LLEnvironment::instance().getLightDirection();
                view.sun={direction.mV[0],direction.mV[1],direction.mV[2],0};
                view.ambient=view.environment.values[7];
            }

#if VS_VULKAN_DIAGNOSTICS
            const auto packetStart = profileFrame ? ProfileClock::now() : ProfileClock::time_point{};
#endif
            auto scene = worldScene->packets(eye, distance, size.mY, size.mX);
            if(auto* particles=session->existingParticles())particles->append(*worldRenderer,*resources,*worldScene,scene,!snapshot);
            if (snapshot && !snapshot->showHUD)
                scene.erase(std::remove_if(scene.begin(), scene.end(), [](const auto& packet) { return packet.hud; }), scene.end());
            worldScene->appendAlphaHighlights(scene);
            parcelPresentation->append(*session,view,size.mX,size.mY,scene);
            if(gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
            {
                selectionLightPresentation->append(*session,!snapshot || snapshot->showHUD,scene);
                beaconPresentation->append(*session,scene);
                trackerPresentation->append(*session,scene);
                LLHUDIcon::appendNative(*worldRenderer,from_region_handle(session->handle()),scene);
            }
#if VS_VULKAN_DIAGNOSTICS
            const auto submitStart = profileFrame ? ProfileClock::now() : ProfileClock::time_point{};
            if (profileFrame)
            {
                packetMilliseconds = std::chrono::duration<double,std::milli>(submitStart-packetStart).count();
                scenePackets = unsigned(scene.size());
                terrainPackets = unsigned(std::count_if(scene.begin(), scene.end(), [](const auto& packet){return packet.terrain;}));
            }
#endif
            worldScene->refreshNameTags();
            worldScene->appendVoice(scene);
            worldRenderer->draw(target, size.mX, size.mY, view, scene,[&]
            {
                if (snapshot && snapshot->layer != SnapshotLayer::Color)
                    if (auto* depth = worldRenderer->depthView()) readTexture(depth->GetTexture(), snapshot->depth);
                if(gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
                {
                    resources->begin(size.mX,size.mY,dpi);
                    LLHUDNameTag::renderNative();
                    const auto tags=resources->finish();
                    renderer->draw(target,size.mX,size.mY,dpi,tags,worldRenderer->depthView());
                    const bool complete = resources->evidence()["deferred_draw_batches"].asInteger() == 0;
                    if (snapshot) snapshot->complete = snapshot->complete && complete;
                    LLHUDNameTag::finishNative(!snapshot && complete);
                }
                if(!RlvActions::hasBehaviour(RLV_BHVR_SETOVERLAY))return;
                resources->begin(size.mX,size.mY,dpi);
                LLVfxManager::instance().runEffect(EVisualEffect::RlvOverlay);
                const auto overlays=resources->finish();
                renderer->draw(target,size.mX,size.mY,dpi,overlays);
                if (snapshot) snapshot->complete = snapshot->complete && resources->evidence()["deferred_draw_batches"].asInteger() == 0;
            });
#if VS_VULKAN_DIAGNOSTICS
            if (profileFrame) worldMilliseconds = std::chrono::duration<double,std::milli>(ProfileClock::now()-submitStart).count();
#endif
        }
#if VS_VULKAN_DIAGNOSTICS
        const auto uiStart = profileFrame ? ProfileClock::now() : ProfileClock::time_point{};
#endif
        if(const auto owner=VSNativeSession::active(); !owner || owner->phase()!=VSNativeSession::Phase::Connected)
        {
            selectionLightPresentation->reset();
            parcelPresentation->reset();
            beaconPresentation->reset();
            trackerPresentation->reset();
        }
        if(!snapshot)
        {
            const auto session=VSNativeSession::active();
            if(session && session->phase()==VSNativeSession::Phase::Connected)
            {
                auto& monitor=LLSceneMonitor::instance();
                monitor.fetchQueryResult();
                monitor.capture([&]
                {
                    if(!sceneMonitor)sceneMonitor=std::make_unique<VSSceneMonitor>(device,context,*renderer);
                    return sceneMonitor->capture(target->GetTexture(),
                        gViewerWindow->getWorldViewWidthRaw(),gViewerWindow->getWorldViewHeightRaw());
                });
                monitor.compare();
            }
        }
        if (!snapshot && sceneObserver) sceneObserver(device, context, target, size.mX, size.mY);
        resources->begin(size.mX, size.mY, dpi);
        if(const auto owner=VSNativeSession::active(); owner && owner->phase()==VSNativeSession::Phase::Connected &&
            gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
        {
            LLHUDText::renderNativeBeaconLabels();
        }
        std::optional<LLPerfStats::RecordSceneTime> uiTime;
        if(!snapshot)uiTime.emplace(LLPerfStats::StatType_t::RENDER_UI);
#if VS_VULKAN_DIAGNOSTICS
        if (snapshot || !vs_parity_scene_active())
#endif
        if (!snapshot || snapshot->showUI) draw();
        auto packets = resources->finish();
        renderer->draw(target, size.mX, size.mY, dpi, packets);
        uiTime.reset();
#if VS_VULKAN_DIAGNOSTICS
        if (!snapshot) vs_parity_baseline_capture_vk(device,context,target,size.mX,size.mY);
#endif
        // A refused solid-fill upload is retried on the next frame. Captures
        // must wait for complete visible UI, so omitted packets cannot make
        // a pixel oracle falsely accept an incomplete frame. A persistent
        // refusal therefore reaches the existing bounded replay timeout.
        if (!snapshot && observer && resources->evidence()["deferred_draw_batches"].asInteger() == 0)
            observer(device, context, target, size.mX, size.mY, dpi, packets);
#if VS_VULKAN_DIAGNOSTICS
        const auto parityPresentStart=std::chrono::steady_clock::now();
        if (profileFrame)
        {
            profilePackets += packetMilliseconds; profileWorld += worldMilliseconds;
            profileUI += std::chrono::duration<double,std::milli>(ProfileClock::now()-uiStart).count();
        }
#endif
        if(!snapshot)updateDebugPixel(target->GetTexture());
        if (!snapshot && swapchain)
        {
            LLPerfStats::RecordSceneTime swapTime(LLPerfStats::StatType_t::RENDER_SWAP);
            swapchain->Present(gSavedSettings.getBOOL("RenderVSyncEnable") ? 1 : 0);
        }
        else
        {
            context->Flush();
            // Present normally retires Diligent's completed device resources.
            // Offscreen replay must also drain that queue each frame.
            device->ReleaseStaleResources(false);
        }
#if VS_VULKAN_DIAGNOSTICS
        if (!snapshot) vs_parity_baseline_after_present(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-parityPresentStart).count());
#endif
        renderer->retire();
        worldRenderer->retire();
#if VS_VULKAN_DIAGNOSTICS
        if (profileFrame)
        {
            const auto end = ProfileClock::now();
            profilePresent += std::chrono::duration<double,std::milli>(end-parityPresentStart).count();
            ++profileCount;
            const auto seconds = std::chrono::duration<double>(end-profileEpoch).count();
            if (seconds >= 2)
            {
                const auto ground = worldScene->terrainStatus();
                LL_INFOS("NativeFrameProfile") << "frames=" << profileCount << " fps=" << profileCount/seconds
                    << " packets_cpu_ms=" << profilePackets/profileCount << " world_submit_cpu_ms=" << profileWorld/profileCount
                    << " ui_cpu_ms=" << profileUI/profileCount << " present_retire_cpu_ms=" << profilePresent/profileCount
                    << " scene_packets=" << scenePackets << " terrain_packets=" << terrainPackets
                    << " terrain_received=" << ground["received_patches"].asInteger()
                    << " terrain_meshes=" << ground["published_meshes"].asInteger()
                    << " terrain_detail_ids=" << ground["detail_ids_known"].asInteger()
                    << " terrain_images=" << ground["detail_images_ready"].asInteger()
                    << " terrain_bytes=" << ground["terrain_bytes"].asReal() << LL_ENDL;
                profileEpoch=end; profileCount=0;
                profilePackets=profileWorld=profileUI=profilePresent=0;
            }
        }
#endif
        return !snapshot || (snapshot->complete && resources->evidence()["deferred_draw_batches"].asInteger() == 0);
    }
};
VSVulkanContext::VSVulkanContext(LLWindow& w, bool validation) : mImpl(std::make_unique<Impl>(w, validation))
{
}
VSVulkanContext::~VSVulkanContext() = default;
bool VSVulkanContext::headlessReplay()
{
#if VS_VULKAN_DIAGNOSTICS
    const char* flag=std::getenv("VS_VULKAN_DIAGNOSTIC_HEADLESS");
    return flag && std::string(flag)=="1" && std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY");
#else
    return false;
#endif
}
VSUIResources& VSVulkanContext::resources()
{ return *mImpl->resources; }
VSWorldScene& VSVulkanContext::world() { return *mImpl->worldScene; }
bool VSVulkanContext::present(float dpi, const std::function<void()>& draw)
{
    mImpl->retirePreviews();
    return mImpl->present(dpi, draw);
}
std::shared_ptr<VSWorldRenderer> VSVulkanContext::createPreviewRenderer()
{
    mImpl->retirePreviews();
    auto renderer = std::make_shared<VSWorldRenderer>(mImpl->device, mImpl->context);
    mImpl->previewRenderers.push_back(renderer);
    return renderer;
}
VSUIRenderer::Image VSVulkanContext::renderPreview(VSWorldRenderer& renderer, unsigned width, unsigned height,
    const VSWorldRenderer::View& view, const std::vector<VSWorldRenderer::Packet>& packets,
    const std::array<float,4>& background)
{
    if (std::none_of(mImpl->previewRenderers.begin(), mImpl->previewRenderers.end(),
        [&renderer](const auto& owner) { return owner.get() == &renderer; })) return {};
    return mImpl->renderer->renderImage(width, height, [&](Diligent::ITextureView* target)
    {
        mImpl->context->ClearRenderTarget(target, background.data(), Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderer.draw(target, width, height, view, packets);
    });
}
bool VSVulkanContext::snapshot(LLImageRaw& raw, unsigned width, unsigned height, float dpi,
                               bool showUI, bool showHUD, SnapshotLayer layer, const std::function<void()>& draw)
{
    using namespace Diligent;
    const auto maximum = graphicsCapabilities().maximumTextureDimension;
    if (!width || !height || width > maximum || height > maximum || width > std::numeric_limits<U16>::max() || height > std::numeric_limits<U16>::max() ||
        std::size_t(width) * height > std::numeric_limits<S32>::max() / 4)
        return false;
    // Use the real image allocation policy before reserving GPU/readback memory.
    if (!raw.resize(U16(width), U16(height), 3) || raw.isBufferInvalid()) return false;
    Impl::Snapshot request;
    request.layer = layer; request.showUI = showUI; request.showHUD = showHUD;
    TextureDesc desc{};
    desc.Name = "Viewer snapshot render target";
    desc.Type = RESOURCE_DIM_TEX_2D;
    desc.Width = width; desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.BindFlags = BIND_RENDER_TARGET;
    mImpl->device->CreateTexture(desc, nullptr, &request.target);
    if (!request.target || !mImpl->present(dpi, draw, &request)) return false;
    std::vector<U8> pixels;
    if (layer == SnapshotLayer::Color)
    {
        if (!mImpl->readTexture(request.target, pixels)) return false;
    }
    else pixels = std::move(request.depth);
    if (pixels.size() != std::size_t(width) * height * 4) return false;
    LLImageDataLock lock(&raw);
    if (!raw.resize(width, height, 3) || raw.isBufferInvalid()) return false;
    const float nearClip = LLViewerCamera::getInstance()->getNear();
    const float farClip = LLViewerCamera::getInstance()->getFar();
    const float factor1 = (farClip + nearClip) / (2.f * farClip * nearClip);
    const float factor2 = (farClip - nearClip) / (2.f * farClip * nearClip);
    for (std::size_t i = 0; i < std::size_t(width) * height; ++i)
    {
        auto* output = raw.getData() + i * 3;
        if (layer == SnapshotLayer::Color) std::copy_n(pixels.data() + i * 4, 3, output);
        else
        {
            float depth;
            std::memcpy(&depth, pixels.data() + i * 4, sizeof(depth));
            // Vulkan's zero-to-one NDC is transposed to the reference GL
            // convention before using its existing depth conversion/packing.
            const float linear = 1.f / (factor1 - (depth * 2.f - 1.f) * factor2);
            if (layer == SnapshotLayer::Depth24)
            {
                const U32 value = F32_to_U32(linear, nearClip, farClip);
                output[0] = U8(llclamp(llfloor(value / 65536.f), 0, 255));
                output[1] = U8(llclamp(llfloor((value - output[0] * 65536) / 256.f), 0, 255));
                output[2] = U8(llclamp(value - output[0] * 65536 - output[1] * 256, 0u, 255u));
            }
            else std::fill_n(output, 3, F32_to_U8(linear, nearClip, farClip));
        }
    }
    return true;
}
void VSVulkanContext::wait()
{
    mImpl->context->WaitForIdle();
    mImpl->renderer->retire();
    mImpl->worldRenderer->retire();
}
LLSD VSVulkanContext::rendererInfo() const
{ return mImpl->renderer_info; }
const std::string& VSVulkanContext::adapter() const
{ return mImpl->adapter; }

void VSVulkanContext::setFrameObserver(FrameObserver observer) { mImpl->observer = std::move(observer); }
void VSVulkanContext::setSceneObserver(SceneObserver observer) { mImpl->sceneObserver = std::move(observer); }

const Diligent::GraphicsAdapterInfo& VSVulkanContext::adapterInfo() const
{
    return mImpl->device->GetAdapterInfo();
}

VSGraphicsCapabilities VSVulkanContext::graphicsCapabilities() const
{
    VSGraphicsCapabilities result;
    if (!mImpl || !mImpl->device) return result;
    const auto& adapter = mImpl->device->GetAdapterInfo();
    result.ready = true;
    result.compression = mImpl->device->GetDeviceInfo().Features.TextureCompressionBC == Diligent::DEVICE_FEATURE_STATE_ENABLED;
    result.adapter = adapter.Description;
    result.anisotropy = adapter.Sampler.MaxAnisotropy;
    result.maximumTextureDimension = adapter.Texture.MaxTexture2DDimension;
    result.wireframe = mImpl->device->GetDeviceInfo().Features.WireframeFill == Diligent::DEVICE_FEATURE_STATE_ENABLED;
    return result;
}

VSWorldRenderer::Statistics VSVulkanContext::worldStatistics() const {return mImpl->worldRenderer->statistics();}
VSUIRenderer::Statistics VSVulkanContext::uiStatistics() const {return mImpl->renderer->statistics();}
std::optional<VSVulkanContext::DebugPixel> VSVulkanContext::inspectPixel(unsigned x,unsigned y)
{
    mImpl->debugPixelRequest=std::array<unsigned,2>{x,y};
    return mImpl->debugPixelResult;
}

void VSVulkanContext::resetPixelInspection()
{
    mImpl->debugPixelRequest.reset();mImpl->debugPixelResult.reset();
    // Keep any in-flight resource alive through its fence, discard its old
    // account sample when it completes, and reuse it only after completion.
    mImpl->debugPixelDiscard=true;
}

bool VSVulkanContext::compareSceneMonitor(float ratio,float tolerance)
{
    if(!mImpl->sceneMonitor)return false;
    LLCoordWindow size;
    if(!mImpl->window.getSize(&size) || size.mX<=0 || size.mY<=0)return false;
    return mImpl->sceneMonitor->compare(unsigned(size.mX),unsigned(size.mY),ratio,tolerance);
}
std::optional<VSSceneMonitor::Result> VSVulkanContext::sceneMonitorResult()
{
    auto result=mImpl->sceneMonitorCompleted;mImpl->sceneMonitorCompleted.reset();return result;
}
VSUIRenderer::Image VSVulkanContext::sceneMonitorImage()const
{
    return mImpl->sceneMonitor?mImpl->sceneMonitor->image():VSUIRenderer::Image{};
}
void VSVulkanContext::resetSceneMonitor()
{
    mImpl->sceneMonitorCompleted.reset();if(mImpl->sceneMonitor)mImpl->sceneMonitor->reset();
}
