// Lazy native glyph atlas; CPU rasterization and GPU publication are separate. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsuifontcache.h"
#include "llimage.h"
#include <atomic>
#include <map>
#include <stdexcept>

namespace { std::atomic<std::uint64_t> cache_serial{0}; }
struct VSUIFontCache::Impl
{
    VSUIResources& resources;
    VSUIFont font;
    unsigned size,x=1,y=1,row_height=0;
    std::uint64_t serial=++cache_serial,epoch=0;
    std::vector<std::string> pages;
    std::map<std::pair<char32_t,unsigned>,Glyph> glyphs;
    Impl(VSUIResources& r,const std::filesystem::path& f,unsigned s) : resources(r),font(f),size(s)
    { if (s<16 || s>4096) throw std::invalid_argument("Invalid native glyph atlas extent"); }
};
VSUIFontCache::VSUIFontCache(VSUIResources& r,const std::filesystem::path& f,unsigned s)
: mImpl(std::make_unique<Impl>(r,f,s)) {}
VSUIFontCache::~VSUIFontCache() { reset(); }
const VSUIFontCache::Glyph& VSUIFontCache::get(char32_t codepoint,unsigned pixel_size)
{
    auto& c=*mImpl;
    const auto key=std::make_pair(codepoint,pixel_size);
    auto found=c.glyphs.find(key); if (found!=c.glyphs.end()) return found->second;
    Glyph glyph; glyph.metrics=c.font.rasterize(codepoint,pixel_size);
    const auto width=glyph.metrics.width,height=glyph.metrics.height;
    if (!width || !height) return c.glyphs.emplace(key,std::move(glyph)).first->second;
    if (width+2>c.size || height+2>c.size) throw std::runtime_error("Glyph exceeds native atlas extent");
    if (c.x+width+1>c.size) { c.x=1; c.y+=c.row_height+1; c.row_height=0; }
    if (c.pages.empty() || c.y+height+1>c.size)
    {
        const auto page="native-glyph-"+std::to_string(c.serial)+"-"+std::to_string(c.epoch)+"-"+std::to_string(c.pages.size());
        LLPointer<LLImageRaw> raw=new LLImageRaw(c.size,c.size,4);
        raw->clear(255,255,255,0); c.resources.publish(page,*raw);
        c.pages.push_back(page); c.x=1;c.y=1;c.row_height=0;
    }
    std::vector<std::uint8_t> bytes(std::size_t(width)*height*4,255);
    for (std::size_t i=0;i<glyph.metrics.coverage.size();++i) bytes[i*4+3]=glyph.metrics.coverage[i];
    glyph.page=c.pages.back(); c.resources.patch(glyph.page,c.x,c.y,width,height,bytes);
    const float s=float(c.size);
    const LLRectf uv(c.x/s,1-c.y/s,(c.x+width)/s,1-(c.y+height)/s);
    glyph.image=c.resources.region(glyph.page,"native-glyph",uv,VSUIRenderer::Sampling::Nearest);
    c.x+=width+1;c.row_height=std::max(c.row_height,height);
    return c.glyphs.emplace(key,std::move(glyph)).first->second;
}
void VSUIFontCache::reset()
{
    auto& c=*mImpl;
    for (const auto& page:c.pages) c.resources.erase(page);
    c.glyphs.clear();c.pages.clear();c.x=1;c.y=1;c.row_height=0;++c.epoch;
}
std::size_t VSUIFontCache::glyphCount() const { return mImpl->glyphs.size(); }
std::size_t VSUIFontCache::pageCount() const { return mImpl->pages.size(); }
