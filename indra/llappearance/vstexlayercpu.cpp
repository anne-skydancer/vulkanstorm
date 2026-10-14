// CPU appearance composition using the shared layer/visual-param owners. LGPL-2.1.
#include "linden_common.h"
#include "lltexlayer.h"
#include "vstexlayerinfo.h"
#include "indra_constants.h"
#include "llavatarappearance.h"
#include "lllocaltextureobject.h"
#include "llwearable.h"
#include "llimagetga.h"
#include "llimage.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace
{
LLPointer<LLImageRaw> staticImage(const std::string& name, const LLTexLayerCPUAllocate& allocate)
{
    if (name.empty())
        return nullptr;
    const auto* encoded = LLTexLayerStaticImageList::getInstance()->getImageTGA(name);
    if (!encoded || encoded->getWidth() <= 0 || encoded->getHeight() <= 0 || encoded->getWidth() > 2048 || encoded->getHeight() > 2048)
        return nullptr;
    LLPointer<LLImageRaw> result = allocate(encoded->getWidth(), encoded->getHeight(), encoded->getComponents());
    if (!result)
        return nullptr;
    // The encoded image cache and decoder belong to the shared asset layer;
    // no texture-manager or graphics-context call is needed here.
    return const_cast<LLImageTGA*>(encoded)->decode(result) ? result : nullptr;
}
std::array<F32, 4> sample(const LLImageRaw& image, F32 u, F32 v, bool mask)
{
    const S32 width = image.getWidth(), height = image.getHeight(), components = image.getComponents();
    if (width <= 0 || height <= 0 || components < 1 || components > 4)
        return {};
    const F32 px = llclamp(u * width - .5f, 0.f, F32(width - 1)), py = llclamp(v * height - .5f, 0.f, F32(height - 1));
    const S32 x = S32(px), y = S32(py);
    const F32 dx = px - x, dy = py - y;
    auto      texel = [&](S32 sx, S32 sy)
    {
        const U8* pixel = image.getData() + (sy * width + sx) * components;
        if (components == 1)
            return mask ? std::array<F32, 4>{ 0, 0, 0, pixel[0] / 255.f }
                        : std::array<F32, 4>{ pixel[0] / 255.f, pixel[0] / 255.f, pixel[0] / 255.f, 1 };
        if (components == 2)
            return std::array<F32, 4>{ pixel[0] / 255.f, pixel[0] / 255.f, pixel[0] / 255.f, pixel[1] / 255.f };
        return std::array<F32, 4>{ pixel[0] / 255.f, pixel[1] / 255.f, pixel[2] / 255.f, components == 4 ? pixel[3] / 255.f : 1 };
    };
    const auto         a = texel(x, y), b = texel(std::min(x + 1, width - 1), y), c = texel(x, std::min(y + 1, height - 1)),
                       d = texel(std::min(x + 1, width - 1), std::min(y + 1, height - 1));
    std::array<F32, 4> result;
    for (unsigned component = 0; component < 4; ++component)
        result[component] = (1 - dy) * ((1 - dx) * a[component] + dx * b[component]) + dy * ((1 - dx) * c[component] + dx * d[component]);
    return result;
}
U8 unorm(F32 value)
{ return U8(llclamp(ll_round(value * 255.f), 0, 255)); }
} // namespace
bool LLTexLayerParamAlpha::prepareCPU(LLPointer<LLImageRaw>& image, F32& weight, const LLTexLayerCPUAllocate& allocate)
{
    image  = nullptr;
    weight = (mAvatarAppearance->getSex() & getSex()) ? mCurWeight : getDefaultWeight();
    if (!std::isfinite(weight))
        return false;
    const auto* info = static_cast<const LLTexLayerParamAlphaInfo*>(getInfo());
    if (info->mStaticImageFileName.empty())
        return true;
    if (mStaticImageInvalid)
        return false;
    if (!mStaticImageTGA)
        mStaticImageTGA = LLTexLayerStaticImageList::getInstance()->getImageTGA(info->mStaticImageFileName);
    if (!mStaticImageTGA || mStaticImageTGA->getWidth() > 2048 || mStaticImageTGA->getHeight() > 2048)
    {
        mStaticImageInvalid = true;
        return false;
    }
    if (!mStaticImageRaw || weight != mCachedEffectiveWeight)
    {
        LLPointer<LLImageRaw> raw = allocate(mStaticImageTGA->getWidth(), mStaticImageTGA->getHeight(), 1);
        if (!raw)
            return false;
        if (!mStaticImageTGA->decodeAndProcess(raw, info->mDomain, weight))
            return false;
        mStaticImageRaw        = raw;
        mCachedEffectiveWeight = weight;
        mNeedsCreateTexture    = true;
    }
    image = mStaticImageRaw;
    return true;
}
bool LLTexLayerTemplate::renderCPU(LLImageRaw& target, const LLTexLayerCPUTexture& texture, bool alphaOnly)
{
    const U32 count   = updateWearableCache();
    bool      success = true;
    for (U32 index = 0; index < count; ++index)
        if (auto* layer = getLayer(index))
        {
            // Net color and cross-wearable drivers belong to the avatar owner,
            // just as in the reference template's per-wearable draw loop.
            if (auto* wearable = mWearableCache[index])
                wearable->writeToAvatar(mAvatarAppearance);
            success &= layer->renderCPU(target, texture, alphaOnly);
        }
    return success;
}
void LLTexLayerTemplate::gatherCPUAlphaMasks(U8* output, S32 width, S32 height) const
{
    const U32 count = updateWearableCache();
    for (U32 index = 0; index < count; ++index)
        if (auto* layer = getLayer(index))
            layer->gatherCPUAlphaMasks(output, width, height);
}
void LLTexLayer::gatherCPUAlphaMasks(U8* output, S32 width, S32 height) const
{
    if (!hasMorph() || !mCPUMorphMask || mCPUMorphMask->getWidth() != width || mCPUMorphMask->getHeight() != height)
        return;
    const U8* mask = mCPUMorphMask->getData();
    for (S32 index = 0; index < width * height; ++index)
        output[index] = U8((U16(output[index]) * (U16(mask[index]) + 1)) >> 8);
}
bool LLTexLayer::renderCPU(LLImageRaw& target, const LLTexLayerCPUTexture& texture, bool alphaOnly)
{
    if (target.getComponents() != 4 || target.getWidth() <= 0 || target.getHeight() <= 0 || target.getWidth() > 2048 ||
        target.getHeight() > 2048 || !mInfo)
        return false;
    LLPointer<LLImageRaw> local;
    if (mInfo->mLocalTexture != -1 && mLocalTextureObject && mLocalTextureObject->getID() != IMG_DEFAULT_AVATAR)
    {
        local = texture(mLocalTextureObject->getID());
        if (!local)
            return texture.fail("Local texture unavailable: " + mInfo->getName());
    }
    LLPointer<LLImageRaw> stat;
    if (!mInfo->mStaticImageFileName.empty())
    {
        stat = staticImage(mInfo->mStaticImageFileName, texture.allocate);
        if (!stat)
            return texture.fail("Static texture unavailable: " + mInfo->mStaticImageFileName);
    }
    if (alphaOnly)
    {
        const auto source = stat ? stat : local;
        if (!source)
            return true;
        for (S32 y = 0; y < target.getHeight(); ++y)
            for (S32 x = 0; x < target.getWidth(); ++x)
            {
                const auto rgba = sample(*source, (x + .5f) / target.getWidth(), (y + .5f) / target.getHeight(), mInfo->mStaticImageIsMask);
                U8*        output = target.getData() + (y * target.getWidth() + x) * 4;
                output[3]         = unorm(output[3] / 255.f * rgba[3]);
            }
        return true;
    }
    LLColor4   color;
    const bool colorSpecified = findNetColor(&color);
    if (is_approx_zero(color.mV[3]))
        return true;
    struct Alpha
    {
        LLPointer<LLImageRaw> image;
        F32                   weight   = 0;
        bool                  multiply = false;
    };
    std::vector<Alpha> alphas;
    for (auto* param : mParamAlphaList)
    {
        if (param->getSkip())
            continue;
        Alpha alpha;
        alpha.multiply = param->getMultiplyBlend();
        if (!param->prepareCPU(alpha.image, alpha.weight, texture.allocate))
            return texture.fail("Alpha parameter unavailable: " + mInfo->getName());
        alphas.push_back(std::move(alpha));
    }
    LLPointer<LLImageRaw> morph;
    if (hasMorph() && !mParamAlphaList.empty())
    {
        morph = texture.allocate(target.getWidth(), target.getHeight(), 1);
        if (!morph)
            return texture.fail("Morph mask allocation refused: " + mInfo->getName());
    }
    for (S32 y = 0; y < target.getHeight(); ++y)
        for (S32 x = 0; x < target.getWidth(); ++x)
        {
            const F32 u = (x + .5f) / target.getWidth(), v = (y + .5f) / target.getHeight();
            U8*       output = target.getData() + (y * target.getWidth() + x) * 4;
            F32       mask   = output[3] / 255.f;
            if (!mParamAlphaList.empty())
            {
                if (!mParamAlphaList.front()->getMultiplyBlend())
                    mask = 0;
                for (const auto& alpha : alphas)
                {
                    const F32 value = alpha.image ? sample(*alpha.image, u, v, true)[3] : alpha.weight;
                    mask            = unorm(alpha.multiply ? mask * value : mask + value) / 255.f;
                }
                if (local && local->getComponents() == 4)
                    mask = unorm(mask * sample(*local, u, v, false)[3]) / 255.f;
                if (stat && mInfo->mStaticImageIsMask)
                    mask = unorm(mask * sample(*stat, u, v, true)[3]) / 255.f;
                mask      = unorm(mask * color.mV[3]) / 255.f;
                output[3] = unorm(mask);
            }
            if (morph)
                morph->getData()[y * target.getWidth() + x] = unorm(mask);
            auto draw = [&](std::array<F32, 4> rgba)
            {
                for (unsigned channel = 0; channel < 4; ++channel)
                    rgba[channel] *= color.mV[channel];
                if (!mInfo->mWriteAllChannels && rgba[3] < .004f)
                    return;
                const F32 factor = mParamAlphaList.empty() ? rgba[3] : output[3] / 255.f;
                for (unsigned channel = 0; channel < 4; ++channel)
                    output[channel] =
                        unorm(mInfo->mWriteAllChannels ? rgba[channel] : rgba[channel] * factor + output[channel] / 255.f * (1 - factor));
            };
            if (local && !mInfo->mUseLocalTextureAlphaOnly)
                draw(sample(*local, u, v, false));
            if (stat)
                draw(sample(*stat, u, v, mInfo->mStaticImageIsMask));
            if ((mInfo->mLocalTexture == -1 || mInfo->mUseLocalTextureAlphaOnly) && !stat && colorSpecified)
                draw({ 1, 1, 1, 1 });
        }
    if (morph)
    {
        mCPUMorphMask    = morph;
        mMorphMasksValid = true;
    }
    return true;
}
bool LLTexLayerSet::renderCPU(LLImageRaw& target, const LLTexLayerCPUTexture& texture)
{
    if (!mInfo || target.getWidth() != mInfo->mWidth || target.getHeight() != mInfo->mHeight || target.getComponents() != 4)
        return texture.fail("Layer-set dimensions do not match bake target");
    mIsVisible = true;
    for (const auto* layer : mMaskLayerList)
        if (layer->isInvisibleAlphaMask())
            mIsVisible = false;
    target.clear(0, 0, 0, mIsVisible ? 255 : 0);
    if (!mIsVisible)
        return true;
    bool success = true;
    for (auto* layer : mLayerList)
        if (layer->getRenderPass() == LLTexLayer::RP_COLOR)
            success &= layer->renderCPU(target, texture);
    auto alpha = staticImage(mInfo->mStaticAlphaFileName, texture.allocate);
    if (!mInfo->mStaticAlphaFileName.empty() && !alpha)
        return texture.fail("Static alpha unavailable: " + mInfo->mStaticAlphaFileName);
    if (alpha || mInfo->mClearAlpha || !mMaskLayerList.empty())
        for (S32 y = 0; y < target.getHeight(); ++y)
            for (S32 x = 0; x < target.getWidth(); ++x)
                target.getData()[(y * target.getWidth() + x) * 4 + 3] =
                    alpha ? unorm(sample(*alpha, (x + .5f) / target.getWidth(), (y + .5f) / target.getHeight(), true)[3]) : 255;
    for (auto* layer : mMaskLayerList)
        success &= layer->renderCPU(target, texture, true);
    if (success)
    {
        LLPointer<LLImageRaw> masks = texture.allocate(target.getWidth(), target.getHeight(), 1);
        if (!masks)
            return texture.fail("Bake morph-mask allocation refused");
        std::fill_n(masks->getData(), target.getWidth() * target.getHeight(), U8(255));
        for (const auto* layer : mLayerList)
            layer->gatherCPUAlphaMasks(masks->getData(), masks->getWidth(), masks->getHeight());
        mCPUMorphMask = masks;
    }
    return success;
}
