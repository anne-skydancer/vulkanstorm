/**
 * @file llvkuicache.h
 *
 * <VulkanStorm> Phase 3c M2: the UI texture cache. Maps an opaque key (the UI
 * texture's identity pointer) to a resident LLVKContext::Texture2D (RGBA8
 * UNORM, single mip, LINEAR+clamp). The caller (newview's funnel hook) supplies
 * already-expanded RGBA8 pixels; llvulkan stays free of the newview/texture
 * dependency. Mirrors the GL UI-texture model: pinned, identity-deduped.
 */

#ifndef LLVKUICACHE_H
#define LLVKUICACHE_H

#include "llvkcontext.h"

#include <unordered_map>
#include <vector>

class LLVKUITextureCache
{
public:
    // Resolve a key to its Vulkan descriptor (set 0 / binding 0). On a cache
    // miss, uploads the supplied RGBA8 pixels (rgba must be w*h*4). Returns
    // VK_NULL_HANDLE (caller falls back to the white texture) on bad input or
    // upload failure. Cached entries are pinned for the session.
    VkDescriptorSet resolve(LLVKContext* ctx, const void* key,
                            const uint8_t* rgba, uint32_t w, uint32_t h);

    // True when the key is already cached (no upload needed this frame).
    bool contains(const void* key) const { return mMap.find(key) != mMap.end(); }

    // Snapshot pre-expanded RGBA8 pixels for a key WITHOUT uploading (CPU-only,
    // safe to call off the render thread). The resolver uploads from this store
    // on the next bind, so the pixels survive the source raw image being freed.
    void storePixels(const void* key, const uint8_t* rgba, uint32_t w, uint32_t h);
    // True when pixels are staged for a key (waiting to upload).
    bool hasPixels(const void* key) const { return mPending.find(key) != mPending.end(); }

    // Drop a cached entry (e.g. when the source texture is discarded).
    void invalidate(const void* key);

    // Free all cached textures (call at session teardown, after device idle).
    void clear(LLVKContext* ctx);

private:
    struct StagedPixels { std::vector<uint8_t> rgba; uint32_t w = 0, h = 0; };
    std::unordered_map<const void*, LLVKContext::Texture2D> mMap;
    std::unordered_map<const void*, StagedPixels> mPending;
};

namespace LLVKUITexture
{
    LLVKUITextureCache& get();
}

#endif // LLVKUICACHE_H
