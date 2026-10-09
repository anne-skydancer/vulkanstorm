// Copy a borrowed plugin buffer into bottom-row-first RGBA. LGPL-2.1.
#pragma once
#include <cstdint>
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
