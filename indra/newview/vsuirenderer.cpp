// Vulkanstorm native UI substrate. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsuirenderer.h"
#pragma push_macro("Bool")
#pragma push_macro("False")
#pragma push_macro("True")
#pragma push_macro("MAP_TYPE")
#undef Bool
#undef False
#undef True
#undef MAP_TYPE
#include <DiligentCore/Graphics/GraphicsEngine/interface/RenderDevice.h>
#include <DiligentCore/Graphics/GraphicsEngine/interface/DeviceContext.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

using namespace Diligent;
namespace
{
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
struct Vertex { float x, y, u, v, r, g, b, a; };
}
struct VSUIRenderer::Texture
{
    RefCntAutoPtr<ITexture> resource;
    RefCntAutoPtr<IRenderDevice> owner;
    std::uint64_t generation;
};
struct VSUIRenderer::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<IShader> vertex, fragment;
    RefCntAutoPtr<IPipelineState> pipelines[2][2][2]; // format, blend, sampler
    std::uint64_t next_generation = 1;
    struct Submission
    {
        Uint64 completion = 0;
        std::vector<Image> images;
        std::vector<RefCntAutoPtr<IShaderResourceBinding>> bindings;
        RefCntAutoPtr<IBuffer> vertices;
    };
    RefCntAutoPtr<IFence> fence;
    Uint64 next_completion = 1;
    std::deque<Submission> submitted;
    Impl(IRenderDevice* d, IDeviceContext* c) : device(d), context(c)
    {
        check(d && c, "UI renderer requires a native device and context");
        FenceDesc desc{}; desc.Name="Native UI retirement";
        device->CreateFence(desc,&fence); check(fence != nullptr,"Native UI completion fence creation failed");
    }
    IPipelineState* pipeline(TEXTURE_FORMAT format, Blend blend, Sampling sampling)
    {
        check(format == TEX_FORMAT_RGBA8_UNORM || format == TEX_FORMAT_BGRA8_UNORM,
              "UI renderer requires an SDR UNORM target");
        auto& result = pipelines[format == TEX_FORMAT_BGRA8_UNORM]
            [blend == Blend::PremultipliedAlpha][sampling == Sampling::Linear];
        if (result) return result;
        const char* vs = R"(
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
layout(location=2) in vec4 tint;
layout(location=0) out vec2 texUV;
layout(location=1) out vec4 color;
void main() { gl_Position=vec4(position,0,1); texUV=uv; color=tint; }
)";
        const char* fs = R"(
layout(location=0) in vec2 texUV;
layout(location=1) in vec4 color;
layout(location=0) out vec4 result;
uniform sampler2D g_Texture;
void main() { result=texture(g_Texture,texUV)*color; }
)";
        if (!vertex || !fragment)
        {
            ShaderCreateInfo shader{};
            shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL; shader.GLSLVersion = {4,5};
            shader.EntryPoint = "main"; shader.Desc.UseCombinedTextureSamplers = true;
            shader.Desc.Name = "Native UI vertex"; shader.Desc.ShaderType = SHADER_TYPE_VERTEX; shader.Source = vs;
            device->CreateShader(shader, &vertex);
            shader.Desc.Name = "Native UI fragment"; shader.Desc.ShaderType = SHADER_TYPE_PIXEL; shader.Source = fs;
            device->CreateShader(shader, &fragment);
            check(vertex && fragment, "Native UI shader creation failed");
        }
        GraphicsPipelineStateCreateInfo ci{};
        ci.PSODesc.Name = "Native ordered UI"; ci.PSODesc.PipelineType = PIPELINE_TYPE_GRAPHICS;
        ci.pVS = vertex; ci.pPS = fragment;
        auto& gp = ci.GraphicsPipeline;
        gp.NumRenderTargets = 1; gp.RTVFormats[0] = format;
        gp.PrimitiveTopology = PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        gp.RasterizerDesc.CullMode = CULL_MODE_NONE; gp.RasterizerDesc.ScissorEnable = true;
        gp.DepthStencilDesc.DepthEnable = false;
        const LayoutElement layout[] = {{0,0,2,VT_FLOAT32,false}, {1,0,2,VT_FLOAT32,false}, {2,0,4,VT_FLOAT32,false}};
        gp.InputLayout.LayoutElements = layout; gp.InputLayout.NumElements = 3;
        auto& rt = gp.BlendDesc.RenderTargets[0]; rt.BlendEnable = true;
        rt.SrcBlend = blend == Blend::StraightAlpha ? BLEND_FACTOR_SRC_ALPHA : BLEND_FACTOR_ONE;
        rt.DestBlend = BLEND_FACTOR_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = BLEND_FACTOR_ONE; rt.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        SamplerDesc sampler{};
        sampler.MinFilter = sampler.MagFilter = sampling == Sampling::Nearest ? FILTER_TYPE_POINT : FILTER_TYPE_LINEAR;
        sampler.MipFilter = FILTER_TYPE_POINT;
        sampler.AddressU = sampler.AddressV = sampler.AddressW = TEXTURE_ADDRESS_CLAMP;
        ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL, "g_Texture", sampler};
        ci.PSODesc.ResourceLayout.NumImmutableSamplers = 1; ci.PSODesc.ResourceLayout.ImmutableSamplers = &immutable;
        device->CreateGraphicsPipelineState(ci, &result);
        check(result != nullptr, "Native UI pipeline creation failed");
        return result;
    }
};
VSUIRenderer::VSUIRenderer(IRenderDevice* d, IDeviceContext* c) : mImpl(std::make_unique<Impl>(d,c)) {}
VSUIRenderer::~VSUIRenderer() = default;
VSUIRenderer::Image VSUIRenderer::upload(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba)
{
    check(width && height && width <= 16384 && height <= 16384 &&
          rgba.size() == std::size_t(width) * height * 4, "Invalid native UI upload extent or byte count");
    auto image = std::make_shared<Texture>();
    image->owner=mImpl->device;
    TextureDesc desc{}; desc.Name = "Native UI texture generation";
    desc.Type = RESOURCE_DIM_TEX_2D; desc.Width = width; desc.Height = height;
    desc.Format = TEX_FORMAT_RGBA8_UNORM; desc.Usage = USAGE_IMMUTABLE; desc.BindFlags = BIND_SHADER_RESOURCE;
    TextureSubResData sub{}; sub.pData = rgba.data(); sub.Stride = std::uint64_t(width) * 4;
    TextureData data{&sub, 1}; mImpl->device->CreateTexture(desc, &data, &image->resource);
    check(image->resource != nullptr, "Native UI texture upload failed");
    image->generation = mImpl->next_generation++;
    return image;
}
std::uint64_t VSUIRenderer::generation(const Image& image) { return image ? image->generation : 0; }
void VSUIRenderer::draw(ITextureView* target, unsigned width, unsigned height, float dpi, const std::vector<Packet>& packets)
{
    check(target && width && height && std::isfinite(dpi) && dpi > 0, "Invalid native UI target or DPI");
    const auto& desc = target->GetTexture()->GetDesc();
    check(desc.Width == width && desc.Height == height, "Native UI target extent differs");
    retire();
    struct Draw { const Packet* packet; Rect clip; Uint32 first; };
    std::vector<Draw> draws;
    std::vector<Vertex> vertices;
    auto* context = mImpl->context.RawPtr();
    for (const auto& p : packets)
    {
        check(p.image != nullptr, "Native UI packet has no texture generation");
        check(p.image->owner.RawPtr() == mImpl->device.RawPtr(),"Native UI texture belongs to a different device");
        check((p.blend == Blend::StraightAlpha || p.blend == Blend::PremultipliedAlpha) &&
              (p.sampling == Sampling::Nearest || p.sampling == Sampling::Linear),"Invalid native UI packet state");
        for (const auto* values : {&p.bounds, &p.uv, &p.color, &p.clip})
            for (float value : *values) check(std::isfinite(value), "Non-finite native UI packet");
        for (float value : p.transform) check(std::isfinite(value), "Non-finite native UI transform");
        check(p.bounds[2] >= p.bounds[0] && p.bounds[3] >= p.bounds[1], "Inverted native UI bounds");
        // Clamp before converting to signed integers; enormous finite clips are valid.
        auto edge = [dpi](float value, unsigned limit, bool end)
        { double v = std::clamp(double(value)*dpi, 0.0, double(limit)); return int(end ? std::ceil(v) : std::floor(v)); };
        Rect clip{edge(p.clip[0],width,false), edge(p.clip[1],height,false),
                  edge(p.clip[2],width,true), edge(p.clip[3],height,true)};
        if (clip.right <= clip.left || clip.bottom <= clip.top || p.bounds[2] == p.bounds[0] || p.bounds[3] == p.bounds[1]) continue;
        Vertex corners[4];
        for (unsigned i=0; i<4; ++i)
        {
            const float x=p.bounds[(i&1)?2:0], y=p.bounds[(i&2)?3:1];
            const auto& t=p.transform;
            const double px=(double(t[0])*x+double(t[2])*y+t[4])*dpi;
            const double py=(double(t[1])*x+double(t[3])*y+t[5])*dpi;
            check(std::abs(px*2/width-1) <= std::numeric_limits<float>::max() &&
                  std::abs(1-py*2/height) <= std::numeric_limits<float>::max(), "Native UI transformed vertex overflow");
            corners[i]={float(px*2/width-1),float(1-py*2/height),p.uv[(i&1)?2:0],p.uv[(i&2)?3:1],
                        p.color[0],p.color[1],p.color[2],p.color[3]};
        }
        check(vertices.size() <= std::numeric_limits<Uint32>::max()-6,"Native UI vertex count overflow");
        draws.push_back({&p,clip,Uint32(vertices.size())});
        vertices.insert(vertices.end(),{corners[0],corners[1],corners[2],corners[2],corners[1],corners[3]});
    }
    if (draws.empty()) return;
    // One vertex upload per ordered packet list; no per-glyph vertex allocation.
    mImpl->submitted.emplace_back(); auto& submission=mImpl->submitted.back();
    BufferDesc bd{}; bd.Name="Native UI packet vertices"; bd.Size=vertices.size()*sizeof(Vertex);
    bd.Usage=USAGE_IMMUTABLE; bd.BindFlags=BIND_VERTEX_BUFFER;
    BufferData data{vertices.data(),bd.Size};
    mImpl->device->CreateBuffer(bd,&data,&submission.vertices);
    check(submission.vertices != nullptr,"Native UI vertex upload failed");
    context->SetRenderTargets(1,&target,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    Viewport viewport{0.f,0.f,float(width),float(height)}; context->SetViewports(1,&viewport,width,height);
    IBuffer* buffers[]{submission.vertices.RawPtr()}; const Uint64 offsets[]{0};
    context->SetVertexBuffers(0,1,buffers,offsets,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);
    for (const auto& draw : draws)
    {
        const auto& p=*draw.packet;
        auto* pipeline=mImpl->pipeline(desc.Format,p.blend,p.sampling);
        RefCntAutoPtr<IShaderResourceBinding> binding; pipeline->CreateShaderResourceBinding(&binding,true);
        check(binding != nullptr,"Native UI resource binding creation failed");
        auto* variable=binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Texture");
        check(variable != nullptr,"Native UI texture binding missing");
        variable->Set(p.image->resource->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        context->SetPipelineState(pipeline); context->SetScissorRects(1,&draw.clip,width,height);
        context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{6,DRAW_FLAG_VERIFY_ALL,1,draw.first});
        submission.images.push_back(p.image); submission.bindings.push_back(binding);
    }
    submission.completion=mImpl->next_completion++;
    context->EnqueueSignal(mImpl->fence,submission.completion);
}
void VSUIRenderer::retire()
{
    const auto completed=mImpl->fence->GetCompletedValue();
    while (!mImpl->submitted.empty() && mImpl->submitted.front().completion &&
           mImpl->submitted.front().completion <= completed) mImpl->submitted.pop_front();
}
