// Native indexed world geometry and submission ownership. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include "vsmaterial.h"
#include <array>
#include <cstddef>
#include <memory>
#include <vector>
#include <functional>
namespace Diligent { struct IRenderDevice; struct IDeviceContext; struct ITextureView; struct IBuffer; }
class VSWorldRenderer
{
public:
    struct Vertex
    {
        float x,y,z,nx,ny,nz,u,v;
        float u1=0,v1=0,tx=0,ty=0,tz=0,tw=0;
        float r=1,g=1,b=1,a=1;
    };
    enum class Topology
    {
        Triangles,
        Lines,
        Points
    };
    struct Geometry;
    using Mesh = std::shared_ptr<const Geometry>;
    struct Packet
    {
        Mesh mesh;
        VSUIRenderer::Image image;
        std::array<VSUIRenderer::Image, 3> channels;
        std::shared_ptr<const VSMaterial::State> surface;
        std::array<float,4> color{1,1,1,1};
        bool fullbright = false;
        bool alpha = false;
        bool masked = false;
        float               alphaCutoff     = .5f;
        bool                doubleSided     = false;
        bool background = false; // Clip-space sky, drawn before depth-tested geometry.
        bool                hud             = false;
        bool                terrain         = false;
        std::uint64_t       terrainKey      = 0;
        float distanceSquared = 0;
        // Region-local vertices never contain large grid coordinates. Apply the
        // current floating-origin translation at submission, without reupload.
        std::array<float,4> regionOffset{};
        std::array<std::uint8_t,16> objectIdentity{};
        std::uint64_t objectGeneration=0,faceGeneration=0;
        unsigned                    faceIndex = 0;
        int                         gltfNode = -1, gltfPrimitive = -1;
    };
    struct View
    {
        // Column-major GL clip convention. Shader converts depth exactly once.
        std::array<float,16> projection{};
        std::array<float,4> sun{0,0,1,0};
        std::array<float,4> ambient{.25f,.25f,.25f,0};
        float                hudZoom = 1;
    };
    struct SurfaceHit
    {
        float                fraction = 1;
        std::array<float, 3> point{}, normal{};
        std::array<float, 2> uv{};
    };
    struct Statistics
    {
        std::uint64_t draws=0,constantBuffersCreated=0,bindingsCreated=0;
        std::uint64_t meshUploads=0,meshUploadBytes=0;
        std::uint64_t constantUploadBytes=0,resourceCommits=0;
        std::uint64_t residentMeshBytes=0,meshBudgetBytes=0,meshAllocationRefusals=0;
        std::uint64_t meshInputRejections           = 0;
        std::uint64_t residentGeometrySnapshotBytes = 0;
        double cpuMilliseconds=0;
    };
    VSWorldRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*, std::size_t meshBudgetBytes = 256 * 1024 * 1024);
    ~VSWorldRenderer();
    Mesh upload(const std::vector<Vertex>&, const std::vector<unsigned>&, Topology = Topology::Triangles);
    // Refusal leaves the currently published generation intact.
    bool       publishMesh(Mesh&, const std::vector<Vertex>&, const std::vector<unsigned>&, Topology = Topology::Triangles);
    void draw(Diligent::ITextureView*, unsigned width, unsigned height,
              const View&, const std::vector<Packet>&);
    void retire();
    Statistics statistics() const;
    static VSWorldContracts::Geometry geometryDescription(const Mesh&);
    // Borrowed exact publication snapshots; retain the Mesh while using them.
    static const std::vector<Vertex>&   geometryVertices(const Mesh&);
    static const std::vector<unsigned>& geometryIndices(const Mesh&);
    // Borrowed buffers; callers retain the exact Mesh generation.
    static std::array<Diligent::IBuffer*, 2> geometryBuffers(const Mesh&);
    // Intersect the exact admitted GPU generation, including animated vertices.
    static bool intersect(const Mesh&, const std::array<float, 3>& start, const std::array<float, 3>& end, SurfaceHit&,
                          float lineRadius = 0, const std::function<bool(const SurfaceHit&)>& accept = {});
    static std::array<std::array<float, 3>, 2> bounds(const Mesh&);

private:
    struct Impl;
    std::unique_ptr<Impl> mImpl;
};
