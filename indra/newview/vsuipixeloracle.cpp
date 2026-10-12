// Independent CPU raster oracle for native viewer UI qualification. LGPL-2.1.
#include "vsuipixeloracle.h"
#include "llviewerprecompiledheaders.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace
{
using Vertex = VSUIRenderer::Vertex;
double edge(const Vertex &a, const Vertex &b, double x, double y)
{
    return (x - a.x) * (b.y - a.y) - (y - a.y) * (b.x - a.x);
}
bool top_left(const Vertex &a, const Vertex &b)
{
    return b.y > a.y || (b.y == a.y && b.x < a.x);
}
} // namespace
std::vector<std::uint8_t> vs_ui_expected_pixels(unsigned width, unsigned height, float dpi,
                                                const std::vector<VSUIRenderer::Packet> &packets, unsigned subpixel_bits,
                                                const std::vector<std::uint8_t>& background, VSUIBlendPrecision precision)
{
    std::vector<std::uint8_t> out(std::size_t(width) * height * 4);
    for (std::size_t i = 0; i < out.size(); i += 4)
    {
        out[i] = 16;
        out[i + 1] = 32;
        out[i + 2] = 48;
        out[i + 3] = 255;
    }
    if (!background.empty())
    {
        if (background.size() != out.size()) throw std::invalid_argument("UI oracle background extent mismatch");
        out = background;
    }
    for (const auto &p : packets)
    {
        const auto extent = VSUIRenderer::imageExtent(p.image);
        const auto &bytes = VSUIRenderer::imagePixels(p.image);
        std::array<Vertex, 6> vertices;
        if (p.triangles)
            vertices = *p.triangles;
        else
        {
            const Vertex a{p.bounds[0], p.bounds[1], p.uv[0], p.uv[1], p.color[0], p.color[1], p.color[2], p.color[3]},
                b{p.bounds[2], p.bounds[1], p.uv[2], p.uv[1], p.color[0], p.color[1], p.color[2], p.color[3]},
                c{p.bounds[0], p.bounds[3], p.uv[0], p.uv[3], p.color[0], p.color[1], p.color[2], p.color[3]},
                d{p.bounds[2], p.bounds[3], p.uv[2], p.uv[3], p.color[0], p.color[1], p.color[2], p.color[3]};
            vertices = {a, c, b, b, c, d};
        }
        for (auto &v : vertices)
        {
            const float x = v.x, y = v.y;
            const auto &t = p.transform;
            v.x = (t[0] * x + t[2] * y + t[4]) * dpi;
            v.y = (t[1] * x + t[3] * y + t[5]) * dpi;
        }
        const auto sample = [&](float u, float v, unsigned component) {
            auto pixel = [&](int x, int y) {
                x = std::clamp(x, 0, int(extent[0]) - 1);
                y = std::clamp(y, 0, int(extent[1]) - 1);
                return double(bytes[(std::size_t(y) * extent[0] + x) * 4 + component]) / 255;
            };
            if (p.sampling == VSUIRenderer::Sampling::Nearest)
                return pixel(int(std::floor(u * extent[0])), int(std::floor(v * extent[1])));
            const double x = u * extent[0] - .5, y = v * extent[1] - .5;
            const int ix = int(std::floor(x)), iy = int(std::floor(y));
            const double fx = x - ix, fy = y - iy;
            return (pixel(ix, iy) * (1 - fx) + pixel(ix + 1, iy) * fx) * (1 - fy) +
                   (pixel(ix, iy + 1) * (1 - fx) + pixel(ix + 1, iy + 1) * fx) * fy;
        };
        for (unsigned first : {0u, 3u})
        {
            Vertex a = vertices[first], b = vertices[first + 1], c = vertices[first + 2];
            Vertex original_a=a, original_b=b, original_c=c;
            if (subpixel_bits)
            {
                // Coverage uses the device's advertised fixed-point raster grid.
                // Keep unrounded positions for varying interpolation; subpixel
                // coverage must not change texture coordinates or relax RGB checks.
                const double grid=std::ldexp(1.0,int(subpixel_bits));
                for (auto* v:{&a,&b,&c})
                {
                    v->x=float(std::round(v->x*grid)/grid);
                    v->y=float(std::round(v->y*grid)/grid);
                }
            }
            double area = edge(a, b, c.x, c.y);
            if (area == 0)
                continue;
            if (area < 0)
            {
                std::swap(b, c);
                std::swap(original_b, original_c);
                area = -area;
            }
            const int l = std::max({0, int(std::floor(std::min({a.x, b.x, c.x}))), int(std::floor(p.clip[0] * dpi))}),
                      r = std::min(
                          {int(width), int(std::ceil(std::max({a.x, b.x, c.x}))), int(std::ceil(p.clip[2] * dpi))}),
                      t = std::max({0, int(std::floor(std::min({a.y, b.y, c.y}))), int(std::floor(p.clip[1] * dpi))}),
                      bottom = std::min(
                          {int(height), int(std::ceil(std::max({a.y, b.y, c.y}))), int(std::ceil(p.clip[3] * dpi))});
            for (int y = t; y < bottom; ++y)
                for (int x = l; x < r; ++x)
                {
                    const double e0 = edge(b, c, x + .5, y + .5), e1 = edge(c, a, x + .5, y + .5),
                                 e2 = edge(a, b, x + .5, y + .5);
                    if (e0 < 0 || e1 < 0 || e2 < 0 || (e0 == 0 && !top_left(b, c)) || (e1 == 0 && !top_left(c, a)) ||
                        (e2 == 0 && !top_left(a, b)))
                        continue;
                    const double original_area=edge(original_a,original_b,original_c.x,original_c.y);
                    const double w0=edge(original_b,original_c,x+.5,y+.5),
                                 w1=edge(original_c,original_a,x+.5,y+.5),
                                 w2=edge(original_a,original_b,x+.5,y+.5);
                    auto interpolate = [&](float Vertex::*m) {
                        return float((w0 * (original_a.*m) + w1 * (original_b.*m) + w2 * (original_c.*m)) / original_area);
                    };
                    const auto u = interpolate(&Vertex::u), v = interpolate(&Vertex::v);
                    const double alpha = sample(u, v, 3) * interpolate(&Vertex::a);
                    auto *dst = out.data() + (std::size_t(y) * width + x) * 4;
                    const float colors[]{interpolate(&Vertex::r), interpolate(&Vertex::g), interpolate(&Vertex::b)};
                    for (unsigned channel = 0; channel < 3; ++channel)
                    {
                        const double src = (p.alpha_mask ? 1 : sample(u, v, channel)) * colors[channel];
                        const bool additive =
                            p.blend == VSUIRenderer::Blend::Additive || p.blend == VSUIRenderer::Blend::AdditiveAlpha;
                        const bool unmodulated = p.blend == VSUIRenderer::Blend::PremultipliedAlpha ||
                                                 p.blend == VSUIRenderer::Blend::Additive || p.blend == VSUIRenderer::Blend::Replace;
                        const double source = src * (unmodulated ? 1 : alpha) * 255;
                        const double destination = dst[channel] * (p.blend == VSUIRenderer::Blend::Replace ? 0 : additive ? 1 : 1 - alpha);
                        // Vulkan basic blend precision need not exceed the destination
                        // format (framebuffer.html#framebuffer-blending). Keep both
                        // independent arithmetic paths: rounding only the sum, or
                        // each product in RGBA8 destination units before addition.
                        const long value = precision == VSUIBlendPrecision::DestinationProducts
                            ? std::lround(source) + std::lround(destination)
                            : std::lround(source + destination);
                        dst[channel] = std::uint8_t(std::clamp(value, 0l, 255l));
                    }
                    const double sourceAlpha = alpha * 255;
                    const double destinationAlpha = p.blend == VSUIRenderer::Blend::Replace ? 0 : dst[3] * (1 - alpha);
                    const long outputAlpha = precision == VSUIBlendPrecision::DestinationProducts
                        ? std::lround(sourceAlpha) + std::lround(destinationAlpha)
                        : std::lround(sourceAlpha + destinationAlpha);
                    dst[3] = std::uint8_t(std::clamp(outputAlpha, 0l, 255l));
                }
        }
    }
    return out;
}

bool vs_ui_pixel_matches(const std::uint8_t* actual, const std::uint8_t* floating,
                         const std::uint8_t* destination, unsigned tolerance)
{
    unsigned floatingError = 0, destinationError = 0;
    for (unsigned c = 0; c < 4; ++c)
    {
        floatingError = std::max(floatingError, unsigned(std::abs(int(actual[c]) - int(floating[c]))));
        destinationError = std::max(destinationError, unsigned(std::abs(int(actual[c]) - int(destination[c]))));
    }
    // All RGBA components must match one complete reference, never a mixture.
    return std::min(floatingError, destinationError) <= tolerance;
}
