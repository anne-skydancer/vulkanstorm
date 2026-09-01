/**
 * @file llvkuicache.cpp
 *
 * <VulkanStorm> Phase 3c M2: UI texture cache implementation. See llvkuicache.h.
 * llvulkan only handles pre-expanded RGBA8 pixels; the caller (newview) owns
 * the LLTexture -> LLImageRaw -> RGBA8 expansion.
 */

#include "linden_common.h"

#include "llvkuicache.h"

#include "llerror.h"

VkDescriptorSet LLVKUITextureCache::resolve(LLVKContext* ctx, const void* key,
                                            const uint8_t* rgba, uint32_t w, uint32_t h)
{
    if (!ctx || !key)
    {
        return VK_NULL_HANDLE;
    }

    auto it = mMap.find(key);
    if (it != mMap.end())
    {
        return it->second.descriptor;
    }

    // Prefer staged pixels (snapshot at load-complete, survives the source raw
    // being freed). Fall back to caller-supplied pixels.
    const uint8_t* px = rgba;
    uint32_t pw = w, ph = h;
    auto pend = mPending.find(key);
    if (pend != mPending.end())
    {
        px = pend->second.rgba.data();
        pw = pend->second.w;
        ph = pend->second.h;
    }

    if (!px || pw == 0 || ph == 0)
    {
        return VK_NULL_HANDLE;
    }

    LLVKContext::Texture2D tex;
    std::string error;
    if (!ctx->createTexture2D(px, pw, ph, tex, error))
    {
        LL_WARNS("Vulkan") << "UI texture cache: upload failed (" << error << ")" << LL_ENDL;
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptor = tex.descriptor;
    mMap.emplace(key, tex);
    if (pend != mPending.end())
    {
        mPending.erase(pend); // uploaded; free the CPU copy
    }
    return descriptor;
}

void LLVKUITextureCache::storePixels(const void* key, const uint8_t* rgba, uint32_t w, uint32_t h)
{
    if (!key || !rgba || w == 0 || h == 0)
    {
        return;
    }
    StagedPixels sp;
    sp.w = w; sp.h = h;
    sp.rgba.assign(rgba, rgba + (size_t)w * h * 4);
    mPending[key] = std::move(sp);
}

void LLVKUITextureCache::invalidate(const void* key)
{
    mMap.erase(key);
}

void LLVKUITextureCache::clear(LLVKContext* ctx)
{
    if (!ctx) { mMap.clear(); return; }
    for (auto& kv : mMap)
    {
        LLVKContext::Texture2D tex = kv.second;
        ctx->destroyTexture2D(tex);
    }
    mMap.clear();
}

namespace LLVKUITexture
{
    LLVKUITextureCache& get()
    {
        static LLVKUITextureCache s_cache;
        return s_cache;
    }
}
