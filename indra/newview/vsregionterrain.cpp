// Actual terrain settings shared by the reference and native region UI. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsregionterrain.h"
#include "vsregioncontext.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llvlcomposition.h"
#include "llviewertexture.h"
#include "vsuiimageprovider.h"
#include "llviewerwindow.h"
#include "llappviewer.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#endif
std::optional<VSRegionTerrain> vs_region_terrain(const VSRegionContext& region)
{
    if(!region.current())return {};
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto session=VSNativeSession::active();
        if(!session || !gViewerWindow->nativeContext())return {};
        return gViewerWindow->nativeContext()->world().terrainSettings(region.getHost());
    }
#endif
    const auto owner=LLWorld::getInstance()->getRegion(region.getHost());
    if(!owner || owner->getRegionID()!=region.getRegionID())return {};
    auto* composition=owner->getComposition();if(!composition)return {};
    VSRegionTerrain result;result.current=[region]{return region.current();};
    result.texturesReady=composition->makeTexturesReady(false,false);
    result.materialsReady=composition->makeMaterialsReady(false,false);
    for(S32 i=0;i<4;++i)
    {
        result.assets[i]=composition->getDetailAssetID(i);
        result.starts[i]=composition->getStartHeight(i);
        result.ranges[i]=composition->getHeightRange(i);
        if(const auto* value=composition->getMaterialOverride(i))result.overrides[i]=new LLGLTFMaterial(*value);
    }
    return result;
}

VSRegionTerrainTexture vs_region_terrain_texture(const LLUUID& id)
{
    VSRegionTerrainTexture result;
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        vs_request_world_image(id,128);
        const auto dimensions=vs_ui_image_dimensions(id);
        result.width=dimensions.first;result.height=dimensions.second;
        result.components=vs_ui_image_components(id);result.raw=vs_ui_image_raw(id);
        return result;
    }
#endif
    const auto image=LLViewerTextureManager::getFetchedTexture(id);
    result.components=image->getComponents();result.width=image->getFullWidth();result.height=image->getFullHeight();
    if(image->hasSavedRawImage())result.raw=image->getSavedRawImage();
    return result;
}
