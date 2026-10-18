// Authenticated script output -> shared editor, admitted icon and real Vulkan pixels.
// Development-only, LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vshudiconreplay.h"
#include "vsnativesession.h"
#include "vsplainchat.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "vsworldrenderer.h"
#include "vsuipixeloracle.h"
#include "llhudicon.h"
#include "llchat.h"
#include "llframetimer.h"
#include "llprimitive.h"
#include "llfocusmgr.h"
#include "fsfloaternearbychat.h"
#include "llfloaterscriptdebug.h"
#include "llviewertexteditor.h"
#include "llviewerwindow.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llfloaterreg.h"
#include "llui.h"
#include "llwindow.h"
#include "llagent.h"
#include "llregionhandle.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llvolumemessage.h"
#include "message.h"
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
#include <stdexcept>

void vs_native_replay_capture(VSVulkanContext&,const std::string&,std::shared_ptr<bool>);
namespace {
constexpr U32 localID=0xf0d00001;
const LLUUID sourceID("9f000000-0000-0000-0000-000000000001");
void requireIcon(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
void objectPacket(const VSNativeSession& owner)
{
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("ObjectUpdate");msg->nextBlock("RegionData");
    msg->addU64("RegionHandle",owner.handle());msg->addU16("TimeDilation",65535);
    msg->nextBlock("ObjectData");msg->addU32("ID",localID);msg->addU8("State",0);msg->addUUID("FullID",sourceID);
    msg->addU32("CRC",0);msg->addU8("PCode",LL_PCODE_VOLUME);msg->addU8("Material",0);msg->addU8("ClickAction",0);
    msg->addVector3("Scale",LLVector3(1,1,1));
    const F32 pose[15]={15,20,30,0,0,0,0,0,0,0,0,0,0,0,0};msg->addBinaryData("ObjectData",pose,sizeof(pose));
    msg->addU32("ParentID",0);msg->addU32("UpdateFlags",0);
    LLVolumeParams params;LLVolumeMessage::packVolumeParams(&params,msg);
    LLPrimitive primitive;primitive.setNumTEs(6);primitive.packTEMessage(msg);
    for(const char* field:{"TextureAnim","NameValue","Data","Text","MediaURL","PSBlock","ExtraParams"})msg->addBinaryData(field,nullptr,0);
    const U8 white[4]={255,255,255,255};msg->addBinaryData("TextColor",white,4);
    msg->addUUID("Sound",LLUUID::null);msg->addUUID("OwnerID",gAgent.getID());
    msg->addF32("Gain",0);msg->addU8("Flags",0);msg->addF32("Radius",0);msg->addU8("JointType",0);
    msg->addVector3("JointPivot",LLVector3::zero);msg->addVector3("JointAxisOrAnchor",LLVector3::zero);msg->sendReliable(owner.host());
}
void chatPacket(const VSNativeSession& owner)
{
    LLMessageSystem* msg=gMessageSystem;msg->newMessage("ChatFromSimulator");msg->nextBlock("ChatData");
    msg->addString("FromName","Native script source");msg->addUUID("SourceID",sourceID);msg->addUUID("OwnerID",gAgent.getID());
    msg->addU8("SourceType",CHAT_SOURCE_OBJECT);msg->addU8("ChatType",CHAT_TYPE_DEBUG_MSG);msg->addU8("Audible",CHAT_AUDIBLE_FULLY);
    msg->addVector3("Position",LLVector3(15,20,30));msg->addString("Message","Native authenticated script debug \xCE\xA9");msg->sendReliable(owner.host());
}
void file(const std::filesystem::path& path,const std::vector<U8>& bytes)
{std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());requireIcon(out.good(),"Icon readback artifact write failed");}
struct State {
    unsigned step=0;
    bool originalShow=false;S32 originalLocation=0;
    VSNativeCamera::State camera;
    F64 deadline=0,receivedAt=0;
    LLPointer<LLHUDIcon> icon;
    LLHandle<LLFloater> output;
    bool captured=false;
    std::shared_ptr<bool> uiReadback=std::make_shared<bool>(false);
    LLSD pixels;
    void capture(VSVulkanContext& owner,Diligent::IRenderDevice* device,Diligent::IDeviceContext* context,
                 Diligent::ITextureView* windowTarget,unsigned width,unsigned height)
    {
        if(captured)return;
        using namespace Diligent;
        const auto origin=from_region_handle(VSNativeSession::active()->handle());
        const auto packet=icon->nativePacket(origin);if(!packet)return;
        requireIcon(packet->image && packet->mesh && packet->color[3]==1.f,"Icon lacks actual opaque admitted draw generation");
        const auto extent=VSUIRenderer::imageExtent(packet->image);const auto w=extent[0],h=extent[1];
        requireIcon(w>2&&h>2&&w<=1024&&h<=1024,"Icon asset has unexpected extent");
        const auto& vertices=VSWorldRenderer::geometryVertices(packet->mesh);requireIcon(vertices.size()==4,"Actual icon publication lacks its four reference vertices");
        const LLVector3 p0(vertices[0].x+packet->regionOffset[0],vertices[0].y+packet->regionOffset[1],vertices[0].z+packet->regionOffset[2]);
        const LLVector3 dx(vertices[1].x-vertices[0].x,vertices[1].y-vertices[0].y,vertices[1].z-vertices[0].z);
        const LLVector3 dy(vertices[2].x-vertices[0].x,vertices[2].y-vertices[0].y,vertices[2].z-vertices[0].z);
        requireIcon(dx.magVecSquared()>0&&dy.magVecSquared()>0,"Icon billboard is degenerate");
        const LLVector3 x=dx*(2.f/dx.magVecSquared()),y=dy*(2.f/dy.magVecSquared());
        VSWorldRenderer::View view;view.projection={x.mV[0],y.mV[0],0,0,x.mV[1],y.mV[1],0,0,x.mV[2],y.mV[2],0,0,-1.f-x*p0,-1.f-y*p0,0,1};
        TextureDesc desc{};desc.Name="Actual script icon pixel qualification";desc.Type=RESOURCE_DIM_TEX_2D;desc.Width=w;desc.Height=h;desc.Format=TEX_FORMAT_RGBA8_UNORM;desc.BindFlags=BIND_RENDER_TARGET;
        RefCntAutoPtr<ITexture> target;device->CreateTexture(desc,nullptr,&target);requireIcon(bool(target),"Icon target allocation failed");
        auto* renderTarget=target->GetDefaultView(TEXTURE_VIEW_RENDER_TARGET);context->SetRenderTargets(1,&renderTarget,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        const float clear[4]={1,0,1,1};context->ClearRenderTarget(renderTarget,clear,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        VSWorldRenderer renderer(device,context);renderer.draw(renderTarget,w,h,view,{*packet});
        desc.Name="Actual script icon readback";desc.Usage=USAGE_STAGING;desc.BindFlags=BIND_NONE;desc.CPUAccessFlags=CPU_ACCESS_READ;
        RefCntAutoPtr<ITexture> staging;device->CreateTexture(desc,nullptr,&staging);requireIcon(bool(staging),"Icon staging allocation failed");
        context->SetRenderTargets(0,nullptr,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
        CopyTextureAttribs copy{};copy.pSrcTexture=target;copy.pDstTexture=staging;copy.SrcTextureTransitionMode=copy.DstTextureTransitionMode=RESOURCE_STATE_TRANSITION_MODE_TRANSITION;
        context->CopyTexture(copy);context->WaitForIdle();MappedTextureSubresource mapped{};context->MapTextureSubresource(staging,0,0,MAP_READ,MAP_FLAG_DO_NOT_WAIT,nullptr,mapped);
        requireIcon(mapped.pData!=nullptr,"Icon staging mapping failed");std::vector<U8> actual(std::size_t(w)*h*4),background(actual.size());
        for(unsigned row=0;row<h;++row)std::memcpy(actual.data()+std::size_t(row)*w*4,static_cast<const U8*>(mapped.pData)+row*mapped.Stride,w*4);
        context->UnmapTextureSubresource(staging,0,0);
        for(std::size_t offset=0;offset<background.size();offset+=4){background[offset]=background[offset+2]=background[offset+3]=255;}
        VSUIRenderer::Packet expectedPacket;expectedPacket.image=packet->image;expectedPacket.bounds={0,0,float(w),float(h)};expectedPacket.clip=expectedPacket.bounds;
        expectedPacket.color=packet->color;expectedPacket.sampling=VSUIRenderer::Sampling::Linear;
        const auto expected=vs_ui_expected_pixels(w,h,1,{expectedPacket},0,background);
        unsigned mismatches=0;for(unsigned row=0;row<h;++row)for(unsigned col=0;col<w;++col)for(unsigned c=0;c<4;++c){const auto n=(std::size_t(row)*w+col)*4+c;if(std::abs(int(actual[n])-int(expected[n]))>3)++mismatches;}
        const auto folder=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"script-icon";std::filesystem::create_directories(folder);
        file(folder/"actual.rgba",actual);file(folder/"expected.rgba",expected);
        pixels["width"]=S32(w);pixels["height"]=S32(h);pixels["image_generation"]=ll_sd_from_U64(VSUIRenderer::generation(packet->image));pixels["mismatched_components"]=S32(mismatches);
        pixels["actual"]="script-icon/actual.rgba";pixels["expected"]="script-icon/expected.rgba";pixels["pixels_verified"]=mismatches==0;
        std::ofstream report(folder/"readback.xml");LLSDSerialize::toPrettyXML(pixels,report);requireIcon(report.good(),"Icon readback metadata archive failed");
        context->SetRenderTargets(1,&windowTarget,nullptr,RESOURCE_STATE_TRANSITION_MODE_TRANSITION);Viewport viewport{0.f,0.f,float(width),float(height)};context->SetViewports(1,&viewport,width,height);
        requireIcon(mismatches==0,"Actual native icon pixels/orientation differ from immutable image and reference alpha blend");captured=true;
    }
};
std::shared_ptr<State> state;
}
bool vs_hud_icon_replay_tick(VSNativeSession& session,LLSD& report)
{
    if(report["connected_script_icon_retirement"].asBoolean())return true;
    auto& context=*gViewerWindow->nativeContext();auto& world=context.world();
    if(!state){state=std::make_shared<State>();state->deadline=LLFrameTimer::getTotalSeconds()+30;state->camera=session.cameraState();state->originalShow=gSavedSettings.getBOOL("ShowScriptErrors");state->originalLocation=gSavedSettings.getS32("ShowScriptErrorsLocation");gSavedSettings.setBOOL("ShowScriptErrors",true);gSavedSettings.setS32("ShowScriptErrorsLocation",1);objectPacket(session);return false;}
    requireIcon(LLFrameTimer::getTotalSeconds()<state->deadline,"Authenticated script debug/icon qualification timed out");
    if(state->step==0){if(!world.object(sourceID))return false;const auto origin=from_region_handle(session.handle());requireIcon(session.cameraLookAt(origin+LLVector3d(5,20,31),origin+LLVector3d(15,20,31)),"Icon reference camera failed");chatPacket(session);state->step=1;return false;}
    if(state->step==1){auto* output=LLFloaterReg::findTypedInstance<LLFloaterScriptDebugOutput>("script_debug_output",sourceID);if(!output)return false;auto* editor=output->findChild<LLViewerTextEditor>("Chat History Editor");if(!editor||editor->getText().find("Native authenticated script debug") == std::string::npos)return false;state->icon=LLHUDIcon::findNative(sourceID);if(!state->icon||!state->icon->nativePickPoint())return false;requireIcon(state->icon->getScriptError()&&state->icon->getSourceID()==sourceID,"Script debug lost actual source identity");requireIcon(output->isInVisibleChain(),"Actual script debug output is hidden");state->output=output->getHandle();state->receivedAt=LLFrameTimer::getTotalSeconds();const auto folder=std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"))/"script-debug";std::filesystem::create_directories(folder);vs_native_replay_capture(context,folder.string(),state->uiReadback);state->step=2;return false;}
    if(state->step==2){if(!*state->uiReadback || LLFrameTimer::getTotalSeconds()-state->receivedAt<.5)return false;context.setFrameObserver({});report["connected_script_debug_controls"]=true;context.setSceneObserver([held=state,&context](auto*d,auto*c,auto*t,unsigned w,unsigned h){held->capture(context,d,c,t,w,h);});state->step=3;return false;}
    if(state->step==3){if(!state->captured)return false;context.setSceneObserver({});report["connected_script_icon_gpu"]=true;report["script_icon_readback"]=state->pixels;
        const auto point=state->icon->nativePickPoint();requireIcon(bool(point),"Published icon lost current pick geometry");LLCoordGL screen;requireIcon(LLViewerCamera::getInstance()->projectPosAgentToScreen(*point,screen,false),"Actual icon cannot project into viewer");LLPickInfo pick(screen,MASK_NONE,true,true,false,false,true,true,nullptr);requireIcon(gViewerWindow->nativePick(pick)&&pick.mPickType==LLPickInfo::PICK_ICON&&pick.mHUDIcon==state->icon.get(),"Actual icon pick did not select its submitted shared owner");
        requireIcon(pick.mHUDIcon->getSourceID()==sourceID,"Icon pick lost source controller identity");auto* host=LLFloaterReg::findInstance("script_debug");requireIcon(host!=nullptr,"Script debug host vanished before icon click");host->closeFloater();
        auto* nearby=FSFloaterNearbyChat::findInstance();const bool nearbyVisible=nearby&&nearby->getVisible();if(nearby)nearby->setVisible(false);
        auto* plain=gViewerWindow->nativeChat();const bool plainVisible=plain&&plain->getVisible();if(plain)plain->setVisible(false);
        requireIcon(!gFocusMgr.getMouseCapture(),"Actual icon click is blocked by an unfinished input capture");
        LLCoordGL physical;LLUI::getInstance()->screenPointToGL(screen.mX,screen.mY,&physical.mX,&physical.mY);
        requireIcon(gViewerWindow->handleMouseDown(gViewerWindow->getWindow(),physical,MASK_NONE),"Native world icon rejected real mouse down");
        requireIcon(gViewerWindow->handleMouseUp(gViewerWindow->getWindow(),physical,MASK_NONE),"Native world icon rejected real mouse up");
        requireIcon(host->isInVisibleChain()&&LLFloaterReg::findInstance("script_debug_output",sourceID)->isInVisibleChain(),"Icon selected source did not open actual script debug editor through window input");
        if(plain)plain->setVisible(plainVisible);
        if(nearby)nearby->setVisible(nearbyVisible);report["connected_script_icon_pick"]=true;
        LLMessageSystem* msg=gMessageSystem;msg->newMessage("KillObject");msg->nextBlock("ObjectData");msg->addU32("ID",localID);msg->sendReliable(session.host());state->step=4;return false;}
    if(world.object(sourceID))return false;requireIcon(state->icon->getSourceID().isNull()&&!state->icon->nativePickPoint(),"Retired simulator object retained clickable icon source");
    if(auto* host=LLFloaterReg::findInstance("script_debug"))host->closeFloater();
    gSavedSettings.setBOOL("ShowScriptErrors",state->originalShow);gSavedSettings.setS32("ShowScriptErrorsLocation",state->originalLocation);
    const auto origin=from_region_handle(session.handle());requireIcon(session.cameraLookAt(origin+LLVector3d(state->camera.center),origin+LLVector3d(state->camera.focus)),"Icon replay did not restore original camera");
    report["connected_script_icon_retirement"]=true;state.reset();return true;
}
