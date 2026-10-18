// Native owner for the actual shared LLTracker controller. LGPL-2.1.
#pragma once
#include "vsworldrenderer.h"
#include <memory>
class VSWorldScene;
class VSNativeSession;
class VSTrackerPresentation
{
public:
    VSTrackerPresentation(VSWorldRenderer&,VSWorldScene&);
    ~VSTrackerPresentation();
    void append(const VSNativeSession&,std::vector<VSWorldRenderer::Packet>&);
    void reset();
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
