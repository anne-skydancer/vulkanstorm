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
        handle = CreateWindowA(wc.lpszClassName, "Software Vulkan test", WS_OVERLAPPEDWINDOW,
                              0, 0, 128, 128, nullptr, nullptr, wc.hInstance, nullptr);
        require(handle != nullptr, "Native HWND creation failed");
        native.hWnd = handle; ShowWindow(handle, SW_SHOW);
    }
    void pump() { MSG msg{}; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); } }
    void size(int w, int h) { SetWindowPos(handle, nullptr, 0, 0, w, h, SWP_NOMOVE | SWP_NOZORDER); pump(); }
    void minimize() { ShowWindow(handle, SW_MINIMIZE); pump(); }
    void restore() { ShowWindow(handle, SW_RESTORE); pump(); }
    ~TestWindow() { DestroyWindow(handle); UnregisterClassA("SoftwareVulkanTest", GetModuleHandle(nullptr)); }
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
    void minimize() { SDL_MinimizeWindow(handle); pump(); }
    void restore() { SDL_RestoreWindow(handle); pump(); }
    ~TestWindow() { SDL_DestroyWindow(handle); SDL_Quit(); }
#endif
};

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
        require(mode == "offscreen" || mode == "present" || mode == "invalid" || mode == "invalid-sync" || mode == "bad-pixels" || mode == "bad-orientation", "Unknown mode");
        const auto selected = preflight(argv[1], mode, argc == 4 ? argv[3] : "");
#if PLATFORM_WIN32
        auto* factory = LoadAndGetEngineFactoryVk();
#else
        auto* factory = GetEngineFactoryVk();
#endif
        require(factory != nullptr, "Diligent factory unavailable"); factory->SetMessageCallback(diagnostic);
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
            draw(device, context, target, a, first, mode == "bad-orientation"); draw(device, context, target, b, second);
            context->WaitForIdle();
            compare(context, a, first, "first.ppm", mode == "bad-pixels");
            compare(context, b, second, "replacement.ppm", false);
            if (mode == "present")
            {
                TestWindow window;
                RefCntAutoPtr<ISwapChain> swapchain;
                SwapChainDesc desc{}; desc.Width = desc.Height = 128; desc.DepthBufferFormat = TEX_FORMAT_UNKNOWN;
                factory->CreateSwapChainVk(device, context, desc, window.native, &swapchain);
                require(swapchain != nullptr, "Native swapchain failed");
                for (unsigned frame = 0; frame < 12; ++frame)
                {
                    if (frame == 3) { window.size(96,80); swapchain->Resize(96,80); }
                    if (frame == 6) { window.minimize(); window.pump(); /* no submission while minimized */ }
                    if (frame == 7) { window.restore(); window.size(128,128); swapchain->Resize(128,128); }
                    if (frame != 6)
                    {
                        auto* rtv = swapchain->GetCurrentBackBufferRTV(); require(rtv != nullptr, "Backbuffer missing");
                        context->SetRenderTargets(1, &rtv, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        const float clear[]{0.1f,0.2f,0.3f,1}; context->ClearRenderTarget(rtv, clear, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                        swapchain->Present(0);
                    }
                    window.pump();
                }
                context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_NONE);
                context->WaitForIdle(); swapchain.Release();
            }
            context->WaitForIdle(); loadedLibraries();
        }
        require(errors == 0 && validationErrors == 0, "Rendering/teardown diagnostics contain errors");
        std::cout << "PASS " << mode << '\n'; return 0;
    }
    catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
