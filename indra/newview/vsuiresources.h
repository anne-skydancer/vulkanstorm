// Viewer UI CPU assets, immutable publication and ordered facade draws. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include "lluiimage.h"
#include <memory>

class LLImageRaw;
class VSUIResources
{
public:
    explicit VSUIResources(VSUIRenderer&);
    ~VSUIResources();
    VSUIResources(const VSUIResources&) = delete;
    VSUIResources& operator=(const VSUIResources&) = delete;
    // LLImageRaw uses bottom-row-first storage; publication canonicalizes it.
    LLUIImagePtr publish(const std::string& key, const LLImageRaw&);
    LLUIImagePtr region(const std::string& key,const std::string& name,const LLRectf& uv,
                        VSUIRenderer::Sampling sampling=VSUIRenderer::Sampling::Linear);
    // Top-left canonical RGBA patch. Replacement never mutates queued packets.
    void patch(const std::string& key, unsigned x, unsigned y, unsigned width,
               unsigned height, const std::vector<std::uint8_t>& rgba);
    void begin(unsigned width, unsigned height, float dpi);
    void setClip(std::array<float,4> top_left_logical);
    std::vector<VSUIRenderer::Packet> finish();
    void erase(const std::string& key);
    std::uint64_t generation(const std::string& key) const;
    void fontBatch(LLImageRaw*,S32,const LLVector4a*,const LLVector2*,const LLColor4U*,S32);
    // End a font producer epoch; queued packets keep their GPU generations.
    void releaseFontPages();
    void triangle(const std::array<std::array<float, 6>, 3>& physical_bottom_left);
    void rectangle(const LLRectf& physical_bottom_left,const LLColor4&);
    void screenClip(const LLRect* logical_bottom_left);
private:
    struct Impl;
    std::shared_ptr<Impl> mImpl;
};
