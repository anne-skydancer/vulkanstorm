// Native graphics facts and implemented setting consumers. LGPL-2.1.
#pragma once
#include <string>
#include <string_view>

struct VSGraphicsCapabilities
{
    bool ready = false;
    bool wireframe = false;
    bool compression = false;
    unsigned anisotropy = 1;
    unsigned maximumTextureDimension = 0;
    std::string adapter;
};

inline bool vs_graphics_feature_implemented(std::string_view name, const VSGraphicsCapabilities& caps)
{
    if (!caps.ready) return false;
    if (name == "RenderCompressTextures") return caps.compression;
    if (name == "RenderAnisotropic") return caps.anisotropy > 1;
    if (name == "RenderMaxTextureResolution") return caps.maximumTextureDimension != 0;
    // Each entry has a live consumer in the native scene/terrain/avatar/image
    // owners or in the shared camera owner used by that scene. A Vulkan device
    // feature alone cannot qualify a viewer rendering pass.
    return name == "RenderAvatarLODFactor" || name == "RenderAvatarPhysicsLODFactor" ||
           name == "RenderAvatarMaxComplexity" || name == "RenderAutoMuteSurfaceAreaLimit" ||
           name == "RenderFarClip" || name == "RenderTerrainLODFactor" || name == "RenderTerrainPBRDetail" ||
           name == "RenderTerrainPBRPlanarSampleCount" || name == "RenderTreeLODFactor" ||
           name == "RenderVolumeLODFactor" || name == "WLSkyDetail" ||
           name == "Disregard128DefaultDrawDistance" || name == "Disregard96DefaultDrawDistance";
}

// Explicitly identified passes with no native renderer consumer yet. Their
// saved GL values must survive recommendations and presets without implying
// that a Vulkan device feature implements the pass.
inline bool vs_graphics_pending_pass(std::string_view name)
{
    return name == "RenderDepthOfField" || name == "RenderGlow" || name == "RenderGlowIterations" ||
           name == "RenderGlowWidth" || name == "RenderGlowStrength" || name == "RenderGlowMinLuminance" ||
           name == "RenderSharpness" || name == "RenderFSAAType" || name == "RenderFSAASamples" ||
           name == "RenderTransparentWater" || name == "RenderGammaFull";
}

template<class Features>
void vs_graphics_limit_features(Features& features, const VSGraphicsCapabilities& caps)
{
    for (auto& [name, feature] : features)
        feature.mAvailable = feature.mAvailable && vs_graphics_feature_implemented(name, caps);
}

inline bool vs_graphics_wireframe_pass(bool requested, bool background, unsigned topology, bool supported)
{
    // The scene triangle rasterizer changes; UI, sky and line/point producers
    // keep their original topology. Topology::Triangles is zero in the owner.
    return requested && supported && !background && topology == 0;
}
