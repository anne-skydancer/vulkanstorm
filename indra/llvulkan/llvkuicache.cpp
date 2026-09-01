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

    if (!rgba || w == 0 || h == 0)
    {
        return VK_NULL_HANDLE;
    }

    LLVKContext::Texture2D tex;
    std::string error;
    if (!ctx->createTexture2D(rgba, w, h, tex, error))
    {
        LL_WARNS("Vulkan") << "UI texture cache: upload failed (" << error << ")" << LL_ENDL;
        return VK_NULL_HANDLE;
    }

    VkDescriptorSet descriptor = tex.descriptor;
    mMap.emplace(key, tex);
    return descriptor;
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
