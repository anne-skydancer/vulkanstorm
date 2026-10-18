// Region-scoped native terrain material ownership. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "lluuid.h"
#include "vsworldrenderer.h"
#include "llgltfmaterial.h"
#include <array>
#include <memory>
#include <utility>
#include <boost/signals2.hpp>
class VSUIResources;
class VSTerrainAssets
{
public:
    explicit VSTerrainAssets(const LLHost&);
    ~VSTerrainAssets();
    void releaseImages();
    boost::signals2::connection observeSettings(const boost::signals2::signal<void()>::slot_type&);
    std::pair<bool,bool> settings(const std::array<LLUUID,4>&,
        std::array<LLPointer<LLGLTFMaterial>,4>& overrides);

    std::shared_ptr<const VSWorldRenderer::Terrain> snapshot(const std::array<LLUUID,4>&,
        unsigned demand, unsigned regionWidth, VSUIResources&, const VSUIRenderer::Image& ramp);
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
