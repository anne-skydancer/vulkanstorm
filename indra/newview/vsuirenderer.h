// Vulkanstorm native UI substrate. LGPL-2.1, like the viewer.
#pragma once
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }

class VSUIRenderer
{
public:
    struct Texture;
    // A packet owns its exact generation, even after its producer replaces it.
    using Image = std::shared_ptr<const Texture>;
    enum class Blend { StraightAlpha, PremultipliedAlpha };
    enum class Sampling { Nearest, Linear };
    struct Packet
    {
        Image image;
        std::array<float, 4> bounds; // left, top, right, bottom in logical pixels
        std::array<float, 4> uv{0, 0, 1, 1};
        std::array<float, 4> color{1, 1, 1, 1};
        std::array<float, 4> clip; // top-left logical coordinates, half-open
        std::array<float, 6> transform{1, 0, 0, 1, 0, 0}; // affine, before DPI
        Blend blend = Blend::StraightAlpha;
        Sampling sampling = Sampling::Nearest;
    };
    VSUIRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*);
    ~VSUIRenderer();
    VSUIRenderer(const VSUIRenderer&) = delete;
    VSUIRenderer& operator=(const VSUIRenderer&) = delete;
    Image upload(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba);
    // Submission order is paint order. Positive DPI scales logical coordinates.
    // The caller owns clear/present; no ambient GL state is consulted.
    void draw(Diligent::ITextureView* target, unsigned width, unsigned height,
              float dpi, const std::vector<Packet>& packets);
    // Nonblocking: release only submissions whose GPU fence has completed.
    // The owner waits for idle before destroying this renderer.
    void retire();
    static std::uint64_t generation(const Image&);
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
