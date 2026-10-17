// Diagnostic-only real cache, memory-policy and minimized-image lifecycle. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3pressurereplay.h"
#include "vsnativesession.h"
#include "vsworldscene.h"
#include "vsuiresources.h"
#include "vsuiimageprovider.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llwindow.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include "llprimitive.h"
#include "llvolumemessage.h"
#include "llregionhandle.h"
#include "message.h"
#include <map>
#include <stdexcept>
#include <cmath>
namespace
{
const LLUUID textureID("a3000000-0000-0000-0000-000000000001");
const LLUUID objectID("b3900000-0000-0000-0000-000000000001");
void         require(bool ok, const char* text)
{
    if (!ok)
        throw std::runtime_error(text);
}
void createObject(VSNativeSession& session)
{
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessage("ObjectUpdate");
    msg->nextBlock("RegionData");
    msg->addU64("RegionHandle", to_region_handle(256, 512));
    msg->addU16("TimeDilation", 65535);
    msg->nextBlock("ObjectData");
    msg->addU32("ID", 995);
    msg->addU8("State", 0);
    msg->addUUID("FullID", objectID);
    msg->addU32("CRC", 0);
    msg->addU8("PCode", LL_PCODE_VOLUME);
    msg->addU8("Material", 0);
    msg->addU8("ClickAction", 0);
    msg->addVector3("Scale", LLVector3(1, 1, 1));
    const auto position = session.position() + LLVector3(3, 0, 0);
    const F32  pose[15] = { position.mV[0], position.mV[1], position.mV[2], 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
    msg->addBinaryData("ObjectData", pose, sizeof(pose));
    msg->addU32("ParentID", 0);
    msg->addU32("UpdateFlags", 0);
    LLVolumeParams params;
    LLVolumeMessage::packVolumeParams(&params, msg);
    LLPrimitive material;
    material.setNumTEs(6);
    for (unsigned face = 0; face < 6; ++face)
    {
        material.setTETexture(U8(face), textureID);
        material.setTEFullbright(U8(face), 1);
    }
    material.packTEMessage(msg);
    for (const char* field : { "TextureAnim", "NameValue", "Data", "Text", "MediaURL", "PSBlock" })
        msg->addBinaryData(field, nullptr, 0);
    msg->addBinaryData("ExtraParams", nullptr, 0);
    const U8 white[4] = { 255, 255, 255, 255 };
    msg->addBinaryData("TextColor", white, 4);
    msg->addUUID("Sound", LLUUID::null);
    msg->addUUID("OwnerID", LLUUID::null);
    msg->addF32("Gain", 0);
    msg->addU8("Flags", 0);
    msg->addF32("Radius", 0);
    msg->addU8("JointType", 0);
    msg->addVector3("JointPivot", LLVector3::zero);
    msg->addVector3("JointAxisOrAnchor", LLVector3::zero);
    require(msg->sendReliable(session.host()) > 0, "R3 pressure ObjectUpdate transport failed");
}

struct State
{
    unsigned                                   step     = 0;
    double                                     deadline = 0;
    LLSD                                       evidence;
    std::map<std::string, LLSD>                saved;
    std::weak_ptr<const VSUIRenderer::Texture> beforeMinimize;
    ~State()
    {
        if (gViewerWindow && gViewerWindow->getWindow())
            gViewerWindow->getWindow()->restore();
        for (const auto& [name, value] : saved)
            gSavedSettings.getControl(name)->setValue(value);
    }
    void save(const char* name) { saved[name] = gSavedSettings.getControl(name)->getValue(); }
};
std::unique_ptr<State> state;
LLSD                   imageEvidence()
{
    const auto cache = vs_ui_texture_cache_evidence(true);
    for (const auto& row : llsd::inArray(cache["images"]))
        if (row["id"].asUUID() == textureID)
            return row;
    throw std::runtime_error("R3 pressure image GPU evidence missing");
}
} // namespace
bool vs_r3_pressure_replay_tick(LLSD& report)
{
    if (!std::getenv("VS_VULKAN_REPLAY_LOCAL_IMAGES") || !std::getenv("VS_VULKAN_REPLAY_TEXTURE_CACHE") ||
        report["r3_pressure"]["complete"].asBoolean())
        return true;
    require(VSVulkanContext::headlessReplay(), "Pressure replay requires an isolated headless window");
    const auto session = VSNativeSession::active();
    require(bool(session), "R3 pressure session missing");
    auto& owner     = *gViewerWindow->nativeContext();
    auto& resources = owner.resources();
    auto& world     = owner.world();
    auto* window    = gViewerWindow->getWindow();
    if (!state)
    {
        state = std::make_unique<State>();
        for (const char* name : { "TextureLoadFullRes", "RenderMaxTextureResolution", "RenderMinFreeMainMemoryThreshold",
                                  "RenderLowMemMinDiscardIncrement", "RenderHighMemMinDiscardDecrement", "TextureDiscardMinimizedTime" })
            state->save(name);
        gSavedSettings.setBOOL("TextureLoadFullRes", true);
        gSavedSettings.setU32("RenderMaxTextureResolution", 2048);
        gSavedSettings.setF32("RenderLowMemMinDiscardIncrement", 5);
        gSavedSettings.setF32("RenderHighMemMinDiscardDecrement", 5);
        gSavedSettings.setF32("TextureDiscardMinimizedTime", 1.2f);
        state->deadline = LLFrameTimer::getTotalSeconds() + 30;
        createObject(*session);
        owner.setSceneObserver([](auto*, auto*, auto*, unsigned, unsigned) { vs_request_world_image(textureID, 1024); });
        return false;
    }
    auto& s = *state;
    require(LLFrameTimer::getTotalSeconds() < s.deadline, "R3 pressure lifecycle timed out");
    auto image = resources.image("remote:" + textureID.asString());
    if (!image)
        return false;
    const auto width = VSUIRenderer::imageExtent(image)[0];
    const auto bias  = resources.textureMemoryPolicy().bias;
    if (s.step == 0)
    {
        if (width != 1024 || !world.object(objectID))
            return false;
        bool       admitted = false;
        const auto packets  = world.packets(session->position(), 128, 480, 640);
        for (const auto& packet : packets)
            if (std::equal(packet.objectIdentity.begin(), packet.objectIdentity.end(), objectID.mData) && packet.image == image)
                admitted = true;
        if (!admitted)
            return false;
        s.evidence["initial"] = imageEvidence();
        gSavedSettings.setU32("RenderMinFreeMainMemoryThreshold", 1000000);
        gSavedSettings.setBOOL("TextureLoadFullRes", false);
        ++s.step;
    }
    else if (s.step == 1)
    {
        if (bias < 2 || width >= 1024)
            return false;
        s.evidence["pressure"]         = imageEvidence();
        s.evidence["pressure"]["bias"] = bias;
        gSavedSettings.setBOOL("TextureLoadFullRes", true);
        ++s.step;
    }
    else if (s.step == 2)
    {
        if (width != 1024)
            return false;
        s.evidence["full_resolution_preference"]         = imageEvidence();
        s.evidence["full_resolution_preference"]["bias"] = bias;
        gSavedSettings.getControl("RenderMinFreeMainMemoryThreshold")->setValue(s.saved["RenderMinFreeMainMemoryThreshold"]);
        ++s.step;
    }
    else if (s.step == 3)
    {
        if (bias > 1.01)
            return false;
        s.beforeMinimize = image;
        gSavedSettings.setBOOL("TextureLoadFullRes", false);
        window->minimize();
        require(window->getMinimized(), "Headless window did not enter minimized state");
        ++s.step;
    }
    else if (s.step == 4)
    {
        require(window->getMinimized(), "R3 pressure unexpectedly restored its window");
        if (bias < 3.9 || width != 256 || !s.beforeMinimize.expired())
            return false;
        s.evidence["minimized"]                                  = imageEvidence();
        s.evidence["minimized"]["bias"]                          = bias;
        s.evidence["minimized"]["previous_scene_images_retired"] = true;
        window->restore();
        ++s.step;
    }
    else if (s.step == 5)
    {
        if (width != 1024 || bias > 1.01)
            return false;
        s.evidence["restored"]         = imageEvidence();
        s.evidence["restored"]["bias"] = bias;
        require(!window->getMinimized(), "R3 pressure window did not restore");
        LLMessageSystem* msg = gMessageSystem;
        msg->newMessage("KillObject");
        msg->nextBlock("ObjectData");
        msg->addU32("ID", 995);
        require(msg->sendReliable(session->host()) > 0, "R3 pressure object retirement transport failed");
        ++s.step;
    }
    else
    {
        if (world.object(objectID))
            return false;
        s.evidence["complete"]       = true;
        s.evidence["object_retired"] = true;
        report["r3_pressure"]        = s.evidence;
        owner.setSceneObserver({});
        state.reset();
        return true;
    }
    return false;
}
void vs_r3_pressure_replay_cleanup()
{
    if (state && gViewerWindow && gViewerWindow->nativeContext())
        gViewerWindow->nativeContext()->setSceneObserver({});
    state.reset();
}
