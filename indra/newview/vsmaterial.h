// Native snapshots of the shared viewer material semantics. LGPL-2.1.
#pragma once
#include "vsworldcontracts.h"
#include <array>
#include <tuple>
class LLMaterial;
class LLTextureEntry;
class LLGLTFMaterial;
namespace VSMaterial
{
enum Channel : unsigned { BaseColor, Normal, ORMOrSpecular, Emissive, ChannelCount };
struct Transform
{
    // Affine transform of bottom-left source UV. Images are canonical top-left;
    // the sampling boundary performs that conversion exactly once.
    std::array<float, 4> matrix{1, 0, 0, 1};
    std::array<float, 2> offset{};
    bool operator==(const Transform& rhs) const {return matrix==rhs.matrix && offset==rhs.offset;}
};
struct State
{
    std::array<std::array<std::uint8_t, 16>, ChannelCount> textures{};
    std::array<Transform, ChannelCount> uv;
    std::array<unsigned, ChannelCount> uvSet{};
    float normalScale=1, occlusionStrength=1;
    std::array<float, 4> base{1, 1, 1, 1}, specular{1, 1, 1, 0};
    std::array<float, 3> emissive{};
    float metallic = 0, roughness = 1, cutoff = .5f, glow = 0;
    unsigned bump = 0, shiny = 0;
    VSWorldContracts::AlphaMode alpha = VSWorldContracts::AlphaMode::Opaque;
    bool pbr = false, fullbright = false, doubleSided = false, emissiveAlpha = false;
    bool operator==(const State& rhs) const
    {
        return std::tie(textures,uv,uvSet,normalScale,occlusionStrength,base,specular,emissive,metallic,roughness,cutoff,glow,bump,shiny,alpha,pbr,fullbright,doubleSided,emissiveAlpha)==
            std::tie(rhs.textures,rhs.uv,rhs.uvSet,rhs.normalScale,rhs.occlusionStrength,rhs.base,rhs.specular,rhs.emissive,rhs.metallic,rhs.roughness,rhs.cutoff,rhs.glow,rhs.bump,rhs.shiny,rhs.alpha,rhs.pbr,rhs.fullbright,rhs.doubleSided,rhs.emissiveAlpha);
    }
};
Transform legacyUV(float scaleS, float scaleT, float offsetS, float offsetT, float rotation);
std::array<float, 2> transform(const Transform&, float u, float v);
State legacy(const LLTextureEntry&, const LLMaterial* = nullptr);
State pbr(const LLGLTFMaterial&, const LLGLTFMaterial* overrides = nullptr);
}
