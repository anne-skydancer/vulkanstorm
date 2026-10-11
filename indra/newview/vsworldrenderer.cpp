// Native indexed world geometry and submission ownership. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldrenderer.h"
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
#include <stdexcept>
using namespace Diligent;
namespace
{
void requireWorld(bool v,const char* message) { if (!v) throw std::runtime_error(message); }
struct alignas(16) Constants
{
    std::array<float,16> projection;
    std::array<float,4> sun,ambient,color,material;
};
static_assert(sizeof(Constants)==128);
static_assert(sizeof(VSWorldRenderer::Vertex)==32);
void checkWorldABI(IShader& shader)
{
    bool found=false;
    for (Uint32 i=0;i<shader.GetResourceCount();++i)
    {
        ShaderResourceDesc resource{};shader.GetResourceDesc(i,resource);
        if (!resource.Name || std::strcmp(resource.Name,"WorldView")) continue;
        const auto* buffer=shader.GetConstantBufferDesc(i);
        requireWorld(buffer && buffer->Size==sizeof(Constants) && buffer->NumVariables==5,"World uniform block ABI mismatch");
        const char* names[]{"projection","sun","ambient","tint","material"};
        const std::size_t offsets[]{offsetof(Constants,projection),offsetof(Constants,sun),offsetof(Constants,ambient),offsetof(Constants,color),offsetof(Constants,material)};
        for (unsigned v=0;v<5;++v)
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
};
struct VSWorldRenderer::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<IShader> vertex, fragment;
    RefCntAutoPtr<IPipelineState> pipelines[2][3];
    RefCntAutoPtr<ITexture> depth, white;
    RefCntAutoPtr<IFence> fence;
    Uint64 next = 1;
    struct Submission
    {
        Uint64 completion = 0;
        std::vector<Packet> packets;
        std::vector<RefCntAutoPtr<IBuffer>> constants;
        std::vector<RefCntAutoPtr<IShaderResourceBinding>> bindings;
        RefCntAutoPtr<ITexture> depth;
    };
    std::deque<Submission> submitted;
    Impl(IRenderDevice* d,IDeviceContext* c):device(d),context(c)
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
    IPipelineState* pipeline(TEXTURE_FORMAT format,bool alpha,bool background)
    {
        requireWorld(format==TEX_FORMAT_RGBA8_UNORM || format==TEX_FORMAT_BGRA8_UNORM,"Unsupported world target format");
        auto& result=pipelines[format==TEX_FORMAT_BGRA8_UNORM][background ? 2 : unsigned(alpha)];
        if (result) return result;
        const char* vs=R"(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; };
layout(location=0) out vec2 texUV;
layout(location=1) out vec3 light;
void main() {
    vec4 p=material.w>0.5 ? vec4(position,1) : projection*vec4(position,1);
    p.z=(p.z+p.w)*0.5;
    gl_Position=p; texUV=uv;
    light=material.x>0.5 ? vec3(1) : ambient.rgb+(vec3(1)-ambient.rgb)*max(dot(normalize(normal),normalize(sun.xyz)),0.0);
}
)";
        const char* fs=R"(
layout(location=0) in vec2 texUV;
layout(location=1) in vec3 light;
layout(location=0) out vec4 result;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; };
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
        gp.DSVFormat=TEX_FORMAT_D32_FLOAT;gp.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
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
VSWorldRenderer::VSWorldRenderer(IRenderDevice* d,IDeviceContext* c):mImpl(std::make_unique<Impl>(d,c)) {}
VSWorldRenderer::~VSWorldRenderer()=default;
VSWorldRenderer::Mesh VSWorldRenderer::upload(const std::vector<Vertex>& vertices,const std::vector<unsigned>& indices)
{
    requireWorld(!vertices.empty() && vertices.size()<=1048576 && !indices.empty() &&
        indices.size()<=3145728 && indices.size()%3==0,"Invalid world geometry size");
    for (const auto& v:vertices)
        for (float f:{v.x,v.y,v.z,v.nx,v.ny,v.nz,v.u,v.v}) requireWorld(std::isfinite(f),"Non-finite world vertex");
    for (auto i:indices) requireWorld(i<vertices.size(),"World index outside geometry");
    auto mesh=std::make_shared<Geometry>();mesh->owner=mImpl->device;mesh->count=unsigned(indices.size());
    BufferDesc bd{};bd.Name="Native world vertex generation";bd.Size=vertices.size()*sizeof(Vertex);
    bd.Usage=USAGE_IMMUTABLE;bd.BindFlags=BIND_VERTEX_BUFFER;
    BufferData data{vertices.data(),bd.Size};mImpl->device->CreateBuffer(bd,&data,&mesh->vertices);
    bd.Name="Native world index generation";bd.Size=indices.size()*sizeof(unsigned);bd.BindFlags=BIND_INDEX_BUFFER;
    data.pData=indices.data();data.DataSize=bd.Size;mImpl->device->CreateBuffer(bd,&data,&mesh->indices);
    requireWorld(mesh->vertices && mesh->indices,"World geometry upload failed");return mesh;
}
void VSWorldRenderer::retire()
{
    const auto complete=mImpl->fence->GetCompletedValue();
    while (!mImpl->submitted.empty() && mImpl->submitted.front().completion<=complete) mImpl->submitted.pop_front();
}
void VSWorldRenderer::draw(ITextureView* target,unsigned width,unsigned height,const View& view,const std::vector<Packet>& packets)
{
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
    std::stable_sort(submitted.packets.begin(),submitted.packets.end(),[](const Packet& a,const Packet& b)
    { if (a.background!=b.background) return a.background; if (a.alpha!=b.alpha) return !a.alpha;return a.alpha ? a.distanceSquared>b.distanceSquared : false; });
    for (const auto& p:submitted.packets)
    {
        requireWorld(p.mesh && p.mesh->owner.RawPtr()==mImpl->device.RawPtr(),"World mesh belongs to another device");
        for (float f:p.color) requireWorld(std::isfinite(f),"Non-finite world material");
        auto* pipeline=mImpl->pipeline(target->GetTexture()->GetDesc().Format,p.alpha,p.background);
        Constants constants{view.projection,view.sun,view.ambient,p.color,{p.fullbright?1.f:0.f,p.masked?1.f:0.f,.5f,p.background?1.f:0.f}};
        BufferDesc bd{};bd.Name="Native world draw constants";bd.Size=sizeof(constants);
        bd.Usage=USAGE_IMMUTABLE;bd.BindFlags=BIND_UNIFORM_BUFFER;
        BufferData data{&constants,sizeof(constants)};RefCntAutoPtr<IBuffer> buffer;
        mImpl->device->CreateBuffer(bd,&data,&buffer);requireWorld(buffer!=nullptr,"World constants upload failed");
        RefCntAutoPtr<IShaderResourceBinding> binding;pipeline->CreateShaderResourceBinding(&binding,true);
        requireWorld(binding!=nullptr,"World binding creation failed");
        for (auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
        {
            auto* variable=binding->GetVariableByName(stage,"WorldView");
            requireWorld(variable!=nullptr,"World constants binding missing");variable->Set(buffer);
        }
        auto* texture=binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Texture");
        requireWorld(texture!=nullptr,"World texture binding missing");
        texture->Set(p.image ? VSUIRenderer::imageView(p.image) : mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        context->SetPipelineState(pipeline);
        IBuffer* vertex=p.mesh->vertices.RawPtr();Uint64 offset=0;
        context->SetVertexBuffers(0,1,&vertex,&offset,RESOURCE_STATE_TRANSITION_MODE_TRANSITION,SET_VERTEX_BUFFERS_FLAG_RESET);
        context->SetIndexBuffer(p.mesh->indices,0,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        DrawIndexedAttribs draw{};draw.NumIndices=p.mesh->count;draw.IndexType=VT_UINT32;draw.Flags=DRAW_FLAG_VERIFY_ALL;
        context->DrawIndexed(draw);submitted.constants.push_back(buffer);submitted.bindings.push_back(binding);
    }
    submitted.completion=mImpl->next++;context->EnqueueSignal(mImpl->fence,submitted.completion);
    context->SetRenderTargets(1,&target,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}
