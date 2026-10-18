// Actual terrain settings shared by the reference and native region UI. LGPL-2.1.
#pragma once
#include "lluuid.h"
#include "llpointer.h"
#include "llgltfmaterial.h"
#include <array>
#include <functional>
#include <optional>
class VSRegionContext;
struct VSRegionTerrain
{
    std::array<LLUUID,4> assets;
    std::array<float,4> starts{},ranges{};
    std::array<LLPointer<LLGLTFMaterial>,4> overrides;
    bool texturesReady=false,materialsReady=false;
    std::function<bool()> current;
};
std::optional<VSRegionTerrain> vs_region_terrain(const VSRegionContext&);

class LLImageRaw;
struct VSRegionTerrainTexture
{
    S32 components=0,width=0,height=0;
    LLPointer<LLImageRaw> raw;
};
VSRegionTerrainTexture vs_region_terrain_texture(const LLUUID&);
