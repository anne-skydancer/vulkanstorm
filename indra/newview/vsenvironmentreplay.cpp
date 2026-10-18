// Actual current-setting sky pixel replay; not full environment shader parity. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsenvironmentreplay.h"
#if VS_VULKAN_DIAGNOSTICS
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsenvironmentstate.h"
#include "vsnativesession.h"
#include "vsnativeim.h"
#include "llenvironment.h"
#include "llsd.h"
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
void requireEnvironment(bool condition,const char* message)
{if(!condition)throw std::runtime_error(message);}
struct EnvironmentCapture
{
    std::string tag;
    std::function<bool()> current=vs_native_im_guard();
    bool complete=false;
    LLSD evidence;
    void capture(VSVulkanContext& owner,Diligent::IRenderDevice* device,Diligent::IDeviceContext* commands,
        Diligent::ITextureView* window,unsigned width,unsigned height)
    {
        if(complete || !current())return;
        using namespace Diligent;
        const auto session=VSNativeSession::active();
        requireEnvironment(session && session->phase()==VSNativeSession::Phase::Connected,"Environment capture lost actual connected owner");
        const auto sky=LLEnvironment::instance().getCurrentSky();
        requireEnvironment(bool(sky),"Environment capture lost actual current sky");
        const auto eye=session->cameraState().center;
        LLVector3 at(1,0,.3f);at.normalize();const LLVector3 up(0,0,1);
        auto view=VSWorldScene::cameraView(eye,at,up,64,64,65536);
        view.environment=vs_environment_state(eye,at,up,1,1.0471975512f,LLEnvironment::instance().getWaterHeight());
        requireEnvironment(view.environment.values[0][0]==1,"Environment capture has no real CPU publication");
        auto packets=owner.world().packets(eye,65536,64,64);
        packets.erase(std::remove_if(packets.begin(),packets.end(),[](const auto& packet){return !packet.background;}),packets.end());
        if(!owner.world().skyCurrent())return; // Bounded replay waits for actual intended dome publication.
        requireEnvironment(packets.size()==1 && packets[0].mesh,"Environment capture omitted real published sky mesh");
        VSWorldRenderer renderer(device,commands);
        TextureDesc description{};description.Name="Actual current environment sky capture";
        description.Type=RESOURCE_DIM_TEX_2D;description.Width=description.Height=64;
        description.Format=TEX_FORMAT_RGBA8_UNORM;description.BindFlags=BIND_RENDER_TARGET;
        RefCntAutoPtr<ITexture> target;device->CreateTexture(description,nullptr,&target);
        requireEnvironment(bool(target),"Environment capture target allocation failed");
        auto* output=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);
        commands->SetRenderTargets(1,&output,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]={1,0,1,1};commands->ClearRenderTarget(output,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        renderer.draw(output,64,64,view,packets);
        description.Name="Actual environment pixel readback";description.Usage=USAGE_STAGING;
        description.BindFlags=BIND_NONE;description.CPUAccessFlags=CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;device->CreateTexture(description,nullptr,&staging);
        requireEnvironment(bool(staging),"Environment readback allocation failed");
        commands->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=staging;
        copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        commands->CopyTexture(copy);commands->WaitForIdle();
        MappedTextureSubresource mapped{};
        commands->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        requireEnvironment(mapped.pData!=nullptr,"Environment readback mapping failed");
        std::vector<U8> pixels(64*64*4);
        for(unsigned row=0;row<64;++row)
            std::memcpy(pixels.data()+row*64*4,static_cast<const U8*>(mapped.pData)+row*mapped.Stride,64*4);
        commands->UnmapTextureSubresource(staging,0,0);
        // Preserve actual bytes even if the coverage/alpha assertion fails.
        const auto relative="environment/"+tag+".rgba";
        const auto directory=std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY");requireEnvironment(directory,"Environment capture has no evidence directory");
        const auto path=std::filesystem::path(directory)/relative;std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(pixels.data()),pixels.size());file.close();
        requireEnvironment(!file.fail(),"Environment capture artifact write failed");
        unsigned filled=0;
        for(unsigned i=0;i<pixels.size();i+=4)
        {
            requireEnvironment(pixels[i+3]==255,"Actual sky capture lost opaque background alpha");
            filled+=!(pixels[i]==255 && pixels[i+1]==0 && pixels[i+2]==255);
        }
        requireEnvironment(filled>64*64*9/10,"Actual sky failed to cover camera background");
        evidence["file"]=relative;evidence["width"]=64;evidence["height"]=64;evidence["filled_pixels"]=S32(filled);
        evidence["sun_up"]=sky->getIsSunUp();evidence["moon_up"]=sky->getIsMoonUp();
        evidence["ambient"]=LLSD::emptyArray();evidence["blue_horizon"]=LLSD::emptyArray();
        for(unsigned i=0;i<3;++i){evidence["ambient"].append(view.environment.values[7][i]);evidence["blue_horizon"].append(view.environment.values[8][i]);}
        complete=true;
        commands->SetRenderTargets(1,&window,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        Viewport viewport{0.f,0.f,float(width),float(height)};commands->SetViewports(1,&viewport,width,height);
    }
};
std::shared_ptr<EnvironmentCapture> capture;
}
bool vs_qualify_environment_pixels(VSVulkanContext& context,const char* tag,LLSD& evidence)
{
    requireEnvironment(std::string(tag)=="day" || std::string(tag)=="night" || std::string(tag)=="edit","Unknown environment capture intent");
    if(capture && !capture->current()){context.setSceneObserver({});capture.reset();}
    if(!capture)
    {
        capture=std::make_shared<EnvironmentCapture>();capture->tag=tag;const auto pending=capture;
        context.setSceneObserver([pending,&context](auto* device,auto* commands,auto* target,unsigned width,unsigned height)
        {pending->capture(context,device,commands,target,width,height);});
    }
    requireEnvironment(capture->tag==tag,"Environment capture intent changed before completion");
    if(!capture->complete)return false;
    context.setSceneObserver({});evidence["environment_pixels"][tag]=capture->evidence;capture.reset();return true;
}
#endif
