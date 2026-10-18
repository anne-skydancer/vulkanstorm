// Transposes twotexturecompareF/onetexturefilterF and the original asynchronous
// sample query. Capture cadence, freeze policy and recordings remain LLSceneMonitor's.
// LGPL-2.1, like the original shaders and viewer.
#include "llviewerprecompiledheaders.h"
#include "vsscenemonitor.h"
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
#include <DiligentCore/Graphics/GraphicsEngine/interface/Query.h>
#include <DiligentCore/Common/interface/RefCntAutoPtr.hpp>
#pragma pop_macro("MAP_TYPE")
#pragma pop_macro("True")
#pragma pop_macro("False")
#pragma pop_macro("Bool")
#include <deque>
#include <stdexcept>

using namespace Diligent;
namespace
{
void requireMonitor(bool value,const char* message)
{
    if(!value)throw std::runtime_error(message);
}
}
struct VSSceneMonitor::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    VSUIRenderer& renderer;
    RefCntAutoPtr<IShader> vertex,compareShader,filterShader;
    RefCntAutoPtr<IPipelineState> comparePipeline,filterPipeline;
    RefCntAutoPtr<IFence> fence;
    Uint64 sequence=0;
    VSUIRenderer::Image frames[2],filtered;
    struct Submission
    {
        Uint64 completion=0;
        std::vector<VSUIRenderer::Image> images;
        RefCntAutoPtr<IShaderResourceBinding> binding;
        RefCntAutoPtr<IBuffer> constants;
    };
    std::deque<Submission> submitted;
    struct Pending
    {
        Uint64 completion=0;
        RefCntAutoPtr<IQuery> query;
        RefCntAutoPtr<ITexture> staging;
        VSUIRenderer::Image image;
        unsigned width=0,height=0,sampledWidth=0,sampledHeight=0;
        bool discard=false;
    } pending;
    Impl(IRenderDevice* d,IDeviceContext* c,VSUIRenderer& r):device(d),context(c),renderer(r)
    {
        FenceDesc desc;desc.Name="Scene monitor completion";device->CreateFence(desc,&fence);
        requireMonitor(fence!=nullptr,"Scene monitor fence creation failed");
    }
    void retire()
    {
        const auto completed=fence->GetCompletedValue();
        while(!submitted.empty() && submitted.front().completion<=completed)submitted.pop_front();
    }
    void pipelines()
    {
        if(comparePipeline)return;
        const char* vs=R"(
layout(location=0) out vec2 uv;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);
uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);}
)";
        const char* compare=R"(
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
uniform sampler2D tex0;
uniform sampler2D tex1;
layout(std140) uniform MonitorParameters {vec4 settings;};
void main(){
    result=abs(texture(tex0,uv)-texture(tex1,uv));
    // The original uploaded matrix is bottom-row-first. Our frame textures and
    // framebuffer coordinates are top-first; preserve its exact phase.
    const int matrix[16]=int[16](1,9,3,11,13,5,15,7,4,12,2,10,16,8,14,6);
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    int row=(int(settings.y)-1-pixel.y)&3;
    float threshold=(float(matrix[row*4+(pixel.x&3)])/255.0)*(255.0/17.0);
    for(int i=0;i<3;++i)if(result[i]<threshold)result[i]=0.0;
}
)";
        const char* filter=R"(
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
uniform sampler2D tex0;
layout(std140) uniform MonitorParameters {vec4 settings;};
void main(){result=texture(tex0,uv);
if(result.r+result.g+result.b<settings.z)discard;
result.a=.95;}
)";
        ShaderCreateInfo shader;shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_GLSL;
        shader.GLSLVersion={4,5};shader.EntryPoint="main";shader.Desc.UseCombinedTextureSamplers=true;
        shader.Desc.Name="Scene monitor vertex";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.Source=vs;
        device->CreateShader(shader,&vertex);
        shader.Desc.Name="Scene monitor reference difference";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.Source=compare;
        device->CreateShader(shader,&compareShader);
        shader.Desc.Name="Scene monitor reference filter";shader.Source=filter;
        device->CreateShader(shader,&filterShader);
        requireMonitor(vertex && compareShader && filterShader,"Scene monitor shader creation failed");
        for(unsigned pass=0;pass<2;++pass)
        {
            GraphicsPipelineStateCreateInfo info;
            info.PSODesc.Name=pass?"Scene monitor filter":"Scene monitor difference";
            info.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;
            info.pVS=vertex;info.pPS=pass?filterShader:compareShader;
            auto& graphics=info.GraphicsPipeline;
            graphics.NumRenderTargets=1;graphics.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
            graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            graphics.RasterizerDesc.CullMode=CULL_MODE_NONE;
            graphics.DepthStencilDesc.DepthEnable=false;
            info.PSODesc.ResourceLayout.DefaultVariableType=SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
            SamplerDesc sampler;sampler.MinFilter=sampler.MagFilter=FILTER_TYPE_LINEAR;
            sampler.MipFilter=FILTER_TYPE_POINT;sampler.MaxLOD=0;
            sampler.AddressU=sampler.AddressV=TEXTURE_ADDRESS_CLAMP;
            const ImmutableSamplerDesc samplers[]={{SHADER_TYPE_PIXEL,"tex0",sampler},{SHADER_TYPE_PIXEL,"tex1",sampler}};
            info.PSODesc.ResourceLayout.NumImmutableSamplers=pass?1:2;
            info.PSODesc.ResourceLayout.ImmutableSamplers=samplers;
            device->CreateGraphicsPipelineState(info,pass?&filterPipeline:&comparePipeline);
        }
        requireMonitor(comparePipeline && filterPipeline,"Scene monitor pipeline creation failed");
    }
    VSUIRenderer::Image pass(IPipelineState* pipeline,const VSUIRenderer::Image& a,
                            const VSUIRenderer::Image& b,unsigned width,unsigned height,
                            const std::array<float,4>& settings,IQuery* query=nullptr)
    {
        Submission submission;submission.images={a,b};
        BufferDesc buffer;buffer.Name="Scene monitor parameters";buffer.Size=sizeof(settings);
        buffer.Usage=USAGE_IMMUTABLE;buffer.BindFlags=BIND_UNIFORM_BUFFER;
        const BufferData data{settings.data(),sizeof(settings)};
        device->CreateBuffer(buffer,&data,&submission.constants);
        pipeline->CreateShaderResourceBinding(&submission.binding,true);
        requireMonitor(submission.constants && submission.binding,"Scene monitor binding allocation failed");
        auto* parameters=submission.binding->GetVariableByName(SHADER_TYPE_PIXEL,"MonitorParameters");
        auto* first=submission.binding->GetVariableByName(SHADER_TYPE_PIXEL,"tex0");
        requireMonitor(parameters && first,"Scene monitor shader resource ABI mismatch");
        parameters->Set(submission.constants);first->Set(VSUIRenderer::imageView(a));
        if(b)
        {
            auto* second=submission.binding->GetVariableByName(SHADER_TYPE_PIXEL,"tex1");
            requireMonitor(second!=nullptr,"Scene monitor second source ABI mismatch");
            second->Set(VSUIRenderer::imageView(b));
        }
        auto image=renderer.renderImage(width,height,[&](ITextureView* target)
        {
            context->SetRenderTargets(1,&target,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            const float clear[]{0,0,0,0};context->ClearRenderTarget(target,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            context->SetPipelineState(pipeline);
            const Viewport viewport{0,0,float(width),float(height),0,1};context->SetViewports(1,&viewport,width,height);
            context->CommitShaderResources(submission.binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            if(query)context->BeginQuery(query);
            context->Draw(DrawAttribs{3,DRAW_FLAG_VERIFY_ALL});
            if(query)context->EndQuery(query);
            context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        });
        if(image)
        {
            submission.images.push_back(image);submission.completion=++sequence;
            context->EnqueueSignal(fence,submission.completion);submitted.push_back(std::move(submission));
        }
        return image;
    }
};
VSSceneMonitor::VSSceneMonitor(IRenderDevice* d,IDeviceContext* c,VSUIRenderer& r):mImpl(std::make_unique<Impl>(d,c,r)){}
VSSceneMonitor::~VSSceneMonitor()=default;
bool VSSceneMonitor::capture(ITexture* source,unsigned width,unsigned height)
{
    auto& impl=*mImpl;impl.retire();
    if(!source || !width || !height || impl.pending.completion ||
       source->GetDesc().Width<width || source->GetDesc().Height<height)return false;
    // The native viewer's SDR window target is RGBA8. Exact copies avoid any
    // color conversion or CPU image round-trip.
    if(source->GetDesc().Format!=TEX_FORMAT_RGBA8_UNORM)return false;
    auto image=impl.renderer.renderImage(width,height,[&](ITextureView* target)
    {
        const Box region{0,width,source->GetDesc().Height-height,source->GetDesc().Height};
        CopyTextureAttribs copy;copy.pSrcTexture=source;copy.pSrcBox=&region;copy.pDstTexture=target->GetTexture();
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        impl.context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        impl.context->CopyTexture(copy);
    });
    if(!image)return false;
    if(impl.frames[1] && VSUIRenderer::imageExtent(impl.frames[1])!=std::array<unsigned,2>{width,height})
        impl.frames[1].reset();
    impl.frames[0]=std::move(impl.frames[1]);impl.frames[1]=std::move(image);
    return true;
}
bool VSSceneMonitor::compare(unsigned width,unsigned height,float ratio,float tolerance)
{
    auto& impl=*mImpl;impl.retire();
    if(impl.pending.completion || !impl.frames[0] || !impl.frames[1] || !width || !height ||
       !std::isfinite(ratio) || ratio<=0 || ratio>1 || !std::isfinite(tolerance))return false;
    const unsigned sampledWidth=unsigned(width*ratio),sampledHeight=unsigned(height*ratio);
    if(!sampledWidth || !sampledHeight)return false;
    impl.pipelines();
    Impl::Pending pending;pending.width=width;pending.height=height;
    pending.sampledWidth=sampledWidth;pending.sampledHeight=sampledHeight;
    if(impl.device->GetDeviceInfo().Features.OcclusionQueries==DEVICE_FEATURE_STATE_ENABLED)
    {
        QueryDesc desc{QUERY_TYPE_OCCLUSION};desc.Name="Scene monitor changed pixels";
        impl.device->CreateQuery(desc,&pending.query);
        requireMonitor(pending.query!=nullptr,"Scene monitor query allocation failed");
    }
    else
    {
        // Same GPU filter and sample count on devices without precise queries.
        // Only the small filtered image is mapped, asynchronously and while
        // this explicitly enabled diagnostic is active.
        TextureDesc desc;desc.Name="Scene monitor asynchronous sample count";
        desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=sampledWidth;desc.Height=sampledHeight;
        desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.MipLevels=1;desc.Usage=USAGE_STAGING;
        desc.CPUAccessFlags=CPU_ACCESS_READ;impl.device->CreateTexture(desc,nullptr,&pending.staging);
        requireMonitor(pending.staging!=nullptr,"Scene monitor staging allocation failed");
    }
    const std::array<float,4> settings{float(width),float(height),tolerance,0};
    const auto difference=impl.pass(impl.comparePipeline,impl.frames[0],impl.frames[1],width,height,settings);
    if(!difference)return false;
    const auto filtered=impl.pass(impl.filterPipeline,difference,{},sampledWidth,sampledHeight,settings,pending.query);
    if(!filtered)return false;
    if(pending.staging)
    {
        CopyTextureAttribs copy;copy.pSrcTexture=VSUIRenderer::imageView(filtered)->GetTexture();
        copy.pDstTexture=pending.staging;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        impl.context->CopyTexture(copy);
    }
    pending.image=filtered;
    pending.completion=++impl.sequence;impl.context->EnqueueSignal(impl.fence,pending.completion);
    impl.pending=std::move(pending);impl.filtered=filtered;
    return true;
}
std::optional<VSSceneMonitor::Result> VSSceneMonitor::poll()
{
    auto& impl=*mImpl;impl.retire();auto& pending=impl.pending;
    if(!pending.completion || impl.fence->GetCompletedValue()<pending.completion)return {};
    Uint64 samples=0;
    if(pending.query)
    {
        QueryDataOcclusion data;
        if(!pending.query->GetData(&data,sizeof(data)))return {};
        samples=data.NumSamples;
    }
    else
    {
        MappedTextureSubresource mapped;
        impl.context->MapTextureSubresource(pending.staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        if(!mapped.pData)return {};
        for(unsigned y=0;y<pending.sampledHeight;++y)
        {
            const auto* row=static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.Stride;
            for(unsigned x=0;x<pending.sampledWidth;++x)samples+=row[x*4+3]!=0;
        }
        impl.context->UnmapTextureSubresource(pending.staging,0,0);
    }
    const Result result{samples,pending.width,pending.height};const bool discard=pending.discard;
    pending={};return discard?std::optional<Result>{}:result;
}
VSUIRenderer::Image VSSceneMonitor::image()const{return mImpl->filtered;}
void VSSceneMonitor::reset()
{
    auto& impl=*mImpl;impl.frames[0].reset();impl.frames[1].reset();impl.filtered.reset();
    // An account or camera reset discards old results, retaining every in-flight
    // resource until its real fence. No queue idle or early destruction.
    if(impl.pending.completion)impl.pending.discard=true;
    impl.retire();
}
