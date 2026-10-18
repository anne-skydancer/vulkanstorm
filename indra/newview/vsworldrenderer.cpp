// Native indexed world geometry and submission ownership. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldrenderer.h"
#include "vsresourcebudget.h"
#include "vsterrainshader.h"
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
    std::array<float,4> surfaceFlags{}, surfaceParams{}, emissive{}, specular{};
    std::array<float,4> channelSets{}, normalOcclusion{}, normalTangent{};
    std::array<std::array<float,4>,VSMaterial::ChannelCount*2> uv{};
};
struct alignas(16) TerrainConstants
{
    std::array<std::array<float,4>,4> base{},factors{},emission{};
    std::array<std::array<float,4>,8> transform{};
    std::array<std::array<float,4>,4> tangent{};
    std::array<float,4> settings{},paint{};
};
static_assert(sizeof(TerrainConstants)==416);
static_assert(sizeof(Constants)==416);
static_assert(sizeof(VSWorldRenderer::Vertex)==72);
void checkWorldABI(IShader& shader)
{
    bool found=false;
    for (Uint32 i=0;i<shader.GetResourceCount();++i)
    {
        ShaderResourceDesc resource{};shader.GetResourceDesc(i,resource);
        if (!resource.Name || std::strcmp(resource.Name,"WorldView")) continue;
        const auto* buffer=shader.GetConstantBufferDesc(i);
        requireWorld(buffer && buffer->Size==sizeof(Constants) && buffer->NumVariables==14,"World uniform block ABI mismatch");
        const char* names[]{"projection","sun","ambient","tint","material","regionOffset","surfaceFlags","surfaceParams","emissive","specular","channelSets","normalOcclusion","normalTangent","uvTransform"};
        const std::size_t offsets[]{offsetof(Constants,projection),offsetof(Constants,sun),offsetof(Constants,ambient),offsetof(Constants,color),offsetof(Constants,material),offsetof(Constants,regionOffset),offsetof(Constants,surfaceFlags),offsetof(Constants,surfaceParams),offsetof(Constants,emissive),offsetof(Constants,specular),offsetof(Constants,channelSets),offsetof(Constants,normalOcclusion),offsetof(Constants,normalTangent),offsetof(Constants,uv)};
        for (unsigned v=0;v<14;++v)
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
    RefCntAutoPtr<IShader> terrainFragments[4];
    RefCntAutoPtr<IPipelineState> pipelines[2][4][3][2][2][5];
    RefCntAutoPtr<ITexture> depth, white, flatNormal;
    RefCntAutoPtr<ISampler> terrainClamp;
    RefCntAutoPtr<IFence> fence;
    Uint64 next = 1,nextGeometryGeneration=1;
    struct DrawResources
    {
        RefCntAutoPtr<IBuffer> constants, terrainConstants;
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
        RefCntAutoPtr<IBuffer> environment;
    };
    std::deque<Submission> submitted;
    std::vector<RefCntAutoPtr<IBuffer>> environmentAvailable;
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
        SamplerDesc sampler{};sampler.MinFilter=sampler.MagFilter=FILTER_TYPE_LINEAR;sampler.MipFilter=FILTER_TYPE_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=TEXTURE_ADDRESS_WRAP;
        RefCntAutoPtr<ISampler> whiteSampler;device->CreateSampler(sampler,&whiteSampler);
        requireWorld(whiteSampler!=nullptr,"World fallback sampler creation failed");white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(whiteSampler);
        const Uint32 normalPixel=0xffff8080;
        sr.pData=&normalPixel;device->CreateTexture(td,&data,&flatNormal);
        requireWorld(flatNormal!=nullptr,"World flat normal creation failed");
        flatNormal->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->SetSampler(whiteSampler);
        sampler.AddressU=sampler.AddressV=sampler.AddressW=TEXTURE_ADDRESS_CLAMP;
        device->CreateSampler(sampler,&terrainClamp);
        requireWorld(terrainClamp!=nullptr,"Terrain clamp sampler creation failed");
    }
    IPipelineState* pipeline(TEXTURE_FORMAT format, bool alpha, bool background, Topology topology, bool doubleSided, bool terrain, unsigned terrainProgram, bool depthWriteAlpha)
    {
        requireWorld(format==TEX_FORMAT_RGBA8_UNORM || format==TEX_FORMAT_BGRA8_UNORM,"Unsupported world target format");
        auto& result = pipelines[format == TEX_FORMAT_BGRA8_UNORM][background ? 2 : alpha ? (depthWriteAlpha ? 3 : 1) : 0][unsigned(topology)][doubleSided][terrain][terrainProgram];
        if (result) return result;
        const char* vs = R"(
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 uv1;
layout(location=4) in vec4 tangent;
layout(location=5) in vec4 vertexColor;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; vec4 surfaceFlags; vec4 surfaceParams; vec4 emissive; vec4 specular; vec4 channelSets; vec4 normalOcclusion; vec4 normalTangent; vec4 uvTransform[10]; };
layout(std140) uniform EnvironmentView { vec4 environment[23]; };
layout(location=0) out vec2 texUV;
layout(location=1) out vec3 light;
layout(location=2) out vec3 worldNormal;
layout(location=3) out vec3 worldPosition;
layout(location=4) out vec2 texUV1;
layout(location=5) out vec4 worldTangent;
layout(location=6) out vec4 faceColor;
layout(location=7) out vec3 skyColor;
layout(location=8) out float skyLightDot;
// Same CPU publication and equations as class1/deferred/skyV.glsl.
vec3 environmentSky(vec3 position,out float lightDot) {
    vec3 lightnorm=environment[4].xyz;
    vec3 sunlight_color=environment[5].xyz,moonlight_color=environment[6].xyz;
    vec3 ambient_color=environment[7].xyz,blue_horizon=environment[8].xyz,blue_density=environment[9].xyz;
    float haze_horizon=environment[10].x,haze_density=environment[10].y;
    float density_multiplier=environment[10].z,max_y=environment[10].w;
    vec3 glow=environment[11].xyz;
    float cloud_shadow=environment[12].x,sun_moon_glow_factor=environment[12].z;
    int sun_up_factor=int(environment[12].y);
    vec3 rel_pos=position-vec3(0,environment[0].w,0)+vec3(0,50,0);
    // Adj position vector to clamp altitude
    if (rel_pos.y > 0.)
    {
        rel_pos *= (max_y / rel_pos.y);
    }
    if (rel_pos.y < 0.)
    {
        rel_pos *= (-32000. / rel_pos.y);
    }

    // Normalized
    vec3  rel_pos_norm = normalize(rel_pos);
    float rel_pos_len  = length(rel_pos);

    // Grab this value and pass to frag shader for rainbows
    float rel_pos_lightnorm_dot = dot(rel_pos_norm, lightnorm.xyz);
    lightDot = rel_pos_lightnorm_dot;

    // Initialize temp variables
    vec3 sunlight = (sun_up_factor == 1) ? sunlight_color : moonlight_color * 0.7; //magic 0.7 to match legacy color

    // Sunlight attenuation effect (hue and brightness) due to atmosphere
    // this is used later for sunlight modulation at various altitudes
    vec3 light_atten = (blue_density + vec3(haze_density * 0.25)) * (density_multiplier * max_y);

    // Calculate relative weights
    vec3 combined_haze = max(abs(blue_density) + vec3(abs(haze_density)), vec3(1e-6));
    vec3 blue_weight   = blue_density / combined_haze;
    vec3 haze_weight   = haze_density / combined_haze;

    // Compute sunlight from rel_pos & lightnorm (for long rays like sky)
    float off_axis = 1.0 / max(1e-6, max(0., rel_pos_norm.y) + lightnorm.y);
    sunlight *= exp(-light_atten * off_axis);

    // Distance
    float density_dist = rel_pos_len * density_multiplier;

    // Transparency (-> combined_haze)
    // ATI Bugfix -- can't store combined_haze*density_dist in a variable because the ati
    // compiler gets confused.
    combined_haze = exp(-combined_haze * density_dist);

    // Compute haze glow
    float haze_glow = 1.0 - rel_pos_lightnorm_dot;
    // haze_glow is 0 at the sun and increases away from sun
    haze_glow = max(haze_glow, .001);
    // Set a minimum "angle" (smaller glow.y allows tighter, brighter hotspot)
    haze_glow *= glow.x;
    // Higher glow.x gives dimmer glow (because next step is 1 / "angle")
    haze_glow = pow(haze_glow, glow.z);
    // glow.z should be negative, so we're doing a sort of (1 / "angle") function

    // Add "minimum anti-solar illumination"
    // For sun, add to glow.  For moon, remove glow entirely. SL-13768
    haze_glow = (sun_moon_glow_factor < 1.0) ? 0.0 : (sun_moon_glow_factor * (haze_glow + 0.25));

    // Haze color above cloud
    vec3 color = (blue_horizon * blue_weight * (sunlight + ambient_color)
               + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient_color));

    // Final atmosphere additive
    color *= (1. - combined_haze);

    // Increase ambient when there are more clouds
    vec3 ambient = ambient_color + max(vec3(0), (1. - ambient_color)) * cloud_shadow * 0.5;

    // Dim sunlight by cloud shadow percentage
    sunlight *= max(0.0, (1. - cloud_shadow));

    // Haze color below cloud
    vec3 add_below_cloud = (blue_horizon * blue_weight * (sunlight + ambient)
                         + (haze_horizon * haze_weight) * (sunlight * haze_glow + ambient));

    // Attenuate cloud color by atmosphere
    combined_haze = sqrt(combined_haze);  // less atmos opacity (more transparency) below clouds

    // At horizon, blend high altitude sky color towards the darker color below the clouds
    color += (add_below_cloud - color) * (1. - sqrt(combined_haze));

    // Haze color above cloud
    return color;
}
void main() {
    skyColor=vec3(0);skyLightDot=0;
    vec4 p;
    if(material.w>1.5) {
        // Triangle-encoded wide lines use actual world endpoints, so camera
        // movement only updates ordinary draw constants, never their meshes.
        vec4 a=projection*vec4(position+regionOffset.xyz,1);
        vec4 b=projection*vec4(normal+regionOffset.xyz,1);
        float first=0.0,last=1.0;
        bool visible=true;
        for(int axis=0;axis<3;++axis) for(int side=0;side<2;++side) {
            float sign=side==0?-1.0:1.0;
            float av=a.w+sign*a[axis],bv=b.w+sign*b[axis];
            if(av<0.0 && bv<0.0) visible=false;
            if(av<0.0 && bv>=0.0) first=max(first,av/(av-bv));
            if(bv<0.0 && av>=0.0) last=min(last,av/(av-bv));
        }
        vec4 initial=a;
        a=mix(initial,b,first);b=mix(initial,b,last);
        p=vec4(2,2,2,1);
        if(visible && first<=last && a.w>0.0 && b.w>0.0) {
            vec3 start=a.xyz/a.w,end=b.xyz/b.w;
            vec2 delta=(end.xy-start.xy)*normalOcclusion.yz;
            float distance=length(delta);
            if(distance>1e-12) {
                vec2 offset=vec2(-delta.y,delta.x)/distance*normalOcclusion.x/normalOcclusion.yz;
                p=vec4(mix(start,end,uv.x),1);
                p.xy+=uv.y*offset;
            }
        }
    } else p=material.w>0.5 ? vec4(position,1) : projection*vec4(position+regionOffset.xyz,1);
    if(material.w>.5 && material.w<1.5 && environment[0].x>.5) {
        vec3 relative=position-vec3(0,environment[0].w,0);
        vec3 world=environment[13].xyz+.333*relative.zxy;
        p=projection*vec4(world,1);
        // LLGLSquashToFarClip's reference layer-zero projection, before Vulkan depth conversion.
        p.z=.99999*p.w;
        skyColor=environmentSky(position,skyLightDot);
    }
    p.z=(p.z+p.w)*0.5;
    gl_Position=p; texUV=uv; texUV1=uv1; worldTangent=tangent; faceColor=vertexColor; worldNormal=normal; worldPosition=position+regionOffset.xyz;
    if(surfaceFlags.x>.5 && dot(tangent.xyz,tangent.xyz)>1e-20) {
        // Match the reference vertex stage before raster interpolation.
        vec3 n=normalize(normal),t=normalize(tangent.xyz),b=tangent.w*cross(n,t);
        worldTangent=vec4(normalize(normalTangent.x*t+normalTangent.y*b),tangent.w*normalTangent.z);
    }
    gl_PointSize=1.0;
    light=material.x>0.5 ? vec3(1) : ambient.rgb+(environment[0].x>.5?environment[16].rgb:vec3(1)-ambient.rgb)*max(dot(normalize(normal),normalize(sun.xyz)),0.0);
}
)";
        const char* fs=R"(
layout(location=0) in vec2 texUV;
layout(location=1) in vec3 light;
layout(location=2) in vec3 worldNormal;
layout(location=3) in vec3 worldPosition;
layout(location=4) in vec2 texUV1;
layout(location=5) in vec4 worldTangent;
layout(location=6) in vec4 faceColor;
layout(location=7) in vec3 skyColor;
layout(location=8) in float skyLightDot;
layout(location=0) out vec4 result;
layout(std140) uniform WorldView { mat4 projection; vec4 sun; vec4 ambient; vec4 tint; vec4 material; vec4 regionOffset; vec4 surfaceFlags; vec4 surfaceParams; vec4 emissive; vec4 specular; vec4 channelSets; vec4 normalOcclusion; vec4 normalTangent; vec4 uvTransform[10]; };
layout(std140) uniform EnvironmentView { vec4 environment[23]; };
uniform sampler2D g_Texture;
uniform sampler2D g_Normal;
uniform sampler2D g_ORM;
uniform sampler2D g_Emissive;
uniform sampler2D g_Occlusion;
vec3 rainbow(float d)
{
    // 'Interesting' values of d are -0.75 .. -0.825, i.e. when view vec nearly opposite of sun vec
    // Rainbox tex is mapped with REPEAT, so -.75 as tex coord is same as 0.25.  -0.825 -> 0.175. etc.
    // SL-13629
    // Unfortunately the texture is inverted, so we need to invert the y coord, but keep the 'interesting'
    // part within the same 0.175..0.250 range, i.e. d = (1 - d) - 1.575
    d         = clamp(-0.575 - d, 0.0, 1.0);

    // With the colors in the lower 1/4 of the texture, inverting the coords leaves most of it inaccessible.
    // So, we can stretch the texcoord above the colors (ie > 0.25) to fill the entire remaining coordinate
    // space. This improves gradation, reduces banding within the rainbow interior. (1-0.25) / (0.425/0.25) = 4.2857
    float interior_coord = max(0.0, d - 0.25) * 4.2857;
    d = clamp(d, 0.0, 0.25) + interior_coord;

    float rad = (environment[18].y - 5.0f) / 1024.0f;
    return pow(texture(g_Texture, vec2(rad+0.5, d)).rgb, vec3(1.8)) * environment[18].x;
}
vec3 halo22(float d)
{
    d       = clamp(d, 0.1, 1.0);
    float v = sqrt(clamp(1 - (d * d), 0, 1));
    return texture(g_Normal, vec2(0, v)).rgb * environment[18].z;
}
vec2 channelUV(int i) {
    float set=i==4 ? normalOcclusion.z : channelSets[i];
    vec2 rawUV=set>.5?texUV1:texUV;
    vec2 uv=vec2(rawUV.x,1.0-rawUV.y);
    vec4 m=uvTransform[2*i];
    vec2 p=vec2(m.x*uv.x+m.z*uv.y,m.y*uv.x+m.w*uv.y)+uvTransform[2*i+1].xy;
    return vec2(p.x,1.0-p.y);
}
vec3 toLinear(vec3 c) { return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),greaterThan(c,vec3(0.04045))); }
vec3 toSRGB(vec3 c) { c=max(c,vec3(0)); return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,greaterThan(c,vec3(0.0031308))); }
vec3 mappedNormal(vec2 uv) {
    vec3 n=normalize(worldNormal);
    if (surfaceFlags.z<0.5) return n;
    vec3 value=texture(g_Normal,uv).xyz*2.0-1.0;
    value.xy*=normalOcclusion.x;
    if (dot(worldTangent.xyz,worldTangent.xyz)>1e-20) {
        // LLFace/rigged and imported glTF producers retain the reference
        // tangent, handedness and normal-transform contracts.
        vec3 t=normalize(worldTangent.xyz),b=worldTangent.w*cross(n,t);
        return normalize(mat3(t,b,n)*value);
    }
    vec3 dp1=dFdx(worldPosition),dp2=dFdy(worldPosition);
    vec2 duv1=dFdx(uv),duv2=dFdy(uv);
    vec3 p2=cross(dp2,n),p1=cross(n,dp1);
    vec3 t=p2*duv1.x+p1*duv2.x,b=p2*duv1.y+p1*duv2.y;
    float scale=max(dot(t,t),dot(b,b));
    if (scale<1e-20) return n;
    // Sampling coordinates are top-left, whereas tangent-space Y is +V.
    // Derivatives include per-channel UV rotation and mirror handedness, as
    // textureUtilV.glsl's tangent-space transform does in the reference path.
    return normalize(mat3(t*inversesqrt(scale),-b*inversesqrt(scale),n)*value);
}
void main() {
    if (material.w>0.5 && material.w<1.5) {
        if(environment[0].x>.5) {
            vec3 color=skyColor+(normalOcclusion.w>.5?rainbow(skyLightDot):vec3(0))+(normalOcclusion.x>.5?halo22(skyLightDot):vec3(0));
            result=vec4(clamp(color*2.0,vec3(0),vec3(5)),1);
        } else result=vec4(mix(vec3(0.65,0.8,0.95),vec3(0.08,0.22,0.48),clamp(texUV.y,0.0,1.0)),1);
        return;
    }
    if (surfaceFlags.x<-.5) {
        // terrainF.glsl's four swatches and shipped two-dimensional alpha ramp.
        vec2 coord=vec2(texUV.x,1.0-texUV.y);
        vec2 uv=coord*vec2(uvTransform[0].x,uvTransform[0].w)+uvTransform[1].xy;
        uv.y=1.0-uv.y;
        vec4 c0=texture(g_Texture,uv),c1=texture(g_Normal,uv);
        vec4 c2=texture(g_ORM,uv),c3=texture(g_Emissive,uv);
        float a1=texture(g_Occlusion,vec2(texUV1.x,1.0-texUV1.y)).a;
        float a2=texture(g_Occlusion,vec2(texUV1.x-2.0,1.0-texUV1.y)).a;
        float af=texture(g_Occlusion,vec2(texUV1.x-1.0,1.0-texUV1.y)).a;
        result=vec4(mix(mix(c3,c2,a2),mix(c1,c0,a1),af).rgb*tint.rgb*light,1);
        return;
    }
    vec2 baseUV=surfaceFlags.x>0.5 ? channelUV(0) : texUV;
    vec4 sampleColor=texture(g_Texture,baseUV);
    vec4 c=sampleColor*tint*faceColor;
    if (surfaceFlags.x>0.5 && surfaceFlags.w<0.5) c.a=tint.a*faceColor.a;
    if (material.y>0.5 && c.a<material.z) discard;
    if (surfaceFlags.x<0.5) { result=vec4(c.rgb*light,c.a); return; }
    vec3 n=mappedNormal(channelUV(1));
    if (!gl_FrontFacing) n=-n;
    vec3 illumination=material.x>0.5 ? vec3(1) : ambient.rgb+(environment[0].x>.5?environment[16].rgb:vec3(1)-ambient.rgb)*max(dot(n,normalize(sun.xyz)),0.0);
    vec3 base=toLinear(sampleColor.rgb)*(surfaceFlags.y>0.5 ? tint.rgb : toLinear(tint.rgb))*faceColor.rgb;
    vec3 emission=emissive.rgb*toLinear(texture(g_Emissive,channelUV(3)).rgb);
    if (surfaceParams.w>0.5) emission+=base*sampleColor.a;
    // ORM channels and legacy specular/gloss remain material inputs for R5/R6,
    // rather than inventing a replacement BRDF in this forward foundation.
    float occlusion=surfaceFlags.y>0.5 ? mix(1.0,(normalOcclusion.w>.5?texture(g_Occlusion,channelUV(4)).r:texture(g_ORM,channelUV(2)).r),normalOcclusion.y) : 1.0;
    result=vec4(toSRGB(base*illumination*occlusion+emission),c.a);
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
            for(auto* shader:{vertex.RawPtr(),fragment.RawPtr()}) {
                bool present=false;
                for(Uint32 i=0;i<shader->GetResourceCount();++i) {
                    ShaderResourceDesc resource{};shader->GetResourceDesc(i,resource);
                    if(!resource.Name || std::strcmp(resource.Name,"EnvironmentView"))continue;
                    const auto* block=shader->GetConstantBufferDesc(i);
                    requireWorld(block && block->Size==sizeof(VSEnvironmentState::values) && block->NumVariables==1 &&
                        block->pVariables[0].Offset==0 && !std::strcmp(block->pVariables[0].Name,"environment"),"Native environment ABI mismatch");
                    present=true;
                }
                requireWorld(present,"Native environment reflection missing");
            }

        }
        auto& terrainFragment=terrainFragments[terrainProgram?terrainProgram-1:0];
        if(terrainProgram && !terrainFragment)
        {
            ShaderCreateInfo shader{};shader.SourceLanguage=SHADER_SOURCE_LANGUAGE_GLSL;
            shader.LoadConstantBufferReflection=true;shader.GLSLVersion={4,5};shader.EntryPoint="main";
            shader.Desc.UseCombinedTextureSamplers=false;
            shader.Desc.Name="Native terrain PBR fragment";shader.Desc.ShaderType=SHADER_TYPE_PIXEL;
            const auto source=VSTerrainShader::source(int(terrainProgram)-4);shader.Source=source.c_str();
            device->CreateShader(shader,&terrainFragment);
            requireWorld(terrainFragment!=nullptr,"Terrain PBR shader creation failed");checkWorldABI(*terrainFragment);
            bool reflected=false;
            for(Uint32 i=0;i<terrainFragment->GetResourceCount();++i)
            {
                ShaderResourceDesc resource{};terrainFragment->GetResourceDesc(i,resource);
                if(!resource.Name || std::strcmp(resource.Name,"TerrainView")) continue;
                const auto* buffer=terrainFragment->GetConstantBufferDesc(i);
                requireWorld(buffer && buffer->Size==sizeof(TerrainConstants),"Terrain uniform block ABI mismatch");
                reflected=true;
            }
            requireWorld(reflected,"Terrain material constants reflection missing");
        }
        GraphicsPipelineStateCreateInfo ci{};ci.PSODesc.Name="Native world geometry";
        ci.PSODesc.PipelineType=PIPELINE_TYPE_GRAPHICS;ci.pVS=vertex;ci.pPS=terrainProgram?terrainFragment:fragment;
        auto& gp=ci.GraphicsPipeline;gp.NumRenderTargets=1;gp.RTVFormats[0]=format;
        gp.DSVFormat                                                       = TEX_FORMAT_D32_FLOAT;
        gp.PrimitiveTopology           = topology == Topology::Triangles ? PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
                                         : topology == Topology::Lines   ? PRIMITIVE_TOPOLOGY_LINE_LIST
                                                                         : PRIMITIVE_TOPOLOGY_POINT_LIST;
        gp.RasterizerDesc.CullMode=doubleSided ? CULL_MODE_NONE : CULL_MODE_BACK;
        // The shared terrain strip producer uses the opposite winding from
        // LLVolume faces. Preserve its indices and front-face normal semantics.
        gp.RasterizerDesc.FrontCounterClockwise=terrain;
        gp.DepthStencilDesc.DepthEnable=!background;gp.DepthStencilDesc.DepthWriteEnable=(!alpha || depthWriteAlpha) && !background;
        gp.DepthStencilDesc.DepthFunc=COMPARISON_FUNC_LESS_EQUAL;
        const LayoutElement layout[]={{0,0,3,VT_FLOAT32,false},{1,0,3,VT_FLOAT32,false},{2,0,2,VT_FLOAT32,false},
            {3,0,2,VT_FLOAT32,false},{4,0,4,VT_FLOAT32,false},{5,0,4,VT_FLOAT32,false}};
        gp.InputLayout.LayoutElements=layout;gp.InputLayout.NumElements=6;
        auto& blend=gp.BlendDesc.RenderTargets[0];blend.BlendEnable=alpha;
        blend.SrcBlend=BLEND_FACTOR_SRC_ALPHA;blend.DestBlend=BLEND_FACTOR_INV_SRC_ALPHA;
        blend.SrcBlendAlpha=BLEND_FACTOR_ONE;blend.DestBlendAlpha=BLEND_FACTOR_INV_SRC_ALPHA;
        ci.PSODesc.ResourceLayout.DefaultVariableType=SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE;
        device->CreateGraphicsPipelineState(ci,&result);requireWorld(result!=nullptr,"World pipeline creation failed");
        return result;
    }
};
VSWorldRenderer::VSWorldRenderer(IRenderDevice* d,IDeviceContext* c,std::size_t meshBudgetBytes):mImpl(std::make_unique<Impl>(d,c,meshBudgetBytes)) {}
VSWorldRenderer::~VSWorldRenderer()=default;
VSWorldRenderer::Mesh VSWorldRenderer::upload(const std::vector<Vertex>& vertices, const std::vector<unsigned>& indices, Topology topology,
                                             unsigned attributes)
{
    const unsigned group = topology == Topology::Triangles ? 3 : topology == Topology::Lines ? 2 : 1;
    const auto     reject = [&]() -> Mesh
    {
        ++mImpl->meshInputRejections;
        return {};
    };
    constexpr unsigned required=VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0;
    constexpr unsigned supported=required|VSWorldContracts::UV1|VSWorldContracts::Tangent|VSWorldContracts::Color;
    if((attributes&required)!=required || (attributes&~supported)) return reject();
    if (vertices.empty() || vertices.size() > 1048576 || indices.empty() || indices.size() > 3145728 || indices.size() % group)
        return reject();
    for (const auto& v:vertices)
        for (float f : { v.x, v.y, v.z, v.nx, v.ny, v.nz, v.u, v.v, v.u1, v.v1, v.tx, v.ty, v.tz, v.tw, v.r, v.g, v.b, v.a })
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
    mesh->description.stride=sizeof(Vertex);
    mesh->description.attributes=attributes;
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
                                  Topology topology,unsigned attributes)
{
    auto replacement = upload(vertices, indices, topology,attributes);
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
                for(const char* name:{"g_Texture","g_Normal","g_ORM","g_Emissive","g_Occlusion"})
                {
                    auto* texture=draw.binding->GetVariableByName(SHADER_TYPE_PIXEL,name);
                    if(!texture) continue;
                    texture->Set(mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                                 SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                }
                if(auto* blend=draw.binding->GetVariableByName(SHADER_TYPE_PIXEL,"terrainBlend"))
                {
                    blend->Set(mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                    IDeviceObject* empty[4];std::fill_n(empty,4,mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE));
                    for(const char* channel:{"terrainColor","terrainNormal","terrainORM","terrainEmissive"})
                    {
                        auto* textures=draw.binding->GetVariableByName(SHADER_TYPE_PIXEL,channel);
                        if(!textures) continue;
                        textures->SetArray(empty,0,4,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
                    }
                }
                mImpl->available[draw.pipeline].push_back(std::move(draw));
                ++mImpl->availableCount;
            }
        if(mImpl->submitted.front().environment && mImpl->environmentAvailable.size()<32)
            mImpl->environmentAvailable.push_back(std::move(mImpl->submitted.front().environment));
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
    if(!mImpl->environmentAvailable.empty()) {
        submitted.environment=std::move(mImpl->environmentAvailable.back());mImpl->environmentAvailable.pop_back();
    } else {
        BufferDesc desc{};desc.Name="Native frame environment";desc.Size=sizeof(view.environment.values);
        desc.Usage=USAGE_DEFAULT;desc.BindFlags=BIND_UNIFORM_BUFFER;
        mImpl->device->CreateBuffer(desc,nullptr,&submitted.environment);
        requireWorld(submitted.environment!=nullptr,"Native frame environment allocation failed");
        ++mImpl->statistics.constantBuffersCreated;
    }
    context->UpdateBuffer(submitted.environment,0,sizeof(view.environment.values),view.environment.values.data(),RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    mImpl->statistics.constantUploadBytes+=sizeof(view.environment.values);

    submitted.depth=mImpl->depth;submitted.packets=packets;
    std::stable_sort(submitted.packets.begin(), submitted.packets.end(),
                     [](const Packet& a, const Packet& b)
                     {
                         if (a.hud != b.hud)
                             return !a.hud;
                         if (a.background != b.background)
                             return a.background;
                         if (a.overlay != b.overlay)
                             return !a.overlay;
                         if (a.overlay) return false; // Preserve shared overlay paint order.
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
        auto* pipeline = mImpl->pipeline(target->GetTexture()->GetDesc().Format, p.alpha, p.background, p.mesh->topology, !p.surface || p.doubleSided || p.background, p.terrain, p.terrainMaterial?unsigned(std::clamp(int(p.terrainMaterial->detail),-3,0)+4):0u,p.depthWriteAlpha);
        for (float f:p.regionOffset) requireWorld(std::isfinite(f),"Non-finite world region translation");
        requireWorld(std::isfinite(p.alphaCutoff) && p.alphaCutoff >= 0 && p.alphaCutoff <= 1, "Invalid world alpha cutoff");
        Constants constants{ view.projection,
                             view.sun,
                             view.ambient,
                             p.color,
                             { p.fullbright ? 1.f : 0.f, p.masked ? 1.f : 0.f, p.alphaCutoff, p.background ? 1.f : p.screenLinePixels>0 ? 2.f : 0.f },
                             p.regionOffset };
        requireWorld(std::isfinite(p.screenLinePixels) && p.screenLinePixels>=0,"Invalid native screen-line width");
        if(p.screenLinePixels>0) constants.normalOcclusion={p.screenLinePixels,float(width),float(height),0};
        if(p.background) {
            constants.normalOcclusion[3]=p.image?1.f:0.f;
            constants.normalOcclusion[0]=p.channels[0]?1.f:0.f;
        }
        if (p.surface)
        {
            const auto& surface=*p.surface;
            constants.surfaceFlags={1, surface.pbr?1.f:0.f, p.channels[0]?1.f:0.f,
                surface.alpha!=VSWorldContracts::AlphaMode::Opaque?1.f:0.f};
            constants.surfaceParams={surface.metallic,surface.roughness,surface.glow,surface.emissiveAlpha?1.f:0.f};
            std::copy(surface.emissive.begin(),surface.emissive.end(),constants.emissive.begin());
            constants.specular=surface.specular;
            constants.normalTangent=surface.normalTangent;
            for(unsigned i=0;i<4;++i) constants.channelSets[i]=float(surface.uvSet[i]);
            constants.normalOcclusion={surface.normalScale,surface.occlusionStrength,float(surface.uvSet[VSMaterial::Occlusion]),surface.independentOcclusion?1.f:0.f};
            for (unsigned i=0;i<VSMaterial::ChannelCount;++i)
            {
                constants.uv[2*i]=surface.uv[i].matrix;
                constants.uv[2*i+1]={surface.uv[i].offset[0],surface.uv[i].offset[1],0,0};
            }
        }
        if (p.terrain) constants.surfaceFlags[0]=-1;
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
        if(p.terrainMaterial)
        {
            const auto& terrain=*p.terrainMaterial;
            TerrainConstants values;
            values.settings={terrain.detail,terrain.planar,terrain.sharpness,0};
            values.paint={terrain.paint?1.f:0.f,terrain.regionWidth,0,0};
            for(unsigned layer=0;layer<4;++layer)
            {
                const auto& state=terrain.layers[layer];values.base[layer]=state.base;
                values.factors[layer]={1,state.roughness,state.metallic,
                    state.alpha==VSWorldContracts::AlphaMode::Mask?state.cutoff/std::max(state.base[3],.000001f):0};
                std::copy(state.emissive.begin(),state.emissive.end(),values.emission[layer].begin());
                values.transform[2*layer]=state.uv[0].matrix;
                values.transform[2*layer+1]={state.uv[0].offset[0],state.uv[0].offset[1],0,0};
                values.tangent[layer]=state.normalTangent;
            }
            if(!resources.terrainConstants)
            {
                BufferDesc bd{};bd.Name="Native terrain material constants";bd.Size=sizeof(values);
                bd.Usage=USAGE_DEFAULT;bd.BindFlags=BIND_UNIFORM_BUFFER;
                mImpl->device->CreateBuffer(bd,nullptr,&resources.terrainConstants);
                requireWorld(resources.terrainConstants!=nullptr,"Terrain constants allocation failed");
                ++mImpl->statistics.constantBuffersCreated;
            }
            context->UpdateBuffer(resources.terrainConstants,0,sizeof(values),&values,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
            mImpl->statistics.constantUploadBytes+=sizeof(values);
            auto* uniforms=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,"TerrainView");
            requireWorld(uniforms!=nullptr,"Terrain uniforms missing");uniforms->Set(resources.terrainConstants,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
            const char* names[]{"terrainColor","terrainNormal","terrainORM","terrainEmissive"};
            ISampler* sampler=mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)->GetSampler();
            for(unsigned channel=0;channel<4;++channel)
            {
                IDeviceObject* views[4];
                for(unsigned layer=0;layer<4;++layer)
                {
                    const auto& image=terrain.images[layer][channel];
                    requireWorld(!image || VSUIRenderer::imageOwnedBy(image,mImpl->device),"Terrain image belongs to another device");
                    auto* view=image?VSUIRenderer::imageView(image,terrain.layers[layer].samplers[channel]):
                        (channel==VSMaterial::Normal?mImpl->flatNormal:mImpl->white)->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE);
                    views[layer]=view;if(image && !channel) sampler=view->GetSampler();
                }
                auto* variable=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,names[channel]);
                if(!variable) continue;
                variable->SetArray(views,0,4,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
            }
            requireWorld(!terrain.blend || VSUIRenderer::imageOwnedBy(terrain.blend,mImpl->device),"Terrain blend belongs to another device");
            for(const auto& item:{std::pair<const char*,IDeviceObject*>{"terrainBlend",terrain.blend?VSUIRenderer::imageView(terrain.blend):mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE)},
                {"terrainSampler",sampler},{"terrainClamp",mImpl->terrainClamp.RawPtr()}})
            {
                auto* variable=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,item.first);
                requireWorld(variable!=nullptr,"Terrain sampler/blend binding missing");
                variable->Set(item.second,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
            }
        }
        else
        {
        for(const auto stage:{SHADER_TYPE_VERTEX,SHADER_TYPE_PIXEL})
            if(auto* environment=resources.binding->GetVariableByName(stage,"EnvironmentView"))environment->Set(submitted.environment,SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        auto* texture=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,"g_Texture");
        requireWorld(texture!=nullptr,"World texture binding missing");
        texture->Set(p.image ? VSUIRenderer::imageView(p.image,p.surface?p.surface->samplers[0]:p.imageSampler) : mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),
                     SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        const char* channels[]{"g_Normal","g_ORM","g_Emissive","g_Occlusion"};
        for (unsigned channel=0;channel<VSMaterial::ChannelCount-1;++channel)
        {
            auto* variable=resources.binding->GetVariableByName(SHADER_TYPE_PIXEL,channels[channel]);
            requireWorld(variable!=nullptr,"World material channel binding missing");
            requireWorld(!p.channels[channel] || VSUIRenderer::imageOwnedBy(p.channels[channel],mImpl->device),
                         "World material image belongs to another device");
            variable->Set(p.channels[channel] ? VSUIRenderer::imageView(p.channels[channel],p.surface?p.surface->samplers[channel+1]:VSWorldContracts::Sampler{}) :
                mImpl->white->GetDefaultView(TEXTURE_VIEW_SHADER_RESOURCE),SET_SHADER_RESOURCE_FLAG_ALLOW_OVERWRITE);
        }
        }
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
