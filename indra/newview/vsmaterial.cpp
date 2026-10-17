// Transpose material values; keep shared LLMaterial/LLGLTFMaterial decoding. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsmaterial.h"
#include "vsnormalbasis.h"
#include "llmaterial.h"
#include "llgltfmaterial.h"
#include "lltextureentry.h"
#include <cmath>
#include <algorithm>
namespace VSMaterial
{
Transform legacyUV(float s, float t, float x, float y, float angle)
{
    const float c = std::cos(angle), r = std::sin(angle);
    Transform out;
    out.matrix = {s*c, -t*r, s*r, t*c};
    out.offset = {x + .5f - .5f*(out.matrix[0]+out.matrix[2]),
                  y + .5f - .5f*(out.matrix[1]+out.matrix[3])};
    return out;
}
Transform pbrUV(const LLGLTFMaterial::TextureTransform& value)
{
    // Match textureUtilV.glsl: convert SL UV to glTF handedness, apply
    // the reference KHR scale/rotation/offset, then convert back.
    const float c = std::cos(value.mRotation), s = std::sin(value.mRotation);
    Transform out;
    out.matrix = {c*value.mScale.mV[0], s*value.mScale.mV[0],
                  -s*value.mScale.mV[1], c*value.mScale.mV[1]};
    out.offset = {value.mOffset.mV[0]+s*value.mScale.mV[1],
                  1-value.mOffset.mV[1]-c*value.mScale.mV[1]};
    return out;
}
std::array<float, 2> transform(const Transform& t, float u, float v)
{ return {t.matrix[0]*u+t.matrix[2]*v+t.offset[0], t.matrix[1]*u+t.matrix[3]*v+t.offset[1]}; }
State legacy(const LLTextureEntry& entry, const LLMaterial* material)
{
    State out;
    for(auto& sampler:out.samplers) sampler.mipmaps=true;
    std::copy_n(entry.getID().mData, 16, out.textures[BaseColor].begin());
    std::copy_n(entry.getColor().mV, 4, out.base.begin());
    out.fullbright = entry.getFullbright() != 0;
    out.glow = entry.getGlow(); out.bump = entry.getBumpmap(); out.shiny = entry.getShiny();
    out.alpha = out.base[3] < 1 ? VSWorldContracts::AlphaMode::Blend : VSWorldContracts::AlphaMode::Opaque;
    out.uv[BaseColor] = legacyUV(entry.getScaleS(), entry.getScaleT(), entry.getOffsetS(), entry.getOffsetT(), entry.getRotation());
    if (material)
    {
        std::copy_n(material->getNormalID().mData, 16, out.textures[Normal].begin());
        std::copy_n(material->getSpecularID().mData, 16, out.textures[ORMOrSpecular].begin());
        out.uv[Normal] = legacyUV(material->getNormalRepeatX(), material->getNormalRepeatY(),
            material->getNormalOffsetX(), material->getNormalOffsetY(), material->getNormalRotation());
        out.uv[ORMOrSpecular] = legacyUV(material->getSpecularRepeatX(), material->getSpecularRepeatY(),
            material->getSpecularOffsetX(), material->getSpecularOffsetY(), material->getSpecularRotation());
        const auto color = material->getSpecularLightColor();
        for (unsigned i = 0; i < 3; ++i) out.specular[i] = float(color.mV[i])/255;
        out.specular[3] = float(material->getEnvironmentIntensity())/255;
        out.roughness = 1-float(material->getSpecularLightExponent())/255;
        out.cutoff = float(material->getAlphaMaskCutoff())/255;
        switch (material->getDiffuseAlphaMode())
        {
            case LLMaterial::DIFFUSE_ALPHA_MODE_BLEND: out.alpha = VSWorldContracts::AlphaMode::Blend; break;
            case LLMaterial::DIFFUSE_ALPHA_MODE_MASK: out.alpha = VSWorldContracts::AlphaMode::Mask; break;
            case LLMaterial::DIFFUSE_ALPHA_MODE_EMISSIVE: out.emissiveAlpha = true; break;
            default: break;
        }
    }
    return out;
}
State pbr(const LLGLTFMaterial& asset, const LLGLTFMaterial* overrides)
{
    LLGLTFMaterial value(asset);
    if (overrides) value.applyOverride(*overrides);
    State out; out.pbr = true;
    for(auto& sampler:out.samplers) sampler.mipmaps=true;
    for (unsigned i = 0; i < LLGLTFMaterial::GLTF_TEXTURE_INFO_COUNT; ++i)
    {
        std::copy_n(value.mTextureId[i].mData, 16, out.textures[i].begin());
        out.uv[i] = pbrUV(value.mTextureTransform[i]);
    }
    const auto& normal=value.mTextureTransform[Normal];
    out.normalTangent=VSNormalBasis::primitive(normal.mScale.mV[0],normal.mScale.mV[1],normal.mRotation);
    std::copy_n(value.mBaseColor.mV, 4, out.base.begin());
    std::copy_n(value.mEmissiveColor.mV, 3, out.emissive.begin());
    out.metallic = value.mMetallicFactor; out.roughness = value.mRoughnessFactor;
    out.doubleSided = value.mDoubleSided; out.cutoff = value.mAlphaCutoff;
    out.alpha = value.mAlphaMode == LLGLTFMaterial::ALPHA_MODE_BLEND ? VSWorldContracts::AlphaMode::Blend :
        value.mAlphaMode == LLGLTFMaterial::ALPHA_MODE_MASK ? VSWorldContracts::AlphaMode::Mask : VSWorldContracts::AlphaMode::Opaque;
    return out;
}
}
