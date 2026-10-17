// Standalone CI tool, deliberately outside viewer runtime sources.
#include <DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <functional>
#include "vsworldrenderer.h"
#include "vsuipixeloracle.h"
#if PLATFORM_WIN32
#include <windows.h>
#include <tlhelp32.h>
#else
#include <SDL.h>
#include <SDL_syswm.h>
#include <X11/Xlib-xcb.h>
#include <dlfcn.h>
#endif

using namespace Diligent;
static std::atomic<unsigned> errors{0};
static std::atomic<unsigned> validationErrors{0};

static void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

static void DILIGENT_CALL_TYPE diagnostic(DEBUG_MESSAGE_SEVERITY severity, const Char* message,
                                         const Char*, const Char*, int)
{
    std::cerr << "DILIGENT " << severity << ": " << message << '\n';
    if (severity >= DEBUG_MESSAGE_SEVERITY_ERROR) ++errors;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL validation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user)
{
    std::cerr << "VALIDATION " << data->pMessageIdName << ": " << data->pMessage << '\n';
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ++validationErrors;
    // The core probe tests validation, not undefined driver behavior. Ask the
    // layer to abort its deliberately invalid call before dispatch to the ICD.
    return user && *static_cast<const bool*>(user) &&
           (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) ? VK_TRUE : VK_FALSE;
}

// Independent loader/layer preflight and an isolated intentional-invalid-use probe.
// Actual rendering, resource ownership and presentation below use DiligentCore.
static uint32_t selectDevice(const std::vector<VkPhysicalDeviceProperties>& devices,
                             const std::string& expected, const std::string& name)
{
    const uint32_t vendor = expected == "amd" ? 0x1002 : expected == "nvidia" ? 0x10de : 0;
    uint32_t selected = static_cast<uint32_t>(devices.size());
    int best = -1;
    unsigned matches = 0;
    for (uint32_t i = 0; i < devices.size(); ++i)
    {
        const auto& info = devices[i];
        const bool gpu = info.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU || info.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU;
        const bool compatible = expected == "auto" || (vendor ? gpu && info.vendorID == vendor :
            info.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU && std::string(info.deviceName).find(expected) != std::string::npos);
        if (!compatible || (!name.empty() && std::string(info.deviceName).find(name) == std::string::npos)) continue;
        ++matches;
        const int priority = info.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 3 :
            info.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2 : info.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 1 : 0;
        if (priority > best) { selected = i; best = priority; }
    }
    require(matches > 0, "No matching Vulkan device available");
    require(expected == "auto" || matches == 1, "Multiple matching devices; specify a unique device name");
    return selected;
}

static void selectionTests()
{
    std::vector<VkPhysicalDeviceProperties> devices(3);
    devices[0].deviceType = VK_PHYSICAL_DEVICE_TYPE_CPU;
    std::strcpy(devices[0].deviceName, "SwiftShader");
    devices[1].deviceType = VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU; devices[1].vendorID = 0x1002;
    std::strcpy(devices[1].deviceName, "AMD test device");
    devices[2].deviceType = VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU; devices[2].vendorID = 0x10de;
    std::strcpy(devices[2].deviceName, "NVIDIA test device");
    require(selectDevice(devices, "auto", "") == 2, "Automatic selection must prefer available hardware");
    require(selectDevice(devices, "auto", "AMD") == 1, "Device-name selection ignored");
    require(selectDevice(devices, "amd", "") == 1, "Explicit vendor selection ignored");
    require(selectDevice(devices, "SwiftShader", "") == 0, "Software isolation ignored");
    devices.resize(1);
    require(selectDevice(devices, "auto", "") == 0, "Software-only Vulkan discovery failed");
    bool rejected = false;
    try { selectDevice(devices, "nvidia", ""); } catch (const std::runtime_error&) { rejected = true; }
    require(rejected, "Missing vendor must not silently use another driver");
    std::cout << "PASS device-selection\n";
}

static VkPhysicalDeviceProperties preflight(const std::string& expected, const std::string& mode,
                                           const std::string& name)
{
    uint32_t count = 0;
    require(vkEnumerateInstanceLayerProperties(&count, nullptr) == VK_SUCCESS, "Layer enumeration failed");
    std::vector<VkLayerProperties> layers(count);
    require(vkEnumerateInstanceLayerProperties(&count, layers.data()) == VK_SUCCESS, "Layer enumeration failed");
    bool found = false;
    for (auto& layer : layers) found |= std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0;
    require(found, "Required validation layer missing");
    const char* layer = "VK_LAYER_KHRONOS_validation";
    const char* extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
    bool abortInvalidCall = mode == "invalid";
    debug.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debug.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debug.pfnUserCallback = validation;
    debug.pUserData = &abortInvalidCall;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.apiVersion = VK_API_VERSION_1_1;
    VkInstanceCreateInfo ci{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ci.pNext = &debug;
    ci.pApplicationInfo = &app;
    ci.enabledLayerCount = 1; ci.ppEnabledLayerNames = &layer;
    ci.enabledExtensionCount = 1; ci.ppEnabledExtensionNames = &extension;
    VkInstance instance{};
    require(vkCreateInstance(&ci, nullptr, &instance) == VK_SUCCESS, "Vulkan instance failed");
    auto createDebug = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
    auto destroyDebug = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
    require(createDebug && destroyDebug, "Debug-utils functions missing");
    VkDebugUtilsMessengerEXT messenger{};
    require(createDebug(instance, &debug, nullptr, &messenger) == VK_SUCCESS, "Debug messenger failed");
    require(vkEnumeratePhysicalDevices(instance, &count, nullptr) == VK_SUCCESS && count > 0, "No Vulkan devices available");
    std::vector<VkPhysicalDevice> devices(count);
    require(vkEnumeratePhysicalDevices(instance, &count, devices.data()) == VK_SUCCESS, "Device enumeration failed");
    std::vector<VkPhysicalDeviceProperties> available(count);
    for (uint32_t i = 0; i < count; ++i) vkGetPhysicalDeviceProperties(devices[i], &available[i]);
    const auto index = selectDevice(available, expected, name);
    const auto physical = devices[index];
    const auto properties = available[index];
    std::cout << "ICD_DEVICE=" << properties.deviceName << " API=" << properties.apiVersion << " DRIVER=" << properties.driverVersion
              << " VENDOR=" << properties.vendorID << " DEVICE=" << properties.deviceID << " TYPE=" << properties.deviceType << '\n';
    if (mode == "invalid" || mode == "invalid-sync")
    {
        uint32_t queues = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queues, nullptr);
        std::vector<VkQueueFamilyProperties> families(queues);
        vkGetPhysicalDeviceQueueFamilyProperties(physical, &queues, families.data());
        uint32_t family = 0;
        while (family < queues && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
        require(family < queues, "Graphics queue missing");
        const float priority = 1.f;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family; queue.queueCount = 1; queue.pQueuePriorities = &priority;
        VkDeviceCreateInfo deviceCI{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
        deviceCI.queueCreateInfoCount = 1; deviceCI.pQueueCreateInfos = &queue;
        VkDevice device{};
        require(vkCreateDevice(physical, &deviceCI, nullptr, &device) == VK_SUCCESS, "Negative-test device failed");
        VkBufferCreateInfo bufferCI{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bufferCI.size = mode == "invalid" ? 0 : 16; // size-00912 probe
        bufferCI.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buffer{};
        VkResult bufferResult = vkCreateBuffer(device, &bufferCI, nullptr, &buffer);
        if (mode == "invalid")
            require(bufferResult == VK_ERROR_VALIDATION_FAILED_EXT && buffer == VK_NULL_HANDLE,
                    "Validation did not abort the deliberately invalid buffer call");
        VkDeviceMemory memory{};
        VkCommandPool pool{};
        if (mode == "invalid-sync")
        {
            require(buffer != VK_NULL_HANDLE, "Synchronization probe buffer failed");
            VkMemoryRequirements requirements{}; vkGetBufferMemoryRequirements(device, buffer, &requirements);
            uint32_t memoryType = 0; while (!(requirements.memoryTypeBits & (1u << memoryType))) ++memoryType;
            VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memoryType;
            require(vkAllocateMemory(device, &allocation, nullptr, &memory) == VK_SUCCESS, "Probe allocation failed");
            require(vkBindBufferMemory(device, buffer, memory, 0) == VK_SUCCESS, "Probe memory bind failed");
            VkCommandPoolCreateInfo poolCI{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; poolCI.queueFamilyIndex = family;
            require(vkCreateCommandPool(device, &poolCI, nullptr, &pool) == VK_SUCCESS, "Probe pool failed");
            VkCommandBufferAllocateInfo commandCI{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            commandCI.commandPool = pool; commandCI.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; commandCI.commandBufferCount = 1;
            VkCommandBuffer command{};
            require(vkAllocateCommandBuffers(device, &commandCI, &command) == VK_SUCCESS, "Probe command allocation failed");
            VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
            require(vkBeginCommandBuffer(command, &begin) == VK_SUCCESS, "Probe begin failed");
            vkCmdFillBuffer(command, buffer, 0, VK_WHOLE_SIZE, 0);
            vkCmdFillBuffer(command, buffer, 0, VK_WHOLE_SIZE, 1); // Deliberate WAW without barrier.
            require(vkEndCommandBuffer(command) == VK_SUCCESS, "Probe end failed");
            vkDestroyCommandPool(device, pool, nullptr);
        }
        if (buffer) vkDestroyBuffer(device, buffer, nullptr);
        if (memory) vkFreeMemory(device, memory, nullptr);
        vkDestroyDevice(device, nullptr);
    }
    destroyDebug(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);
    require(validationErrors == 0, "Validation reported an error");
    return properties;
}

struct TestWindow
{
    NativeWindow native{};
#if PLATFORM_WIN32
    HWND handle{};
    TestWindow()
    {
        WNDCLASSA wc{}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandle(nullptr); wc.lpszClassName = "SoftwareVulkanTest";
        require(RegisterClassA(&wc) != 0, "RegisterClass failed");
        RECT bounds{0,0,128,128};
        require(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE) != 0, "Client-size adjustment failed");
        handle = CreateWindowA(wc.lpszClassName, "Software Vulkan test", WS_OVERLAPPEDWINDOW,
                              0, 0, bounds.right-bounds.left, bounds.bottom-bounds.top, nullptr, nullptr, wc.hInstance, nullptr);
        require(handle != nullptr, "Native HWND creation failed");
        native.hWnd = handle; ShowWindow(handle, SW_SHOW);
    }
    void pump() { MSG msg{}; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); } }
    void size(int w, int h)
    {
        RECT bounds{0,0,w,h};
        require(AdjustWindowRect(&bounds, WS_OVERLAPPEDWINDOW, FALSE) != 0, "Client-size adjustment failed");
        require(SetWindowPos(handle, nullptr, 0, 0, bounds.right-bounds.left, bounds.bottom-bounds.top,
                             SWP_NOMOVE | SWP_NOZORDER) != 0, "Native window resize failed");
        pump();
    }
    bool minimized() const { return IsIconic(handle) != 0; }
    std::array<int,2> extent() const
    {
        RECT bounds{}; require(GetClientRect(handle, &bounds) != 0, "Client-size query failed");
        return {bounds.right-bounds.left, bounds.bottom-bounds.top};
    }
    void minimize() { ShowWindow(handle, SW_MINIMIZE); pump(); }
    void restore() { ShowWindow(handle, SW_RESTORE); pump(); }
    void close()
    {
        require(DestroyWindow(handle) != 0, "Native window destruction failed"); handle = nullptr;
        require(UnregisterClassA("SoftwareVulkanTest", GetModuleHandle(nullptr)) != 0, "Native window class cleanup failed");
    }
    ~TestWindow() { if (handle) { DestroyWindow(handle); UnregisterClassA("SoftwareVulkanTest", GetModuleHandle(nullptr)); } }
#else
    SDL_Window* handle{};
    TestWindow()
    {
        require(SDL_Init(SDL_INIT_VIDEO) == 0, "SDL video init failed");
        require(std::string(SDL_GetCurrentVideoDriver()) == "x11", "Expected SDL2 X11");
        handle = SDL_CreateWindow("Software Vulkan test", 0, 0, 128, 128, SDL_WINDOW_RESIZABLE);
        require(handle != nullptr, "SDL native window failed");
        SDL_SysWMinfo wm{}; SDL_VERSION(&wm.version);
        require(SDL_GetWindowWMInfo(handle, &wm) == SDL_TRUE && wm.subsystem == SDL_SYSWM_X11, "Native X11 handles unavailable");
        native.pDisplay = wm.info.x11.display; native.WindowId = static_cast<Uint32>(wm.info.x11.window);
        native.pXCBConnection = XGetXCBConnection(wm.info.x11.display);
        require(native.pXCBConnection != nullptr, "X11/XCB connection unavailable");
    }
    void pump() { SDL_Event event; while (SDL_PollEvent(&event)) {} }
    void size(int w, int h) { SDL_SetWindowSize(handle, w, h); pump(); }
    bool minimized() const { return (SDL_GetWindowFlags(handle) & SDL_WINDOW_MINIMIZED) != 0; }
    std::array<int,2> extent() const
    {
        int w=0, h=0; SDL_GetWindowSize(handle, &w, &h); return {w,h};
    }
    void minimize() { SDL_MinimizeWindow(handle); pump(); }
    void restore() { SDL_RestoreWindow(handle); pump(); }
    void close() { SDL_DestroyWindow(handle); handle = nullptr; SDL_Quit(); }
    ~TestWindow() { if (handle) close(); }
#endif
};

struct PresentationEvidence
{
    unsigned presented = 0;
    unsigned zeroSkipped = 0;
    unsigned minimizedSkipped = 0;
    bool minimizeObserved = false;
    std::vector<std::string> stages;

    void write() const
    {
        std::ofstream out("presentation.json");
        out << "{\n  \"window_api\": \"" <<
#if PLATFORM_WIN32
            "Win32"
#else
            "SDL2/X11"
#endif
            << "\",\n  \"presented_frames\": " << presented
            << ",\n  \"zero_extent_skips\": " << zeroSkipped
            << ",\n  \"minimized_skips\": " << minimizedSkipped
            << ",\n  \"native_minimize_observed\": " << (minimizeObserved ? "true" : "false")
            << ",\n  \"shutdown_complete\": true,\n  \"stages\": [";
        for (size_t i=0; i<stages.size(); ++i) out << (i ? ", " : "") << '"' << stages[i] << '"';
        out << "]\n}\n"; out.flush();
        require(out.good(), "Presentation artifact write failed");
    }
};

static PresentationEvidence presentation(IEngineFactoryVk* factory, IRenderDevice* device, IDeviceContext* context)
{
    PresentationEvidence evidence;
    TestWindow window;
    require(window.extent() == std::array<int,2>{128,128}, "Initial native client extent differs");
    RefCntAutoPtr<ISwapChain> swapchain;
    SwapChainDesc desc{}; desc.Width = desc.Height = 128; desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
    factory->CreateSwapChainVk(device, context, desc, window.native, &swapchain);
    require(swapchain != nullptr, "Native swapchain failed");
    require(swapchain->GetDesc().Width == 128 && swapchain->GetDesc().Height == 128, "Initial swapchain extent differs");
    evidence.stages.push_back("create-128x128");

    auto frame = [&](unsigned width, unsigned height, bool minimized)
    {
        // Zero/minimized client notifications suspend WSI operations. Vulkan
        // forbids a zero image extent; do not issue Resize/Draw/Present for it.
        if (width == 0 || height == 0 || minimized)
        {
            if (width == 0 || height == 0) ++evidence.zeroSkipped;
            else ++evidence.minimizedSkipped;
            return;
        }
        auto* rtv = swapchain->GetCurrentBackBufferRTV(); require(rtv != nullptr, "Backbuffer missing");
        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[]{0.1f,0.2f,0.3f,1}; context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        swapchain->Present(0); ++evidence.presented; window.pump();
    };
    for (unsigned i=0; i<3; ++i) frame(128,128,false);
    evidence.stages.push_back("present-initial");
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
    window.size(160,96); swapchain->Resize(160,96);
    require(window.extent() == std::array<int,2>{160,96}, "Resized native client extent differs");
    require(swapchain->GetDesc().Width == 160 && swapchain->GetDesc().Height == 96, "Resized swapchain extent differs");
    for (unsigned i=0; i<3; ++i) frame(160,96,false);
    evidence.stages.push_back("resize-present-160x96");
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
    const auto beforeSuspension = evidence.presented;
    frame(0,0,false); evidence.stages.push_back("zero-0x0-suspended");
    frame(0,96,false); evidence.stages.push_back("zero-0x96-suspended");
    frame(160,0,false); evidence.stages.push_back("zero-160x0-suspended");
    window.minimize(); window.pump(); evidence.minimizeObserved = window.minimized();
#if PLATFORM_WIN32
    require(evidence.minimizeObserved, "Native HWND did not enter minimized state");
#endif
    frame(160,96,true); frame(160,96,true);
    require(evidence.presented == beforeSuspension, "Presented while zero-sized or minimized");
    require(swapchain->GetDesc().Width == 160 && swapchain->GetDesc().Height == 96, "Suspension resized the swapchain");
    evidence.stages.push_back("minimized-suspended");
    window.restore(); window.size(128,128); swapchain->Resize(128,128);
    require(!window.minimized(), "Native window did not leave minimized state");
    require(window.extent() == std::array<int,2>{128,128}, "Restored native client extent differs");
    require(swapchain->GetDesc().Width == 128 && swapchain->GetDesc().Height == 128, "Restored swapchain extent differs");
    for (unsigned i=0; i<3; ++i) frame(128,128,false);
    evidence.stages.push_back("restore-present-128x128");
    require(evidence.presented == 9 && evidence.zeroSkipped == 3 && evidence.minimizedSkipped == 2, "Incomplete presentation sequence");
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
    context->WaitForIdle(); swapchain.Release();
    evidence.stages.push_back("swapchain-released");
    window.close(); evidence.stages.push_back("window-destroyed");
    return evidence;
}

using Pixel = std::array<unsigned char, 4>;
using Image = std::array<Pixel, 4>;
static const Image first{{Pixel{255,0,0,128}, Pixel{0,255,0,128}, Pixel{0,0,255,128}, Pixel{255,255,255,128}}};
static const Image second{{Pixel{0,255,255,128}, Pixel{255,0,255,128}, Pixel{255,255,0,128}, Pixel{0,0,0,128}}};
static const Pixel background{16,32,48,255};

static RefCntAutoPtr<IPipelineState> pipeline(IRenderDevice* device, bool flipOrientation)
{
    // Diligent's Vulkan viewport maps +NDC Y to the top row. Interpolated
    // texture V must therefore be zero at +Y; this exercises that convention.
    const std::string vs = std::string{R"(
layout(location=0) out vec2 texUV;
void main() { vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2); texUV=vec2(p.x,)"} +
        (flipOrientation ? "p.y" : "1.0-p.y") + R"(); gl_Position=vec4(p*2.0-1.0,0,1); }
)";
    const char* fs = R"(
layout(location=0) in vec2 texUV;
layout(location=0) out vec4 color;
uniform sampler2D g_Texture;
void main() { color=texture(g_Texture,texUV); }
)";
    ShaderCreateInfo shaderCI{}; shaderCI.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL;
    shaderCI.GLSLVersion = {4,5}; // Diligent inserts the single #version directive.
    shaderCI.Desc.UseCombinedTextureSamplers = true;
    shaderCI.EntryPoint = "main";
    RefCntAutoPtr<IShader> vertex, fragment;
    shaderCI.Desc.Name = "Test vertex"; shaderCI.Desc.ShaderType = SHADER_TYPE_VERTEX; shaderCI.Source = vs.c_str();
    device->CreateShader(shaderCI, &vertex); require(vertex != nullptr, "Vertex shader failed");
    shaderCI.Desc.Name = "Test fragment"; shaderCI.Desc.ShaderType = SHADER_TYPE_PIXEL; shaderCI.Source = fs;
    device->CreateShader(shaderCI, &fragment); require(fragment != nullptr, "Fragment shader failed");
    GraphicsPipelineStateCreateInfo ci{};
    ci.PSODesc.Name = "Textured alpha/scissor pipeline";
    ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
    ci.pVS = vertex; ci.pPS = fragment;
    ci.GraphicsPipeline.NumRenderTargets = 1;
    ci.GraphicsPipeline.RTVFormats[0] = TEX_FORMAT_RGBA8_UNORM;
    ci.GraphicsPipeline.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    ci.GraphicsPipeline.RasterizerDesc.CullMode = CULL_MODE_NONE;
    ci.GraphicsPipeline.RasterizerDesc.ScissorEnable = true;
    ci.GraphicsPipeline.DepthStencilDesc.DepthEnable = false;
    auto& blend = ci.GraphicsPipeline.BlendDesc.RenderTargets[0];
    blend.BlendEnable = true; blend.SrcBlend = BLEND_FACTOR_SRC_ALPHA; blend.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
    blend.SrcBlendAlpha = BLEND_FACTOR_ONE; blend.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
    ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
    SamplerDesc sampler{}; sampler.MinFilter = sampler.MagFilter = sampler.MipFilter = FILTER_TYPE_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
    ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL, "g_Texture", sampler};
    ci.PSODesc.ResourceLayout.NumImmutableSamplers = 1; ci.PSODesc.ResourceLayout.ImmutableSamplers = &immutable;
    RefCntAutoPtr<IPipelineState> result; device->CreateGraphicsPipelineState(ci, &result);
    require(result != nullptr, "Pipeline failed"); return result;
}

static RefCntAutoPtr<ITexture> texture(IRenderDevice* device, bool staging, bool target)
{
    TextureDesc desc{}; desc.Name = staging ? "Readback" : target ? "RGBA target" : "Uploaded UI texture";
    desc.Type = RESOURCE_DIM_TEX_2D; desc.Width = desc.Height = target || staging ? 8 : 2;
    desc.Format = TEX_FORMAT_RGBA8_UNORM;
    desc.Usage = staging ? USAGE_STAGING : USAGE_DEFAULT;
    desc.BindFlags = staging ? BIND_NONE : target ? BIND_RENDER_TARGET : BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = staging ? CPU_ACCESS_READ : CPU_ACCESS_NONE;
    RefCntAutoPtr<ITexture> result; device->CreateTexture(desc, nullptr, &result);
    require(result != nullptr, "Texture creation failed"); return result;
}

static void draw(IRenderDevice* device, IDeviceContext* context, ITexture* target, ITexture* readback, const Image& image, bool flipOrientation = false)
{
    auto pso = pipeline(device, flipOrientation);
    auto uploaded = texture(device, false, false);
    TextureSubResData data{}; data.pData = image.data(); data.Stride = 2 * sizeof(Pixel);
    context->UpdateTexture(uploaded, 0, 0, Box{0,2,0,2}, data, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    RefCntAutoPtr<IShaderResourceBinding> binding; pso->CreateShaderResourceBinding(&binding, true);
    auto* variable = binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
    require(variable != nullptr, "Texture binding missing"); variable->Set(uploaded->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    auto* rtv = target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    const float clear[]{16.f/255,32.f/255,48.f/255,1}; context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->SetPipelineState(pso);
    Viewport viewport{0.f,0.f,8.f,8.f}; context->SetViewports(1, &viewport, 8, 8);
    // Asymmetric bounds expose scissor origin and axis conversion mistakes.
    Rect scissor{1,2,6,7}; context->SetScissorRects(1, &scissor, 8, 8);
    context->CommitShaderResources(binding, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs attributes{3, DRAW_FLAG_VERIFY_ALL}; context->Draw(attributes);
    context->SetRenderTargets(0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE);
    CopyTextureAttribs copy{}; copy.pSrcTexture = target; copy.pDstTexture = readback;
    copy.SrcTextureTransitionMode = copy.DstTextureTransitionMode = RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);
    context->Flush();
    context->FinishFrame();
    // Drop caller-owned bindings, PSO and upload before waiting. Diligent must
    // retain submitted resources; the following generation replaces all three.
}

static void compare(IDeviceContext* context, ITexture* texture, const Image& image, const char* file, bool corrupt)
{
    MappedTextureSubresource mapped{};
    context->MapTextureSubresource(texture, 0, 0, MAP_READ, MAP_FLAG_NONE, nullptr, mapped);
    require(mapped.pData != nullptr, "Readback mapping failed");
    std::ofstream output(file, std::ios::binary); output << "P6\n8 8\n255\n";
    unsigned mismatches = 0;
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x)
    {
        const auto* pixel = static_cast<const unsigned char*>(mapped.pData) + y * mapped.Stride + x * 4;
        output.write(reinterpret_cast<const char*>(pixel), 3);
        Pixel expected = background;
        if (x >= 1 && x < 6 && y >= 2 && y < 7)
        {
            auto& source = image[(y / 4) * 2 + x / 4];
            for (unsigned c = 0; c < 3; ++c)
                expected[c] = static_cast<unsigned char>(std::lround((source[c]*128.0 + background[c]*127.0) / 255.0));
        }
        if (corrupt && x == 0 && y == 0) expected[0] = 255;
        for (unsigned c = 0; c < 4; ++c)
            if (std::abs(int(pixel[c]) - int(expected[c])) > 1) ++mismatches;
    }
    context->UnmapTextureSubresource(texture, 0, 0);
    require(output.good(), "Readback artifact write failed");
    require(mismatches == 0, "Pixel oracle mismatch (one UNORM rounding unit permitted)");
}

// Execute the production native-world renderer; no substitute test renderer.
static void worldContract(IRenderDevice* device, IDeviceContext* context, ITexture* target, ITexture* readback)
{
    VSWorldRenderer renderer(device,context);
    auto quad=[&](float z) {
        std::vector<VSWorldRenderer::Vertex> vertices{{-1,1,z,0,0,1,0,0},{1,1,z,0,0,1,1,0},
            {-1,-1,z,0,0,1,0,1},{1,-1,z,0,0,1,1,1}};
        return renderer.upload(vertices,{0,2,1,1,2,3});
    };
    auto nearGeometry=quad(-.5f), farGeometry=quad(.5f);
    VSWorldRenderer::View view;view.projection={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    VSWorldRenderer::Packet nearPacket;nearPacket.mesh=nearGeometry;nearPacket.fullbright=true;nearPacket.color={1,0,0,1};
    VSWorldRenderer::Packet farPacket;farPacket.mesh=farGeometry;farPacket.fullbright=true;farPacket.color={0,0,1,1};
    auto* rtv=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    auto capture=[&](const char* file,const std::function<Pixel(unsigned,unsigned)>& oracle) {
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=readback;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);context->Flush();context->WaitForIdle();
        MappedTextureSubresource mapped{};context->MapTextureSubresource(readback,0,0,MAP_READ,MAP_FLAG_NONE,nullptr,mapped);
        require(mapped.pData!=nullptr,"World readback failed");
        std::ofstream artifact(file,std::ios::binary);artifact<<"P6\n8 8\n255\n";
        unsigned mismatches=0;
        for (unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x) {
            const auto* pixel=static_cast<const unsigned char*>(mapped.pData)+y*mapped.Stride+x*4;
            artifact.write(reinterpret_cast<const char*>(pixel),3);
            const auto expected=oracle(x,y);
            for(unsigned c=0;c<4;++c) if(std::abs(int(pixel[c])-int(expected[c]))>1) ++mismatches;
        }
        context->UnmapTextureSubresource(readback,0,0);
        require(artifact.good(),"World artifact write failed");require(!mismatches,"World pixel oracle mismatch");
    };
    renderer.draw(rtv,8,8,view,{nearPacket,farPacket});capture("first.ppm",[](unsigned,unsigned){return Pixel{255,0,0,255};});
    // Replace caller-owned geometry with an alpha generation; old submitted
    // resources must remain valid until the renderer's completion fence retires.
    VSWorldRenderer::Packet alpha;alpha.mesh=quad(-.75f);alpha.fullbright=true;
    alpha.alpha=true;alpha.color={0,1,0,128.f/255};alpha.distanceSquared=1;
    renderer.draw(rtv,8,8,view,{nearPacket,farPacket,alpha});capture("replacement.ppm",[](unsigned,unsigned){return Pixel{127,128,0,255};});
    VSUIRenderer images(device,context);
    const std::vector<std::uint8_t> texels{255,0,0,255,0,255,0,255,0,0,255,255,255,255,0,255};
    VSWorldRenderer::Packet textured; textured.mesh=quad(0);textured.fullbright=true;
    textured.image=images.upload(2,2,texels);
    // Sample texel centers: repeat + linear filtering then cannot blur the oracle.
    // A second generation has each corner fixed to one texel center.
    for (unsigned corner=0;corner<4;++corner) {
        const float u=(corner&1)?.75f:.25f,v=(corner&2)?.75f:.25f;
        std::vector<VSWorldRenderer::Vertex> verts{{-1,1,0,0,0,1,u,v},{1,1,0,0,0,1,u,v},
            {-1,-1,0,0,0,1,u,v},{1,-1,0,0,0,1,u,v}};
        textured.mesh=renderer.upload(verts,{0,2,1,1,2,3});
        renderer.draw(rtv,8,8,view,{textured});
        textured.mesh.reset(); // Submission retains this exact geometry generation.
        const Pixel expected{texels[corner*4],texels[corner*4+1],texels[corner*4+2],255};
        const auto path="texture-"+std::to_string(corner)+".ppm";
        capture(path.c_str(),[expected](unsigned,unsigned){return expected;});
    }
    auto clipped=quad(-2.f);farPacket.mesh=clipped;farPacket.color={0,0,1,1};
    renderer.draw(rtv,8,8,view,{nearPacket,farPacket});
    capture("near-clip.ppm",[](unsigned,unsigned){return Pixel{255,0,0,255};});
    // Production material/UV/color-space charts, independently specified pixels.
    VSWorldRenderer::Packet materialPacket=textured;
    auto surface=std::make_shared<VSMaterial::State>();
    materialPacket.surface=surface;materialPacket.doubleSided=true;
    materialPacket.image=images.upload(1,1,{128,128,128,0});
    materialPacket.mesh=quad(0);materialPacket.fullbright=true;
    renderer.draw(rtv,8,8,view,{materialPacket});
    capture("material-opaque.ppm",[](unsigned,unsigned){return Pixel{128,128,128,255};});
    surface=std::make_shared<VSMaterial::State>(*surface);surface->pbr=true;
    materialPacket.surface=surface;materialPacket.color={.5f,.5f,.5f,1};
    renderer.draw(rtv,8,8,view,{materialPacket});
    capture("material-linear-factor.ppm",[](unsigned,unsigned){return Pixel{92,92,92,255};});
    // Emissive is a linear factor times an sRGB texture, independent of lighting.
    surface=std::make_shared<VSMaterial::State>(*surface);surface->emissive={1,0,0};
    materialPacket.surface=surface;materialPacket.color={0,0,0,1};
    materialPacket.channels[2]=images.upload(1,1,{128,0,0,255});
    renderer.draw(rtv,8,8,view,{materialPacket});
    capture("material-emissive.ppm",[](unsigned,unsigned){return Pixel{128,0,0,255};});
    surface=std::make_shared<VSMaterial::State>();surface->uv[0].offset={.5f,0};
    materialPacket.surface=surface;materialPacket.channels={};materialPacket.color={1,1,1,1};
    materialPacket.image=images.upload(2,2,texels);
    materialPacket.mesh=renderer.upload({{-1,1,0,0,0,1,.25f,.25f},{1,1,0,0,0,1,.25f,.25f},
        {-1,-1,0,0,0,1,.25f,.25f},{1,-1,0,0,0,1,.25f,.25f}},{0,2,1,1,2,3});
    renderer.draw(rtv,8,8,view,{materialPacket});
    capture("material-transform.ppm",[](unsigned,unsigned){return Pixel{0,255,0,255};});
    VSWorldRenderer::Packet sky;sky.background=true;sky.fullbright=true;
    sky.mesh=renderer.upload({{-1,-1,0,0,0,1,0,0},{1,-1,0,0,0,1,1,0},
        {1,1,0,0,0,1,1,1},{-1,1,0,0,0,1,0,1}},{0,1,2,0,2,3});
    renderer.draw(rtv,8,8,view,{sky});
    capture("sky.ppm",[](unsigned,unsigned y){
        const float t=1-(float(y)+.5f)/8;
        return Pixel{uint8_t(std::lround((.65f-.57f*t)*255)),uint8_t(std::lround((.8f-.58f*t)*255)),uint8_t(std::lround((.95f-.47f*t)*255)),255};
    });
    renderer.draw(rtv,8,8,view,{nearPacket,alpha,sky});
    capture("water-depth.ppm",[](unsigned,unsigned){return Pixel{127,128,0,255};});
    // Completed submissions must recycle their per-draw resources, while new
    // constants still reach the GPU. This exercises actual fence retirement,
    // opaque/alpha/background pools and mutable-descriptor replacement.
    nearPacket.color={0,0,1,1};
    renderer.draw(rtv,8,8,view,{nearPacket,alpha,sky});
    const auto reused=renderer.statistics();
    require(reused.draws==3 && !reused.constantBuffersCreated && !reused.bindingsCreated,
            "Completed world draw resources were not reused");
    capture("resource-reuse.ppm",[](unsigned,unsigned){return Pixel{0,128,127,255};});
    std::cout<<"WORLD_REUSE_DRAWS="<<reused.draws<<"\nWORLD_REUSE_CONSTANT_ALLOCATIONS="<<reused.constantBuffersCreated
             <<"\nWORLD_REUSE_BINDING_ALLOCATIONS="<<reused.bindingsCreated<<"\nWORLD_SUBMISSION_CPU_MS="<<reused.cpuMilliseconds<<'\n';
    auto white=images.upload(1,1,{255,255,255,255});
    VSUIRenderer::Packet a;a.image=white;a.bounds={0,0,4,8};a.clip={0,0,8,8};a.color={1,0,0,1};
    auto b=a;b.bounds={4,0,8,8};
    auto overlay=a;overlay.bounds={0,0,8,8};overlay.clip={2,0,6,8};overlay.color={0,1,0,128.f/255};
    auto blue=overlay;blue.image=images.replace(white,0,0,1,1,{0,0,255,255});
    blue.clip={4,2,8,6};blue.color={1,1,1,1};blue.blend=VSUIRenderer::Blend::Replace;
    images.draw(rtv,8,8,1,{a,b,overlay,blue});
    require(images.statistics().packets==4 && images.statistics().draws==3,"UI adjacency batching broke resource/scissor boundaries");
    capture("ui-batch.ppm",[](unsigned x,unsigned y){
        if (x>=4 && y>=2 && y<6) return Pixel{0,0,255,255};
        return x>=2 && x<6 ? Pixel{127,128,0,255} : Pixel{255,0,0,255};
    });
    std::cout<<"UI_BATCH_PACKETS="<<images.statistics().packets<<"\nUI_BATCH_DRAWS="<<images.statistics().draws<<'\n';
    // Font pages keep their GPU identity while local glyph bytes change. Queue
    // reads between updates without waiting: synchronization validation must
    // cover read/transfer/read ordering on the same atlas, not replacement copies.
    const auto           atlasBytesBefore = images.statistics().residentImageBytes;
    auto                 atlas            = images.createFontAtlas(2, 2, texels);
    auto*                atlasTexture     = VSUIRenderer::imageView(atlas)->GetTexture();
    const auto           atlasGeneration  = VSUIRenderer::generation(atlas);
    VSUIRenderer::Packet glyph;
    glyph.image  = atlas;
    glyph.bounds = { 0, 0, 8, 8 };
    glyph.clip   = { 0, 0, 8, 8 };
    glyph.blend  = VSUIRenderer::Blend::Replace;
    images.draw(rtv, 8, 8, 1, { glyph });
    for (unsigned update = 0; update < 64; ++update)
    {
        auto pixels = texels;
        pixels[0]   = static_cast<std::uint8_t>(update);
        images.updateFontAtlas(atlas, pixels);
        images.draw(rtv, 8, 8, 1, { glyph });
        require(VSUIRenderer::imageView(atlas)->GetTexture() == atlasTexture && VSUIRenderer::generation(atlas) == atlasGeneration,
                "Glyph update replaced the font atlas");
        require(images.statistics().residentImageBytes == atlasBytesBefore + texels.size() * 2,
                "Glyph update allocated another atlas reservation");
    }
    images.updateFontAtlas(atlas, texels);
    images.draw(rtv, 8, 8, 1, { glyph });
    capture("font-atlas-update.ppm",
            [&](unsigned x, unsigned y)
            {
                const unsigned corner = (x >= 4 ? 1 : 0) + (y >= 4 ? 2 : 0);
                return Pixel{ texels[corner * 4], texels[corner * 4 + 1], texels[corner * 4 + 2], 255 };
            });
    require(VSUIRenderer::imagePixels(atlas) == texels, "Font oracle snapshot differs from local glyph bytes");
    glyph.image.reset();
    atlas.reset();
    images.retire();
    require(images.statistics().residentImageBytes == atlasBytesBefore, "Retired font atlas retained its reservation");
    std::cout << "UI_FONT_ATLAS_UPDATES=65\nUI_FONT_ATLAS_ALLOCATION_GENERATIONS=1\nUI_FONT_ATLAS_RETIRED=1\n";
    context->WaitForIdle();renderer.retire();images.retire();
    // Exercise the same production allocation and fence retirement with a
    // one-quad budget. No cache-entry removal may free submitted GPU buffers.
    const std::vector<VSWorldRenderer::Vertex> budgetVertices{{-1,1,0,0,0,1,0,0},{1,1,0,0,0,1,1,0},
        {-1,-1,0,0,0,1,0,1},{1,-1,0,0,0,1,1,1}};
    const std::vector<unsigned> budgetIndices{0,2,1,1,2,3};
    const auto meshBytes=budgetVertices.size()*sizeof(VSWorldRenderer::Vertex)+budgetIndices.size()*sizeof(unsigned);
    VSWorldRenderer limited(device,context,meshBytes);
    VSWorldRenderer::Packet retained;retained.mesh=limited.upload(budgetVertices,budgetIndices);
    require(retained.mesh!=nullptr,"Budgeted first generation was refused");
    const auto buffers = VSWorldRenderer::geometryBuffers(retained.mesh);
    std::array<IReferenceCounters*, 2> bufferLifetimes{};
    for (unsigned i = 0; i < buffers.size(); ++i)
    { bufferLifetimes[i] = buffers[i]->GetReferenceCounters(); bufferLifetimes[i]->AddWeakRef(); }
    struct BufferObservations
    {
        std::array<IReferenceCounters*, 2>& counters;
        ~BufferObservations() { for (auto* counter : counters) counter->ReleaseWeakRef(); }
    } bufferObservations{bufferLifetimes};
    retained.fullbright=true;retained.color={1,0,0,1};
    limited.draw(rtv,8,8,view,{retained});
    require(limited.statistics().residentMeshBytes==meshBytes,"Submitted geometry lost its byte reservation");
    const auto priorMesh = retained.mesh.get();
    require(!limited.publishMesh(retained.mesh, budgetVertices, budgetIndices),
            "Mesh budget admitted overlapping live generations");
    require(retained.mesh.get() == priorMesh, "Refused mesh publication discarded the live generation");
    require(limited.statistics().meshAllocationRefusals==1,"Budget refusal evidence is missing");
    capture("budget-retained.ppm",[](unsigned,unsigned){return Pixel{255,0,0,255};});
    retained.mesh.reset();limited.retire();require(limited.statistics().residentMeshBytes==0,"Completed mesh reservation was not retired");
    for (const auto* counters : bufferLifetimes)
        require(counters->GetNumStrongRefs() == 0, "Context retained uncharged completed geometry buffers");
    std::cout << "WORLD_BOUND_BUFFERS_RETIRED=1\n";
    retained.mesh=limited.upload(budgetVertices,budgetIndices);
    require(retained.mesh!=nullptr,"Released mesh budget could not admit replacement");
    const auto imageBytesBefore = images.statistics().residentImageBytes;
    retained.image = images.upload(1, 1, {0,255,0,255}, VSWorldContracts::ImageRole::RemoteAsset);
    require(retained.image != nullptr, "Budget lifetime texture upload failed");
    auto* textureLifetime = VSUIRenderer::imageView(retained.image)->GetTexture()->GetReferenceCounters();
    textureLifetime->AddWeakRef();
    const auto releaseWeak = [](IReferenceCounters* counters) { counters->ReleaseWeakRef(); };
    std::unique_ptr<IReferenceCounters, decltype(releaseWeak)> lifetimeGuard(textureLifetime, releaseWeak);
    retained.color={1,1,1,1};limited.draw(rtv,8,8,view,{retained});retained.mesh.reset();retained.image.reset();
    require(images.statistics().residentImageBytes == imageBytesBefore + 8,
            "Submitted image lost its charged owner before completion");
    require(limited.statistics().meshAllocationRefusals==1,"Per-frame statistics erased budget refusal evidence");
    capture("budget-replacement.ppm",[](unsigned,unsigned){return Pixel{0,255,0,255};});
    limited.retire();require(limited.statistics().residentMeshBytes==0,"Replacement submission did not release its bytes");
    require(images.statistics().residentImageBytes == imageBytesBefore,
            "Completed world image reservation was not released");
    require(textureLifetime->GetNumStrongRefs() == 0, "Cached world binding retained an uncharged texture");
    std::cout << "WORLD_BOUND_IMAGE_RETIRED=1\n";
    std::cout<<"WORLD_BUDGET_BYTES="<<meshBytes<<"\nWORLD_BUDGET_REFUSALS=1\nWORLD_BUDGET_RETIRED=1\n";
    // Quantize source RGBA before blend factors at destination precision.
    // Exercise this through the real renderer without opening a window.
    const float grayClear[]{31.f/255,31.f/255,31.f/255,1};
    context->SetRenderTargets(1,&rtv,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearRenderTarget(rtv,grayClear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    auto border=images.upload(1,1,{141,141,141,1});
    std::vector<VSUIRenderer::Packet> borders;
    for (unsigned column=0;column<3;++column) for(unsigned layer=0;layer<6;++layer)
    {
        VSUIRenderer::Packet packet;packet.image=border;
        packet.bounds={float(column*2),0,float(column*2+2),8};packet.clip={0,0,8,8};
        packet.color={1,1,1,column==0?.4f:column==1?.6f:.95f};borders.push_back(packet);
    }
    std::vector<std::uint8_t> background(8*8*4,31);
    for (unsigned i=3;i<background.size();i+=4) background[i]=255;
    const auto floating=vs_ui_expected_pixels(8,8,1,borders,8,background);
    const auto products=vs_ui_expected_pixels(8,8,1,borders,8,background,VSUIBlendPrecision::DestinationProducts);
    images.draw(rtv,8,8,1,borders);
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=readback;
    copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);context->Flush();context->WaitForIdle();
    MappedTextureSubresource mapped{};context->MapTextureSubresource(readback,0,0,MAP_READ,MAP_FLAG_NONE,nullptr,mapped);
    require(mapped.pData!=nullptr,"UI blend precision readback failed");
    unsigned mismatch=0;
    std::ofstream actualFile("ui-blend-precision.ppm",std::ios::binary);
    std::ofstream floatFile("ui-blend-precision-expected.ppm",std::ios::binary);
    std::ofstream productFile("ui-blend-precision-expected-products.ppm",std::ios::binary);
    for(auto* file:{&actualFile,&floatFile,&productFile}) *file<<"P6\n8 8\n255\n";
    for(unsigned y=0;y<8;++y) for(unsigned x=0;x<8;++x)
    {
        const auto* actual=static_cast<const std::uint8_t*>(mapped.pData)+y*mapped.Stride+x*4;
        const auto offset=(y*8+x)*4;
        actualFile.write(reinterpret_cast<const char*>(actual),3);
        floatFile.write(reinterpret_cast<const char*>(floating.data()+offset),3);
        productFile.write(reinterpret_cast<const char*>(products.data()+offset),3);
        if(!vs_ui_pixel_matches(actual,floating.data()+offset,products.data()+offset,3)) ++mismatch;
    }
    context->UnmapTextureSubresource(readback,0,0);
    actualFile.close();floatFile.close();productFile.close();
    require(actualFile.good()&&floatFile.good()&&productFile.good(),"UI blend precision evidence write failed");
    require(!mismatch,"UI blend precision pixel oracle mismatch");
    images.retire();std::cout<<"UI_BLEND_PRECISION_PIXELS=0\n";
}

static void loadedLibraries()
{
#if PLATFORM_WIN32
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    require(snapshot != INVALID_HANDLE_VALUE, "Loaded module snapshot failed");
    MODULEENTRY32W entry{}; entry.dwSize = sizeof(entry);
    if (Module32FirstW(snapshot, &entry)) do
    {
        char path[4 * MAX_PATH]{};
        require(WideCharToMultiByte(CP_UTF8, 0, entry.szExePath, -1, path, sizeof(path), nullptr, nullptr) != 0,
                "Loaded module path conversion failed");
        std::cout << "LOADED=" << path << '\n';
    } while (Module32NextW(snapshot, &entry));
    CloseHandle(snapshot);
#else
    for (auto symbol : {reinterpret_cast<void*>(&vkEnumerateInstanceVersion), reinterpret_cast<void*>(&GetEngineFactoryVk)})
    {
        Dl_info info{};
        require(dladdr(symbol, &info) != 0, "Loaded library identity unavailable");
        std::cout << "LOADED=" << info.dli_fname << '\n';
    }
    std::ifstream maps("/proc/self/maps"); std::string line;
    while (std::getline(maps, line)) if (line.find(".so") != std::string::npos) std::cout << "LOADED=" << line << '\n';
#endif
}

int main(int argc, char** argv)
{
    try
    {
        if (argc == 2 && std::string(argv[1]) == "--test-selection") { selectionTests(); return 0; }
        require(argc == 3 || argc == 4, "Usage: diligent_render_test {auto|SwiftShader|llvmpipe|amd|nvidia} MODE [DEVICE_NAME]");
        require(std::string(argv[1]) == "SwiftShader" || std::string(argv[1]) == "llvmpipe" ||
                std::string(argv[1]) == "amd" || std::string(argv[1]) == "nvidia" || std::string(argv[1]) == "auto", "Unknown device selector");
        const std::string mode = argv[2];
        require(mode == "world" || mode == "offscreen" || mode == "present" || mode == "invalid" || mode == "invalid-sync" || mode == "bad-pixels" || mode == "bad-orientation", "Unknown mode");
        const auto selected = preflight(argv[1], mode, argc == 4 ? argv[3] : "");
#if PLATFORM_WIN32
        auto* factory = LoadAndGetEngineFactoryVk();
#else
        auto* factory = GetEngineFactoryVk();
#endif
        require(factory != nullptr, "Diligent factory unavailable"); factory->SetMessageCallback(diagnostic);
        PresentationEvidence presentationEvidence;
        {
            RefCntAutoPtr<IRenderDevice> device; RefCntAutoPtr<IDeviceContext> context;
            const char* layer = "VK_LAYER_KHRONOS_validation";
            EngineVkCreateInfo ci{}; ci.EnableValidation = true;
            ci.InstanceLayerCount = 1; ci.ppInstanceLayerNames = &layer;
            Uint32 adapterCount = 0;
            factory->EnumerateAdapters({1,1}, adapterCount, nullptr);
            require(adapterCount > 0, "No Diligent Vulkan adapters available");
            std::vector<GraphicsAdapterInfo> adapters(adapterCount);
            factory->EnumerateAdapters({1,1}, adapterCount, adapters.data());
            unsigned matches = 0;
            for (Uint32 i = 0; i < adapterCount; ++i)
                if (adapters[i].VendorId == selected.vendorID && adapters[i].DeviceId == selected.deviceID &&
                    std::string(adapters[i].Description) == selected.deviceName)
                { if (matches == 0) ci.AdapterId = i; ++matches; }
            require(matches > 0, "Diligent adapter does not match the preflight device");
            factory->CreateDeviceAndContextsVk(ci, &device, &context);
            require(device && context, "Diligent device creation failed");
            const auto& actual = device->GetAdapterInfo();
            require(actual.VendorId == selected.vendorID && actual.DeviceId == selected.deviceID &&
                    std::string(actual.Description) == selected.deviceName, "Diligent selected unexpected adapter");
            std::cout << "DILIGENT_DEVICE=" << device->GetAdapterInfo().Description << '\n';
            auto target = texture(device, false, true);
            auto a = texture(device, true, false), b = texture(device, true, false);
            if (mode == "world") worldContract(device,context,target,a);
            else
            {
            draw(device, context, target, a, first, mode == "bad-orientation"); draw(device, context, target, b, second);
            context->WaitForIdle();
            compare(context, a, first, "first.ppm", mode == "bad-pixels");
            compare(context, b, second, "replacement.ppm", false);
            }
            if (mode == "present")
                presentationEvidence = presentation(factory, device, context);
            context->WaitForIdle(); loadedLibraries();
        }
        require(errors == 0 && validationErrors == 0, "Rendering/teardown diagnostics contain errors");
        if (mode == "present")
        { presentationEvidence.stages.push_back("device-context-released"); presentationEvidence.write(); }
        std::cout << "PASS " << mode << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
