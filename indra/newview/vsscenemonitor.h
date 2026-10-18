// Native resources for the original scene-loading monitor. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include <memory>
#include <optional>

namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITexture; }
class VSSceneMonitor
{
public:
    struct Result { std::uint64_t samples; unsigned width,height; };
    VSSceneMonitor(Diligent::IRenderDevice*,Diligent::IDeviceContext*,VSUIRenderer&);
    ~VSSceneMonitor();
    bool capture(Diligent::ITexture*,unsigned width,unsigned height);
    bool compare(unsigned width,unsigned height,float ratio,float tolerance);
    std::optional<Result> poll();
    VSUIRenderer::Image image() const;
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
