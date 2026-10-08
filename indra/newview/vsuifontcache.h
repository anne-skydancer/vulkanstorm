// Lazy native glyph atlas; CPU rasterization and GPU publication are separate. LGPL-2.1.
#pragma once
#include "vsuifont.h"
#include "vsuiresources.h"
#include <memory>

class VSUIFontCache
{
public:
    struct Glyph
    {
        VSUIFont::Glyph metrics;
        LLUIImagePtr image;
        std::string page;
    };
    VSUIFontCache(VSUIResources&,const std::filesystem::path& font,unsigned page_size=256);
    ~VSUIFontCache();
    VSUIFontCache(const VSUIFontCache&)=delete;
    VSUIFontCache& operator=(const VSUIFontCache&)=delete;
    const Glyph& get(char32_t codepoint,unsigned pixel_size);
    void reset();
    std::size_t glyphCount() const;
    std::size_t pageCount() const;
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
