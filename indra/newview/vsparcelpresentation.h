// Shared parcel selection and access feedback on native world geometry. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include <memory>
class VSWorldScene;
class VSUIResources;
class VSNativeSession;
class VSVulkanContext;
class LLSD;
class VSParcelPresentation
{
public:
    VSParcelPresentation(VSWorldRenderer&, VSUIResources&, VSWorldScene&);
    ~VSParcelPresentation();
    void append(const VSNativeSession&, const VSWorldRenderer::View&, unsigned width, unsigned height,
                std::vector<VSWorldRenderer::Packet>&);
    void reset();
#if VS_VULKAN_DIAGNOSTICS
    static bool qualifyRectangle(VSVulkanContext&, LLSD& evidence);
#endif
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
