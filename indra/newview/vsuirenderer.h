// Vulkanstorm native UI substrate. LGPL-2.1, like the viewer.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <optional>

namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }

class VSUIRenderer
{
public:
    struct Texture;
    // A packet owns its exact generation, even after its producer replaces it.
    using Image = std::shared_ptr<const Texture>;
    enum class Blend { StraightAlpha, PremultipliedAlpha, Additive, AdditiveAlpha };
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
    VSUIRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*);
    ~VSUIRenderer();
    VSUIRenderer(const VSUIRenderer&) = delete;
    VSUIRenderer& operator=(const VSUIRenderer&) = delete;
    Image upload(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba);
    Image replace(const Image& old,unsigned x,unsigned y,unsigned width,unsigned height,
                  const std::vector<std::uint8_t>& rgba);
    // Submission order is paint order. Positive DPI scales logical coordinates.
    // The caller owns clear/present; no ambient GL state is consulted.
    void draw(Diligent::ITextureView* target, unsigned width, unsigned height,
              float dpi, const std::vector<Packet>& packets);
    // Nonblocking: release only submissions whose GPU fence has completed.
    // The owner waits for idle before destroying this renderer.
    void retire();
    static std::uint64_t generation(const Image&);
    // CPU publication snapshots for an independent diagnostic raster oracle.
    static const std::vector<std::uint8_t>& imagePixels(const Image&);
    static std::array<unsigned,2> imageExtent(const Image&);
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
