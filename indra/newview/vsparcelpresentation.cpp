// Shared parcel selection and access feedback on native world geometry. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsparcelpresentation.h"
#include "vsparcelgeometry.h"
#include "vsworldscene.h"
#include "vsuiresources.h"
#include "vsnativesession.h"
#include "vsregionlimits.h"
#include "llviewerparcelmgr.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "llimage.h"
#include "llparcel.h"
#include "llviewercamera.h"
#include <stdexcept>
#include "lldir.h"
#include "llregionflags.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <limits>

namespace
{
using Boundary = LLViewerParcelMgr::NativeParcelBoundary;
using Vertex = VSWorldRenderer::Vertex;
constexpr float gridStep = PARCEL_GRID_STEP_METERS;
constexpr float parcelPostHeight = .666f;
float vertexAlpha(float value)
{
    return U8(llclamp(value, 0.f, 1.f) * 255.f) / 255.f;
}
struct Crop
{
    S32 left=0, bottom=0, right=0, top=0;
    bool operator==(const Crop& other) const
    {
        return left==other.left && bottom==other.bottom && right==other.right && top==other.top;
    }
};
Crop crop(const VSParcelBitmap& bitmap, const LLVector3& center, float radius)
{
    const auto columns = S32(bitmap.columns()), rows = S32(bitmap.rows());
    return {llclamp(S32(std::floor((center.mV[VX]-radius)/gridStep))-1,0,columns),
            llclamp(S32(std::floor((center.mV[VY]-radius)/gridStep))-1,0,rows),
            llclamp(S32(std::ceil((center.mV[VX]+radius)/gridStep))+1,0,columns),
            llclamp(S32(std::ceil((center.mV[VY]+radius)/gridStep))+1,0,rows)};
}
bool occupied(const VSParcelBitmap& bitmap, S32 x, S32 y)
{
    return x>=0 && y>=0 && bitmap.cell(U32(x),U32(y)).value_or(false);
}
// Iterate only the visible cells. This is exactly the shared XOR boundary rule,
// without expanding a potentially enormous region-wide segment array each frame.
template<class Emit>
void edges(const VSParcelBitmap& bitmap, const Crop& area, Emit&& emit)
{
    for(S32 y=area.bottom;y<=area.top;++y)
        for(S32 x=area.left;x<=area.right;++x)
        {
            if(x<S32(bitmap.columns()) && occupied(bitmap,x,y-1)!=occupied(bitmap,x,y))
                emit(x*gridStep,y*gridStep,(x+1)*gridStep,y*gridStep,true);
            if(y<S32(bitmap.rows()) && occupied(bitmap,x-1,y)!=occupied(bitmap,x,y))
                emit(x*gridStep,y*gridStep,x*gridStep,(y+1)*gridStep,false);
        }
}
float collisionAlpha(float x1,float y1,float x2,float y2,bool south,const LLVector3& position)
{
    const float dx = south ? position.mV[VX]-llclamp(position.mV[VX],x1,x2) : position.mV[VX]-x1+5.f;
    const float dy = south ? position.mV[VY]-y1+5.f : position.mV[VY]-llclamp(position.mV[VY],y1,y2);
    const float distance=dx*dx+dy*dy;
    return vertexAlpha(llclamp(distance<25.f?.95f:distance>169.f?0.f:30.f/distance,0.f,.95f));
}
}
struct VSParcelPresentation::Impl
{
    VSWorldRenderer& renderer;
    VSUIResources& resources;
    VSWorldScene& scene;
    struct Publication
    {
        std::function<bool()> current;
        VSWorldScene::TerrainField terrain;
        VSWorldRenderer::Mesh mesh;
        Crop area;
        LLVector3 origin, agent;
        float height=0;
        bool absolute=false;
    } selection, collision;
    struct Rectangle
    {
        std::function<bool()> current;
        VSWorldScene::TerrainField terrain;
        VSWorldRenderer::Mesh posts, walls;
        LLVector3 origin;
    } rectangle;
    std::array<VSUIResources::PreparedImage,2> prepared;
    Impl(VSWorldRenderer& renderer,VSUIResources& resources,VSWorldScene& scene)
        : renderer(renderer),resources(resources),scene(scene) {}
    VSUIRenderer::Image accessImage(bool pass)
    {
        const unsigned index=pass?1:0;
        const std::string file=pass?"world/NoEntryPassLines.png":"world/NoEntryLines.png";
        const std::string key="world-static:"+file;
        if(auto image=resources.image(key))return image;
        if(!prepared[index])
        {
            const auto path=gDirUtilp->findSkinnedFilename(LLDir::TEXTURES,file);
            LLPointer<LLImageFormatted> formatted=LLImageFormatted::createFromExtension(file);
            LLPointer<LLImageRaw> raw=new LLImageRaw;
            if(path.empty() || !formatted || !formatted->load(path) || !formatted->decode(raw,0))
                throw std::runtime_error("Native parcel access image decode failed: "+file);
            prepared[index]=VSUIResources::prepareImage(*raw,VSWorldContracts::ImageRole::RemoteAsset,false);
        }
        // Publication refusal is temporary; retain decoded local data for retry.
        if(!resources.publishPrepared(key,prepared[index]))return {};
        prepared[index].reset();
        return resources.image(key);
    }
    void appendRectangle(const Boundary& boundary,const VSNativeSession& owner,const VSWorldRenderer::View&,
                         unsigned,unsigned,std::vector<VSWorldRenderer::Packet>& packets)
    {
        if(!boundary.current || !boundary.current())return;
        const auto offset=VSWorldContracts::regionOffset(owner.handle(),boundary.region.getHandle());
        const LLVector3 origin(offset[0],offset[1],offset[2]);
        const auto revision=scene.terrainRevision(boundary.region.getHost());
        if(!revision)return;
        auto& result=rectangle;
        const auto scale=LLUI::getScaleFactor();
        const float linePixels=scale.mV[VX]+scale.mV[VY]; // 2 * reference mean UI scale.
        if(!result.current || !result.current() || !result.terrain.current || !result.terrain.current() ||
           result.terrain.revision!=*revision)
        {
            const auto field=scene.terrainField(boundary.region.getHost());
            if(!field)return;
            const auto west=boundary.region.getPosRegionFromGlobal(boundary.westSouth);
            const auto east=boundary.region.getPosRegionFromGlobal(boundary.eastNorth);
            const float x1=west.mV[VX],y1=west.mV[VY],x2=east.mV[VX],y2=east.mV[VY];
            // Preserve renderRect's exact edge lookup fudge and four independent heights.
            // LLSurface::create initializes unreceived grid samples to zero.
            const float sw=field->referenceHeight(x1,y1).value_or(0.f),se=field->referenceHeight(x2-.01f,y1).value_or(0.f),
                        ne=field->referenceHeight(x2-.01f,y2-.01f).value_or(0.f),nw=field->referenceHeight(x1,y2-.01f).value_or(0.f);
            const std::array<std::array<float,3>,8> corners{{
                {x1,y2,nw},{x1,y2,nw+parcelPostHeight},{x2,y2,ne},{x2,y2,ne+parcelPostHeight},
                {x2,y1,se},{x2,y1,se+parcelPostHeight},{x1,y1,sw},{x1,y1,sw+parcelPostHeight}}};
            std::vector<Vertex> posts,walls;
            std::vector<unsigned> postIndices,wallIndices;
            constexpr float endpoint[]={0,1,1,0,1,0},side[]={-1,-1,1,-1,1,1};
            for(unsigned index=0;index<8;index+=2)
                for(unsigned triangle=0;triangle<6;++triangle)
                {
                    const auto& start=corners[index];const auto& end=corners[index+1];
                    postIndices.push_back(unsigned(posts.size()));
                    posts.push_back({start[0],start[1],start[2],end[0],end[1],end[2],endpoint[triangle],side[triangle]});
                }
            // Original triangle-strip order, including the final reversed pair.
            constexpr unsigned strip[]={0,1,2,3,4,5,7,6,1,0};
            for(unsigned index=0;index<8;++index)
                for(unsigned corner:{strip[index],strip[index+1],strip[index+2]})
                {
                    const auto& point=corners[corner];
                    Vertex vertex{point[0],point[1],point[2],0,0,1,0,0};vertex.a=vertexAlpha(.2f);
                    wallIndices.push_back(unsigned(walls.size()));walls.push_back(vertex);
                }
            VSWorldRenderer::Mesh nextPosts,nextWalls;
            constexpr unsigned attributes=VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::Color;
            const bool accepted=(!posts.empty() && renderer.publishMesh(nextPosts,posts,postIndices,VSWorldRenderer::Topology::Triangles,attributes)) &&
                renderer.publishMesh(nextWalls,walls,wallIndices,VSWorldRenderer::Topology::Triangles,attributes);
            if(accepted)result={boundary.current,*field,std::move(nextPosts),std::move(nextWalls),origin};
            else if(!result.current || !result.current())return;
        }
        VSWorldRenderer::Packet packet;
        packet.fullbright=true;packet.overlay=true;packet.doubleSided=true;packet.color={1,1,0,1};
        if(result.posts)
        {
            packet.mesh=result.posts;packet.screenLinePixels=linePixels;packet.regionOffset=offset;packets.push_back(packet);
        }
        if(result.walls)
        {
            packet.mesh=result.walls;packet.screenLinePixels=0;packet.regionOffset=offset;
            packet.alpha=true;packet.depthWriteAlpha=true;packets.push_back(packet);
        }
    }
    void appendBoundary(const Boundary& boundary,const VSNativeSession& owner,
                        std::vector<VSWorldRenderer::Packet>& packets)
    {
        if(!boundary.bitmap || !boundary.current || !boundary.current())return;
        const auto offset=VSWorldContracts::regionOffset(owner.handle(),boundary.region.getHandle());
        const LLVector3 origin(offset[0],offset[1],offset[2]);
        const auto eye=owner.cameraState().center-origin, agent=owner.position()-origin;
        const float radius=boundary.collision?18.f:LLViewerCamera::instance().getFar();
        const Crop area=crop(*boundary.bitmap,boundary.collision?agent:eye,radius);
        const bool absolute=!boundary.collision && gSavedSettings.getBOOL("FSRenderParcelSelectionToMaxBuildHeight");
        const float height=boundary.collision?
            (boundary.banState==BA_BANNED || boundary.region.getRegionFlag(REGION_FLAGS_BLOCK_FLYOVER)?BAN_HEIGHT:PARCEL_HEIGHT):
            (absolute?vs_region_limits().getMaxPrimZPos()+parcelPostHeight:parcelPostHeight);
        auto& publication=boundary.collision?collision:selection;
        const auto revision=scene.terrainRevision(boundary.region.getHost());
        if(!revision)return;
        if(!publication.current || !publication.current() || !publication.terrain.current || !publication.terrain.current() ||
           publication.terrain.revision!=*revision || !(publication.area==area) || publication.origin!=origin ||
           publication.height!=height || publication.absolute!=absolute || (boundary.collision && publication.agent!=agent))
        {
            const auto rebuild=[&]()->bool
            {
                const auto field=scene.terrainField(boundary.region.getHost());
                if(!field)return false;
                std::size_t count=0;
                edges(*boundary.bitmap,area,[&](float x1,float y1,float x2,float y2,bool south)
                {
                    if(!boundary.collision || collisionAlpha(x1,y1,x2,y2,south,agent)>0)++count;
                });
                if(!count)
                {
                    publication={boundary.current,*field,{},area,origin,agent,height,absolute};return true;
                }
                const auto bytes=U64(count)*6*(sizeof(Vertex)+sizeof(unsigned));
                if(bytes>std::numeric_limits<std::size_t>::max())return false;
                const auto temporary=scene.reserveSpatialBytes(std::size_t(bytes));
                if(!temporary)return false;
                std::vector<Vertex> vertices;
                std::vector<unsigned> indices;
                vertices.reserve(count*6);indices.reserve(count*6);
                edges(*boundary.bitmap,area,[&](float x1,float y1,float x2,float y2,bool south)
                {
                    const float alpha=boundary.collision?collisionAlpha(x1,y1,x2,y2,south,agent):vertexAlpha(.2f);
                    if(alpha==0)return;
                    auto direction=south?VSParcelGeometry::Direction::South:VSParcelGeometry::Direction::West;
                    if(boundary.collision)
                    {
                        if(south && agent.mV[VY]-y1>=0)direction=VSParcelGeometry::Direction::North;
                        if(!south && agent.mV[VX]-x1<=0)direction=VSParcelGeometry::Direction::East;
                        x1+=.1f;y1+=.1f;x2+=.1f;y2+=.1f;
                    }
                    const auto segment=VSParcelGeometry::segment(x1,y1,x2,y2,height,direction,float(field->width),float(field->height),
                        origin.mV[VX],origin.mV[VY],absolute,[&](float x,float y)
                        {
                            // Same initial zero grid as LLSurface::create; accepted
                            // received heights remain in the actual CPU scene.
                            return field->referenceHeight(x,y).value_or(0.f);
                        });
                    for(const auto& source:segment)
                    {
                        Vertex vertex{source.x,source.y,source.z,0,0,1,source.u,source.textured?1.f-source.v:0.f};
                        vertex.a=alpha;
                        indices.push_back(unsigned(vertices.size()));vertices.push_back(vertex);
                    }
                });
                VSWorldRenderer::Mesh next;
                const bool accepted=vertices.empty() || renderer.publishMesh(next,vertices,indices,VSWorldRenderer::Topology::Triangles,
                    VSWorldContracts::Position|VSWorldContracts::Normal|VSWorldContracts::UV0|VSWorldContracts::Color);
                if(accepted)
                    publication={boundary.current,*field,std::move(next),area,origin,agent,height,absolute};
                return accepted;
            };
            if(!rebuild() && (!publication.current || !publication.current()))return;
            // The same intent keeps its accepted mesh on backpressure, with
            // actual current camera constants. Its old revision retries later.

        }
        if(!publication.mesh)return;
        VSWorldRenderer::Packet packet;
        packet.mesh=publication.mesh;packet.regionOffset=offset;
        packet.fullbright=true;packet.alpha=true;packet.overlay=true;packet.doubleSided=true;
        packet.color=boundary.collision?std::array<float,4>{1,1,1,1}:std::array<float,4>{1,1,0,1};
        if(boundary.collision)
        {
            packet.image=accessImage(boundary.usePass && boundary.banState==BA_NOT_ON_LIST);
            packet.imageSampler.mipmaps=true;
            packet.imageSampler.anisotropy=gSavedSettings.getBOOL("RenderAnisotropic")?16u:1u;
            if(!packet.image)return; // Never render an untextured access barrier.
        }
        packets.push_back(std::move(packet));
    }
};
VSParcelPresentation::VSParcelPresentation(VSWorldRenderer& renderer,VSUIResources& resources,VSWorldScene& scene)
    :mImpl(std::make_unique<Impl>(renderer,resources,scene)) {}
VSParcelPresentation::~VSParcelPresentation()=default;
void VSParcelPresentation::reset()
{
    mImpl->selection={};mImpl->collision={};mImpl->rectangle={};mImpl->prepared={};
}
void VSParcelPresentation::append(const VSNativeSession& owner,const VSWorldRenderer::View& view,unsigned width,unsigned height,
                                  std::vector<VSWorldRenderer::Packet>& packets)
{
    if(!LLViewerParcelMgr::instanceExists()){reset();return;}
    auto& parcels=LLViewerParcelMgr::instance();
    if(const auto boundary=parcels.nativeSelectionBoundary())
    {
        if(boundary->bitmap)
        {
            mImpl->rectangle={};mImpl->appendBoundary(*boundary,owner,packets);
        }
        else
        {
            mImpl->selection={};mImpl->appendRectangle(*boundary,owner,view,width,height,packets);
        }
    }
    else {mImpl->selection={};mImpl->rectangle={};}
    if(const auto boundary=parcels.nativeCollisionBoundary())mImpl->appendBoundary(*boundary,owner,packets);
    else mImpl->collision={};
}

#if VS_VULKAN_DIAGNOSTICS
#include "vsvulkancontext.h"
#include "llsdutil.h"
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
#include <filesystem>
#include <fstream>
#include <cstring>
#include <cstdlib>
namespace
{
void requireParcel(bool condition,const char* message)
{
    if(!condition)throw std::runtime_error(message);
}
struct ParcelRectangleReplay
{
    bool complete=false;
    LLSD evidence;
    void capture(VSVulkanContext& owner,Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
                 Diligent::ITextureView* windowTarget,unsigned windowWidth,unsigned windowHeight)
    {
        if(complete)return;
        using namespace Diligent;
        const auto session=VSNativeSession::active();
        requireParcel(session && session->phase()==VSNativeSession::Phase::Connected,"Parcel pixels lost connected owner");
        const auto boundary=LLViewerParcelMgr::instance().nativeSelectionBoundary();
        requireParcel(boundary && !boundary->bitmap && boundary->current(),"Parcel rectangle pixels require actual selected rectangle");
        requireParcel(!LLViewerParcelMgr::instance().nativeCollisionBoundary(),"Unexpected collision overlay in rectangle qualification");
        const auto field=owner.world().terrainField(boundary->region.getHost());
        requireParcel(bool(field),"Parcel rectangle has no current terrain owner");
        const auto west=boundary->region.getPosRegionFromGlobal(boundary->westSouth);
        const auto east=boundary->region.getPosRegionFromGlobal(boundary->eastNorth);
        const float floor=field->referenceHeight(west.mV[VX],west.mV[VY]).value_or(0.f);
        for(const auto& point:std::array<std::array<float,2>,3>{{{east.mV[VX]-.01f,west.mV[VY]},
            {east.mV[VX]-.01f,east.mV[VY]-.01f},{west.mV[VX],east.mV[VY]-.01f}}})
            requireParcel(field->referenceHeight(point[0],point[1]).value_or(0.f)==floor,"Parcel pixel fixture requires actual flat received/initial terrain");
        const auto offset=VSWorldContracts::regionOffset(session->handle(),boundary->region.getHandle());
        const float extent=std::max(east.mV[VX]-west.mV[VX],east.mV[VY]-west.mV[VY]);
        requireParcel(extent>=4 && extent<=16,"Parcel pixel fixture lost actual grid-cell selection");
        const LLVector3 eye((west.mV[VX]+east.mV[VX])*.5f+offset[0],
            west.mV[VY]-extent*2+offset[1],floor+parcelPostHeight*.5f+offset[2]);
        const auto view=VSWorldScene::cameraView(eye,{0,1,0},{0,0,1},64,64,extent*8+32);
        VSWorldRenderer renderer(device,context);
        VSParcelPresentation presentation(renderer,owner.resources(),owner.world());
        std::vector<VSWorldRenderer::Packet> packets;
        presentation.append(*session,view,64,64,packets);
        requireParcel(packets.size()==2 && packets[0].screenLinePixels>0 && packets[0].overlay &&
            packets[1].alpha && packets[1].depthWriteAlpha && packets[1].overlay,
            "Actual selected rectangle omitted shared posts, translucent walls or depth policy");
        TextureDesc description{};description.Name="Actual parcel rectangle pixel target";
        description.Type=RESOURCE_DIM_TEX_2D;description.Width=description.Height=64;
        description.Format=TEX_FORMAT_RGBA8_UNORM;description.BindFlags=BIND_RENDER_TARGET;
        RefCntAutoPtr<ITexture> target;device->CreateTexture(description,nullptr,&target);
        requireParcel(bool(target),"Parcel pixel target allocation failed");
        auto* output=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        context->SetRenderTargets(1,&output,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]={1,0,1,1};context->ClearRenderTarget(output,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderer.draw(output,64,64,view,packets);
        description.Name="Parcel rectangle staging readback";description.Usage=USAGE_STAGING;
        description.BindFlags=BIND_NONE;description.CPUAccessFlags=CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;device->CreateTexture(description,nullptr,&staging);
        requireParcel(bool(staging),"Parcel pixel staging allocation failed");
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=staging;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);context->WaitForIdle();
        MappedTextureSubresource mapped{};
        context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        requireParcel(mapped.pData!=nullptr,"Parcel pixel readback mapping failed");
        std::vector<U8> pixels(64*64*4);
        for(unsigned row=0;row<64;++row)
            std::memcpy(pixels.data()+row*64*4,static_cast<const U8*>(mapped.pData)+row*mapped.Stride,64*4);
        context->UnmapTextureSubresource(staging,0,0);
        // renderRect paints the north wall before the nearer south wall. Both
        // cover the center: two ordinary 51/255 alpha blends over magenta.
        constexpr std::array<U8,4> expected{255,92,163,255};
        const auto center=(32*64+32)*4;
        for(unsigned channel=0;channel<4;++channel)
            requireParcel(std::abs(int(pixels[center+channel])-expected[channel])<=1,
                "Actual parcel walls differ from reference paint order/blending pixels");
        std::vector<std::array<float,4>> expectedPosts;
        const auto scale=LLUI::getScaleFactor();
        const float linePixels=scale.mV[VX]+scale.mV[VY];
        const auto projected=[&](float x,float y,float z)
        {
            std::array<float,4> clip{};
            for(unsigned row=0;row<4;++row)
                clip[row]=view.projection[row]*(x+offset[0])+view.projection[4+row]*(y+offset[1])+
                    view.projection[8+row]*(z+offset[2])+view.projection[12+row];
            return clip;
        };
        for(float x:{west.mV[VX],east.mV[VX]})
            for(float y:{west.mV[VY],east.mV[VY]})
            {
                const auto line=VSParcelGeometry::screenLine(projected(x,y,floor),
                    projected(x,y,floor+parcelPostHeight),linePixels,64,64);
                requireParcel(bool(line),"Reference parcel post projection failed");
                std::array<float,4> bounds{2,2,-2,-2};
                for(const auto& point:*line)
                {
                    bounds[0]=std::min(bounds[0],point.x);bounds[1]=std::min(bounds[1],point.y);
                    bounds[2]=std::max(bounds[2],point.x);bounds[3]=std::max(bounds[3],point.y);
                }
                expectedPosts.push_back(bounds);
            }
        unsigned yellow=0,expectedInterior=0;
        for(unsigned row=0;row<64;++row)
            for(unsigned column=0;column<64;++column)
            {
                const auto index=(row*64+column)*4;
                const bool opaque=pixels[index]==255 && pixels[index+1]==255 && pixels[index+2]==0;
                yellow+=opaque;
                const float x=(column+.5f)/32.f-1.f,y=1.f-(row+.5f)/32.f;
                bool interior=false,boundaryPixel=false;
                for(const auto& bounds:expectedPosts)
                {
                    interior|=x>bounds[0]+1e-6f && x<bounds[2]-1e-6f && y>bounds[1]+1e-6f && y<bounds[3]-1e-6f;
                    boundaryPixel|=x>=bounds[0]-1e-6f && x<=bounds[2]+1e-6f && y>=bounds[1]-1e-6f && y<=bounds[3]+1e-6f;
                }
                if(interior){++expectedInterior;requireParcel(opaque,"Actual parcel post width/placement differs from reference CPU projection");}
                if(opaque)requireParcel(boundaryPixel,"Actual parcel post pixels extend beyond reference projection");
            }
        requireParcel(yellow>0 && expectedInterior>0,"Actual projected parcel posts failed opaque yellow pixel qualification");
        const auto relative=std::string("parcel/rectangle.rgba");
        const auto path=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/relative;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());
        file.close();requireParcel(!file.fail(),"Parcel pixel artifact write failed");
        evidence["file"]=relative;evidence["width"]=64;evidence["height"]=64;
        evidence["pixels_verified"]=true;evidence["opaque_post_pixels"]=S32(yellow);
        complete=true;
        context->SetRenderTargets(1,&windowTarget,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0.f,0.f,float(windowWidth),float(windowHeight)};
        context->SetViewports(1,&viewport,windowWidth,windowHeight);
    }
};
std::shared_ptr<ParcelRectangleReplay> rectangleReplay;
}
bool VSParcelPresentation::qualifyRectangle(VSVulkanContext& context,LLSD& evidence)
{
    if(evidence["connected_parcel_rectangle_pixels"].asBoolean())return true;
    if(!rectangleReplay)
    {
        rectangleReplay=std::make_shared<ParcelRectangleReplay>();
        const auto replay=rectangleReplay;
        context.setSceneObserver([replay,&context](auto* device,auto* commands,auto* target,unsigned width,unsigned height)
        {replay->capture(context,device,commands,target,width,height);});
    }
    if(!rectangleReplay->complete)return false;
    context.setSceneObserver({});
    evidence["parcel_rectangle"]=rectangleReplay->evidence;
    evidence["connected_parcel_rectangle_pixels"]=true;
    rectangleReplay.reset();
    return true;
}
#endif
