// Vulkanstorm native UI substrate. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsuirenderer.h"
#include "vsimagemips.h"
#define STB_DXT_IMPLEMENTATION
#include "vstextureupload.h"
#include <tuple>
#include <map>
#include "vsresourcebudget.h"
#include "vsimageusage.h"
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
using Vertex=VSUIRenderer::Vertex;
VSWorldContracts::AlphaMode alphaMode(const std::vector<std::uint8_t>& pixels)
{
    bool transparent=false,partial=false;
    for(std::size_t i=3;i<pixels.size();i+=4) {transparent|=pixels[i]!=255;partial|=pixels[i]!=0 && pixels[i]!=255;}
    return !transparent?VSWorldContracts::AlphaMode::Opaque:
        partial?VSWorldContracts::AlphaMode::Blend:VSWorldContracts::AlphaMode::Mask;
}
}
struct VSUIRenderer::Texture
{
    RefCntAutoPtr<ITexture> resource;
    RefCntAutoPtr<IRenderDevice> owner;
    std::uint64_t generation;
    std::vector<std::uint8_t> pixels;
    VSWorldContracts::Image description;
    std::shared_ptr<const VSResourceBudget::Reservation> residency;
    VSImageUsage::Charge usage;
    bool                                                 fontAtlas = false;
    using SamplerKey=std::tuple<int,int,int,int,bool,bool,unsigned>;
    mutable std::map<SamplerKey,RefCntAutoPtr<ITextureView>> sampledViews;
};
struct VSUIRenderer::Impl
{
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<IShader> vertex, fragments[2],bumpVertex,bumpFragment;
    RefCntAutoPtr<IPipelineState> bumpPipeline;
    RefCntAutoPtr<IPipelineState> pipelines[2][5][2][2]; // format, blend, sampler, mask
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
    std::vector<Image> pendingUploads;
    Statistics statistics;
    const std::shared_ptr<VSImageUsage::State> imageUsage=std::make_shared<VSImageUsage::State>();
    VSResourceBudget coreImages{256 * 1024 * 1024},remoteImages{512 * 1024 * 1024};
    // Eleven maximum-size local bakes cost 322 MiB including independent CPU
    // snapshots. Bound three complete generations (published, candidate and
    // retiring submission) separately from UI and fetched world assets.
    VSResourceBudget  compositeImages{ 1024 * 1024 * 1024 };
    VSResourceBudget& imageBudget(VSWorldContracts::ImageRole role)
    {
        if (role == VSWorldContracts::ImageRole::AvatarComposite)
            return compositeImages;
        if (role == VSWorldContracts::ImageRole::RemoteAsset || role == VSWorldContracts::ImageRole::Media)
            return remoteImages;
        return coreImages;
    }
    std::uint64_t imageAllocationRefusals=0;
    Impl(IRenderDevice* d, IDeviceContext* c) : device(d), context(c)
    {
        check(d && c, "UI renderer requires a native device and context");
        FenceDesc desc{}; desc.Name="Native UI retirement";
        device->CreateFence(desc,&fence); check(fence != nullptr,"Native UI completion fence creation failed");
    }
    IPipelineState* pipeline(TEXTURE_FORMAT format, Blend blend, Sampling sampling,bool mask)
    {
        check(format == TEX_FORMAT_RGBA8_UNORM || format == TEX_FORMAT_BGRA8_UNORM,
              "UI renderer requires an SDR UNORM target");
        auto& result = pipelines[format == TEX_FORMAT_BGRA8_UNORM]
            [static_cast<unsigned>(blend)][sampling == Sampling::Linear][mask];
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
        const char* mask_fs = R"(
layout(location=0) in vec2 texUV;
layout(location=1) in vec4 color;
layout(location=0) out vec4 result;
uniform sampler2D g_Texture;
void main() { result=vec4(color.rgb,texture(g_Texture,texUV).a*color.a); }
)";
        auto& fragment=fragments[mask];
        if (!vertex || !fragment)
        {
            ShaderCreateInfo shader{};
            shader.SourceLanguage = SHADER_SOURCE_LANGUAGE_GLSL; shader.GLSLVersion = {4,5};
            shader.EntryPoint = "main"; shader.Desc.UseCombinedTextureSamplers = true;
            shader.Desc.Name = "Native UI vertex"; shader.Desc.ShaderType = SHADER_TYPE_VERTEX; shader.Source = vs;
            if (!vertex) device->CreateShader(shader, &vertex);
            shader.Desc.Name = "Native UI fragment"; shader.Desc.ShaderType = SHADER_TYPE_PIXEL; shader.Source = mask ? mask_fs : fs;
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
        auto& rt = gp.BlendDesc.RenderTargets[0]; rt.BlendEnable = blend != Blend::Replace;
        rt.SrcBlend = blend == Blend::StraightAlpha || blend == Blend::AdditiveAlpha ? BLEND_FACTOR_SRC_ALPHA : BLEND_FACTOR_ONE;
        rt.DestBlend = blend == Blend::Additive || blend == Blend::AdditiveAlpha ? BLEND_FACTOR_ONE : BLEND_FACTOR_INV_SRC_ALPHA;
        rt.SrcBlendAlpha = BLEND_FACTOR_ONE; rt.DestBlendAlpha = BLEND_FACTOR_INV_SRC_ALPHA;
        ci.PSODesc.ResourceLayout.DefaultVariableType = SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        SamplerDesc sampler{};
        sampler.MinFilter = sampler.MagFilter = sampling == Sampling::Nearest ? FILTER_TYPE_POINT : FILTER_TYPE_LINEAR;
        sampler.MipFilter = FILTER_TYPE_POINT;
        // LLUIImageList::getUIImageByID/loadUIImageByID uses MIPMAP_NO.
        // A world subscriber may share this image's mip chain, but UI draws
        // sample its base level independently of the world sampler view.
        sampler.MaxLOD = 0;
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
Diligent::ITextureView* VSUIRenderer::imageView(const Image& image)
{ return image ? image->resource->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE) : nullptr; }
const std::vector<std::uint8_t>& VSUIRenderer::imagePixels(const Image& image) { return image->pixels; }
std::array<unsigned,2> VSUIRenderer::imageExtent(const Image& image)
{ const auto& d=image->resource->GetDesc();return {d.Width,d.Height}; }
VSUIRenderer::Image VSUIRenderer::upload(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba,
                                       VSWorldContracts::ImageRole role,bool compress)
{ return uploadPrepared(VSTextureUpload::prepare(width,height,rgba,role,compress && supportsCompression())); }
bool VSUIRenderer::supportsCompression() const
{
    if(mImpl->device->GetDeviceInfo().Features.TextureCompressionBC!=DEVICE_FEATURE_STATE_ENABLED) return false;
    const auto& info=mImpl->device->GetTextureFormatInfoExt(TEX_FORMAT_BC3_UNORM);
    return (info.BindFlags&BIND_SHADER_RESOURCE)!=0 && (info.Dimensions&RESOURCE_DIMENSION_SUPPORT_TEX_2D)!=0;
}
VSUIRenderer::Image VSUIRenderer::uploadPrepared(const std::shared_ptr<const VSTextureUpload::Prepared>& prepared)
{
    check(prepared && VSTextureUpload::valid(*prepared),"Invalid prepared native image");
    if(prepared->compressed && !supportsCompression())
        return upload(prepared->width,prepared->height,prepared->pixels,prepared->role,false);
    return uploadPreparedTexture(*prepared,false);
}
VSUIRenderer::FontAtlas VSUIRenderer::createFontAtlas(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba)
{
    auto atlas = uploadTexture(width, height, rgba, VSWorldContracts::ImageRole::CoreUI, true);
    check(atlas != nullptr, "Native font atlas allocation refused");
    return atlas;
}
VSUIRenderer::FontAtlas VSUIRenderer::uploadTexture(unsigned width, unsigned height, const std::vector<std::uint8_t>& rgba,
                                                    VSWorldContracts::ImageRole role, bool fontAtlas)
{
    return uploadPreparedTexture(*VSTextureUpload::prepare(width,height,rgba,role,false),fontAtlas);
}
VSUIRenderer::FontAtlas VSUIRenderer::uploadPreparedTexture(const VSTextureUpload::Prepared& prepared,bool fontAtlas)
{
    check(VSTextureUpload::valid(prepared),"Invalid native UI upload extent or byte count");
    const auto width=prepared.width,height=prepared.height;
    const auto role=prepared.role;
    const auto& rgba=prepared.pixels;
    const auto maximum=mImpl->device->GetAdapterInfo().Texture.MaxTexture2DDimension;
    if (width>maximum || height>maximum) {++mImpl->imageAllocationRefusals;return {};}
    auto& budget = mImpl->imageBudget(role);
    // Charge both the GPU payload and the retained independent CPU snapshot.
    const auto gpuBytes=prepared.gpuBytes;
    auto residency=budget.reserve(rgba.size()+gpuBytes);
    if (!residency) {++mImpl->imageAllocationRefusals;return {};}
    auto image = std::make_shared<Texture>();
    image->residency=std::move(residency);
    image->owner=mImpl->device;
    TextureDesc desc{}; desc.Name = "Native UI texture generation";
    desc.Type = RESOURCE_DIM_TEX_2D; desc.Width = width; desc.Height = height;
    desc.Format                                                      = prepared.compressed?TEX_FORMAT_BC3_UNORM:TEX_FORMAT_RGBA8_UNORM;
    desc.Usage                                                       = fontAtlas ? USAGE_DEFAULT : USAGE_IMMUTABLE;
    desc.BindFlags                                                   = BIND_SHADER_RESOURCE;
    image->fontAtlas                                                 = fontAtlas;
    desc.MipLevels=static_cast<Uint32>(prepared.levels.size());
    std::vector<TextureSubResData> subresources;
    for(const auto& level:prepared.levels)
    {
        TextureSubResData sub{};sub.pData=level.bytes.data();sub.Stride=level.rowBytes;
        subresources.push_back(sub);
    }
    TextureData data{subresources.data(),static_cast<Uint32>(subresources.size())};mImpl->device->CreateTexture(desc,&data,&image->resource);
    check(image->resource != nullptr, "Native UI texture upload failed");
    image->generation = mImpl->next_generation++;
    image->description={width,height,4,VSWorldContracts::ImageOrigin::TopLeft,
        VSWorldContracts::ColorEncoding::EncodedSRGB,VSWorldContracts::AlphaMode::Blend,role,image->generation};
    image->description.alpha=alphaMode(rgba);
    image->description.mipLevels=desc.MipLevels;
    image->description.compression=prepared.compressed?VSWorldContracts::ImageCompression::BC3:VSWorldContracts::ImageCompression::None;
    image->description.gpuBytes=gpuBytes;
    image->pixels=rgba;
    image->usage.attach(mImpl->imageUsage,gpuBytes,rgba.size());
    // A newly uploaded image may be replaced or refused by its producer before
    // its first draw. Keep its charged owner through an upload-completion fence.
    mImpl->pendingUploads.push_back(image);
    return image;
}

VSUIRenderer::Image VSUIRenderer::generateBump(const Image& source,unsigned bumpCode,float normalScale,unsigned heightChannel)
{
    check(source && imageOwnedBy(source,mImpl->device) && bumpCode>0 && bumpCode<32 && heightChannel<4 &&
          std::isfinite(normalScale) && std::abs(normalScale)<=1.e6f,"Invalid native bump generation input");
    auto& impl=*mImpl;
    if(!impl.bumpPipeline)
    {
        // The reference normgenF.glsl uses a four-neighbour cross-product
        // kernel. Source images here are top-first, hence the +/-V inversion.
        const char* vertex=R"(
layout(location=0) out vec2 uv;
void main(){vec2 p=vec2((gl_VertexIndex<<1)&2,gl_VertexIndex&2);uv=vec2(p.x,1.0-p.y);gl_Position=vec4(p*2.0-1.0,0,1);}
)";
        const char* fragment=R"(
layout(location=0) in vec2 uv;
layout(location=0) out vec4 result;
layout(std140) uniform BumpView {vec4 settings;};
uniform sampler2D g_Source;
float heightAt(vec2 p) {
    vec4 color=texture(g_Source,p);int mode=int(settings.w+.5);
    if(mode>2) return color[mode-3]*255.0;
    float value=dot(color.rgb,vec3(.2995,.5875,.1145));
    return mode==2?1.0-value:value;
}
void main(){
    float c=heightAt(uv),factor=settings.w>2.5?1.0:512.0;
    float r=(heightAt(uv+vec2(settings.x,0))-c)*factor;
    float l=(heightAt(uv-vec2(settings.x,0))-c)*factor;
    float u=(heightAt(uv+vec2(0,settings.y))-c)*factor;
    float d=(heightAt(uv-vec2(0,settings.y))-c)*factor;
    vec3 right=vec3(settings.z,0,r),left=vec3(-settings.z,0,l);
    vec3 up=vec3(0,-settings.z,u),down=vec3(0,settings.z,d);
    vec3 n=cross(right,down)+cross(down,left)+cross(left,up)+cross(up,right);
    float magnitude=length(n);n=magnitude>0?n/magnitude:vec3(0);
    vec3 color=n*.5+.5;
    if(settings.w>2.5) color=floor(clamp(color,0.0,1.0)*255.0)/255.0;
    result=vec4(color,settings.w>2.5?c/255.0:c);
}
)";
        ShaderCreateInfo shader{};shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_GLSL;shader.GLSLVersion={4,5};
        shader.EntryPoint="main";shader.Desc.UseCombinedTextureSamplers=true;
        shader.Desc.Name="Native reference bump vertex";shader.Desc.ShaderType=SHADER_TYPE_VERTEX;shader.Source=vertex;
        impl.device->CreateShader(shader,&impl.bumpVertex);
        shader.Desc.Name="Native reference bump fragment";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;shader.Source=fragment;
        impl.device->CreateShader(shader,&impl.bumpFragment);
        check(impl.bumpVertex && impl.bumpFragment,"Native bump shader creation failed");
        GraphicsPipelineStateCreateInfo info{};info.PSODesc.Name="Native reference bump generation";
        info.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;info.pVS=impl.bumpVertex;info.pPS=impl.bumpFragment;
        auto& graphics=info.GraphicsPipeline;graphics.NumRenderTargets=1;graphics.RTVFormats[0]=TEX_FORMAT_RGBA8_UNORM;
        graphics.PrimitiveTopology=PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;graphics.RasterizerDesc.CullMode=CULL_MODE_NONE;
        graphics.DepthStencilDesc.DepthEnable=false;
        info.PSODesc.ResourceLayout.DefaultVariableType=SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        SamplerDesc sampler{};sampler.MinFilter=sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_POINT;
        sampler.MaxLOD=0;sampler.AddressU=sampler.AddressV=TEXTURE_ADDRESS_WRAP;
        const ImmutableSamplerDesc immutable{SHADER_TYPE_PIXEL,"g_Source",sampler};
        info.PSODesc.ResourceLayout.NumImmutableSamplers=1;info.PSODesc.ResourceLayout.ImmutableSamplers=&immutable;
        impl.device->CreateGraphicsPipelineState(info,&impl.bumpPipeline);
        check(impl.bumpPipeline!=nullptr,"Native bump pipeline creation failed");
    }
    const auto extent=imageExtent(source);const auto width=extent[0],height=extent[1];
    const auto bytes=VSImageMips::bytes(width,height,true);
    auto residency=impl.remoteImages.reserve(bytes);
    if(!residency){++impl.imageAllocationRefusals;return {};}
    auto image=std::make_shared<Texture>();image->owner=impl.device;image->residency=std::move(residency);
    TextureDesc description{};description.Name="Native reference bump generation";description.Type=RESOURCE_DIM_TEX_2D;
    description.Width=width;description.Height=height;description.Format=TEX_FORMAT_RGBA8_UNORM;
    description.MipLevels=0;description.Usage=USAGE_DEFAULT;description.BindFlags=BIND_SHADER_RESOURCE|BIND_RENDER_TARGET;
    description.MiscFlags=MISC_TEXTURE_FLAG_GENERATE_MIPS;
    impl.device->CreateTexture(description,nullptr,&image->resource);check(image->resource!=nullptr,"Native bump target creation failed");
    const std::array<float,4> parameters{1.f/width,1.f/height,normalScale,float(bumpCode<=2?bumpCode:3+heightChannel)};
    BufferDesc buffer{};buffer.Name="Native bump parameters";buffer.Size=sizeof(parameters);buffer.Usage=USAGE_IMMUTABLE;buffer.BindFlags=BIND_UNIFORM_BUFFER;
    const BufferData data{parameters.data(),sizeof(parameters)};RefCntAutoPtr<IBuffer> constants;
    impl.device->CreateBuffer(buffer,&data,&constants);check(constants!=nullptr,"Native bump parameters allocation failed");
    RefCntAutoPtr<IShaderResourceBinding> binding;impl.bumpPipeline->CreateShaderResourceBinding(&binding,true);
    check(binding!=nullptr,"Native bump binding creation failed");
    auto* parametersBinding=binding->GetVariableByName(SHADER_TYPE_PIXEL,"BumpView");
    auto* sourceBinding=binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Source");
    check(parametersBinding && sourceBinding,"Native bump shader resource ABI mismatch");
    parametersBinding->Set(constants);sourceBinding->Set(imageView(source));
    auto* target=image->resource->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
    impl.context->SetRenderTargets(1,&target,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    impl.context->SetPipelineState(impl.bumpPipeline);
    const Viewport viewport{0,0,float(width),float(height),0,1};impl.context->SetViewports(1,&viewport,width,height);
    impl.context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    DrawAttribs draw{3,DRAW_FLAG_VERIFY_ALL};impl.context->Draw(draw);
    impl.context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    if(image->resource->GetDesc().MipLevels>1) impl.context->GenerateMips(image->resource->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
    image->generation=impl.next_generation++;
    image->description={width,height,4,VSWorldContracts::ImageOrigin::TopLeft,VSWorldContracts::ColorEncoding::Data,
        VSWorldContracts::AlphaMode::Opaque,VSWorldContracts::ImageRole::RemoteAsset,image->generation};
    image->description.mipLevels=image->resource->GetDesc().MipLevels;image->description.gpuBytes=bytes;
    image->usage.attach(impl.imageUsage,bytes,0);
    impl.submitted.emplace_back();auto& submission=impl.submitted.back();
    submission.images={source,image};submission.bindings.push_back(std::move(binding));submission.vertices=std::move(constants);
    submission.completion=impl.next_completion++;impl.context->EnqueueSignal(impl.fence,submission.completion);
    return image;
}
std::uint64_t VSUIRenderer::generation(const Image& image) { return image ? image->generation : 0; }
void          VSUIRenderer::updateFontAtlas(const FontAtlas& atlas, const std::vector<std::uint8_t>& rgba)
{
    check(atlas && atlas->fontAtlas && atlas->owner.RawPtr() == mImpl->device.RawPtr(), "Invalid native font atlas owner");
    const auto& desc = atlas->resource->GetDesc();
    check(rgba.size() == atlas->pixels.size(), "Invalid native font atlas byte count");
    if (rgba == atlas->pixels)
        return;
    // LLFontFreeType updates its existing GL atlas with setSubImage. Do the
    // same on the immediate Vulkan context: transitions order earlier reads,
    // this transfer and subsequent reads without allocating another texture.
    // Retain the atlas even if its font cache is reset before the transfer ends.
    mImpl->submitted.emplace_back();
    auto& submission = mImpl->submitted.back();
    submission.images.push_back(atlas);
    TextureSubResData data{};
    data.pData  = rgba.data();
    data.Stride = std::uint64_t(desc.Width) * 4;
    mImpl->context->UpdateTexture(atlas->resource, 0, 0, Box{ 0, desc.Width, 0, desc.Height }, data,
                                  RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    atlas->pixels            = rgba;
    atlas->description.alpha = alphaMode(rgba);
    submission.completion    = mImpl->next_completion++;
    mImpl->context->EnqueueSignal(mImpl->fence, submission.completion);
}
VSUIRenderer::Image VSUIRenderer::replace(const Image& old,unsigned x,unsigned y,unsigned width,unsigned height,
                                         const std::vector<std::uint8_t>& rgba)
{
    check(old && old->owner.RawPtr()==mImpl->device.RawPtr(),"Invalid native UI replacement source");
    auto desc=old->resource->GetDesc();
    check(width && height && x<=desc.Width && y<=desc.Height && width<=desc.Width-x && height<=desc.Height-y &&
          rgba.size()==std::size_t(width)*height*4,"Invalid native UI replacement patch");
    if(desc.MipLevels>1 || old->description.compression==VSWorldContracts::ImageCompression::BC3)
    {
        auto replacement=old->pixels;
        for(unsigned row=0;row<height;++row)
            std::copy_n(rgba.data()+std::size_t(row)*width*4,width*4,replacement.data()+(std::size_t(y+row)*desc.Width+x)*4);
        return upload(desc.Width,desc.Height,replacement,old->description.role,
                      old->description.compression==VSWorldContracts::ImageCompression::BC3);
    }
    auto& budget   = mImpl->imageBudget(old->description.role);
    auto residency=budget.reserve(old->pixels.size()*2);
    if (!residency) {++mImpl->imageAllocationRefusals;return {};}
    auto image=std::make_shared<Texture>(); image->owner=mImpl->device;image->residency=std::move(residency);
    image->pixels=old->pixels;
    for (unsigned row=0;row<height;++row)
        std::copy_n(rgba.data()+std::size_t(row)*width*4,width*4,
                    image->pixels.data()+(std::size_t(y+row)*desc.Width+x)*4);
    desc.Name="Native UI replacement generation"; desc.Usage=USAGE_DEFAULT;
    mImpl->device->CreateTexture(desc,nullptr,&image->resource);
    check(image->resource != nullptr,"Native UI replacement allocation failed");
    // Copy the unmodified atlas/image on the GPU and upload only changed bytes.
    // Retain both generations until the queued copy and patch have completed.
    mImpl->submitted.emplace_back(); auto& submission=mImpl->submitted.back();
    submission.images={old,image};
    CopyTextureAttribs copy{}; copy.pSrcTexture=old->resource; copy.pDstTexture=image->resource;
    copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    mImpl->context->CopyTexture(copy);
    TextureSubResData data{}; data.pData=rgba.data(); data.Stride=std::uint64_t(width)*4;
    mImpl->context->UpdateTexture(image->resource,0,0,Box{x,x+width,y,y+height},data,
        RESOURCE_STATE_TRANSITION_MODE_TRANSITION,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    image->generation=mImpl->next_generation++;
    image->description=old->description;image->description.generation=image->generation;
    image->description.alpha=alphaMode(image->pixels);
    image->usage.attach(mImpl->imageUsage,image->description.gpuBytes,image->pixels.size());
    submission.completion=mImpl->next_completion++;
    mImpl->context->EnqueueSignal(mImpl->fence,submission.completion);
    return image;
}
void VSUIRenderer::draw(ITextureView* target, unsigned width, unsigned height, float dpi, const std::vector<Packet>& packets)
{
    mImpl->statistics={};mImpl->statistics.packets=packets.size();
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
        check((p.blend == Blend::StraightAlpha || p.blend == Blend::PremultipliedAlpha ||
               p.blend == Blend::Additive || p.blend == Blend::AdditiveAlpha || p.blend == Blend::Replace) &&
              (p.sampling == Sampling::Nearest || p.sampling == Sampling::Linear),"Invalid native UI packet state");
        for (const auto* values : {&p.bounds, &p.uv, &p.color, &p.clip})
            for (float value : *values) check(std::isfinite(value), "Non-finite native UI packet");
        for (float value : p.transform) check(std::isfinite(value), "Non-finite native UI transform");
        check(p.bounds[2] >= p.bounds[0] && p.bounds[3] >= p.bounds[1], "Inverted native UI bounds");
        // Clamp before converting to signed integers; enormous finite clips are valid.
        auto edge = [dpi](float value, unsigned limit, bool end)
        {
            // Packet coordinates and DPI are float UI values. Round their
            // product at that precision before outward scissor rounding, so
            // integer physical scissors survive logical-coordinate conversion
            // (607 / 1.25f must not become a 608-pixel clip on reconstruction).
            const double v=std::clamp(double(value*dpi),0.0,double(limit));
            return int(end ? std::ceil(v) : std::floor(v));
        };
        Rect clip{edge(p.clip[0],width,false), edge(p.clip[1],height,false),
                  edge(p.clip[2],width,true), edge(p.clip[3],height,true)};
        if (clip.right <= clip.left || clip.bottom <= clip.top ||
            (!p.triangles && (p.bounds[2] == p.bounds[0] || p.bounds[3] == p.bounds[1]))) continue;
        if (p.triangles)
        {
            check(vertices.size() <= std::numeric_limits<Uint32>::max()-6,"Native UI vertex count overflow");
            draws.push_back({&p,clip,Uint32(vertices.size())});
            for (auto v:*p.triangles)
            {
                for (float value: {v.x,v.y,v.u,v.v,v.r,v.g,v.b,v.a})
                    check(std::isfinite(value),"Non-finite native UI triangle");
                const auto& t=p.transform;
                const double x=(double(t[0])*v.x+double(t[2])*v.y+t[4])*dpi;
                const double y=(double(t[1])*v.x+double(t[3])*v.y+t[5])*dpi;
                check(std::abs(x*2/width-1)<=std::numeric_limits<float>::max() &&
                      std::abs(1-y*2/height)<=std::numeric_limits<float>::max(),"Native UI transformed vertex overflow");
                v.x=float(x*2/width-1);v.y=float(1-y*2/height);
                vertices.push_back(v);
            }
            continue;
        }
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
    mImpl->statistics.vertices=vertices.size();
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
    for (std::size_t index=0;index<draws.size();)
    {
        const auto& draw=draws[index];
        const auto& p=*draw.packet;
        Uint32 count=6;
        std::size_t next=index+1;
        // Vertices already contain each packet's transform and color. Merge
        // only adjacent compatible draws: never reorder painter operations,
        // cross a scissor boundary or combine distinct image generations.
        while (next<draws.size())
        {
            const auto& candidate=draws[next];const auto& q=*candidate.packet;
            if (q.image!=p.image || q.blend!=p.blend || q.sampling!=p.sampling || q.alpha_mask!=p.alpha_mask ||
                candidate.clip.left!=draw.clip.left || candidate.clip.right!=draw.clip.right ||
                candidate.clip.top!=draw.clip.top || candidate.clip.bottom!=draw.clip.bottom ||
                candidate.first!=draw.first+count) break;
            count+=6;++next;
        }
        auto* pipeline=mImpl->pipeline(desc.Format,p.blend,p.sampling,p.alpha_mask);
        RefCntAutoPtr<IShaderResourceBinding> binding; pipeline->CreateShaderResourceBinding(&binding,true);
        check(binding != nullptr,"Native UI resource binding creation failed");
        auto* variable=binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Texture");
        check(variable != nullptr,"Native UI texture binding missing");
        variable->Set(p.image->resource->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
        context->SetPipelineState(pipeline); context->SetScissorRects(1,&draw.clip,width,height);
        context->CommitShaderResources(binding,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        context->Draw(DrawAttribs{count,DRAW_FLAG_VERIFY_ALL,1,draw.first});
        ++mImpl->statistics.draws;
        submission.images.push_back(p.image); submission.bindings.push_back(binding);
        index=next;
    }
    submission.completion=mImpl->next_completion++;
    context->EnqueueSignal(mImpl->fence,submission.completion);
}
VSUIRenderer::Statistics VSUIRenderer::statistics() const
{
    auto result=mImpl->statistics;
    result.residentImageBytes     = mImpl->coreImages.used() + mImpl->remoteImages.used() + mImpl->compositeImages.used();
    result.imageBudgetBytes       = mImpl->coreImages.limit() + mImpl->remoteImages.limit() + mImpl->compositeImages.limit();
    result.residentGPUImageBytes=mImpl->imageUsage->gpu.load();
    result.residentImageSnapshotBytes=mImpl->imageUsage->snapshot.load();
    result.imageAllocationRefusals=mImpl->imageAllocationRefusals;return result;
}
VSWorldContracts::Image VSUIRenderer::imageDescription(const Image& image)
{ return image?image->description:VSWorldContracts::Image{}; }
#if defined(VS_VULKAN_DIAGNOSTICS) && VS_VULKAN_DIAGNOSTICS
std::vector<std::uint8_t> VSUIRenderer::readbackImage(const Image& image)
{
    check(image && imageOwnedBy(image,mImpl->device),"Invalid native image readback owner");
    auto desc=image->resource->GetDesc();
    check(desc.Format==TEX_FORMAT_RGBA8_UNORM && desc.Width && desc.Height,"Unsupported native image readback format");
    desc.Name="Native cached-image diagnostic readback";desc.MipLevels=1;
    desc.Usage=USAGE_STAGING;desc.BindFlags=BIND_NONE;desc.CPUAccessFlags=CPU_ACCESS_READ;
    desc.MiscFlags=MISC_TEXTURE_FLAG_NONE;
    RefCntAutoPtr<ITexture> staging;mImpl->device->CreateTexture(desc,nullptr,&staging);
    check(staging!=nullptr,"Native cached-image staging allocation failed");
    std::vector<std::uint8_t> pixels(std::size_t(desc.Width)*desc.Height*4);
    auto* context=mImpl->context.RawPtr();
    context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    CopyTextureAttribs copy{};copy.pSrcTexture=image->resource;copy.pDstTexture=staging;
    copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
    context->CopyTexture(copy);context->Flush();context->WaitForIdle();
    MappedTextureSubresource mapped{};
    context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_NONE,nullptr,mapped);
    check(mapped.pData!=nullptr,"Native cached-image mapping failed");
    for(unsigned row=0;row<desc.Height;++row)
        std::copy_n(static_cast<const std::uint8_t*>(mapped.pData)+row*mapped.Stride,desc.Width*4,
                    pixels.data()+std::size_t(row)*desc.Width*4);
    context->UnmapTextureSubresource(staging,0,0);
    return pixels;
}
#endif
Diligent::ITextureView* VSUIRenderer::imageView(const Image& image,const VSWorldContracts::Sampler& input)
{
    if(!image) return nullptr;
    const auto& capabilities=image->owner->GetAdapterInfo().Sampler;
    const bool aniso=capabilities.MaxAnisotropy>1 && input.mipmaps &&
        input.minification==VSWorldContracts::Filter::Linear && input.magnification==VSWorldContracts::Filter::Linear;
    const unsigned anisotropy=aniso?std::clamp(input.anisotropy,1u,unsigned(capabilities.MaxAnisotropy)):1u;
    const Texture::SamplerKey key{int(input.minification),int(input.magnification),int(input.u),int(input.v),input.mipmaps,input.trilinear,anisotropy};
    if(const auto found=image->sampledViews.find(key);found!=image->sampledViews.end()) return found->second;
    SamplerDesc sampler{};
    sampler.MinFilter=input.minification==VSWorldContracts::Filter::Nearest?FILTER_TYPE_POINT:FILTER_TYPE_LINEAR;
    sampler.MagFilter=input.magnification==VSWorldContracts::Filter::Nearest?FILTER_TYPE_POINT:FILTER_TYPE_LINEAR;
    sampler.MipFilter=input.trilinear?FILTER_TYPE_LINEAR:FILTER_TYPE_POINT;
    if(anisotropy>1) sampler.MinFilter=sampler.MagFilter=sampler.MipFilter=FILTER_TYPE_ANISOTROPIC;
    sampler.MaxAnisotropy=anisotropy;
    sampler.MaxLOD=input.mipmaps?float(image->description.mipLevels-1):0;
    const auto address=[](VSWorldContracts::AddressMode mode) {return mode==VSWorldContracts::AddressMode::Clamp?TEXTURE_ADDRESS_CLAMP:
        mode==VSWorldContracts::AddressMode::Mirror?TEXTURE_ADDRESS_MIRROR:TEXTURE_ADDRESS_WRAP;};
    sampler.AddressU=address(input.u);sampler.AddressV=address(input.v);sampler.AddressW=TEXTURE_ADDRESS_CLAMP;
    RefCntAutoPtr<ISampler> state;image->owner->CreateSampler(sampler,&state);check(state!=nullptr,"Native image sampler creation failed");
    auto description=image->resource->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->GetDesc();
    RefCntAutoPtr<ITextureView> view;image->resource->CreateView(description,&view);check(view!=nullptr,"Native sampled image view creation failed");
    view->SetSampler(state);
    // A finite sampler lattice is owned by the image generation itself. It
    // cannot keep an evicted image's residency alive through a global cache.
    check(image->sampledViews.size()<512,"Native image sampler variant bound exceeded");
    auto inserted=image->sampledViews.emplace(key,std::move(view));return inserted.first->second;
}
bool VSUIRenderer::imageOwnedBy(const Image& image,const IRenderDevice* device)
{ return image && image->owner.RawPtr()==device; }
VSWorldContracts::Capabilities VSUIRenderer::capabilities() const
{
    VSWorldContracts::Capabilities result;
    result.maximumImageDimension=mImpl->device->GetAdapterInfo().Texture.MaxTexture2DDimension;
    result.rgba8=mImpl->device->GetTextureFormatInfo(TEX_FORMAT_RGBA8_UNORM).Supported;
    result.bgra8=mImpl->device->GetTextureFormatInfo(TEX_FORMAT_BGRA8_UNORM).Supported;
    result.depth32=mImpl->device->GetTextureFormatInfo(TEX_FORMAT_D32_FLOAT).Supported;
    return result;
}
void VSUIRenderer::flushUploads()
{
    if(!mImpl->pendingUploads.empty())
    {
        mImpl->submitted.emplace_back();auto& uploads=mImpl->submitted.back();
        uploads.images=std::move(mImpl->pendingUploads);uploads.completion=mImpl->next_completion++;
        mImpl->context->EnqueueSignal(mImpl->fence,uploads.completion);
    }
}
void VSUIRenderer::retire()
{
    flushUploads();
    const auto completed=mImpl->fence->GetCompletedValue();
    while (!mImpl->submitted.empty() && mImpl->submitted.front().completion &&
           mImpl->submitted.front().completion <= completed) mImpl->submitted.pop_front();
}
