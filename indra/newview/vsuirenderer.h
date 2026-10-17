// Vulkanstorm native UI substrate. LGPL-2.1, like the viewer.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>
#include "vsworldcontracts.h"

namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }
namespace VSTextureUpload { struct Prepared; }

class VSUIRenderer
{
public:
    struct Texture;
    // A packet owns its resource, even after its producer replaces it. Ordinary
    // images are immutable generations; font atlas cells follow the shared cache.
    using Image = std::shared_ptr<const Texture>;
    // Font caches append glyphs to stable atlas cells, as in LLFontBitmapCache.
    // Only this handle permits in-place updates; ordinary images stay immutable.
    using FontAtlas = std::shared_ptr<Texture>;
    enum class Blend { StraightAlpha, PremultipliedAlpha, Additive, AdditiveAlpha, Replace };
    enum class Sampling { Nearest, Linear };
    struct Vertex { float x,y,u,v,r,g,b,a; };
    struct Packet
    {
        Image image;
        std::array<float, 4> bounds{}; // left, top, right, bottom in logical pixels
        std::array<float, 4> uv{0, 0, 1, 1};
        std::array<float, 4> color{1, 1, 1, 1};
        std::array<float, 4> clip{}; // top-left logical coordinates, half-open
        std::array<float, 6> transform{1, 0, 0, 1, 0, 0}; // affine, before DPI
        Blend blend = Blend::StraightAlpha;
        Sampling sampling = Sampling::Nearest;
        bool alpha_mask = false; // image alpha with tint RGB, like LLUIImage::drawSolid
        std::optional<std::array<Vertex,6>> triangles; // native font geometry, same logical coordinate space
    };
    struct Statistics { std::uint64_t packets=0,draws=0,vertices=0,residentImageBytes=0,imageBudgetBytes=0,imageAllocationRefusals=0; };
    VSUIRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*);
    ~VSUIRenderer();
    VSUIRenderer(const VSUIRenderer&) = delete;
    VSUIRenderer& operator=(const VSUIRenderer&) = delete;
    Image upload(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba,
                 VSWorldContracts::ImageRole role=VSWorldContracts::ImageRole::CoreUI,bool compress=false);
    Image uploadPrepared(const std::shared_ptr<const VSTextureUpload::Prepared>&);
    bool supportsCompression() const;
    Image replace(const Image& old,unsigned x,unsigned y,unsigned width,unsigned height,
                  const std::vector<std::uint8_t>& rgba);
    FontAtlas     createFontAtlas(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba);
    void          updateFontAtlas(const FontAtlas&, const std::vector<std::uint8_t>& rgba);
    // Submission order is paint order. Positive DPI scales logical coordinates.
    // The caller owns clear/present; no ambient GL state is consulted.
    void draw(Diligent::ITextureView* target, unsigned width, unsigned height,
              float dpi, const std::vector<Packet>& packets);
    // Nonblocking: release only submissions whose GPU fence has completed.
    // The owner waits for idle before destroying this renderer.
    void retire();
    // World-only clients enqueue upload completion before their own idle/fence
    // boundary. UI draw/retire calls do this automatically without waiting.
    void flushUploads();
    Statistics statistics() const;
    static std::uint64_t generation(const Image&);
    // CPU pixels for an independent diagnostic raster oracle. Font atlas pixels
    // are synchronized before the frame's draws and observer, like the GPU page.
    static const std::vector<std::uint8_t>& imagePixels(const Image&);
    static std::array<unsigned,2> imageExtent(const Image&);
#if defined(VS_VULKAN_DIAGNOSTICS) && VS_VULKAN_DIAGNOSTICS
    std::vector<std::uint8_t> readbackImage(const Image&);
#endif
    // Borrowed view; callers retain the exact Image generation through completion.
    static Diligent::ITextureView* imageView(const Image&);
    static Diligent::ITextureView* imageView(const Image&,const VSWorldContracts::Sampler&);
    static VSWorldContracts::Image imageDescription(const Image&);
    static bool imageOwnedBy(const Image&, const Diligent::IRenderDevice*);
    VSWorldContracts::Capabilities capabilities() const;
private:
    FontAtlas uploadTexture(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba, VSWorldContracts::ImageRole role,
                            bool fontAtlas);
    FontAtlas uploadPreparedTexture(const VSTextureUpload::Prepared&,bool fontAtlas);
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
