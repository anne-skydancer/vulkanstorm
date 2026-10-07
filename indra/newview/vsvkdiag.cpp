// Native viewer window/device/presentation checkpoint. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsvulkandiagnostic.h"
#include "llwindow.h"
#include "llwindowcallbacks.h"
#include "llkeyboard.h"
#include "llgl.h"
// The viewer PCH imports X11 and sys/mman.h macros with Diligent type names.
// Preserve the platform definitions while parsing the GHI's C++ interfaces.
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
#include <vulkan/vulkan.h>
#include <boost/json.hpp>
#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#if LL_WINDOWS
#include <tlhelp32.h>
#else
#include <SDL2/SDL.h>
#include <SDL2/SDL_syswm.h>
#include <X11/Xlib-xcb.h>
#endif

namespace
{
std::atomic<unsigned> diagnostic_errors{0};
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}
void DILIGENT_CALL_TYPE diagnostic(Diligent::DEBUG_MESSAGE_SEVERITY severity,
    const Diligent::Char* message, const Diligent::Char*, const Diligent::Char*, int)
{
    std::cerr << "DILIGENT " << severity << ": " << message << '\n';
    if (severity >= Diligent::DEBUG_MESSAGE_SEVERITY_ERROR) ++diagnostic_errors;
}
void loadedLibraries()
{
#if LL_WINDOWS
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    require(snapshot != INVALID_HANDLE_VALUE, "Cannot capture loaded libraries");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) do
    {
        std::cout << "LOADED=" << ll_convert_wide_to_string(entry.szExePath) << '\n';
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
#else
    std::ifstream maps("/proc/self/maps");
    require(maps.good(), "Cannot capture loaded libraries");
    std::string line;
    while (std::getline(maps, line)) std::cout << "LOADED=" << line << '\n';
#endif
}
}

struct VSVulkanDiagnostic::Impl : LLWindowCallbacks
{
    LLWindow* window = nullptr;
    Diligent::IEngineFactoryVk* factory = nullptr;
    Diligent::RefCntAutoPtr<Diligent::IRenderDevice> device;
    Diligent::RefCntAutoPtr<Diligent::IDeviceContext> context;
    Diligent::RefCntAutoPtr<Diligent::ISwapChain> swapchain;
    std::filesystem::path evidence;
    boost::json::array stages;
    boost::json::object identity;
    std::string failure;
    std::string injected;
    unsigned phase = 0, frames = 0, phase_frames = 0, waits = 0;
    unsigned zero_skips = 0, minimized_skips = 0, resize_events = 0;
    bool quit = false, minimized_observed = false, cleaned = false;
    bool clear_readback_verified = false;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    explicit Impl(const std::string& directory) : evidence(directory)
    {
        if (const char* value = std::getenv("VS_VULKAN_DIAGNOSTIC_FAIL")) injected = value;
        std::filesystem::create_directories(evidence);
        std::filesystem::remove(evidence / "viewer-presentation.json");
    }
    bool handleCloseRequest(LLWindow*, bool) override { quit = true; return false; }
    void handleQuit(LLWindow*) override { quit = true; }
    void handleResize(LLWindow*, S32, S32) override { ++resize_events; }
    void inject(const char* stage)
    {
        if (injected == stage) throw std::runtime_error(std::string("Injected failure: ") + stage);
    }
    void error(const std::exception& exception)
    {
        if (failure.empty()) failure = exception.what();
        std::cerr << "VIEWER_VULKAN_FAILURE=" << failure << '\n';
    }
    void initialize()
    {
        using namespace Diligent;
        diagnostic_errors = 0;
        inject("before-window");
        window = LLWindowManager::createWindow(this, "Vulkanstorm Vulkan diagnostic",
            "VulkanstormVulkanDiagnostic", 100, 100, 320, 240,
            0, false, false, true, false, false, 0, 0, 4.6f, false,
            LLWindowManager::GraphicsAPI::Vulkan);
        require(window != nullptr, "Viewer native window creation failed");
        require(!gGLManager.mInited, "Native viewer window initialized GL");
        stages.emplace_back("viewer-window-created");
        window->show();
        inject("after-window");
        Uint32 count = 0;
        require(vkEnumerateInstanceLayerProperties(&count, nullptr) == VK_SUCCESS,
            "Cannot enumerate Vulkan validation layers");
        std::vector<VkLayerProperties> layers(count);
        require(vkEnumerateInstanceLayerProperties(&count, layers.data()) == VK_SUCCESS,
            "Cannot enumerate Vulkan validation layers");
        require(std::any_of(layers.begin(), layers.end(), [](const VkLayerProperties& layer)
            { return std::string(layer.layerName) == "VK_LAYER_KHRONOS_validation"; }),
            "Required validation layer is unavailable");
        factory = LoadAndGetEngineFactoryVk();
        require(factory != nullptr, "Diligent Vulkan factory unavailable");
        factory->SetMessageCallback(diagnostic);
        Uint32 adapters_count = 0;
        factory->EnumerateAdapters({1, 1}, adapters_count, nullptr);
        require(adapters_count > 0, "No Vulkan adapter available");
        std::vector<GraphicsAdapterInfo> adapters(adapters_count);
        factory->EnumerateAdapters({1, 1}, adapters_count, adapters.data());
        EngineVkCreateInfo ci{};
        ci.EnableValidation = true;
        const char* layer = "VK_LAYER_KHRONOS_validation";
        ci.InstanceLayerCount = 1; ci.ppInstanceLayerNames = &layer;
        // Prefer a machine GPU when present; software devices remain eligible.
        ci.AdapterId = 0;
        for (Uint32 i = 1; i < adapters_count; ++i)
            if (adapters[i].Type > adapters[ci.AdapterId].Type) ci.AdapterId = i;
        factory->CreateDeviceAndContextsVk(ci, &device, &context);
        require(device && context, "Diligent Vulkan device creation failed");
        const auto& actual = device->GetAdapterInfo();
        require(actual.VendorId == adapters[ci.AdapterId].VendorId &&
                actual.DeviceId == adapters[ci.AdapterId].DeviceId &&
                std::string(actual.Description) == adapters[ci.AdapterId].Description,
            "Diligent selected an unexpected adapter");
        identity = {{"name", actual.Description}, {"vendor_id", actual.VendorId},
            {"device_id", actual.DeviceId}, {"adapter_type", static_cast<unsigned>(actual.Type)}};
        std::cout << "DILIGENT_DEVICE=" << actual.Description << '\n';
        stages.emplace_back("device-created");
        loadedLibraries();
        inject("after-device");
        NativeWindow native{};
#if LL_WINDOWS
        native.hWnd = window->getPlatformWindow();
#else
        SDL_SysWMinfo info{}; SDL_VERSION(&info.version);
        require(SDL_GetWindowWMInfo(static_cast<SDL_Window*>(window->getPlatformWindow()), &info) &&
            info.subsystem == SDL_SYSWM_X11, "Viewer SDL window is not X11");
        native.WindowId = info.info.x11.window;
        native.pDisplay = info.info.x11.display;
        native.pXCBConnection = XGetXCBConnection(info.info.x11.display);
        require(native.pXCBConnection != nullptr, "Viewer SDL XCB connection is unavailable");
#endif
        LLCoordWindow size;
        require(window->getSize(&size) && size.mX == 320 && size.mY == 240,
            "Initial viewer client extent differs");
        SwapChainDesc desc{}; desc.Width = size.mX; desc.Height = size.mY;
        desc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM;
        desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
        desc.Usage = SWAP_CHAIN_USAGE_RENDER_TARGET | SWAP_CHAIN_USAGE_COPY_SOURCE;
        factory->CreateSwapChainVk(device, context, desc, native, &swapchain);
        require(swapchain != nullptr, "Viewer swapchain creation failed");
        require(swapchain->GetDesc().Width == 320 && swapchain->GetDesc().Height == 240,
            "Initial viewer swapchain extent differs");
        stages.emplace_back("swapchain-created-320x240");
        loadedLibraries();
        inject("after-swapchain");
    }
    void render(unsigned width, unsigned height, bool minimized)
    {
        if (!width || !height) { ++zero_skips; return; }
        if (minimized) { ++minimized_skips; return; }
        if (swapchain->GetDesc().Width != width || swapchain->GetDesc().Height != height)
        {
            context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            swapchain->Resize(width, height);
        }
        require(swapchain->GetDesc().Width == width && swapchain->GetDesc().Height == height,
            "Viewer swapchain resize did not publish the expected extent");
        auto* target = swapchain->GetCurrentBackBufferRTV();
        require(target != nullptr, "Viewer swapchain has no backbuffer");
        context->SetRenderTargets(1, &target, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float color[] = {injected == "bad-clear" ? 0.5f : 16.f / 255,
            32.f / 255, 48.f / 255, 1.0f};
        context->ClearRenderTarget(target, color, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        if (phase == 4 && phase_frames == 2) verifyClear(target->GetTexture());
        swapchain->Present(1);
        ++frames;
    }
    void verifyClear(Diligent::ITexture* source)
    {
        using namespace Diligent;
        auto desc = source->GetDesc();
        require(desc.Format == TEX_FORMAT_RGBA8_UNORM || desc.Format == TEX_FORMAT_BGRA8_UNORM,
            "Viewer clear oracle requires an SDR UNORM surface");
        const bool bgra = desc.Format == TEX_FORMAT_BGRA8_UNORM;
        desc.Name = "Viewer diagnostic clear readback";
        desc.Usage = USAGE_STAGING; desc.BindFlags = BIND_NONE;
        desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> readback;
        device->CreateTexture(desc, nullptr, &readback);
        require(readback != nullptr, "Viewer clear readback allocation failed");
        context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{}; copy.pSrcTexture = source; copy.pDstTexture = readback;
        copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);
        context->WaitForIdle();
        MappedTextureSubresource mapped{};
        context->MapTextureSubresource(readback, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
        require(mapped.pData != nullptr, "Viewer clear readback mapping failed");
        std::ofstream output(evidence / "viewer-clear.ppm", std::ios::binary);
        output << "P6\n" << desc.Width << ' ' << desc.Height << "\n255\n";
        unsigned mismatches = 0;
        for (unsigned y = 0; y < desc.Height; ++y) for (unsigned x = 0; x < desc.Width; ++x)
        {
            const auto* pixel = static_cast<const unsigned char*>(mapped.pData) + y * mapped.Stride + x * 4;
            const unsigned char rgb[] = {pixel[bgra ? 2 : 0], pixel[1], pixel[bgra ? 0 : 2]};
            output.write(reinterpret_cast<const char*>(rgb), 3);
            if (std::abs(int(rgb[0]) - 16) > 1 || std::abs(int(rgb[1]) - 32) > 1 ||
                std::abs(int(rgb[2]) - 48) > 1 || pixel[3] != 255) ++mismatches;
        }
        context->UnmapTextureSubresource(readback, 0, 0);
        require(output.good(), "Cannot write viewer clear readback");
        require(mismatches == 0, "Viewer clear pixel oracle mismatch");
        clear_readback_verified = true;
    }
    bool frame()
    {
        if (!failure.empty()) return true;
        require(std::chrono::steady_clock::now() - started < std::chrono::seconds(30),
            "Viewer diagnostic lifecycle timed out");
        window->gatherInput();
        require(!gGLManager.mInited, "Native viewer frame entered GL initialization");
        require(!quit, "Viewer diagnostic window closed before completion");
        inject("frame");
        LLCoordWindow size;
        require(window->getSize(&size), "Cannot query viewer client extent");
        if (phase == 0 || phase == 1 || phase == 4)
        {
            const unsigned width = phase == 1 ? 640 : 320;
            const unsigned height = phase == 1 ? 360 : 240;
            if (size.mX != width || size.mY != height || window->getMinimized())
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return false;
            }
            render(width, height, false);
            if (++phase_frames < 3) return false;
            phase_frames = 0;
            stages.emplace_back(phase == 0 ? "present-initial" :
                phase == 1 ? "resize-present-640x360" : "restore-present-320x240");
            if (phase == 0)
            {
                require(window->setSize(LLCoordWindow(640, 360)), "Viewer window resize failed");
                phase = 1;
            }
            else if (phase == 1) phase = 2;
            else
            {
                require(frames == 9 && zero_skips == 3 && minimized_skips == 2,
                    "Viewer lifecycle counters differ");
                if (injected == "gl-trap") window->swapBuffers();
                return true;
            }
        }
        else if (phase == 2)
        {
            const unsigned before = frames;
            render(0, 0, false); render(0, 360, false); render(640, 0, false);
            require(before == frames, "Zero extent submitted a viewer frame");
            stages.emplace_back("zero-extents-suspended");
            window->minimize();
            phase = 3;
        }
        else if (phase == 3)
        {
            if (!window->getMinimized() && ++waits < 50)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                return false;
            }
            minimized_observed = window->getMinimized();
#if LL_WINDOWS
            require(minimized_observed, "Viewer HWND did not minimize");
#endif
            const unsigned before = frames;
            render(640, 360, true); render(640, 360, true);
            require(before == frames, "Minimized viewer submitted a frame");
            stages.emplace_back("minimized-suspended");
            window->restore();
            require(window->setSize(LLCoordWindow(320, 240)), "Viewer restoration resize failed");
            phase = 4;
        }
        return false;
    }
    void cleanup()
    {
        if (cleaned) return;
        if (context)
        {
            context->SetRenderTargets(0, nullptr, nullptr, Diligent::RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->WaitForIdle();
        }
        swapchain.Release(); stages.emplace_back("swapchain-released");
        if (window)
        {
            require(LLWindowManager::destroyWindow(window), "Viewer native window destruction failed");
            window = nullptr;
            delete gKeyboard; gKeyboard = nullptr;
        }
        stages.emplace_back("viewer-window-destroyed");
        context.Release(); device.Release(); stages.emplace_back("device-context-released");
        cleaned = true;
        require(diagnostic_errors == 0, "Viewer Vulkan diagnostics contain errors");
        inject("shutdown");
    }
    void write()
    {
#if LL_WINDOWS
        constexpr const char* window_api = "Win32";
#else
        constexpr const char* window_api = "SDL2/X11";
#endif
        boost::json::object record{{"schema", 1}, {"mode", "viewer-native-diagnostic"},
            {"application_lifecycle", "LLAppViewer::init/frame/cleanup"},
            {"window_factory", "LLWindowManager::createWindow"},
            {"window_api", window_api},
            {"device", identity}, {"stages", stages}, {"presented_frames", frames},
            {"zero_extent_skips", zero_skips}, {"minimized_skips", minimized_skips},
            {"native_minimize_observed", minimized_observed}, {"resize_events", resize_events},
            {"clear_readback_verified", clear_readback_verified},
            {"validation_errors", diagnostic_errors.load()}, {"shutdown_complete", cleaned},
            {"passed", failure.empty() && cleaned}, {"failure", failure}};
        std::ofstream output(evidence / "viewer-presentation.json");
        output << boost::json::serialize(record) << '\n';
        require(output.good(), "Cannot write viewer presentation evidence");
    }
};

VSVulkanDiagnostic::VSVulkanDiagnostic(const std::string& path) : mImpl(std::make_unique<Impl>(path)) {}
VSVulkanDiagnostic::~VSVulkanDiagnostic()
{
    if (!mImpl->cleaned) try { mImpl->cleanup(); } catch (const std::exception& e) { mImpl->error(e); }
}
void VSVulkanDiagnostic::initialize()
{
    try { mImpl->initialize(); } catch (const std::exception& e) { mImpl->error(e); }
}
bool VSVulkanDiagnostic::frame()
{
    try { return mImpl->frame(); } catch (const std::exception& e) { mImpl->error(e); return true; }
}
void VSVulkanDiagnostic::cleanup()
{
    try { mImpl->cleanup(); } catch (const std::exception& e) { mImpl->error(e); }
    try { mImpl->write(); } catch (const std::exception& e) { mImpl->error(e); }
    std::cout << (exitCode() == 0 ? "PASS viewer-native-diagnostic" : "FAIL viewer-native-diagnostic") << '\n';
}
int VSVulkanDiagnostic::exitCode() const { return mImpl->failure.empty() && mImpl->cleaned ? 0 : 1; }
