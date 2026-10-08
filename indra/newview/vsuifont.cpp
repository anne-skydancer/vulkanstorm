// CPU font rasterization; no GL or Vulkan resource ownership. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsuifont.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <stdexcept>

struct VSUIFont::Impl
{
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    std::vector<unsigned char> bytes;
    ~Impl() { if (face) FT_Done_Face(face); if (library) FT_Done_FreeType(library); }
};
VSUIFont::VSUIFont(const std::filesystem::path& filename) : mImpl(std::make_unique<Impl>())
{
    std::ifstream input(filename, std::ios::binary | std::ios::ate);
    const auto length = input.tellg();
    if (!input || length <= 0 || length > std::numeric_limits<FT_Long>::max())
        throw std::runtime_error("Cannot read native UI font");
    mImpl->bytes.resize(std::size_t(length)); input.seekg(0);
    if (!input.read(reinterpret_cast<char*>(mImpl->bytes.data()), length) ||
        FT_Init_FreeType(&mImpl->library) ||
        FT_New_Memory_Face(mImpl->library,mImpl->bytes.data(),FT_Long(length),0,&mImpl->face) ||
        FT_Select_Charmap(mImpl->face,FT_ENCODING_UNICODE))
        throw std::runtime_error("Cannot initialize native UI Unicode font");
}
VSUIFont::~VSUIFont() = default;
VSUIFont::Glyph VSUIFont::rasterize(char32_t codepoint, unsigned pixel_size)
{
    if (!pixel_size || pixel_size > 4096 || codepoint > 0x10ffff || (codepoint >= 0xd800 && codepoint <= 0xdfff))
        throw std::runtime_error("Invalid native UI glyph request");
    auto face=mImpl->face;
    Glyph result; result.index=FT_Get_Char_Index(face,FT_ULong(codepoint));
    if (FT_Set_Pixel_Sizes(face,0,pixel_size) ||
        FT_Load_Glyph(face,result.index,FT_LOAD_DEFAULT) || FT_Render_Glyph(face->glyph,FT_RENDER_MODE_NORMAL))
        throw std::runtime_error("Native UI glyph rasterization failed");
    const auto& bitmap=face->glyph->bitmap;
    if (bitmap.width && bitmap.rows && (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY || bitmap.num_grays != 256))
        throw std::runtime_error("Unsupported native UI glyph coverage format");
    result.width=bitmap.width; result.height=bitmap.rows;
    result.left=face->glyph->bitmap_left; result.top=face->glyph->bitmap_top;
    result.advance=float(face->glyph->advance.x)/64;
    result.coverage.resize(std::size_t(result.width)*result.height);
    if (!result.width || !result.height) return result;
    for (unsigned y=0; y<result.height; ++y)
    {
        const auto* row=bitmap.buffer + (bitmap.pitch >= 0 ? y : result.height-1-y)*std::abs(bitmap.pitch);
        std::copy_n(row,result.width,result.coverage.data()+std::size_t(y)*result.width);
    }
    return result;
}
