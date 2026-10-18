// Actual shared debug beacon geometry submitted by the native peer. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include <memory>
class VSWorldScene;
class VSNativeSession;
class VSBeaconPresentation
{
public:
    VSBeaconPresentation(VSWorldRenderer&,VSWorldScene&);
    ~VSBeaconPresentation();
    void append(const VSNativeSession&,std::vector<VSWorldRenderer::Packet>&);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
