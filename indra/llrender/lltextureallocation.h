/** Mutable texture allocation ledger; contains no GL calls. */
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>

struct LLTextureAllocation
{
    struct Level
    {
        std::uint32_t width, height, format, layers;
        std::uint64_t bytes;
    };
    // Cube faces share a texture name, but have independent mip images.
    std::map<std::uint64_t, Level> levels;
    std::uint64_t bytes = 0;

    void set(std::uint32_t face, std::uint32_t mip, const Level& value)
    {
        const auto key = (std::uint64_t(face) << 32) | mip;
        auto it = levels.find(key);
        if (it != levels.end()) bytes -= it->second.bytes;
        levels[key] = value;
        bytes += value.bytes;
    }

    template<class Size>
    void generate(std::uint32_t max_mip, Size size)
    {
        // Inserting into a map preserves iterators. Only base levels start chains.
        for (auto it = levels.begin(); it != levels.end(); ++it)
        {
            if ((it->first & 0xffffffffULL) != 0) continue;
            const auto face = std::uint32_t(it->first >> 32);
            auto level = it->second;
            for (std::uint32_t mip = 1; mip <= max_mip && (level.width > 1 || level.height > 1); ++mip)
            {
                level.width = std::max(1u, level.width / 2);
                level.height = std::max(1u, level.height / 2);
                level.bytes = size(level.format, level.width, level.height) * level.layers;
                set(face, mip, level);
            }
        }
    }
};
