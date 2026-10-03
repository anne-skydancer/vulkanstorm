// Check the packaged public API, import libraries, and staged runtimes without
// creating a GPU device. This is not renderer or driver qualification.
#include <DiligentCore/Graphics/GraphicsEngineVulkan/interface/EngineFactoryVk.h>
#include <vulkan/vulkan.h>
#include <iostream>

int main()
{
    if (!Diligent::LoadAndGetEngineFactoryVk())
        return 1;
    uint32_t version = 0;
    const auto result = vkEnumerateInstanceVersion(&version);
    std::cout << "Diligent factory loaded; Vulkan loader result=" << result
              << "; headers=" << VK_HEADER_VERSION << '\n';
    return result == VK_SUCCESS ? 0 : 2;
}
