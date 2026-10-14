// Native indexed world geometry and submission ownership. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldrenderer.h"
#include "vsresourcebudget.h"
#pragma push_macro("Bool")
#pragma push_macro("True")
#pragma push_macro("False")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef True
#undef False
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("False")
#pragma pop_macro("True")
#pragma pop_macro("Bool")
#include <algorithm>
#include <cmath>
#include <deque>
#include <cstddef>
#include <cstring>
#include <chrono>
#include <map>
#include <stdexcept>
using namespace Diligent;
namespace
{
void requireWorld(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
struct alignas(16) Constants
{
    std::array<float,16> projection;
    std::array<float,4> sun,ambient,color,material,regionOffset;
};
static_assert(sizeof(Constants)==144);
static_assert(sizeof(VSWorldRenderer::Vertex)==32);
void checkWorldABI(IShader& shader)
{
    bool found=false;
    for (Uint32 i=0;i<shader.GetResourceCount();++i)
    {
        ShaderResourceDesc resource{};shader.GetResourceDesc(i,resource);
        if (!resource.Name || std::strcmp(resource.Name,"WorldView")) continue;
        const auto* buffer=shader.GetConstantBufferDesc(i);
        requireWorld(buffer && buffer->Size==sizeof(Constants) && buffer->NumVariables==6,"World uniform block ABI mismatch");
        const char* names[]{"projection","sun","ambient","tint","material","regionOffset"};
        const std::size_t offsets[]{offsetof(Constants,projection),offsetof(Constants,sun),offsetof(Constants,ambient),offsetof(Constants,color),offsetof(Constants,material),offsetof(Constants,regionOffset)};
        for (unsigned v=0;v<6;++v)
        {
            const auto& variable=buffer->pVariables[v];
            requireWorld(variable.Name && !std::strcmp(variable.Name,names[v]) && variable.Offset==offsets[v],"World uniform member ABI mismatch");
        }
        found=true;
    }
    requireWorld(found,"World shader reflection is missing its view block");
}
}
struct VSWorldRenderer::Geometry
{
    RefCntAutoPtr<IBuffer> vertices, indices;
    RefCntAutoPtr<IRenderDevice> owner;
    unsigned count = 0;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    std::shared_ptr<const VSResourceBudget::Reservation> snapshotResidency;
    std::vector<Vertex>                                  cpuVertices;
    std::vector<unsigned>                                cpuIndices;
    std::array<std::array<float, 3>, 2>                  bounds{};
    VSWorldContracts::Geometry description;
    Topology                                             topology = Topology::Triangles;
};
struct VSWorldRenderer::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<IShader> vertex, fragment;
    RefCntAutoPtr<IPipelineState> pipelines[2][3][3];
    RefCntAutoPtr<ITexture> depth, white;
    RefCntAutoPtr<IFence> fence;
    Uint64 next = 1,nextGeometryGeneration=1;
    struct DrawResources
    {
        RefCntAutoPtr<IBuffer> constants;
        RefCntAutoPtr<IShaderResourceBinding> binding;
        IPipelineState* pipeline=nullptr; // Owned by this renderer's pipeline table.
    };
    std::map<IPipelineState*,std::vector<DrawResources>> available;
    std::size_t availableCount=0;
    Statistics statistics;
    VSResourceBudget meshBudget;
    VSResourceBudget                                     snapshotBudget;
    Uint64 pendingMeshUploads=0,pendingMeshBytes=0,meshAllocationRefusals=0;
    Uint64                                               meshInputRejections = 0;
    struct Submission
    {
        Uint64 completion = 0;
        std::vector<Packet> packets;
        std::vector<DrawResources> draws;
        RefCntAutoPtr<ITexture> depth;
    };
    std::deque<Submission> submitted;
    Impl(IRenderDevice* d, IDeviceContext* c, std::size_t meshBudgetBytes) :
        device(d),
        context(c),
        meshBudget(meshBudgetBytes),
        snapshotBudget(meshBudgetBytes)
    {
        requireWorld(d && c,"World renderer requires a device and context");
        FenceDesc fd{};fd.Name="Native world completion";
        device->CreateFence(fd,&fence);requireWorld(fence!=nullptr,"World completion fence failed");
        TextureDesc td{};td.Name="Native world missing-texture white";
        td.Type=RESOURCE_DIM_TEX_2D;td.Width=td.Height=1;td.Format=TEX_FORMAT_RGBA8_UNORM;
        td.Usage=USAGE_IMMUTABLE;td.BindFlags=BIND_SHADER_RESOURCE;
        const Uint32 pixel=0xffffffff;
        TextureSubResData sr{};sr.pData=&pixel;sr.Stride=4;TextureData data{&sr,1};
        device->CreateTexture(td,&data,&white);requireWorld(white!=nullptr,"World fallback texture failed");
    }
    IPipelineState* pipeline(TEXTURE_FORMAT format, bool alpha, bool background, Topology topology)
    {
        requireWorld(format==TEX_FORMAT_RGBA8_UNORM || format==TEX_FORMAT_BGRA8_UNORM,"Unsupported world target format");
        auto& result = pipelines[format == TEX_FORMAT_BGRA8_UNORM][background ? 2 : unsigned(alpha)][unsigned(topology)];
        if (result) return result;
        const char* vs = R"(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; };
layout(location=0) out vec2 texUV;
layout(location=1) out vec3 light;
void main() {
    vec4 p=material.w>0.5 ? vec4(position,1) : projection*vec4(position+regionOffset.xyz,1);
    p.z=(p.z+p.w)*0.5;
    gl_Position=p; texUV=uv;
    gl_PointSize=1.0;
    light=material.x>0.5 ? vec3(1) : ambient.rgb+(vec3(1)-ambient.rgb)*max(dot(normalize(normal),normalize(sun.xyz)),0.0);
}
)";
        const char* fs=R"(
layout(location=0) in vec2 texUV;
layout(location=1) in vec3 light;
layout(location=0) out vec4 result;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; };
uniform sampler2D g_Texture;
void main() {
    if (material.w>0.5) {
        result=vec4(mix(vec3(0.65,0.8,0.95),vec3(0.08,0.22,0.48),clamp(texUV.y,0.0,1.0)),1);
        return;
    }
    vec4 c=texture(g_Texture,texUV)*tint;
    if (material.y>0.5 && c.a<material.z) discard;
    result=vec4(c.rgb*light,c.a);
}
)";
        if (!vertex)
        {
            ShaderCreateInfo shader{};shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_GLSL;
            shader.LoadConstantBufferReflection=true;
            shader.GLSLVersion={4,5};shader.EntryPoint="main";shader.Desc.UseCombinedTextureSamplers=true;
            shader.Desc.Name="Native world vertex";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.Source=vs;
            device->CreateShader(shader,&vertex);
            shader.Desc.Name="Native world fragment";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.Source=fs;
            device->CreateShader(shader,&fragment);
            requireWorld(vertex && fragment,"World shader creation failed");
            checkWorldABI(*vertex);checkWorldABI(*fragment);
        }
        GraphicsPipelineStateCreateInfo ci{};ci.PSODesc.Name="Native world geometry";
        ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vertex;ci.pPS=fragment;
        auto& gp=ci.GraphicsPipeline;gp.NumRenderTargets=1;gp.RTVFormats[0]=format;
        gp.DSVFormat                                                       = TEX_FORMAT_D32_FLOAT;
        gp.PrimitiveTopology           = topology == Topology::Triangles ? PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                         : topology == Topology::Lines   ? PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                         : PRIMITIVE_TOPOLOGY_POINT_LIST;
        gp.RasterizerDesc.CullMode=CULL_MODE_NONE;
        gp.DepthStencilDesc.DepthEnable=!background;gp.DepthStencilDesc.DepthWriteEnable=!alpha && !background;
        gp.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS_EQUAL;
        const LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false}};
        gp.InputLayout.LayoutElements=layout;gp.InputLayout.NumElements=3;
        auto& blend=gp.BlendDesc.RenderTargets[0];blend.BlendEnable=alpha;
        blend.SrcBlend=BLEND_FACTOR_SRC_ALPHA;blend.DestBlend=BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha=BLEND_FACTOR_ONE;blend.DestBlendAlpha=BLEND_FACTOR_INV_SRC_ALPHA;
        ci.PSODesc.ResourceLayout.DefaultVariableType=SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        SamplerDesc sampler{};sampler.MinFilter=sampler.MagFilter=FILTER_TYPE_LINEAR;
        sampler.MipFilter=FILTER_TYPE_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=TEXTURE_ADDRESS_WRAP;
        ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL,"g_Texture",sampler};
        ci.PSODesc.ResourceLayout.NumImmutableSamplers=1;ci.PSODesc.ResourceLayout.ImmutableSamplers=&immutable;
        device->CreateGraphicsPipelineState(ci,&result);requireWorld(result!=nullptr,"World pipeline creation failed");
        return result;
    }
};
VSWorldRenderer::VSWorldRenderer(IRenderDevice* d,IDeviceContext* c,std::size_t meshBudgetBytes):mImpl(std::make_unique<Impl>(d,c,meshBudgetBytes)) {}
VSWorldRenderer::~VSWorldRenderer()=default;
VSWorldRenderer::Mesh VSWorldRenderer::upload(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, Topology topology)
{
    const unsigned group = topology == Topology::Triangles ? 3 : topology == Topology::Lines ? 2 : 1;
    const auto     reject = [&]() -> Mesh
    {
        ++mImpl->meshInputRejections;
        return {};
    };
    if (vertices.empty() || vertices.size() > 1048576 || indices.empty() || indices.size() > 3145728 || indices.size() % group)
        return reject();
    for (const auto& v:vertices)
        for (float f : { v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v })
            if (!std::isfinite(f))
                return reject();
    for (auto i : indices)
        if (i >= vertices.size())
            return reject();
    auto residency=mImpl->meshBudget.reserve(vertices.size()*sizeof(Vertex)+indices.size()*sizeof(unsigned));
    if (!residency) { ++mImpl->meshAllocationRefusals;return {}; }
    auto snapshotResidency = mImpl->snapshotBudget.reserve(vertices.size() * sizeof(Vertex) + indices.size() * sizeof(unsigned));
    if (!snapshotResidency)
    {
        ++mImpl->meshAllocationRefusals;
        return {};
    }
    auto mesh=std::make_shared<Geometry>();mesh->owner=mImpl->device;mesh->count=unsigned(indices.size());
    mesh->topology                                                              = topology;
    mesh->description.vertices=vertices.size();mesh->description.indices=indices.size();
    mesh->description.generation=mImpl->nextGeometryGeneration++;
    mesh->residency=std::move(residency);
    mesh->snapshotResidency                                             = std::move(snapshotResidency);
    mesh->cpuVertices                                                   = vertices;
    mesh->cpuIndices                                                    = indices;
    const auto& first                                                   = vertices[indices[0]];
    mesh->bounds[0] = mesh->bounds[1] = { first.x, first.y, first.z };
    for (const auto index : indices)
    {
        const auto& vertex = vertices[index];
        const float point[]{ vertex.x, vertex.y, vertex.z };
        for (unsigned axis = 0; axis < 3; ++axis)
        {
            mesh->bounds[0][axis] = std::min(mesh->bounds[0][axis], point[axis]);
            mesh->bounds[1][axis] = std::max(mesh->bounds[1][axis], point[axis]);
        }
    }
    BufferDesc bd{};bd.Name="Native world vertex generation";bd.Size=vertices.size()*sizeof(Vertex);
    bd.Usage=USAGE_IMMUTABLE;bd.BindFlags=BIND_VERTEX_BUFFER;
    BufferData data{vertices.data(),bd.Size};mImpl->device->CreateBuffer(bd,&data,&mesh->vertices);
    bd.Name="Native world index generation";bd.Size=indices.size()*sizeof(unsigned);bd.BindFlags=BIND_INDEX_BUFFER;
    data.pData=indices.data();data.DataSize=bd.Size;mImpl->device->CreateBuffer(bd,&data,&mesh->indices);
    requireWorld(mesh->vertices && mesh->indices,"World geometry upload failed");
    ++mImpl->pendingMeshUploads;mImpl->pendingMeshBytes+=vertices.size()*sizeof(Vertex)+indices.size()*sizeof(unsigned);
    return mesh;
}
bool VSWorldRenderer::publishMesh(Mesh& current, const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices,
                                  Topology topology)
{
    auto replacement = upload(vertices, indices, topology);
    if (!replacement) return false;
    current = std::move(replacement);
    return true;
}
void VSWorldRenderer::retire()
{
    const auto complete=mImpl->fence->GetCompletedValue();
    while (!mImpl->submitted.empty() && mImpl->submitted.front().completion && mImpl->submitted.front().completion<=complete)
    {
        // Reuse only resources whose GPU submission has completed. Keep a
        // bounded cache; packets/images/geometry retire with the submission.
        for (auto& draw:mImpl->submitted.front().draws)
            if (mImpl->availableCount<16384)
            {
                // A cached SRB owns its bound texture independently of Packet.image.
                // Remove that reference while the charged packet owner still lives.
                auto* texture = draw.binding->GetVariableByName(SHADER_TYPE_PIXEL, "g_Texture");
                requireWorld(texture != nullptr, "Retired world texture binding missing");
                texture->Set(mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                             SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                mImpl->available[draw.pipeline].push_back(std::move(draw));
                ++mImpl->availableCount;
            }
        mImpl->submitted.pop_front();
    }
}
VSWorldRenderer::Statistics VSWorldRenderer::statistics() const
{
    auto result=mImpl->statistics;
    result.residentMeshBytes=mImpl->meshBudget.used();result.meshBudgetBytes=mImpl->meshBudget.limit();
    result.meshAllocationRefusals=mImpl->meshAllocationRefusals;
    result.meshInputRejections                                              = mImpl->meshInputRejections;
    result.residentGeometrySnapshotBytes                                    = mImpl->snapshotBudget.used();
    return result;
}
VSWorldContracts::Geometry VSWorldRenderer::geometryDescription(const Mesh& mesh)
{ return mesh?mesh->description:VSWorldContracts::Geometry{}; }
const std::vector<VSWorldRenderer::Vertex>& VSWorldRenderer::geometryVertices(const Mesh& mesh)
{
    static const std::vector<Vertex> empty;
    return mesh ? mesh->cpuVertices : empty;
}
const std::vector<unsigned>& VSWorldRenderer::geometryIndices(const Mesh& mesh)
{
    static const std::vector<unsigned> empty;
    return mesh ? mesh->cpuIndices : empty;
}
std::array<IBuffer*, 2> VSWorldRenderer::geometryBuffers(const Mesh& mesh)
{ return mesh ? std::array<IBuffer*, 2>{mesh->vertices.RawPtr(), mesh->indices.RawPtr()} : std::array<IBuffer*, 2>{}; }
std::array<std::array<float, 3>, 2> VSWorldRenderer::bounds(const Mesh& mesh)
{ return mesh ? mesh->bounds : std::array<std::array<float, 3>, 2>{}; }
bool VSWorldRenderer::intersect(const Mesh& mesh, const std::array<float, 3>& start, const std::array<float, 3>& end, SurfaceHit& hit,
                                float lineRadius, const std::function<bool(const SurfaceHit&)>& accept)
{
    if (!mesh || !std::isfinite(lineRadius) || lineRadius < 0)
        return false;
    using Vector  = std::array<float, 3>;
    auto subtract = [](const Vector& a, const Vector& b) -> Vector
    {
        return { a[0] - b[0], a[1] - b[1], a[2] - b[2] };
    };
    auto dot = [](const Vector& a, const Vector& b)
    {
        return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
    };
    auto cross = [](const Vector& a, const Vector& b) -> Vector
    {
        return { a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0] };
    };
    const Vector direction = subtract(end, start);
    float        clipNear = 0, clipFar = 1;
    for (unsigned axis = 0; axis < 3; ++axis)
    {
        if (!std::isfinite(start[axis]) || !std::isfinite(end[axis]))
            return false;
        const float lo = mesh->bounds[0][axis] - lineRadius, hi = mesh->bounds[1][axis] + lineRadius;
        if (std::abs(direction[axis]) < 1.e-12f)
        {
            if (start[axis] < lo || start[axis] > hi)
                return false;
        }
        else
        {
            float a = (lo - start[axis]) / direction[axis], b = (hi - start[axis]) / direction[axis];
            if (a > b)
                std::swap(a, b);
            clipNear = std::max(clipNear, a);
            clipFar  = std::min(clipFar, b);
            if (clipNear > clipFar)
                return false;
        }
    }
    bool  found   = false;
    float closest = 1;
    auto  point   = [](const Vertex& v) -> Vector
    {
        return { v.x, v.y, v.z };
    };
    const auto& vertices = mesh->cpuVertices;
    const auto& indices  = mesh->cpuIndices;
    if (mesh->topology == Topology::Triangles)
    {
        for (std::size_t i = 0; i < indices.size(); i += 3)
        {
            const auto&  a     = vertices[indices[i]];
            const auto&  b     = vertices[indices[i + 1]];
            const auto&  c     = vertices[indices[i + 2]];
            const Vector edge1 = subtract(point(b), point(a)), edge2 = subtract(point(c), point(a));
            const Vector p           = cross(direction, edge2);
            const float  determinant = dot(edge1, p);
            if (std::abs(determinant) < 1.e-12f)
                continue;
            const Vector t = subtract(start, point(a));
            const float  u = dot(t, p) / determinant;
            if (u < 0 || u > 1)
                continue;
            const Vector q = cross(t, edge1);
            const float  v = dot(direction, q) / determinant;
            if (v < 0 || u + v > 1)
                continue;
            const float fraction = dot(edge2, q) / determinant;
            if (fraction < 0 || fraction > closest)
                continue;
            SurfaceHit candidate;
            candidate.fraction = fraction;
            for (unsigned axis = 0; axis < 3; ++axis)
                candidate.point[axis] = start[axis] + fraction * direction[axis];
            candidate.normal   = { (1 - u - v) * a.nx + u * b.nx + v * c.nx, (1 - u - v) * a.ny + u * b.ny + v * c.ny,
                                   (1 - u - v) * a.nz + u * b.nz + v * c.nz };
            const float length = std::sqrt(dot(candidate.normal, candidate.normal));
            if (length > 0)
                for (float& value : candidate.normal)
                    value /= length;
            candidate.uv = { (1 - u - v) * a.u + u * b.u + v * c.u, (1 - u - v) * a.v + u * b.v + v * c.v };
            if (accept && !accept(candidate))
                continue;
            closest = fraction;
            found   = true;
            hit     = candidate;
        }
    }
    else if (lineRadius > 0)
    {
        const float length = dot(direction, direction);
        if (!(length > 0))
            return false;
        const unsigned group = mesh->topology == Topology::Lines ? 2 : 1;
        for (std::size_t i = 0; i < indices.size(); i += group)
        {
            const auto&  a    = vertices[indices[i]];
            const auto&  b    = vertices[indices[i + group - 1]];
            const Vector edge = subtract(point(b), point(a)), offset = subtract(start, point(a));
            const float  edgeLength = dot(edge, edge), d = dot(direction, edge), s = dot(direction, offset), t = dot(edge, offset);
            const float  denominator = length * edgeLength - d * d;
            float        along       = denominator > 1.e-12f ? std::clamp((length * t - d * s) / denominator, 0.f, 1.f) : 0;
            float        fraction    = std::clamp((d * along - s) / length, 0.f, 1.f);
            if (edgeLength > 0)
                along = std::clamp((t + d * fraction) / edgeLength, 0.f, 1.f);
            fraction = std::clamp((d * along - s) / length, 0.f, 1.f);
            Vector delta{};
            for (unsigned axis = 0; axis < 3; ++axis)
                delta[axis] = start[axis] + fraction * direction[axis] - (point(a)[axis] + along * edge[axis]);
            if (dot(delta, delta) > lineRadius * lineRadius || fraction > closest)
                continue;
            SurfaceHit candidate;
            candidate.fraction = fraction;
            for (unsigned axis = 0; axis < 3; ++axis)
                candidate.point[axis] = point(a)[axis] + along * edge[axis];
            candidate.normal = { a.nx, a.ny, a.nz };
            candidate.uv     = { a.u + along * (b.u - a.u), a.v + along * (b.v - a.v) };
            if (accept && !accept(candidate))
                continue;
            closest = fraction;
            found   = true;
            hit     = candidate;
        }
    }
    return found;
}
void VSWorldRenderer::draw(ITextureView* target,unsigned width,unsigned height,const View& view,const std::vector<Packet>& packets)
{
    const auto started=std::chrono::steady_clock::now();
    mImpl->statistics={};mImpl->statistics.meshUploads=mImpl->pendingMeshUploads;
    mImpl->statistics.meshUploadBytes=mImpl->pendingMeshBytes;
    mImpl->pendingMeshUploads=mImpl->pendingMeshBytes=0;
    requireWorld(target && width && height,"Invalid world target");
    requireWorld(target->GetTexture()->GetDesc().Width==width && target->GetTexture()->GetDesc().Height==height,"World extent mismatch");
    for (float f:view.projection) requireWorld(std::isfinite(f),"Non-finite world camera");
    retire();
    if (packets.empty()) return;
    auto* context=mImpl->context.RawPtr();
    if (!mImpl->depth || mImpl->depth->GetDesc().Width!=width || mImpl->depth->GetDesc().Height!=height)
    {
        TextureDesc desc{};desc.Name="Native world depth generation";desc.Type=RESOURCE_DIM_TEX_2D;
        desc.Width=width;desc.Height=height;desc.Format=TEX_FORMAT_D32_FLOAT;desc.BindFlags=BIND_DEPTH_STENCIL;
        mImpl->device->CreateTexture(desc,nullptr,&mImpl->depth);requireWorld(mImpl->depth!=nullptr,"World depth creation failed");
    }
    auto* depth=mImpl->depth->GetDefaultView(TEXTURE_VIEW_DEPTH_STENCIL);
    context->SetRenderTargets(1,&target,depth,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    context->ClearDepthStencil(depth,CLEAR_DEPTH_FLAG,1,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport viewport{0.f,0.f,float(width),float(height)};context->SetViewports(1,&viewport,width,height);
    mImpl->submitted.emplace_back();auto& submitted=mImpl->submitted.back();
    submitted.depth=mImpl->depth;submitted.packets=packets;
    std::stable_sort(submitted.packets.begin(), submitted.packets.end(),
                     [](const Packet& a, const Packet& b)
                     {
                         if (a.hud != b.hud)
                             return !a.hud;
                         if (a.background != b.background)
                             return a.background;
                         if (a.alpha != b.alpha)
                             return !a.alpha;
                         return a.alpha ? a.distanceSquared > b.distanceSquared : false;
                     });
    float hudMin = 0, hudMax = 0;
    bool  hudKnown = false;
    for (const auto& packet : submitted.packets)
        if (packet.hud && packet.mesh)
        {
            const auto box = packet.mesh->bounds;
            if (!hudKnown)
            {
                hudMin   = box[0][0];
                hudMax   = box[1][0];
                hudKnown = true;
            }
            else
            {
                hudMin = std::min(hudMin, box[0][0]);
                hudMax = std::max(hudMax, box[1][0]);
            }
        }
    const float hudDepth = std::max(1.f, (hudMax - hudMin) * 1.1f), hudShift = -(hudMin + hudMax) * .5f + hudDepth * .5f;
    const float zoom = std::isfinite(view.hudZoom) ? std::clamp(view.hudZoom, .01f, 1.f) : 1;
    // Orthographic HUD and Cory's reference-frame rotation, matching
    // get_hud_matrices() without depending on the legacy avatar/GL context.
    const std::array<float, 16> hudProjection{ 0, 0, .01f * zoom, 0, -2.f * zoom * height / width, 0, 0, 0, 0, 2.f * zoom,
                                               0, 0, 0,           0, .01f * hudShift - 1.f,        1 };
    bool                        drawingHud = false;
    for (const auto& p:submitted.packets)
    {
        requireWorld(p.mesh && p.mesh->owner.RawPtr()==mImpl->device.RawPtr(),"World mesh belongs to another device");
        requireWorld(!p.image || VSUIRenderer::imageOwnedBy(p.image,mImpl->device),"World image belongs to another device");
        for (float f:p.color) requireWorld(std::isfinite(f),"Non-finite world material");
        auto* pipeline = mImpl->pipeline(target->GetTexture()->GetDesc().Format, p.alpha, p.background, p.mesh->topology);
        for (float f:p.regionOffset) requireWorld(std::isfinite(f),"Non-finite world region translation");
        requireWorld(std::isfinite(p.alphaCutoff) && p.alphaCutoff >= 0 && p.alphaCutoff <= 1, "Invalid world alpha cutoff");
        Constants constants{ view.projection,
                             view.sun,
                             view.ambient,
                             p.color,
                             { p.fullbright ? 1.f : 0.f, p.masked ? 1.f : 0.f, p.alphaCutoff, p.background ? 1.f : 0.f },
                             p.regionOffset };
        if (p.hud)
        {
            if (!drawingHud)
            {
                context->ClearDepthStencil(depth, CLEAR_DEPTH_FLAG, 1, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
                drawingHud = true;
            }
            constants.projection   = hudProjection;
            constants.regionOffset = {};
        }
        BufferDesc bd{};bd.Name="Native world draw constants";bd.Size=sizeof(constants);
        Impl::DrawResources resources;
        auto& available=mImpl->available[pipeline];
        if (!available.empty()) {resources=std::move(available.back());available.pop_back();--mImpl->availableCount;}
        bd.Usage=USAGE_DEFAULT;bd.BindFlags=BIND_UNIFORM_BUFFER;
        if (!resources.constants)
        {
            mImpl->device->CreateBuffer(bd,nullptr,&resources.constants);
            requireWorld(resources.constants!=nullptr,"World constants allocation failed");++mImpl->statistics.constantBuffersCreated;
        }
        context->UpdateBuffer(resources.constants,0,sizeof(constants),&constants,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        mImpl->statistics.constantUploadBytes+=sizeof(constants);
        if (resources.pipeline!=pipeline || !resources.binding)
        {
            resources.binding.Release();pipeline->CreateShaderResourceBinding(&resources.binding,true);
            requireWorld(resources.binding!=nullptr,"World binding creation failed");
            resources.pipeline=pipeline;++mImpl->statistics.bindingsCreated;
            for (auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
            {
                auto* variable=resources.binding->GetVariableByName(stage,"WorldView");
                requireWorld(variable!=nullptr,"World constants binding missing");variable->Set(resources.constants);
            }
        }
        auto* texture=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Texture");
        requireWorld(texture!=nullptr,"World texture binding missing");
        texture->Set(p.image ? VSUIRenderer::imageView(p.image) : mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                     SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        context->SetPipelineState(pipeline);
        IBuffer* vertex=p.mesh->vertices.RawPtr();Uint64 offset=0;
        context->SetVertexBuffers(0,1,&vertex,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);
        context->SetIndexBuffer(p.mesh->indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->CommitShaderResources(resources.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        ++mImpl->statistics.resourceCommits;
        DrawIndexedAttribs draw{};draw.NumIndices=p.mesh->count;draw.IndexType=VT_UINT32;draw.Flags=DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(draw);submitted.draws.push_back(std::move(resources));++mImpl->statistics.draws;
    }
    // Recorded draws and submission packets own the buffers through completion;
    // the CPU context must not retain uncharged last-bound geometry afterwards.
    context->SetVertexBuffers(0, 0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_NONE,
                              SET_VERTEX_BUFFERS_FLAG_RESET);
    context->SetIndexBuffer(nullptr, 0, RESOURCE_STATE_TRANSITION_MODE_NONE);
    submitted.completion=mImpl->next++;context->EnqueueSignal(mImpl->fence,submitted.completion);
    context->SetRenderTargets(1,&target,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    mImpl->statistics.cpuMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
}
