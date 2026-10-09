// Native viewer window/device/presentation checkpoint. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsvulkandiagnostic.h"
#include "vsuirenderer.h"
#include "vsuifixture.h"
#include "vsuipixeloracle.h"
#include "vsuifont.h"
#include "vsuiresources.h"
#include "vsuifontcache.h"
#include "vsuifontbridge.h"
#include "llfontgl.h"
#include "llfontfreetype.h"
#include "llimage.h"
#include "llimagetga.h"
#include "lluictrlfactory.h"
#include "llfloaterreg.h"
#include "llpanel.h"
#include "llviewerwindow.h"
#include "llappviewer.h"
#include "llpluginprocessparent.h"
#include "llnotifications.h"
#include "llfloatertos.h"
#include "lltoastalertpanel.h"
#include "llviewermedia.h"
#include "vsvulkancontext.h"
#include "llviewercontrol.h"
#include "llprogressview.h"
#include "fspanellogin.h"
#include "llsecapi.h"
#include "llviewernetwork.h"
#include "llrootview.h"
#include "lllineeditor.h"
#include "lltexteditor.h"
#include "llbutton.h"
#include "llfocusmgr.h"
#include "lldir.h"
#include "llmimetypes.h"
#include "llerrorcontrol.h"
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
    bool startup_mode = false, startup_login = false, startup_progress = false;
    unsigned startup_readbacks = 0;
    std::unique_ptr<LLViewerWindow> startup_window;
    bool startup_http = false;
    LLNotificationPtr startup_alert;
    LLHandle<LLFloater> startup_critical;
    bool startup_alert_verified = false, startup_critical_verified = false;
    LLError::RecorderPtr startup_log;
    unsigned ui_readbacks = 0;
    bool ui_xui_verified=false,ui_input_verified=false,ui_focus_verified=false;
    std::function<bool(llwchar)> unicode_handler;
    std::function<bool(KEY,MASK)> key_handler;
    std::function<void(bool)> focus_handler;
    std::function<bool(LLCoordGL,MASK,bool)> mouse_handler;
    std::function<void(LLCoordGL,MASK)> hover_handler;
    std::function<bool(LLCoordGL,S32)> scroll_handler;
    LLCoordGL mouse_point;
    unsigned mouse_releases=0,mouse_moves=0,scroll_events=0;
    bool ui_mouse_verified=false,ui_scroll_verified=false;
    std::array<std::string,3> ui_skin_selection;
    bool ui_facade_verified=false,ui_atlas_verified=false,ui_font_producer_verified=false,ui_admission_verified=false;
    std::unique_ptr<VSUIRenderer> ui;
    const std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    explicit Impl(const std::string& directory) : evidence(directory)
    {
        if (const char* value = std::getenv("VS_VULKAN_DIAGNOSTIC_FAIL")) injected = value;
        ui_mode = std::getenv("VS_VULKAN_DIAGNOSTIC_UI") != nullptr;
        startup_mode = std::getenv("VS_VULKAN_DIAGNOSTIC_STARTUP") != nullptr;
        std::filesystem::create_directories(evidence);
        std::filesystem::remove(evidence / "viewer-presentation.json");
    }
    bool handleCloseRequest(LLWindow*, bool) override { quit = true; return false; }
    void handleQuit(LLWindow*) override { quit = true; }
    void handleResize(LLWindow*, S32, S32) override { ++resize_events; }
    void handleFocus(LLWindow*) override { if (focus_handler) focus_handler(true); }
    void handleFocusLost(LLWindow*) override { if (focus_handler) focus_handler(false); }
    bool handleTranslatedKeyDown(KEY key,MASK mask,bool) override { return key_handler && key_handler(key,mask); }
    bool handleUnicodeChar(llwchar c,MASK) override { return unicode_handler && unicode_handler(c); }
    bool handleMouseDown(LLWindow*,LLCoordGL point,MASK mask) override
    { mouse_point=point;return mouse_handler && mouse_handler(point,mask,true); }
    bool handleMouseUp(LLWindow*,LLCoordGL point,MASK mask) override
    { mouse_point=point;const bool handled=mouse_handler && mouse_handler(point,mask,false);if(handled) ++mouse_releases;return handled; }
    void handleMouseMove(LLWindow*,LLCoordGL point,MASK mask) override
    { mouse_point=point;if(hover_handler) { hover_handler(point,mask);++mouse_moves; } }
    void handleScrollWheel(LLWindow*,S32 clicks) override
    { if(scroll_handler && scroll_handler(mouse_point,clicks)) ++scroll_events; }
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
        if (startup_mode) { initializeStartup(); return; }
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
        if (phase == 4 && phase_frames == 2)
        {
            verifyClear(target->GetTexture());
            if (ui_mode) verifyWidgets(target);
        }
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
        {
            // No UI singleton or constructor may be reached through these denied routes.
            VSUIAdmission admission([](const std::type_info&) { return false; },
                                    [](std::string_view) { return false; });
            const auto* panel_builder=LLDefaultChildRegistry::instance().getValue("panel");
            require(panel_builder && (*panel_builder)({},nullptr,{})==nullptr,
                    "Denied XUI widget reached construction");
            require(LLFloaterReg::build<LLFloater>(LLSD())==nullptr,
                    "Denied direct floater reached construction");
            require(LLFloaterReg::getInstance("native-denied",LLSD())==nullptr &&
                    LLFloaterReg::showInstance("native-denied",LLSD(),false)==nullptr &&
                    !LLFloaterReg::canShowInstance("native-denied",LLSD()),
                    "Denied floater reached registry callbacks");
            LLFloaterReg::showInitialVisibleInstances();
        }
        require(VSUIAdmission::widget(typeid(LLPanel)) && VSUIAdmission::floater("native-denied"),
                "Scoped native admission leaked into legacy UI");
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
            try { operation(); } catch (const std::exception&) { caught=true; }
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
        VSUIResources resources(*ui);
        LLPointer<LLImageRaw> raw=new LLImageRaw(2,2,4),decoded=new LLImageRaw;
        const std::vector<unsigned char> pattern{255,0,0,128,0,255,0,128,0,0,255,128,255,255,255,128};
        std::copy_n(pattern.data()+8,8,raw->getData());std::copy_n(pattern.data(),8,raw->getData()+8);
        LLPointer<LLImageTGA> tga=new LLImageTGA;
        require(tga->encode(raw) && tga->decode(decoded),"Viewer CPU image codec failed");
        resources.publish("decoded",*decoded);
        auto image=resources.region("decoded","decoded",LLRectf(0,1,1,0),VSUIRenderer::Sampling::Nearest);
        rejected([&]() { image->getImage(); });
        resources.begin(320,240,dpi);resources.setClip({12,10,36,34});
        image->draw(8,int(240/dpi)-40,32,32,LLColor4::white);
        packets=resources.finish(); require(packets.size()==1,"Native image facade did not publish one packet");
        quads.push_back({2,2,pattern,{8,8,40,40},{12,10,36,34},0});
        std::weak_ptr<const VSUIRenderer::Texture> old_generation=packets[0].image;
        const auto first_generation=VSUIRenderer::generation(packets[0].image);
        auto replacement=ui->upload(1,1,{0,255,255,255});
        require(VSUIRenderer::generation(replacement) > first_generation,"UI texture generation did not advance");
        const auto original=resources.generation("decoded");
        resources.patch("decoded",0,0,1,1,{0,255,255,128});
        require(resources.generation("decoded")>original,"UI patch did not replace its generation");
        rejected([&]() { resources.patch("decoded",2,0,1,1,{0,0,0,255}); });
        resources.begin(320,240,dpi);
        auto corner=resources.region("decoded","patched-corner",LLRectf(0,1,.5f,.5f),VSUIRenderer::Sampling::Nearest);
        corner->draw(46,int(240/dpi)-32,8,8,LLColor4::white);
        image->drawSolid(8,int(240/dpi)-72,16,12,LLColor4(.2f,.6f,.8f,.5f));
        auto facade_packets=resources.finish();packets.insert(packets.end(),facade_packets.begin(),facade_packets.end());
        facade_packets.clear();
        quads.push_back({1,1,{0,255,255,128},{46,24,54,32},full,0});
        quads.push_back({1,1,{51,153,204,64},{8,60,24,72},full,0});
        // Nine-slice borders use the same existing LLUIImage scaling facade.
        LLPointer<LLImageRaw> sliced=new LLImageRaw(3,3,4);
        std::vector<Pixel> tile;
        for (unsigned i=0;i<9;++i) tile.push_back(Pixel{static_cast<unsigned char>(20+i*20),80,160,200});
        for (unsigned y=0;y<3;++y) for (unsigned x=0;x<3;++x)
            std::copy_n(tile[y*3+x].data(),4,sliced->getData()+((2-y)*3+x)*4);
        auto nine=resources.publish("nine",*sliced);nine->setScaleRegion(LLRectf(1.f/3,2.f/3,2.f/3,1.f/3));
        auto nearest=resources.region("nine","nine-region",LLRectf(0,1,1,0),VSUIRenderer::Sampling::Nearest);
        nearest->setScaleRegion(LLRectf(1.f/3,2.f/3,2.f/3,1.f/3));
        resources.begin(320,240,dpi);nearest->draw(110,int(240/dpi)-32,16,24,LLColor4::white);
        facade_packets=resources.finish();require(facade_packets.size()==9,"UI nine-slice did not publish nine quads");
        packets.insert(packets.end(),facade_packets.begin(),facade_packets.end());facade_packets.clear();
        const float xs[]{110,111,125,126},ys[]{8,9,31,32};
        for (unsigned row=0;row<3;++row) for (unsigned col=0;col<3;++col)
            quads.push_back({1,1,std::vector<unsigned char>(tile[row*3+col].begin(),tile[row*3+col].end()),
                            {xs[col],ys[row],xs[col+1],ys[row+1]},full,0});
        VSUIFontCache atlas(resources,std::filesystem::path("fonts")/"DejaVuSans.ttf",32);
        const auto& cached=atlas.get(U'A',18);const auto page_generation=resources.generation(cached.page);
        require(&cached==&atlas.get(U'A',18) && atlas.glyphCount()==1 && resources.generation(cached.page)==page_generation,
            "Native lazy glyph lookup republished a cached glyph");
        resources.begin(320,240,dpi);
        cached.image->drawSolid(110,int(240/dpi)-50-int(cached.metrics.height),cached.metrics.width,cached.metrics.height,LLColor4::white);
        auto glyph_packets=resources.finish();require(glyph_packets.size()==1,"Native cached glyph did not draw");
        packets.insert(packets.end(),glyph_packets.begin(),glyph_packets.end());glyph_packets.clear();
        std::vector<unsigned char> glyph_bytes(std::size_t(cached.metrics.width)*cached.metrics.height*4,255);
        for (std::size_t i=0;i<cached.metrics.coverage.size();++i) glyph_bytes[i*4+3]=cached.metrics.coverage[i];
        quads.push_back({cached.metrics.width,cached.metrics.height,std::move(glyph_bytes),
            {110,50,110.f+cached.metrics.width,50.f+cached.metrics.height},full,0});
        for (char32_t cp=U'B';cp<=U'Z';++cp) atlas.get(cp,18);
        require(atlas.pageCount()>1,"Native glyph atlas rollover was not exercised");
        atlas.reset();require(atlas.glyphCount()==0 && atlas.pageCount()==0,"Native glyph atlas reset retained producers");
        resources.erase("decoded");image=nullptr;corner=nullptr;
        {
            VSUIFontBridge bridge(resources);
            struct ScaleGuard
            {
                float x=LLFontGL::sScaleX,y=LLFontGL::sScaleY;
                ~ScaleGuard() { LLFontGL::sScaleX=x;LLFontGL::sScaleY=y; }
            } scale;
            LLFontGL::sScaleX=dpi;LLFontGL::sScaleY=dpi;
            LLFontGL producer;
            LLPointer<LLFontFreetype> reference=new LLFontFreetype;
            require(producer.loadFace("fonts/DejaVuSans.ttf",13.5f,96*dpi,96*dpi,false,0) &&
                    reference->loadFace("fonts/DejaVuSans.ttf",13.5f,96*dpi,96*dpi,false,0),
                    "Native viewer font face load failed");
            resources.begin(320,240,dpi);
            const auto rendered=producer.renderUTF8("AV\xCE\xA9\xD0\x96",0,32.f,240/dpi-100,LLColor4::white,
                LLFontGL::LEFT,LLFontGL::BASELINE,LLFontGL::NORMAL,LLFontGL::NO_SHADOW);
            require(rendered==4,"Native viewer font producer did not render its text");
            require(producer.renderUTF8("B",0,130.f,240/dpi-100,LLColor4::white,
                LLFontGL::LEFT,LLFontGL::BASELINE,LLFontGL::NORMAL,LLFontGL::NO_SHADOW)==1,
                "Native viewer lazy atlas update did not render its text");
            auto font_packets=resources.finish();
            require(!font_packets.empty(),"Native viewer font producer emitted no geometry");
            require(VSUIRenderer::generation(font_packets.front().image)<
                    VSUIRenderer::generation(font_packets.back().image),
                    "Native viewer atlas update did not replace its GPU generation");
            packets.insert(packets.end(),font_packets.begin(),font_packets.end());font_packets.clear();
            float pen=32*dpi;
            const std::array<llwchar,5> text{'A','V',0x03a9,0x0416,'B'};
            for (std::size_t character=0;character<text.size();++character)
            {
                const auto cp=text[character];
                if (character==4) pen=130*dpi;
                const auto* glyph=reference->getGlyphInfo(cp,EFontGlyphType::Grayscale);
                const auto* bitmap=reference->getFontBitmapCache()->getImageRaw(glyph->mBitmapEntry.first,glyph->mBitmapEntry.second);
                std::vector<unsigned char> bytes(std::size_t(glyph->mWidth)*glyph->mHeight*4,255);
                for (int y=0;y<glyph->mHeight;++y) for (int x=0;x<glyph->mWidth;++x)
                    bytes[(std::size_t(y)*glyph->mWidth+x)*4+3]=bitmap->getData()[
                        ((std::size_t(glyph->mYBitmapOffset+glyph->mHeight-1-y)*bitmap->getWidth())+glyph->mXBitmapOffset+x)*2+1];
                const float left=std::round(pen+glyph->mXBearing)/dpi,top=100-std::round(float(glyph->mYBearing))/dpi;
                quads.push_back({unsigned(glyph->mWidth),unsigned(glyph->mHeight),std::move(bytes),
                    {left,top,left+glyph->mWidth/dpi,top+glyph->mHeight/dpi},full,0});
                pen+=glyph->mXAdvance;
                if (character<3)
                    pen+=reference->getXKerning(glyph,reference->getGlyphInfo(text[character+1],EFontGlyphType::Grayscale));
                pen=std::round(pen);
            }
            require(!gGLManager.mInited,"Existing font producer entered GL initialization");
        }
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
        ui_facade_verified=true;ui_atlas_verified=true;ui_font_producer_verified=true;ui_admission_verified=true;
        ++ui_readbacks; ui_verified=ui_readbacks == 2;
    }
    void verifyWidgets(Diligent::ITextureView* target)
    {
        using namespace Diligent;
        VSUIResources resources(*ui);
        VSUIFixture fixture(resources,window);
        ui_skin_selection=fixture.skinSelection();
        fixture.verifyInput();
        if(injected=="ui-gl-trap") gGL.begin(LLRender::TRIANGLES);
        struct ClearHandler { Impl& owner; ~ClearHandler() { owner.unicode_handler={};owner.key_handler={};owner.focus_handler={};owner.mouse_handler={};owner.hover_handler={};owner.scroll_handler={}; } } guard{*this};
        unicode_handler=[&](llwchar c) { return fixture.unicode(c); };
        key_handler=[&](KEY key,MASK mask) { return fixture.key(key,mask); };
        focus_handler=[&](bool focus) { fixture.focus(focus); };
        mouse_handler=[&](LLCoordGL p,MASK mask,bool down) { return fixture.mouse(p.mX,p.mY,mask,down); };
        hover_handler=[&](LLCoordGL p,MASK mask) { fixture.hover(p.mX,p.mY,mask); };
        scroll_handler=[&](LLCoordGL p,S32 clicks) { return fixture.scroll(p.mX,p.mY,clicks); };
        fixture.prepareMouseInput();
#if LL_WINDOWS
        const auto hwnd=static_cast<HWND>(window->getPlatformWindow());
        require(PostMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(25,65)),"Cannot queue native editor hover");
        for(unsigned attempt=0;attempt<100 && (mouse_point.mX!=25 || mouse_point.mY!=174);++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(mouse_point.mX==25 && mouse_point.mY==174,"Native editor hover did not establish click coordinates");
        require(PostMessageW(hwnd,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(25,65)) &&
                PostMessageW(hwnd,WM_LBUTTONUP,0,MAKELPARAM(25,65)),"Cannot queue native viewer mouse input");
#else
        SDL_Event mouse{};mouse.type=SDL_MOUSEBUTTONDOWN;
        mouse.button.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
        mouse.button.button=SDL_BUTTON_LEFT;mouse.button.x=25;mouse.button.y=65;
        require(SDL_PushEvent(&mouse)==1,"Cannot queue native viewer mouse input");
        mouse.type=SDL_MOUSEBUTTONUP;require(SDL_PushEvent(&mouse)==1,"Cannot queue native viewer mouse release");
#endif
        for(unsigned attempt=0;attempt<100 && mouse_releases==0;++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(mouse_releases!=0,"Native window mouse release did not reach XUI");
        fixture.finishMouseInput();ui_mouse_verified=true;
#if LL_WINDOWS
        require(PostMessageW(static_cast<HWND>(window->getPlatformWindow()),WM_CHAR,L'z',0),
                "Cannot queue native viewer character input");
#else
        SDL_Event event{};event.type=SDL_TEXTINPUT;
        event.text.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
        event.text.text[0]='z';
        require(SDL_PushEvent(&event)==1,"Cannot queue native viewer character input");
#endif
        for (unsigned attempt=0;attempt<100 && fixture.inputText()!="native z";++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(fixture.inputText()=="native z","Native window event did not reach the XUI editor");
        #if LL_WINDOWS
        require(PostMessageW(static_cast<HWND>(window->getPlatformWindow()),WM_KEYDOWN,VK_BACK,0) &&
                PostMessageW(static_cast<HWND>(window->getPlatformWindow()),WM_KEYUP,VK_BACK,0),
                "Cannot queue native viewer keyboard input");
#else
        event={};event.type=SDL_KEYDOWN;
        event.key.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
        event.key.keysym.sym=SDLK_BACKSPACE;event.key.keysym.scancode=SDL_SCANCODE_BACKSPACE;
        require(SDL_PushEvent(&event)==1,"Cannot queue native viewer keyboard input");
        event.type=SDL_KEYUP;require(SDL_PushEvent(&event)==1,"Cannot queue native viewer key release");
#endif
        for (unsigned attempt=0;attempt<100 && fixture.inputText()!="native ";++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(fixture.inputText()=="native ","Native window key did not reach the XUI editor");
        ui_input_verified=true;
        for (bool focus : {false,true})
        {
#if LL_WINDOWS
            require(PostMessageW(static_cast<HWND>(window->getPlatformWindow()),focus?WM_SETFOCUS:WM_KILLFOCUS,0,0),
                    "Cannot queue native viewer focus input");
#else
            event={};event.type=SDL_WINDOWEVENT;
            event.window.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
            event.window.event=focus?SDL_WINDOWEVENT_FOCUS_GAINED:SDL_WINDOWEVENT_FOCUS_LOST;
            require(SDL_PushEvent(&event)==1,"Cannot queue native viewer focus input");
#endif
            for (unsigned attempt=0;attempt<100 && fixture.focused()!=focus;++attempt)
            { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
            require(fixture.focused()==focus,"Native window focus did not reach the XUI owner");
        }
        ui_focus_verified=true;
        fixture.prepareScrollInput();
        const auto previous_top=fixture.transcriptTop();
        const auto previous_moves=mouse_moves;
#if LL_WINDOWS
        require(PostMessageW(hwnd,WM_MOUSEMOVE,0,MAKELPARAM(30,150)),"Cannot queue native viewer hover");
#else
        mouse={};mouse.type=SDL_MOUSEMOTION;
        mouse.motion.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
        mouse.motion.x=30;mouse.motion.y=150;
        require(SDL_PushEvent(&mouse)==1,"Cannot queue native viewer hover");
#endif
        for(unsigned attempt=0;attempt<100 && mouse_moves==previous_moves;++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(mouse_moves!=previous_moves && mouse_point.mX==30 && mouse_point.mY>=89 && mouse_point.mY<=90,
                "Native hover coordinates did not reach XUI");
#if LL_WINDOWS
        POINT wheel_point{30,150};
        require(ClientToScreen(hwnd,&wheel_point) &&
                PostMessageW(hwnd,WM_MOUSEWHEEL,MAKEWPARAM(0,3*WHEEL_DELTA),MAKELPARAM(wheel_point.x,wheel_point.y)),
                "Cannot queue native viewer wheel");
#else
        mouse={};mouse.type=SDL_MOUSEWHEEL;
        mouse.wheel.windowID=SDL_GetWindowID(static_cast<SDL_Window*>(window->getPlatformWindow()));
        mouse.wheel.y=3;
        require(SDL_PushEvent(&mouse)==1,"Cannot queue native viewer wheel");
#endif
        for(unsigned attempt=0;attempt<100 && scroll_events==0;++attempt)
        { window->gatherInput();std::this_thread::sleep_for(std::chrono::milliseconds(5)); }
        require(scroll_events!=0,"Native window wheel did not reach XUI");
        fixture.draw();
        std::cout<<"VIEWER_SCROLL_TOP="<<previous_top<<" -> "<<fixture.transcriptTop()<<'\n';
        require(fixture.transcriptTop()!=previous_top,"Native window wheel did not move the transcript viewport");
        ui_scroll_verified=true;
        auto packets=fixture.draw();require(!packets.empty(),"Existing XUI published no native geometry");
        auto pixels=vs_ui_expected_pixels(320,240,1,packets);
        const auto additive=(230*320+245)*4,alpha_additive=(230*320+265)*4;
        require(std::abs(int(pixels[additive])-42)<=1 && std::abs(int(pixels[alpha_additive])-29)<=1,
                "Native glow oracle differs from known blend pixels");
        if (injected=="bad-xui") pixels[0]=255;
        ui->draw(target,320,240,1,packets);
        auto desc=target->GetTexture()->GetDesc();const bool bgra=desc.Format==TEX_FORMAT_BGRA8_UNORM;
        desc.Name="Viewer XUI readback";desc.Usage=USAGE_STAGING;
        desc.BindFlags=BIND_NONE;desc.CPUAccessFlags=CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> readback;device->CreateTexture(desc,nullptr,&readback);
        require(readback!=nullptr,"Cannot allocate existing XUI readback");
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};copy.pSrcTexture=target->GetTexture();copy.pDstTexture=readback;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);context->WaitForIdle();ui->retire();
        MappedTextureSubresource mapped{};
        context->MapTextureSubresource(readback,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        require(mapped.pData!=nullptr,"Cannot map existing XUI readback");
        std::ofstream actual(evidence/"viewer-xui.ppm",std::ios::binary);
        std::ofstream expected(evidence/"viewer-xui-expected.ppm",std::ios::binary);
        actual<<"P6\n320 240\n255\n";expected<<"P6\n320 240\n255\n";
        unsigned mismatches=0;
        for(unsigned y=0;y<240;++y) for(unsigned x=0;x<320;++x)
        {
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.Stride+x*4;
            const unsigned char rgb[]{pixel[bgra?2:0],pixel[1],pixel[bgra?0:2]};
            const auto* oracle=pixels.data()+(y*320+x)*4;
            actual.write(reinterpret_cast<const char*>(rgb),3);
            expected.write(reinterpret_cast<const char*>(oracle),3);
            for(unsigned c=0;c<3;++c) if(std::abs(int(rgb[c])-int(oracle[c]))>3) ++mismatches;
            if(pixel[3]!=255) ++mismatches;
        }
        context->UnmapTextureSubresource(readback,0,0);
        require(actual.good() && expected.good(),"Cannot preserve existing XUI readbacks");
        std::cout<<"VIEWER_XUI_MISMATCHES="<<mismatches<<'\n';
        require(mismatches==0,"Viewer XUI pixel oracle mismatch");ui_xui_verified=true;
    }
    bool frame()
    {
        if (startup_mode) return frameStartup();
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
                require(!ui_mode || (ui_verified && ui_xui_verified && ui_input_verified && ui_focus_verified && ui_mouse_verified && ui_scroll_verified),"Viewer UI fixture sequence incomplete");
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
        if (startup_mode) { cleanupStartup(); return; }
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

    void initializeStartup()
    {
        startup_log = LLError::addGenericRecorder([](LLError::ELevel level, const std::string& message)
        { if (level >= LLError::LEVEL_INFO) std::cerr << "NATIVE_STARTUP " << message << '\n'; });
        diagnostic_errors = 0;
        auto* engine = Diligent::LoadAndGetEngineFactoryVk();
        require(engine != nullptr, "Native startup factory is missing");
        engine->SetMessageCallback(diagnostic);
        gDirUtilp->initAppDirs("VulkanstormNativeStartupDiagnostic", std::filesystem::current_path().string());
        require(gSavedSettings.loadFromFile("app_settings/settings.xml", true, false) > 0, "Native startup settings are missing");
        if (const char* skin = std::getenv("VS_VULKAN_DIAGNOSTIC_SKIN")) gSavedSettings.setString("SkinCurrent", skin);
        if (const char* theme = std::getenv("VS_VULKAN_DIAGNOSTIC_THEME")) gSavedSettings.setString("SkinCurrentTheme", theme);
        gDirUtilp->setSkinFolder(gSavedSettings.getString("SkinCurrent"), gSavedSettings.getString("SkinCurrentTheme"), "en");
        const auto skin_settings = gDirUtilp->getExpandedFilename(LL_PATH_TOP_SKIN, "settings.xml");
        if (std::filesystem::is_regular_file(skin_settings))
            require(gSavedSettings.loadFromFile(skin_settings, true, false) > 0, "Native startup skin settings cannot be loaded");
        LLAppViewer::instance()->getAppCoreHttp().init();
        startup_http = true;
        gSavedSettings.setBOOL("RenderDebugGLSession", true);
        gSavedSettings.setBOOL("PluginUseReadThread", false);
        require(gViewerWindow == nullptr, "Native startup requires exclusive viewer ownership");
        LLViewerWindow::Params params;
        params.title("Vulkanstorm native startup qualification").name("VulkanstormNativeStartupDiagnostic")
            .x(100).y(100).width(640).height(480).min_width(320).min_height(240);
        startup_window = std::make_unique<LLViewerWindow>(params, true);
        gViewerWindow = startup_window.get();
        window = startup_window->getWindow();
        require(!gGLManager.mInited, "Native LLViewerWindow initialized GL");
        stages.emplace_back("native-viewer-window-created");
        std::cout << "DILIGENT_DEVICE=" << startup_window->nativeContext()->adapter() << '\n';
        const auto& actual = startup_window->nativeContext()->adapterInfo();
        identity = {{"name", actual.Description}, {"vendor_id", actual.VendorId},
            {"device_id", actual.DeviceId}, {"adapter_type", static_cast<unsigned>(actual.Type)}};
        loadedLibraries();
        startup_window->nativeContext()->setFrameObserver([this](auto* dev, auto* ctx, auto* target, auto width, auto height, auto dpi, const auto& packets)
        { captureStartup(dev, ctx, target, width, height, dpi, packets); });
        startup_window->initBase();
        startup_progress = startup_window->getProgressView() != nullptr;
        require(startup_progress, "Required native progress owner is missing");
        stages.emplace_back("native-startup-progress-created");
        initializeSecHandler();
        LLGridManager::instance();
#if LL_LINUX
        LLMIMETypes::parseMIMETypes("mime_types_linux.xml");
#else
        LLMIMETypes::parseMIMETypes("mime_types.xml");
#endif
        FSPanelLogin::show(startup_window->getWindowRectScaled(), [](S32, void*) { throw std::runtime_error("Unexpected authentication request in offline startup qualification"); }, nullptr);
        auto* holder = startup_window->getLoginPanelHolder();
        require(holder && holder->findChild<LLLineEditor>("password_edit") && holder->findChild<LLButton>("connect_btn") &&
            holder->findChild<LLMediaCtrl>("login_html"), "Required existing login controls are missing");
        startup_login = true;
        stages.emplace_back("native-login-controller-created");
        FSPanelLogin::giveFocus();
        auto* password = holder->findChild<LLLineEditor>("password_edit");
        password->setFocus(true);
        require(startup_window->handleUnicodeChar('x', MASK_NONE) && password->getText() == "x", "Native viewer Unicode route did not edit the login control");
        require(startup_window->handleTranslatedKeyDown(KEY_BACKSPACE, MASK_NONE, false) && password->getText().empty(), "Native viewer key route did not edit the login control");
        startup_window->handleFocusLost(window);
        require(!gFocusMgr.getAppHasFocus() && !gFocusMgr.getMouseCapture(), "Native viewer focus loss did not release capture");
        startup_window->handleFocus(window);
        window->show();
        inject("startup-ui");
    }
    void captureStartup(Diligent::IRenderDevice* dev, Diligent::IDeviceContext* ctx, Diligent::ITextureView* target,
        unsigned width, unsigned height, float dpi, const std::vector<VSUIRenderer::Packet>& packets)
    {
        using namespace Diligent;
        require(!packets.empty(), "Native startup traversal emitted no UI packets");
        auto desc = target->GetTexture()->GetDesc();
        require(desc.Format == TEX_FORMAT_RGBA8_UNORM || desc.Format == TEX_FORMAT_BGRA8_UNORM, "Unsupported startup readback format");
        const bool bgra = desc.Format == TEX_FORMAT_BGRA8_UNORM;
        desc.Name = "Native LLViewerWindow startup readback";
        desc.Usage = USAGE_STAGING; desc.BindFlags = BIND_NONE; desc.CPUAccessFlags = CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> readback;
        dev->CreateTexture(desc, nullptr, &readback);
        require(readback != nullptr, "Native startup readback allocation failed");
        ctx->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{}; copy.pSrcTexture = target->GetTexture(); copy.pDstTexture = readback;
        copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        ctx->CopyTexture(copy); ctx->WaitForIdle();
        MappedTextureSubresource mapped{};
        ctx->MapTextureSubresource(readback, 0, 0, MAP_READ, MAP_FLAG_DO_NOT_WAIT, nullptr, mapped);
        require(mapped.pData != nullptr, "Native startup readback mapping failed");
        const auto expected = vs_ui_expected_pixels(width, height, dpi, packets);
        std::ofstream image(evidence / ("startup-" + std::to_string(startup_readbacks) + ".ppm"), std::ios::binary);
        image << "P6\n" << width << ' ' << height << "\n255\n";
        unsigned mismatch = 0;
        for (unsigned y=0; y<height; ++y) for (unsigned x=0; x<width; ++x)
        {
            const auto* p = static_cast<const std::uint8_t*>(mapped.pData) + y*mapped.Stride + x*4;
            const std::uint8_t rgba[]{p[bgra?2:0],p[1],p[bgra?0:2],p[3]};
            image.write(reinterpret_cast<const char*>(rgba),3);
            for (unsigned c=0; c<4; ++c)
                if (std::abs(int(rgba[c])-int(expected[(std::size_t(y)*width+x)*4+c])) > 3) { ++mismatch; break; }
        }
        ctx->UnmapTextureSubresource(readback,0,0);
        require(image.good(), "Native startup readback artifact could not be saved");
        std::cerr << "NATIVE_STARTUP_PIXELS=" << mismatch << " packets=" << packets.size() << " dpi=" << dpi << '\n';
        require(mismatch == 0, "Native startup pixel oracle mismatch");
        ++startup_readbacks;
    }
    bool frameStartup()
    {
        if (!failure.empty()) return true;
        require(std::chrono::steady_clock::now()-started < std::chrono::seconds(60), "Native startup qualification timed out");
        window->gatherInput();
        require(!gGLManager.mInited, "Native startup frame entered GL initialization");
        inject("startup-frame");
        if (frames == 1)
        {
            LLSD args;
            args["MESSAGE"] = "Native viewer modal alert";
            startup_alert = LLNotifications::instance().add("GenericAlert", args, LLSD());
            require(startup_alert && !LLNotificationChannel::getInstance("AlertModal")->isEmpty(), "Native modal alert was not admitted");
            startup_alert_verified = true;
        }
        if (frames == 2)
        {
            LLNotifications::instance().cancel(startup_alert);
            startup_alert.reset();
            LLSD data;
            data["message"] = "Native viewer critical agreement message";
            auto* critical = LLFloaterReg::showInstance("message_critical", data, true);
            require(critical && critical->findChild<LLTextEditor>("tos_text") && critical->findChild<LLButton>("Continue"), "Required native critical-message controls are missing");
            startup_critical = critical->getHandle();
            startup_critical_verified = true;
        }
        if (frames == 3)
        {
            if (auto* critical = startup_critical.get()) critical->closeFloater();
            require(window->setSize(LLCoordWindow(800,600)), "Native startup resize failed");
            gSavedSettings.setF32("UIScaleFactor",1.25f);
            startup_window->reshape(800,600);
            startup_window->getProgressView()->initTextures(1,true);
            startup_window->getProgressView()->setPercent(55.f);
            startup_window->getProgressView()->setVisible(true);
            stages.emplace_back("native-startup-resize-dpi-progress");
        }
        if (frames == 6)
        {
            const auto before=startup_readbacks;
            window->minimize(); window->gatherInput();
#if LL_WINDOWS
            require(window->getMinimized(), "Native startup HWND did not minimize");
#endif
            if (window->getMinimized())
            {
                startup_window->drawNativeUI();
                require(startup_readbacks == before, "Minimized native viewer submitted UI");
            }
            window->restore();
            startup_window->getProgressView()->setVisible(false);
            stages.emplace_back("native-startup-restored");
        }
        startup_window->drawNativeUI();
        ++frames;
        return frames == 9;
    }
    void cleanupStartup()
    {
        if (startup_window)
        {
            startup_window->nativeContext()->setFrameObserver({});
            startup_window->shutdownViews();
            stages.emplace_back("native-startup-ui-released");
            startup_window->shutdownGL();
            stages.emplace_back("native-startup-graphics-released");
            startup_window.reset();
            gViewerWindow=nullptr;window=nullptr;
            delete gKeyboard;gKeyboard=nullptr;
            stages.emplace_back("native-startup-window-released");
        }
        LLPluginProcessParent::shutdown();
        clearSecHandler();
        if (startup_http)
        {
            LLAppViewer::instance()->getAppCoreHttp().cleanup();
            startup_http = false;
        }
        cleaned=true;
        require(diagnostic_errors == 0, "Native startup validation contains errors");
        inject("startup-cleanup");
    }
    void writeStartup()
    {
        boost::json::object record{{"schema",1},{"mode","viewer-native-startup"},{"device",identity},{"stages",stages},
            {"login_controls_verified",startup_login},{"progress_owner_verified",startup_progress},
            {"modal_alert_verified",startup_alert_verified},{"critical_dialog_verified",startup_critical_verified},
            {"presented_frames",frames},{"readbacks",startup_readbacks},{"validation_errors",diagnostic_errors.load()},
            {"normal_session_admitted",false},{"live_browser_qualified",false},{"authentication_qualified",false},
            {"shutdown_complete",cleaned},{"passed",failure.empty() && cleaned && startup_readbacks == 9},{"failure",failure}};
        std::ofstream out(evidence / "viewer-startup.json");out << boost::json::serialize(record) << '\n';
        require(out.good(), "Cannot write native startup evidence");
    }
    void write()
    {
        if (startup_mode) { writeStartup(); return; }
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
            {"ui_xui_verified",ui_xui_verified},{"ui_input_verified",ui_input_verified},{"ui_focus_verified",ui_focus_verified},
            {"ui_mouse_verified",ui_mouse_verified},{"ui_scroll_verified",ui_scroll_verified},
            {"ui_skin",ui_skin_selection[0]},{"ui_theme",ui_skin_selection[1]},{"ui_language",ui_skin_selection[2]},
            {"ui_facade_verified",ui_facade_verified},{"ui_atlas_verified",ui_atlas_verified},
            {"ui_font_producer_verified",ui_font_producer_verified},
            {"ui_admission_verified",ui_admission_verified},
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
