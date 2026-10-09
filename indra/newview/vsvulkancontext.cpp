// Native Vulkan ownership for the normal viewer window. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsvulkancontext.h"
#include "llwindow.h"
#include "llviewercontrol.h"
#include "vsuirenderer.h"
#include "vsuiresources.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <stdexcept>
#include <vector>
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
} // namespace
struct VSVulkanContext::Impl
{
    LLWindow&                                         window;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice>  device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain>     swapchain;
    std::unique_ptr<VSUIRenderer>                     renderer;
    std::unique_ptr<VSUIResources>                    resources;
    std::string                                       adapter;
    FrameObserver observer;
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
            LL_INFOS("NativeVulkan") << "Machine Vulkan adapter: " << adapter << LL_ENDL;
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
            renderer  = std::make_unique<VSUIRenderer>(device, context);
            resources = std::make_unique<VSUIResources>(*renderer);
        }
        catch (...)
        {
            cleanup();
            throw;
        }
    }
    void cleanup()
    {
        if (context)
        {
            context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->WaitForIdle();
        }
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
        swapchain.Release();
#if !LL_WINDOWS
        if (display)
            XSync(display, False);
#endif
        context.Release();
        device.Release();
    }
    ~Impl() { cleanup(); }
    bool present(float dpi, const std::function<void()>& draw)
    {
        using namespace Diligent;
        LLCoordWindow size;
        require(window.getSize(&size), "Cannot query native viewer extent");
        if (size.mX <= 0 || size.mY <= 0 || window.getMinimized())
            return false;
        if (swapchain->GetDesc().Width != unsigned(size.mX) || swapchain->GetDesc().Height != unsigned(size.mY))
        {
            context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            swapchain->Resize(size.mX, size.mY);
        }
        require(swapchain->GetDesc().Width == unsigned(size.mX) && swapchain->GetDesc().Height == unsigned(size.mY),
                "Native swapchain resize did not publish the client extent");
        auto* target = swapchain->GetCurrentBackBufferRTV();
        require(target != nullptr, "Native viewer backbuffer is unavailable");
        context->SetRenderTargets(1, &target, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float color[]{ 16.f / 255, 32.f / 255, 48.f / 255, 1 };
        context->ClearRenderTarget(target, color, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        resources->begin(size.mX, size.mY, dpi);
        draw();
        auto packets = resources->finish();
        renderer->draw(target, size.mX, size.mY, dpi, packets);
        if (observer) observer(device, context, target, size.mX, size.mY, dpi, packets);
        swapchain->Present(gSavedSettings.getBOOL("RenderVSyncEnable") ? 1 : 0);
        renderer->retire();
        return true;
    }
};
VSVulkanContext::VSVulkanContext(LLWindow& w, bool validation) : mImpl(std::make_unique<Impl>(w, validation))
{
}
VSVulkanContext::~VSVulkanContext() = default;
VSUIResources& VSVulkanContext::resources()
{ return *mImpl->resources; }
bool VSVulkanContext::present(float dpi, const std::function<void()>& draw)
{ return mImpl->present(dpi, draw); }
void VSVulkanContext::wait()
{ mImpl->context->WaitForIdle(); }
const std::string& VSVulkanContext::adapter() const
{ return mImpl->adapter; }

void VSVulkanContext::setFrameObserver(FrameObserver observer) { mImpl->observer = std::move(observer); }

const Diligent::GraphicsAdapterInfo& VSVulkanContext::adapterInfo() const
{
    return mImpl->device->GetAdapterInfo();
}
