// Native viewer window/device/presentation checkpoint. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsvulkandiagnostic.h"
#include "vsuirenderer.h"
#include "vsuifont.h"
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
#include <array>
#include <cmath>
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
#if !LL_WINDOWS
    Display* native_display = nullptr; // Borrowed from the owned SDL window.
#endif
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
    bool ui_mode = false, ui_verified = false;
    unsigned ui_readbacks = 0;
    std::unique_ptr<VSUIRenderer> ui;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    explicit Impl(const std::string& directory) : evidence(directory)
    {
        if (const char* value = std::getenv("VS_VULKAN_DIAGNOSTIC_FAIL")) injected = value;
        ui_mode = std::getenv("VS_VULKAN_DIAGNOSTIC_UI") != nullptr;
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
        native_display = info.info.x11.display;
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
        if (ui_mode) ui = std::make_unique<VSUIRenderer>(device, context);
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
        if (ui_mode && phase == 4 && phase_frames < 2) verifyUI(target, phase_frames == 0 ? 1.f : 2.f);
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
    void verifyUI(Diligent::ITextureView* target, float dpi)
    {
        using namespace Diligent;
        using Pixel = std::array<unsigned char,4>;
        struct OracleQuad
        {
            unsigned width, height;
            std::vector<unsigned char> bytes;
            std::array<float,4> bounds, clip;
            float dx;
            bool premultiplied = false;
        };
        std::vector<VSUIRenderer::Packet> packets;
        std::vector<OracleQuad> quads;
        auto rejected = [](auto operation)
        {
            bool caught=false;
            try { operation(); } catch (const std::runtime_error&) { caught=true; }
            require(caught,"Invalid UI resource request was admitted");
        };
        rejected([&]() { ui->upload(0,1,{}); });
        rejected([&]() { ui->upload(2,2,{0,0,0,255}); });
        auto add = [&](unsigned w, unsigned h, std::vector<unsigned char> bytes,
                       std::array<float,4> bounds, std::array<float,4> clip, float dx=0)
        {
            VSUIRenderer::Packet p;
            p.image=ui->upload(w,h,bytes); p.bounds=bounds; p.clip=clip; p.transform[4]=dx;
            packets.push_back(p); quads.push_back({w,h,std::move(bytes),bounds,clip,dx});
        };
        const std::array<float,4> full{0,0,320/dpi,240/dpi};
        // Asymmetric UV colors, clip and overlap expose origin/order mistakes.
        add(2,2,{255,0,0,128, 0,255,0,128, 0,0,255,128, 255,255,255,128},
            {8,8,40,40},{12,10,36,34});
        std::weak_ptr<const VSUIRenderer::Texture> old_generation=packets[0].image;
        const auto first_generation=VSUIRenderer::generation(packets[0].image);
        auto replacement=ui->upload(1,1,{0,255,255,255});
        require(VSUIRenderer::generation(replacement) > first_generation,"UI texture generation did not advance");
        add(1,1,{200,20,40,128},{20,16,44,36},full,2);
        // The replacement is drawn separately; the original queued quad must
        // still sample its old immutable bytes without a completion wait.
        VSUIRenderer::Packet next;
        next.image=replacement; next.bounds={46,8,54,16}; next.clip=full;
        packets.push_back(next); quads.push_back({1,1,{0,255,255,255},next.bounds,full,0});
        add(1,1,{100,10,20,128},{8,44,40,56},full);
        packets.back().blend=VSUIRenderer::Blend::PremultipliedAlpha;
        quads.back().premultiplied=true;
        add(1,1,{20,40,200,128},{24,44,48,56},full);
        packets.back().sampling=VSUIRenderer::Sampling::Linear;
        VSUIFont font(std::filesystem::path("fonts") / "DejaVuSans.ttf");
        rejected([&]() { font.rasterize(char32_t(0xd800),18); });
        rejected([&]() { font.rasterize(U'A',0); });
        require(font.rasterize(char32_t(0x10ffff),18).index == 0,"Missing UI glyph did not use .notdef");
        float pen=60;
        for (char32_t cp : {U'A',U'\u03a9',U'\u0416'})
        {
            auto glyph=font.rasterize(cp,18);
            require(glyph.index && glyph.width && glyph.height,"UI fixture Unicode glyph missing");
            std::vector<unsigned char> bytes(std::size_t(glyph.width)*glyph.height*4,255);
            for (std::size_t i=0;i<glyph.coverage.size();++i) bytes[i*4+3]=glyph.coverage[i];
            const float left=pen+glyph.left, top=32-float(glyph.top);
            add(glyph.width,glyph.height,std::move(bytes),
                {left,top,left+glyph.width,top+glyph.height},full);
            pen+=glyph.advance;
        }
        auto small=font.rasterize(U'A',12), large=font.rasterize(U'A',24);
        require(small.coverage != large.coverage,"UI glyph size replacement did not change raster bytes");
        std::vector<unsigned char> small_bytes(std::size_t(small.width)*small.height*4,255);
        for (std::size_t i=0;i<small.coverage.size();++i) small_bytes[i*4+3]=small.coverage[i];
        add(small.width,small.height,std::move(small_bytes),{88,48,88.f+small.width,48.f+small.height},full);
        const auto old_glyph_generation=VSUIRenderer::generation(packets.back().image);
        std::weak_ptr<const VSUIRenderer::Texture> old_glyph=packets.back().image;
        std::vector<unsigned char> large_bytes(std::size_t(large.width)*large.height*4,255);
        for (std::size_t i=0;i<large.coverage.size();++i) large_bytes[i*4+3]=large.coverage[i];
        add(large.width,large.height,std::move(large_bytes),{60,48,60.f+large.width,48.f+large.height},full);
        require(VSUIRenderer::generation(packets.back().image) > old_glyph_generation,
            "UI glyph generation did not advance");
        if (injected == "ui-orientation") packets[0].uv={0,1,1,0};
        ui->draw(target,320,240,dpi,packets);
        packets.clear(); replacement.reset(); next.image.reset();
        require(!old_generation.expired(),"UI packet generation retired before completion");
        require(!old_glyph.expired(),"UI glyph generation retired before completion");
        // Read the actual native swapchain image before presentation.
        auto desc=target->GetTexture()->GetDesc();
        const bool bgra=desc.Format == TEX_FORMAT_BGRA8_UNORM;
        desc.Name="Viewer UI readback"; desc.Usage=USAGE_STAGING;
        desc.BindFlags=BIND_NONE; desc.CPUAccessFlags=CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> readback; device->CreateTexture(desc,nullptr,&readback);
        require(readback != nullptr,"Viewer UI readback allocation failed");
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{}; copy.pSrcTexture=target->GetTexture(); copy.pDstTexture=readback;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy); context->WaitForIdle();
        ui->retire(); require(old_generation.expired(),"UI generation retained beyond explicit retirement");
        require(old_glyph.expired(),"UI glyph retained beyond fence completion");
        MappedTextureSubresource mapped{};
        context->MapTextureSubresource(readback,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        require(mapped.pData != nullptr,"Viewer UI readback mapping failed");
        const std::string stem=dpi == 1 ? "viewer-ui-1x" : "viewer-ui-2x";
        std::ofstream actual(evidence/(stem+".ppm"),std::ios::binary);
        std::ofstream expected(evidence/(stem+"-expected.ppm"),std::ios::binary);
        actual << "P6\n320 240\n255\n"; expected << "P6\n320 240\n255\n";
        unsigned mismatches=0;
        for (unsigned y=0;y<240;++y) for (unsigned x=0;x<320;++x)
        {
            Pixel oracle{16,32,48,255};
            const double lx=(x+0.5)/dpi, ly=(y+0.5)/dpi;
            for (const auto& q : quads)
            {
                if (x < std::floor(q.clip[0]*dpi) || x >= std::ceil(q.clip[2]*dpi) ||
                    y < std::floor(q.clip[1]*dpi) || y >= std::ceil(q.clip[3]*dpi) ||
                    lx < q.bounds[0]+q.dx || lx >= q.bounds[2]+q.dx || ly < q.bounds[1] || ly >= q.bounds[3]) continue;
                const auto tx=unsigned((lx-q.bounds[0]-q.dx)*q.width/(q.bounds[2]-q.bounds[0]));
                const auto ty=unsigned((ly-q.bounds[1])*q.height/(q.bounds[3]-q.bounds[1]));
                const auto* src=q.bytes.data()+(std::size_t(ty)*q.width+tx)*4;
                for (unsigned c=0;c<3;++c)
                    oracle[c]=static_cast<unsigned char>(std::lround((src[c]*double(q.premultiplied?255:src[3])+oracle[c]*double(255-src[3]))/255));
            }
            if (injected == "bad-ui" && x == 0 && y == 0) oracle[0]=255;
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.Stride+x*4;
            const unsigned char rgb[]{pixel[bgra?2:0],pixel[1],pixel[bgra?0:2]};
            actual.write(reinterpret_cast<const char*>(rgb),3);
            expected.write(reinterpret_cast<const char*>(oracle.data()),3);
            for (unsigned c=0;c<3;++c) if (std::abs(int(rgb[c])-int(oracle[c])) > 2) ++mismatches;
            if (pixel[3] != 255) ++mismatches;
        }
        context->UnmapTextureSubresource(readback,0,0);
        require(actual.good() && expected.good(),"Cannot write viewer UI readbacks");
        require(mismatches == 0,"Viewer UI pixel oracle mismatch");
        ++ui_readbacks; ui_verified=ui_readbacks == 2;
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
                require(!ui_mode || ui_verified,"Viewer UI fixture sequence incomplete");
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
        if (ui) { ui->retire(); ui.reset(); }
#if !LL_WINDOWS
        // Vulkan idle does not drain asynchronous XCB surface requests. In
        // particular, an unpresented software swapchain may still have queued
        // shared-memory attachments that must reach X11 before memory retires.
        if (native_display) XSync(native_display, False);
#endif
        swapchain.Release(); stages.emplace_back("swapchain-released");
#if !LL_WINDOWS
        // Retire the driver's X11 detach/free requests while SDL still owns
        // the display connection and native window.
        if (native_display) XSync(native_display, False);
#endif
        if (window)
        {
            require(LLWindowManager::destroyWindow(window), "Viewer native window destruction failed");
            window = nullptr;
#if !LL_WINDOWS
            native_display = nullptr;
#endif
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
            {"ui_fixture_enabled", ui_mode}, {"ui_readback_verified", ui_verified},
            {"ui_readbacks", ui_readbacks},
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
