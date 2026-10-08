// CPU font rasterization; no GL or Vulkan resource ownership. LGPL-2.1.
#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

class VSUIFont
{
public:
    struct Glyph
    {
        unsigned width = 0, height = 0, index = 0;
        int left = 0, top = 0;
        float advance = 0;
        std::vector<std::uint8_t> coverage; // tightly packed, top row first
    };
    explicit VSUIFont(const std::filesystem::path& filename);
    ~VSUIFont();
    VSUIFont(const VSUIFont&) = delete;
    VSUIFont& operator=(const VSUIFont&) = delete;
    // Call on the owning CPU thread. Missing characters use FreeType's .notdef.
    Glyph rasterize(char32_t codepoint, unsigned pixel_size);
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
