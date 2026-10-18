// Native indexed world geometry and submission ownership. LGPL-2.1.
#pragma once
#include "vsuirenderer.h"
#include "vsmaterial.h"
#include "vsenvironmentstate.h"
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
    struct Terrain
    {
        std::array<VSMaterial::State,4> layers;
        std::array<std::array<VSUIRenderer::Image,4>,4> images;
        VSUIRenderer::Image blend;
        float detail=0,planar=3,sharpness=8,regionWidth=256;
        bool paint=false;
    };
    // Numeric factors retain LLRender::eBlendFactor's stable protocol values.
    struct Blend
    {
        unsigned source=7,destination=9,sourceAlpha=0,destinationAlpha=9,writeMask=15;
        bool operator==(const Blend& b) const
        {return source==b.source && destination==b.destination && sourceAlpha==b.sourceAlpha && destinationAlpha==b.destinationAlpha && writeMask==b.writeMask;}
        bool operator!=(const Blend& b) const {return !(*this==b);}
    };
    struct AvatarTiming
    {
        std::array<std::uint8_t,16> identity{};
        std::function<bool()> current;
        std::function<void(float)> publish;
        std::shared_ptr<const void> residency;
    };
    enum class DepthCompare { LessEqual, Always, Greater, GreaterEqual };
    struct Packet
    {
        Mesh mesh;
        VSUIRenderer::Image image;
        VSWorldContracts::Sampler imageSampler;
        std::array<VSUIRenderer::Image, VSMaterial::ChannelCount-1> channels;
        std::shared_ptr<const VSMaterial::State> surface;
        std::array<float,4> color{1,1,1,1};
        bool fullbright = false;
        bool constantColor=false; // Original flat debug/highlight fragment output; ignores vertex tint.
        std::optional<VSMaterial::Transform> highlightUV; // Original textured highlightF; independent of lighting/vertex tint.
        bool alpha = false;
        std::optional<Blend> blend;
        bool masked = false;
        float               alphaCutoff     = .5f;
        bool                doubleSided     = false;
        bool cullFront=false;
        std::shared_ptr<const std::array<float,16>> projectionOverride;
        std::optional<bool> cull; // Preview passes retain their explicit original culling state.
        bool alphaOnly=false; // Reserved GL_ALPHA sources feed exclusion/debug passes, not ordinary scene color.
        bool glowOnly=false; // Original emissive pass; writes genuine separate floating-point glow.
        bool overlay=false; // Shared world feedback, after scene transparency and before HUD.
        float screenPointPixels=0; // Triangle-encoded square points, independent of optional largePoints.
        bool clearDepthBefore=false;
        std::optional<bool> depthWrite;
        float screenLinePixels=0; // Triangle-encoded lines; portable physical-pixel width.
        DepthCompare depthCompare=DepthCompare::LessEqual;
        bool depthTest=true; // Reference debug feedback can draw a translucent occluded pass.
        bool depthWriteAlpha=false; // Reference renderRect writes depth for its translucent walls.
        bool background = false; // Clip-space sky, drawn before depth-tested geometry.
        bool                hud             = false;
        bool                terrain         = false;
        std::shared_ptr<const Terrain> terrainMaterial;
        std::uint64_t       terrainKey      = 0;
        float distanceSquared = 0;
        // Region-local vertices never contain large grid coordinates. Apply the
        // current floating-origin translation at submission, without reupload.
        std::array<float,4> regionOffset{};
        std::shared_ptr<const AvatarTiming> avatarTiming;
        std::shared_ptr<const AvatarTiming> attachmentTiming;
        std::array<std::uint8_t,16> objectIdentity{};
        // Complete source identity for picking and UI feedback. Anonymous
        // preview geometry leaves this empty and cannot authorize object actions.
        std::array<std::uint64_t,5> objectSource{}; // address, port, local ID, region incarnation, account generation
        std::array<std::uint8_t,16> objectAccount{};
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
        bool wireframe = false;
        bool glow = true;
        bool preserveOrder = false; // Immediate preview controllers own their original pass order.
        std::optional<std::uint64_t> timingFrame;
        VSEnvironmentState environment;
    };
    struct SurfaceHit
    {
        float                fraction = 1;
        std::array<float, 3> point{}, normal{};
        std::array<float, 2> uv{};
        // CPU-only hit witnesses; GPU Vertex layout remains unchanged.
        std::array<float,4> tangent{};
        std::array<float,3> rawNormal{};
        std::array<unsigned,3> triangle{};
        std::array<float,3> barycentric{};
        bool triangleKnown=false;
    };
    struct Statistics
    {
        std::uint64_t draws=0,constantBuffersCreated=0,bindingsCreated=0;
        std::uint64_t meshUploads=0,meshUploadBytes=0;
        std::uint64_t constantUploadBytes=0,resourceCommits=0;
        std::uint64_t residentMeshBytes=0,meshBudgetBytes=0,meshAllocationRefusals=0;
        std::uint64_t meshInputRejections           = 0;
        std::uint64_t residentGeometrySnapshotBytes = 0;
        std::uint64_t avatarTimingQueries=0,avatarTimingRefusals=0,avatarTimingCompletions=0;
        bool avatarTimingSupported=false;
        double cpuMilliseconds=0;
    };
    VSWorldRenderer(Diligent::IRenderDevice*, Diligent::IDeviceContext*, std::size_t meshBudgetBytes = 256 * 1024 * 1024);
    ~VSWorldRenderer();
    Mesh upload(const std::vector<Vertex>&, const std::vector<unsigned>&, Topology = Topology::Triangles,
                unsigned attributes=VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0);
    // Refusal leaves the currently published generation intact.
    bool       publishMesh(Mesh&, const std::vector<Vertex>&, const std::vector<unsigned>&, Topology = Topology::Triangles,
                           unsigned attributes=VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0);
    void draw(Diligent::ITextureView*, unsigned width, unsigned height,
              const View&, const std::vector<Packet>&,const std::function<void()>& beforeHud = {});
    // Borrowed current world attachment. The UI submission retains its texture
    // through its own fence; a world resize cannot invalidate an active draw.
    Diligent::ITextureView* depthView() const;
    Diligent::ITextureView* glowView() const;
    void retire();
    bool pending() const;
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
