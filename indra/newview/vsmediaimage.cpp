// Shared native publication of borrowed media plugin pixels. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsmediaimage.h"
#include "vsmediapixels.h"
#include "vsuiresources.h"
#include "llpluginclassmedia.h"
#include "llimage.h"

LLUIImagePtr vs_update_media_image(VSUIResources& resources, const std::string& key, LLPluginClassMedia& plugin)
{
    auto previous = resources.facade(key);
    if (!plugin.textureValid()) return previous;
    const S32 width = plugin.getWidth(), height = plugin.getHeight();
    if (width <= 0 || height <= 0 || width > 16384 || height > 16384 ||
        plugin.getBitsWidth() < width || plugin.getBitsHeight() < height ||
        plugin.getBitsWidth() > 16384 || plugin.getBitsHeight() > 16384 || !plugin.getBitsData() ||
        plugin.getTextureDepth() != 4 || plugin.getTextureFormatType() != GL_UNSIGNED_BYTE ||
        plugin.getTextureFormatSwapBytes() ||
        (plugin.getTextureFormatInternal() != GL_RGB && plugin.getTextureFormatInternal() != GL_RGBA) ||
        (plugin.getTextureFormatPrimary() != GL_RGBA && plugin.getTextureFormatPrimary() != GL_BGRA))
        return previous;
    const auto image = resources.image(key);
    const bool resize = !image || VSUIRenderer::imageExtent(image) !=
        std::array<unsigned, 2>{unsigned(width), unsigned(height)};
    LLRect dirty;
    const bool changed = plugin.getDirty(&dirty);
    if (!resize && !changed) return previous;
    const bool bottom = plugin.getTextureCoordsOpenGL();
    const bool bgra = plugin.getTextureFormatPrimary() == GL_BGRA;
    const bool opaque = plugin.getTextureFormatInternal() == GL_RGB;
    bool accepted = false;
    if (resize)
    {
        auto pixels = vs_media_pixels(plugin.getBitsData(), width, height, plugin.getBitsWidth(),
                                      plugin.getBitsHeight(), bgra, bottom, opaque);
        LLPointer<LLImageRaw> raw = new LLImageRaw(width, height, 4);
        memcpy(raw->getData(), pixels.data(), pixels.size());
        auto next = resources.publish(key, *raw);
        if (next) { previous = next; accepted = true; }
    }
    else
    {
        auto patch = vs_media_patch(plugin.getBitsData(), width, height, plugin.getBitsWidth(),
                                    plugin.getBitsHeight(), bgra, bottom, opaque,
                                    dirty.mLeft, dirty.mBottom, dirty.mRight, dirty.mTop);
        if (patch.width && patch.height)
            accepted = resources.patch(key, patch.x, patch.y, patch.width, patch.height, patch.pixels);
        else accepted = true; // Fully clipped dirtiness cannot change visible pixels.
    }
    if (accepted) plugin.resetDirty();
    return previous;
}
