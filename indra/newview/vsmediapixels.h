// Copy a borrowed plugin buffer into bottom-row-first RGBA. LGPL-2.1.
#pragma once
#include <cstdint>
#include <algorithm>
#include <stdexcept>
#include <vector>
inline std::vector<std::uint8_t> vs_media_pixels(const std::uint8_t* data, unsigned width, unsigned height, unsigned stride_pixels,
                                                 unsigned buffer_height, bool bgra, bool bottom_first, bool opaque = false)
{
    if (!data || !width || !height || width > stride_pixels || height > buffer_height || stride_pixels > 16384 || buffer_height > 16384)
        throw std::runtime_error("Invalid native media buffer extent");
    std::vector<std::uint8_t> rgba(std::size_t(width) * height * 4);
    for (unsigned y = 0; y < height; ++y)
    {
        const auto* row = data + std::size_t(bottom_first ? y : height - 1 - y) * stride_pixels * 4;
        auto*       out = rgba.data() + std::size_t(y) * width * 4;
        for (unsigned x = 0; x < width; ++x)
        {
            out[4 * x]     = row[4 * x + (bgra ? 2 : 0)];
            out[4 * x + 1] = row[4 * x + 1];
            out[4 * x + 2] = row[4 * x + (bgra ? 0 : 2)];
            out[4 * x + 3] = opaque ? 255 : row[4 * x + 3];
        }
    }
    return rgba;
}
struct VSMediaPatch
{
    unsigned x=0, y=0, width=0, height=0;
    std::vector<std::uint8_t> pixels;
};
// Dirty coordinates use the plugin buffer's own origin. Patch pixels and
// position use the native image's canonical top-left origin.
inline VSMediaPatch vs_media_patch(const std::uint8_t* data, unsigned width, unsigned height,
                                  unsigned stride, unsigned buffer_height, bool bgra, bool bottom_first,
                                  bool opaque, int left, int low, int right, int high)
{
    if (!data || !width || !height || width>stride || height>buffer_height || stride>16384 || buffer_height>16384)
        throw std::runtime_error("Invalid native media buffer extent");
    left=std::clamp(left,0,int(width));right=std::clamp(right,0,int(width));
    low=std::clamp(low,0,int(height));high=std::clamp(high,0,int(height));
    if (right<=left || high<=low) return {};
    VSMediaPatch patch{unsigned(left),bottom_first ? height-unsigned(high) : unsigned(low),
                       unsigned(right-left),unsigned(high-low),{}};
    patch.pixels.resize(std::size_t(patch.width)*patch.height*4);
    for (unsigned y=0;y<patch.height;++y)
    {
        const unsigned source_y=bottom_first ? unsigned(high)-1-y : unsigned(low)+y;
        const auto* source=data+(std::size_t(source_y)*stride+left)*4;
        auto* dest=patch.pixels.data()+std::size_t(y)*patch.width*4;
        for (unsigned x=0;x<patch.width;++x)
        {
            dest[4*x]=source[4*x+(bgra?2:0)];dest[4*x+1]=source[4*x+1];
            dest[4*x+2]=source[4*x+(bgra?0:2)];dest[4*x+3]=opaque?255:source[4*x+3];
        }
    }
    return patch;
}
