// Region-scoped native terrain material ownership. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "lluuid.h"
#include "vsworldrenderer.h"
#include <array>
#include <memory>
class VSUIResources;
class VSTerrainAssets
{
public:
    explicit VSTerrainAssets(const LLHost&);
    ~VSTerrainAssets();
    void releaseImages();
    std::shared_ptr<const VSWorldRenderer::Terrain> snapshot(const std::array<LLUUID,4>&,
        unsigned demand, unsigned regionWidth, VSUIResources&, const VSUIRenderer::Image& ramp);
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
