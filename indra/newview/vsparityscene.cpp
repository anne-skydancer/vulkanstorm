// Inject one immutable message corpus into the actual peer scene producers. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsparityscene.h"
#include "vsparitybaseline.h"
#include "llappviewer.h"
#include "llstartup.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"
#include "llviewerobjectlist.h"
#include "llviewercamera.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llsky.h"
#include "llsurface.h"
#include "lldrawable.h"
#include "llavatarappearance.h"
#include "llviewerobject.h"
#include "llpostprocess.h"
#include "pipeline.h"
#include "fspanellogin.h"
#include "llprogressview.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llsdutil_math.h"
#include "llsdjson.h"
#include "message.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <random>
#include <stdexcept>
namespace
{
bool ready=false,seeded=false;
LLVector3 eye(10,20,30),at(1,0,0),up(0,0,1);
const U64 handle=(U64(256)<<32)|512;
LLHost fixtureHost;
void receive(LLMessageSystem* msg,void**)
{
    const auto start=std::chrono::steady_clock::now();
    if (msg->getSender()!=fixtureHost) throw std::runtime_error("Foreign scene fixture message");
    if (gViewerWindow->isNativeVulkan())
        gViewerWindow->nativeContext()->world().receive(*msg,handle,LLUUID::null);
    else gObjectList.processObjectUpdate(msg,nullptr,OUT_FULL);
    if (gViewerWindow->isNativeVulkan())
    {
        if (gViewerWindow->nativeContext()->world().evidence()["objects"].asInteger()!=2)
            throw std::runtime_error("Native production scene did not accept both fixture objects");
    }
    else for (const char* id:{"90000000-0000-0000-0000-000000000001","90000000-0000-0000-0000-000000000002"})
    {
        if (!gObjectList.findObject(LLUUID(id)))
            throw std::runtime_error("OpenGL production scene did not accept both fixture objects");
    }
    LLSD timing;timing["initial_scene_decode_cpu_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    vs_parity_baseline_metrics(timing);
    LLSD corpus;
    for (S32 i=0;i<msg->getNumberOfBlocks("ObjectData");++i)
    {
        U32 local;LLUUID id;msg->getU32("ObjectData","ID",local,i);msg->getUUID("ObjectData","FullID",id,i);
        LLSD object;object["ID"]=ll_sd_from_U32(local);object["FullID"]=id;
        for (const char* field:{"ObjectData","TextureEntry"})
        {
            S32 size=msg->getSize("ObjectData",i,field);LLSD::Binary bytes(size);
            msg->getBinaryData("ObjectData",field,bytes.data(),size,i,size);object[field]=bytes;
        }
        corpus["objects"].append(object);
    }
    std::ofstream output(std::filesystem::path(std::getenv("VS_PARITY_BASELINE_DIRECTORY"))/"scene-messages.xml");
    LLSDSerialize::toPrettyXML(corpus,output);
    if (!output.good() || corpus["objects"].size()!=2) throw std::runtime_error("Incomplete actual scene replay");
    ready=true;
}
void seed()
{
    const char* path=std::getenv("VS_PARITY_SCENE_FILE");
    std::ifstream input(path);
    const std::string text((std::istreambuf_iterator<char>(input)),{});
    if (!input.good() && !input.eof()) throw std::runtime_error("Cannot read scene fixture");
    const LLSD config=LlsdFromJson(boost::json::parse(text));
    if (config["scene"].asString()!="static-world-primitives-v1") throw std::runtime_error("Unknown scene fixture");
    if (gAgent.getID().notNull()) throw std::runtime_error("Offline fixture cannot replace a real account");
    eye=ll_vector3_from_sd(config["camera"]["eye"]);
    at=ll_vector3_from_sd(config["camera"]["at"]);
    up=ll_vector3_from_sd(config["camera"]["up"]);
    if (!eye.isFinite() || !at.isFinite() || !up.isFinite()) throw std::runtime_error("Invalid fixture camera");
    LLMessageSystem* msg=gMessageSystem;
    if (!msg) throw std::runtime_error("Normal startup did not initialize the message system");
    fixtureHost=LLHost("127.0.0.1",msg->getListenPort());
    if (config["camera"]["near"].asReal()!=.1 || config["camera"]["far"].asReal()!=32 || config["camera"]["fov_degrees"].asReal()!=60 ||
        config["simulation"]["object_count"].asInteger()!=2 || config["simulation"]["avatars"].asInteger()!=0 ||
        config["environment"]["animated_environment_enabled"].asBoolean() || config["environment"]["sky_water_passes_enabled"].asBoolean() ||
        !config["environment"]["fullbright_objects"].asBoolean())
        throw std::runtime_error("Unsupported fixed fixture projection/environment/workload");
    gSavedSettings.setF32("RenderFarClip",32);
    if (!gViewerWindow->isNativeVulkan())
    {
        // The offline corpus bypasses authentication, not graphics class ownership.
        // Initialize the same world classes as STATE_WORLD_INIT before sky or regions.
        gAgent.init();
        gAgentCamera.init();
        LLSurface::initClasses();
        LLDrawable::initClass();
        LLPostProcess::initClass();
        LLAvatarAppearance::initClass("avatar_lad.xml","avatar_skeleton.xml");
        LLViewerObject::initVOClasses();
        if (!LLWorld::instanceExists()) LLWorld::createInstance();
        auto* region=LLWorld::getInstance()->addRegion(handle,fixtureHost,256,256);
        if (!region) throw std::runtime_error("Cannot create reference fixture region");
        gAgent.setRegion(region);gAgent.setPositionAgent(eye);
        gSky.init();
        gPipeline.clearAllRenderTypes();
        gPipeline.setRenderTypeMask(LLPipeline::RENDER_TYPE_VOLUME,LLPipeline::RENDER_TYPE_SIMPLE,LLPipeline::RENDER_TYPE_FULLBRIGHT,
            LLRenderPass::PASS_SIMPLE,LLRenderPass::PASS_FULLBRIGHT,LLPipeline::END_RENDER_TYPES);
        if (gPipeline.hasRenderDebugFeatureMask(LLPipeline::RENDER_DEBUG_FEATURE_UI))
            gPipeline.toggleRenderDebugFeature(LLPipeline::RENDER_DEBUG_FEATURE_UI);
    }
    msg->enableCircuit(fixtureHost,true);
    msg->setMessageAdmission([](const std::string& name,const LLHost& host)
        { return (name=="ObjectUpdate" || name=="PacketAck") && host==fixtureHost; });
    msg->setHandlerFunc("ObjectUpdate",receive);
    msg->newMessage("ObjectUpdate");msg->nextBlock("RegionData");
    msg->addU64("RegionHandle",handle);msg->addU16("TimeDilation",65535);
    std::vector<unsigned> order{0,1};std::mt19937 random(U32(config["seed"].asInteger()));
    std::shuffle(order.begin(),order.end(),random);
    for (unsigned i:order)
    {
        msg->nextBlock("ObjectData");msg->addU32("ID",900+i);msg->addU8("State",0);
        msg->addUUID("FullID",LLUUID(i?"90000000-0000-0000-0000-000000000002":"90000000-0000-0000-0000-000000000001"));
        msg->addU32("CRC",0);msg->addU8("PCode",LL_PCODE_VOLUME);msg->addU8("Material",0);msg->addU8("ClickAction",0);
        msg->addVector3("Scale",LLVector3(1,1,1));
        const F32 pose[15]={15,18.f+float(i)*4,30,0,0,0,0,0,0,0,0,0,0,0,0};
        msg->addBinaryData("ObjectData",pose,sizeof(pose));msg->addU32("ParentID",0);msg->addU32("UpdateFlags",0);
        LLVolumeParams params;LLVolumeMessage::packVolumeParams(&params,msg);
        LLPrimitive material;material.setNumTEs(6);
        for (U8 face=0;face<6;++face)
        { material.setTEFullbright(face,1);material.setTEColor(face,i?LLColor4(.2f,.8f,.3f,1):LLColor4(.8f,.2f,.3f,1)); }
        material.packTEMessage(msg);
        for (const char* field:{"TextureAnim","NameValue","Data","Text","MediaURL","PSBlock","ExtraParams"})
            msg->addBinaryData(field,nullptr,0);
        const U8 color[4]={255,255,255,255};msg->addBinaryData("TextColor",color,4);
        msg->addUUID("Sound",LLUUID::null);msg->addUUID("OwnerID",LLUUID::null);
        msg->addF32("Gain",0);msg->addU8("Flags",0);msg->addF32("Radius",0);msg->addU8("JointType",0);
        msg->addVector3("JointPivot",LLVector3::zero);msg->addVector3("JointAxisOrAnchor",LLVector3::zero);
    }
    std::filesystem::path out(std::getenv("VS_PARITY_BASELINE_DIRECTORY"));
    std::ofstream snapshot(out/"scene-input.json");snapshot<<text;
    if (!snapshot.good() || msg->sendReliable(fixtureHost)<=0) throw std::runtime_error("Cannot start actual loopback replay");
    seeded=true;
    gDisconnected=false;
    FSPanelLogin::closePanel();gViewerWindow->getProgressView()->setVisible(false);
}
}
LLSD vs_parity_scene_inputs()
{
    LLSD result;
    result["camera"]["eye"]=ll_sd_from_vector3(eye);
    result["camera"]["at"]=ll_sd_from_vector3(at);
    result["camera"]["up"]=ll_sd_from_vector3(up);
    result["camera"]["near"]=.1;result["camera"]["far"]=gSavedSettings.getF32("RenderFarClip");
    result["camera"]["fov_degrees"]=60;
    result["fullbright_objects"]=true;result["sky_water_passes_enabled"]=false;
    result["motion_delta_seconds"]=F64(gFrameIntervalSeconds.value());result["epoch_seconds"]=1700000000.;
    result["frame"]=60;result["object_count"]=2;
    return result;
}
bool vs_parity_scene_active() { return ready; }
bool vs_parity_scene_frame()
{
    if (!std::getenv("VS_PARITY_SCENE_FILE")) return false;
    if (!std::getenv("VS_PARITY_BASELINE_DIRECTORY")) throw std::runtime_error("Scene fixture requires isolated capture directory");
    if (!seeded && LLStartUp::getStartupState()==STATE_LOGIN_WAIT) seed();
    if (seeded)
    {
        const auto start=std::chrono::steady_clock::now();
        LockMessageChecker checker(gMessageSystem);
        unsigned count=0;while (checker.checkMessages(0) && ++count<128) {}
        checker.processAcks();
        LLSD timing;timing["fixture_service_poll_cpu_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        vs_parity_baseline_metrics(timing);
        if (ready) LLStartUp::setStartupState(STATE_STARTED);
    }
    if (!ready || gViewerWindow->isNativeVulkan()) return false;
    vs_parity_baseline_before_render();
    LLViewerCamera::getInstance()->setViewHeightInPixels(gViewerWindow->getWindowRectRaw().getHeight());
    LLViewerCamera::getInstance()->setAspect(F32(gViewerWindow->getWindowRectRaw().getWidth())/gViewerWindow->getWindowRectRaw().getHeight());
    LLViewerCamera::getInstance()->setNear(.1f);
    LLViewerCamera::getInstance()->setDefaultFOV(F_PI/3);LLViewerCamera::getInstance()->setFar(32);
    LLViewerCamera::getInstance()->updateCameraLocation(eye,up,eye+at);
    const auto start=std::chrono::steady_clock::now();
    gObjectList.update(gAgent);
    LLSD timing;timing["scene_update_cpu_ms"]=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    timing["skinning_workload_avatars"]=0;vs_parity_baseline_metrics(timing);
    display();
    return true;
}
VSWorldRenderer::View vs_parity_scene_view(unsigned width,unsigned height)
{ return VSWorldScene::cameraView(eye,at,up,width,height,32); }
LLVector3 vs_parity_scene_eye() { return eye; }
