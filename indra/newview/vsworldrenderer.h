// Native indexed world geometry and submission ownership. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include <array>
#include <memory>
#include <vector>
namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; }
class VSWorldRenderer
{
public:
    struct Vertex { float x,y,z,nx,ny,nz,u,v; };
    struct Geometry;
    using Mesh = std::shared_ptr<const Geometry>;
    struct Packet
    {
        Mesh mesh;
        VSUIRenderer::Image image;
        std::array<float,4> color{1,1,1,1};
        bool fullbright = false;
        bool alpha = false;
        bool masked = false;
        bool background = false; // Clip-space sky, drawn before depth-tested geometry.
        float distanceSquared = 0;
    };
    struct View
    {
        // Column-major GL clip convention. Shader converts depth exactly once.
        std::array<float,16> projection{};
        std::array<float,4> sun{0,0,1,0};
        std::array<float,4> ambient{.25f,.25f,.25f,0};
    };
    VSWorldRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*);
    ~VSWorldRenderer();
    Mesh upload(const std::vector<Vertex>&, const std::vector<unsigned>&);
    void draw(Diligent::ITextureView*, unsigned width, unsigned height,
              const View&, const std::vector<Packet>&);
    void retire();
private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
