// Replay ordinary appearance actions through their actual connected owners.
// Diagnostic build only; no substitute avatar or inventory service. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr2replay.h"
#include "vsr2readback.h"
#include "vsnativesession.h"
#include "vsnativeappearance.h"
#include "vsworldavatar.h"
#include "vsavatarservices.h"
#include "vswearable.h"
#include "vsvulkancontext.h"
#include "vsuiresources.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llavatarappearance.h"
#include "llavatarappearancedefines.h"
#include "lllocaltextureobject.h"
#include "llviewerinventory.h"
#include "llinventorymodel.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llappearancemgr.h"
#include "llattachmentsmgr.h"
#include "llframetimer.h"
#include "llcharacter.h"
#include "lljoint.h"
#include "llkeyframemotion.h"
#include "llkeyframestandmotion.h"
#include "llkeyframewalkmotion.h"
#include "llkeyframefallmotion.h"
#include "llemote.h"
#include "llphysicsmotion.h"
#include "vsavatarmotions.h"
#include "llagent.h"
#include "lldir.h"
#include "llxfermanager.h"
#include "llimagej2c.h"
#include "llmultigesture.h"
#include "llgesturemgr.h"
#include "lldatapacker.h"
#include "aoengine.h"
#include "aoset.h"
#include "llanimationstates.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "llsdjson.h"
#include "llsdutil_math.h"
#include "llprimitive.h"
#include "llquantize.h"
#include "llvolumemessage.h"
#include "message.h"
#include <array>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <cstdlib>
namespace
{
using namespace LLAvatarAppearanceDefines;
void require(bool condition, const char* message)
{
    if (!condition)
        throw std::runtime_error(message);
}
const LLUUID cof("70000000-0000-0000-0000-000000000017");
const LLUUID bodyFolder("70000000-0000-0000-0000-000000000010");
const LLUUID clothingFolder("70000000-0000-0000-0000-000000000007");
const LLUUID       objectsFolder("70000000-0000-0000-0000-000000000008");
const LLUUID animationID("c1000000-0000-0000-0000-000000000001");
const LLUUID geometryID("c2000000-0000-0000-0000-000000000001");
const LLUUID riggedAssetID("c3000000-0000-0000-0000-000000000001");
const LLUUID       remoteAvatarID("c5000000-0000-0000-0000-000000000001");
constexpr unsigned completedStep = 53;
struct MotionCase
{
    std::string label;
    LLUUID      source, mapped;
    bool        female, newWalk;
    std::string kind;
};
const std::vector<MotionCase>& motionCases()
{
    static const std::vector<MotionCase> cases = []
    {
        std::vector<MotionCase> rows{ { "stand", ANIM_AGENT_STAND, ANIM_AGENT_STAND, true, false, "stand" },
                                      { "female-old-walk", ANIM_AGENT_WALK, ANIM_AGENT_FEMALE_WALK, true, false, "walk" },
                                      { "female-old-run", ANIM_AGENT_RUN, ANIM_AGENT_RUN, true, false, "walk" },
                                      { "female-new-walk", ANIM_AGENT_WALK, ANIM_AGENT_FEMALE_WALK_NEW, true, true, "walk" },
                                      { "female-new-run", ANIM_AGENT_RUN, ANIM_AGENT_FEMALE_RUN_NEW, true, true, "walk" },
                                      { "fall-standup", ANIM_AGENT_STANDUP, ANIM_AGENT_STANDUP, true, true, "fall" },
                                      { "ground-sit", ANIM_AGENT_SIT_GROUND_CONSTRAINED, ANIM_AGENT_SIT_GROUND_CONSTRAINED, true, true,
                                        "keyframe" },
                                      { "crouch", ANIM_AGENT_CROUCH, ANIM_AGENT_CROUCH, true, true, "stand" },
                                      { "crouch-walk", ANIM_AGENT_CROUCHWALK, ANIM_AGENT_CROUCHWALK, true, true, "walk" },
                                      { "turn-left", ANIM_AGENT_TURNLEFT, ANIM_AGENT_TURNLEFT, true, true, "walk" },
                                      { "turn-right", ANIM_AGENT_TURNRIGHT, ANIM_AGENT_TURNRIGHT, true, true, "walk" },
                                      { "female-sit", ANIM_AGENT_SIT, ANIM_AGENT_SIT_FEMALE, true, true, "keyframe" },
                                      { "male-sit", ANIM_AGENT_SIT, ANIM_AGENT_SIT, false, true, "keyframe" },
                                      { "male-old-walk", ANIM_AGENT_WALK, ANIM_AGENT_WALK, false, false, "walk" },
                                      { "male-old-run", ANIM_AGENT_RUN, ANIM_AGENT_RUN, false, false, "walk" },
                                      { "male-new-walk", ANIM_AGENT_WALK, ANIM_AGENT_WALK_NEW, false, true, "walk" },
                                      { "male-new-run", ANIM_AGENT_RUN, ANIM_AGENT_RUN_NEW, false, true, "walk" },
                                      { "female-emote-smile", ANIM_AGENT_EXPRESS_SMILE, ANIM_AGENT_EXPRESS_SMILE, true, true, "emote" } };
        for (const auto& id :
             { ANIM_AGENT_EXPRESS_AFRAID, ANIM_AGENT_EXPRESS_ANGER, ANIM_AGENT_EXPRESS_BORED, ANIM_AGENT_EXPRESS_CRY,
               ANIM_AGENT_EXPRESS_DISDAIN, ANIM_AGENT_EXPRESS_EMBARRASSED, ANIM_AGENT_EXPRESS_FROWN, ANIM_AGENT_EXPRESS_KISS,
               ANIM_AGENT_EXPRESS_LAUGH, ANIM_AGENT_EXPRESS_OPEN_MOUTH, ANIM_AGENT_EXPRESS_REPULSED, ANIM_AGENT_EXPRESS_SAD,
               ANIM_AGENT_EXPRESS_SHRUG, ANIM_AGENT_EXPRESS_SMILE, ANIM_AGENT_EXPRESS_SURPRISE, ANIM_AGENT_EXPRESS_TONGUE_OUT,
               ANIM_AGENT_EXPRESS_TOOTHSMILE, ANIM_AGENT_EXPRESS_WINK, ANIM_AGENT_EXPRESS_WORRY })
        {
            const auto* name = gAnimLibrary.animStateToString(id);
            require(name, "R2 motion fixture lacks a standard animation name");
            rows.push_back({ std::string("emote-") + name, id, id, false, true, "emote" });
        }
        return rows;
    }();
    return cases;
}
LLUUID       item(unsigned index)
{ return LLUUID(llformat("b1000000-0000-0000-0000-%012u", index)); }
LLUUID asset(unsigned index)
{ return LLUUID(llformat("b2000000-0000-0000-0000-%012u", index)); }
struct Replay
{
    bool                                 savedNewWalk = false, savedPhysics = false;
    F32                                  savedPhysicsLOD = 0;
    unsigned                             motionCase = 0, motionPhase = 0;
    double                               motionTime = 0;
    std::array<F32, 3>                   physicsWeights{};
    unsigned                             step = 0, logged = ~0u;
    double                               deadline     = 0;
    double                               nextProgress = 0;
    VSNativeSession::AccountHandle       account;
    LLSD                                 evidence;
    LLVector3                            initialSize;
    std::filesystem::path                directory;
    std::map<LLUUID, S32>                serverAnimations;
    bool                                 animationReply = false;
    bool                                 attachmentQueued = false, attachmentRezSeen = false;
    bool                                 detachQueued = false, detachSeen = false;
    std::vector<VSWorldRenderer::Packet> initialPose;
    LLQuaternion                         initialShoulder;
    LLQuaternion                         remoteShoulder;
    F32                                  remoteHeight              = 0;
    bool                                 visibilitySettingsChanged = false, savedFriendsOnly = false;
    U32                                  savedComplexity     = 0;
    S32                                  savedComplexityMode = 0;
    unsigned                             topologyCase      = 0;
    unsigned                             gltfAnimationCase = 0;
    LLVector3                            originalShoulderPosition;
    bool                                 legacyEnabled = false, originalLegacySetting = false;
    struct LegacyUpload
    {
        LLUUID          asset;
        U32             expectedBytes = 0, nextPacket = 0;
        std::vector<U8> bytes;
        bool            request = true, complete = false, reply = false;
    };
    U64                                                         nextLegacyTransfer = 0xc400000000000000ULL;
    std::map<U64, LegacyUpload>                                 legacyUploads;
    std::vector<std::pair<U64, U32>>                            legacyAcks;
    std::shared_ptr<bool>                                       bakeReadback     = std::make_shared<bool>(false);
    unsigned                                                    codecCase        = 0;
    bool                                                        controlsReleased = false;
    std::function<bool()>                                       retiredAccountGuard;
    std::vector<std::weak_ptr<const VSWorldRenderer::Geometry>> logoutMeshes;
    std::vector<std::weak_ptr<const VSUIRenderer::Texture>>     logoutImages;
    unsigned                                                    hudCase = 0;
    std::vector<unsigned>                                       hudPoints;
    std::optional<VSWorldScene::ObjectHandle>                   retiredObject;
    VSWorldRenderer::Mesh                                       initialGLTF;
    std::weak_ptr<const VSWorldRenderer::Geometry>              retiredGLTF;
    std::vector<VSWorldRenderer::Packet>                        geometry(VSNativeSession& session)
    {
        auto packets = gViewerWindow->nativeContext()->world().packets(session.position() + LLVector3(0, -4, 1), 128, 480, 640);
        packets.erase(std::remove_if(packets.begin(), packets.end(), [](const auto& packet)
                                     { return !std::equal(packet.objectIdentity.begin(), packet.objectIdentity.end(), geometryID.mData); }),
                      packets.end());
        return packets;
    }
    void gltfBounds(const VSWorldScene::ObjectHandle& object, const VSWorldRenderer::Mesh& mesh)
    {
        const auto selected = gViewerWindow->nativeContext()->world().selectionBounds(object);
        require(selected.has_value(), "R2 glTF selection bounds are missing");
        const auto bounds = VSWorldRenderer::bounds(mesh);
        for (unsigned side = 0; side < 2; ++side)
            for (unsigned axis = 0; axis < 3; ++axis)
                require(std::abs((*selected)[side][axis] - bounds[side][axis]) < 1.e-5f,
                        "R2 glTF selection bounds differ from admitted vertices");
    }
    ~Replay()
    {
        restoreVisibilitySettings();
        if (gMessageSystem)
        {
            gMessageSystem->setHandlerFunc("AgentAnimation", nullptr);
            gMessageSystem->setHandlerFunc("ForceScriptControlRelease", nullptr);
            gMessageSystem->setHandlerFunc("RezMultipleAttachmentsFromInv", nullptr);
            gMessageSystem->setHandlerFunc("DetachAttachmentIntoInv", nullptr);
            if (legacyEnabled)
            {
                gMessageSystem->setHandlerFunc("AssetUploadRequest", nullptr);
                gMessageSystem->setHandlerFunc("SendXferPacket", continue_file_receive);
            }
        }
        if (legacyEnabled)
            gSavedSettings.setBOOL("UploadBakedTexOld", originalLegacySetting);
    }
    static void legacyRequest(LLMessageSystem* message, void** data)
    {
        auto&      replay  = *reinterpret_cast<Replay*>(data);
        const auto session = VSNativeSession::active();
        if (!session || !session->current(replay.account) || message->getSender() != session->host())
            return;
        require(replay.legacyUploads.size() < 32, "R2 legacy bake fixture upload limit exceeded");
        LLTransactionID transaction;
        S8              type;
        message->getUUID("AssetBlock", "TransactionID", transaction);
        message->getS8("AssetBlock", "Type", type);
        require(type == LLAssetType::AT_TEXTURE && transaction.notNull(), "R2 bake upload lost texture/transaction identity");
        LegacyUpload upload;
        upload.asset   = transaction.makeAssetID(gAgent.getSecureSessionID());
        const S32 size = message->getSize("AssetBlock", "AssetData");
        require(size >= 0 && size < 4096, "R2 inline bake upload exceeds packet bound");
        if (size)
        {
            upload.bytes.resize(size);
            message->getBinaryData("AssetBlock", "AssetData", upload.bytes.data(), size);
            upload.expectedBytes = size;
            upload.request       = false;
            upload.complete      = true;
        }
        replay.legacyUploads.emplace(++replay.nextLegacyTransfer, std::move(upload));
    }
    static void legacyPacket(LLMessageSystem* message, void** data)
    {
        auto& replay = *reinterpret_cast<Replay*>(data);
        U64   transfer;
        U32   packet;
        message->getU64("XferID", "ID", transfer);
        message->getU32("XferID", "Packet", packet);
        const auto found = replay.legacyUploads.find(transfer);
        if (found == replay.legacyUploads.end())
        {
            continue_file_receive(message, nullptr);
            return;
        }
        const auto session = VSNativeSession::active();
        if (!session || !session->current(replay.account) || message->getSender() != session->host())
            return;
        const U32 number = packet & 0x7fffffff;
        auto&     upload = found->second;
        if (number < upload.nextPacket)
        {
            replay.legacyAcks.emplace_back(transfer, number);
            return;
        }
        require(number == upload.nextPacket && !upload.complete, "R2 legacy bake packet sequence is invalid");
        const S32 size = message->getSize("DataPacket", "Data");
        require(size > 0 && size <= 1204, "R2 legacy bake payload exceeds negotiated packet size");
        std::array<U8, 1204> bytes{};
        message->getBinaryData("DataPacket", "Data", bytes.data(), size);
        unsigned offset = 0;
        if (!number)
        {
            require(size > 4, "R2 first legacy bake packet lacks its size prefix");
            upload.expectedBytes = U32(bytes[0]) | (U32(bytes[1]) << 8) | (U32(bytes[2]) << 16) | (U32(bytes[3]) << 24);
            require(upload.expectedBytes > 0 && upload.expectedBytes <= 64 * 1024 * 1024, "R2 legacy bake stream exceeds admission bound");
            upload.bytes.reserve(upload.expectedBytes);
            offset = 4;
        }
        require(upload.bytes.size() + size - offset <= upload.expectedBytes, "R2 legacy bake stream exceeds declared length");
        upload.bytes.insert(upload.bytes.end(), bytes.begin() + offset, bytes.begin() + size);
        ++upload.nextPacket;
        replay.legacyAcks.emplace_back(transfer, number);
        if (packet & 0x80000000)
        {
            require(upload.bytes.size() == upload.expectedBytes, "R2 legacy bake final packet is truncated");
            upload.complete = true;
        }
    }
    void legacyResponses(VSNativeSession& session)
    {
        if (!legacyEnabled)
            return;
        LLMessageSystem* message = gMessageSystem;
        for (auto& [id, upload] : legacyUploads)
        {
            if (upload.request)
            {
                upload.request = false;
                message->newMessage("RequestXfer");
                message->nextBlock("XferID");
                message->addU64("ID", id);
                message->addString("Filename", "");
                message->addU8("FilePath", LL_PATH_NONE);
                message->addBOOL("DeleteOnCompletion", false);
                message->addBOOL("UseBigPackets", false);
                message->addUUID("VFileID", upload.asset);
                message->addS16("VFileType", LLAssetType::AT_TEXTURE);
                require(message->sendReliable(session.host()) > 0, "R2 legacy bake xfer request failed");
            }
        }
        for (const auto& [id, packet] : legacyAcks)
        {
            message->newMessage("ConfirmXferPacket");
            message->nextBlock("XferID");
            message->addU64("ID", id);
            message->addU32("Packet", packet);
            require(message->sendReliable(session.host()) > 0, "R2 legacy bake packet acknowledgement failed");
        }
        legacyAcks.clear();
        for (auto& [id, upload] : legacyUploads)
            if (upload.complete && !upload.reply)
            {
                std::ofstream output(directory / (upload.asset.asString() + ".legacy.j2c"), std::ios::binary);
                output.write(reinterpret_cast<const char*>(upload.bytes.data()), upload.bytes.size());
                require(output.good(), "R2 legacy bake wire evidence write failed");
                message->newMessage("AssetUploadComplete");
                message->nextBlock("AssetBlock");
                message->addUUID("UUID", upload.asset);
                message->addS8("Type", LLAssetType::AT_TEXTURE);
                message->addBOOL("Success", true);
                require(message->sendReliable(session.host()) > 0, "R2 legacy bake completion transport failed");
                upload.reply = true;
                LLSD row;
                row["asset"]   = upload.asset;
                row["bytes"]   = S32(upload.bytes.size());
                row["packets"] = S32(upload.nextPacket);
                evidence["legacy_uploads"].append(row);
            }
    }
    static void animationRequest(LLMessageSystem* message, void** data)
    {
        auto&      replay = *reinterpret_cast<Replay*>(data);
        const auto owner  = VSNativeSession::active();
        if (!owner || !owner->current(replay.account) || message->getSender() != owner->host())
            return;
        LLUUID agent, session;
        message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", session);
        require(agent == replay.account.agent && session == replay.account.session, "R2 animation request lost account identity");
        const auto count = message->getNumberOfBlocks("AnimationList");
        require(count >= 0 && count <= 256, "R2 animation request was unbounded");
        for (S32 index = 0; index < count; ++index)
        {
            LLUUID id;
            bool   start = false;
            message->getUUID("AnimationList", "AnimID", id, index);
            message->getBOOL("AnimationList", "StartAnim", start, index);
            if (start && id.notNull())
                ++replay.serverAnimations[id];
            else if (id.isNull())
                replay.serverAnimations.clear();
            else
                replay.serverAnimations.erase(id);
        }
        replay.animationReply = true;
    }
    static void controlRelease(LLMessageSystem* message, void** data)
    {
        auto&      replay = *reinterpret_cast<Replay*>(data);
        const auto owner  = VSNativeSession::active();
        if (!owner || !owner->current(replay.account) || message->getSender() != owner->host())
            return;
        LLUUID agent, session;
        message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", session);
        require(agent == replay.account.agent && session == replay.account.session, "R2 control release lost its account identity");
        replay.controlsReleased = true;
    }
    void sendAnimations(VSNativeSession& session)
    {
        if (!animationReply)
            return;
        animationReply           = false;
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("AvatarAnimation");
        message->nextBlock("Sender");
        message->addUUID("ID", account.agent);
        for (const auto& [id, sequence] : serverAnimations)
        {
            message->nextBlock("AnimationList");
            message->addUUID("AnimID", id);
            message->addS32("AnimSequenceID", sequence);
        }
        for (const auto& [id, sequence] : serverAnimations)
        {
            message->nextBlock("AnimationSourceList");
            message->addUUID("ObjectID", account.agent);
        }
        require(message->sendReliable(session.host()) > 0, "R2 simulator animation response could not be sent");
    }
    void restoreVisibilitySettings()
    {
        if (!visibilitySettingsChanged)
            return;
        gSavedSettings.setBOOL("RenderAvatarFriendsOnly", savedFriendsOnly);
        gSavedSettings.setU32("RenderAvatarMaxComplexity", savedComplexity);
        gSavedSettings.setS32("RenderAvatarComplexityMode", savedComplexityMode);
        gSavedSettings.setBOOL("UseNewWalkRun", savedNewWalk);
        gSavedSettings.setBOOL("AvatarPhysics", savedPhysics);
        gSavedSettings.setF32("RenderAvatarPhysicsLODFactor", savedPhysicsLOD);
        visibilitySettingsChanged = false;
    }
    std::vector<VSWorldRenderer::Packet> body(VSNativeSession& session, const LLUUID* identity = nullptr)
    {
        const LLUUID& id      = identity ? *identity : account.agent;
        auto& world   = gViewerWindow->nativeContext()->world();
        auto  packets = world.packets(session.position() + LLVector3(0, -4, 1), 128, 480, 640);
        packets.erase(std::remove_if(packets.begin(), packets.end(), [&](const auto& packet)
                                     { return !std::equal(packet.objectIdentity.begin(), packet.objectIdentity.end(), id.mData); }),
                      packets.end());
        require(!packets.empty(), "R2 real avatar did not publish visible body triangles");
        auto bounds = VSWorldRenderer::bounds(packets.front().mesh);
        for (const auto& packet : packets)
        {
            const auto mesh    = VSWorldRenderer::geometryDescription(packet.mesh);
            const auto buffers = VSWorldRenderer::geometryBuffers(packet.mesh);
            require(mesh.vertices && mesh.indices && buffers[0] && buffers[1], "R2 avatar publication lacks actual GPU geometry");
            const auto candidate = VSWorldRenderer::bounds(packet.mesh);
            for (unsigned axis = 0; axis < 3; ++axis)
            {
                bounds[0][axis] = std::min(bounds[0][axis], candidate[0][axis]);
                bounds[1][axis] = std::max(bounds[1][axis], candidate[1][axis]);
            }
        }
        const auto object = world.object(id);
        require(object.has_value(), "R2 animated avatar has no scene handle");
        const auto selected = world.selectionBounds(*object);
        require(selected.has_value(), "R2 animated selection bounds are missing");
        for (unsigned side = 0; side < 2; ++side)
            for (unsigned axis = 0; axis < 3; ++axis)
                require(std::abs((*selected)[side][axis] - bounds[side][axis]) < 1.e-5f,
                        "R2 selection bounds differ from admitted body geometry");
        return packets;
    }
    void fixtures(VSWorldAvatar& avatar, const LLUUID& agent)
    {
        auto&                            appearance = avatar.appearanceOwner();
        std::vector<std::pair<S32, F32>> weights;
        for (auto* param = appearance.getFirstVisualParam(); param; param = appearance.getNextVisualParam())
            weights.emplace_back(param->getID(), param->getWeight());
        struct Restore
        {
            LLAvatarAppearance&                     avatar;
            const std::vector<std::pair<S32, F32>>& weights;
            ~Restore()
            {
                for (const auto& [id, value] : weights)
                    avatar.setVisualParamWeight(id, value, false);
                avatar.updateVisualParams();
            }
        } restore{ appearance, weights };
        LLSD             manifest;
        const std::array types{ LLWearableType::WT_SHAPE, LLWearableType::WT_SKIN,  LLWearableType::WT_HAIR, LLWearableType::WT_EYES,
                                LLWearableType::WT_SHIRT, LLWearableType::WT_SHAPE, LLWearableType::WT_SHIRT };
        for (unsigned index : { 1u, 2u, 3u, 4u, 5u, 6u, 7u, 10u, 11u })
        {
            const auto type     = index == 11 ? LLWearableType::WT_PHYSICS : index == 10 ? LLWearableType::WT_SKIRT : types[index - 1];
            auto       wearable = VSWearable::create(item(index), asset(index), {});
            wearable->setType(type, &appearance);
            wearable->setName("R2 fixture " + std::to_string(index));
            wearable->setDefinitionVersion(LLWearable::getCurrentDefinitionVersion());
            if (type == LLWearableType::WT_SHAPE)
                wearable->setVisualParamWeight(33, index == 6 ? .85f : .25f, false);
            if (type == LLWearableType::WT_PHYSICS)
                wearable->setVisualParamWeight(10014, .8f, false);
            for (const auto& [texture, entry] : LLAvatarAppearance::getDictionary()->getTextures())
                if (entry->mIsLocalTexture && entry->mWearableType == type)
                {
                    LLLocalTextureObject local(nullptr, wearable->getDefaultTextureImageID(texture));
                    wearable->setLocalTextureObject(texture, local);
                }
            require(wearable->exportFile((directory / (asset(index).asString() + ".wearable")).string()), "R2 wearable export failed");
            LLSD row;
            row["item_id"]    = item(index);
            row["asset_id"]   = asset(index);
            row["parent_id"]  = index <= 4 || index == 6 ? bodyFolder : clothingFolder;
            row["type"]       = index <= 4 || index == 6 ? "bodypart" : "clothing";
            row["inv_type"]   = "wearable";
            row["flags"]      = S32(type);
            row["name"]       = wearable->getName();
            row["desc"]       = "Production appearance fixture";
            row["created_at"] = 1;
            for (const char* key : { "creator_id", "owner_id", "last_owner_id" })
                row["permissions"][key] = agent;
            row["permissions"]["group_id"]       = LLUUID::null;
            row["permissions"]["is_owner_group"] = false;
            for (const char* key : { "base_mask", "owner_mask", "next_owner_mask" })
                row["permissions"][key] = 0x7fffffff;
            row["permissions"]["group_mask"]    = 0;
            row["permissions"]["everyone_mask"] = 0;
            row["sale_info"]["sale_type"]       = 0;
            row["sale_info"]["sale_price"]      = 0;
            manifest["items"].append(row);
        }
        for (unsigned index : { 8, 9 })
        {
            LLSD row        = manifest["items"][4];
            row["item_id"]  = item(index);
            row["asset_id"] = index == 8 ? animationID : asset(index);
            row["flags"]    = 0;
            row["type"]     = index == 8 ? "animatn" : "gesture";
            row["inv_type"] = index == 8 ? "animation" : "gesture";
            row["name"]     = index == 8 ? "R2 actual animation" : "R2 actual gesture";
            manifest["items"].append(row);
        }
        LLSD attachment         = manifest["items"][4];
        attachment["item_id"]   = item(12);
        attachment["asset_id"]  = LLUUID("c6000000-0000-0000-0000-000000000001");
        attachment["parent_id"] = objectsFolder;
        attachment["type"]      = "object";
        attachment["inv_type"]  = "object";
        attachment["flags"]     = 0;
        attachment["name"]      = "R2 inventory attachment";
        manifest["items"].append(attachment);
        LLMultiGesture gesture;
        gesture.mName       = "R2 actual gesture";
        gesture.mTrigger    = "/r2-gesture";
        auto* start         = new LLGestureStepAnimation;
        start->mAnimAssetID = animationID;
        start->mAnimName    = "R2 shoulder";
        auto* wait          = new LLGestureStepWait;
        wait->mFlags        = WAIT_FLAG_TIME;
        wait->mWaitSeconds  = 1;
        auto* stop          = new LLGestureStepAnimation;
        stop->mAnimAssetID  = animationID;
        stop->mFlags        = ANIM_FLAG_STOP;
        gesture.mSteps      = { start, wait, stop };
        std::vector<U8>         bytes(gesture.getMaxSerialSize());
        LLDataPackerAsciiBuffer packer(reinterpret_cast<char*>(bytes.data()), S32(bytes.size()));
        require(gesture.serialize(packer), "R2 actual gesture serialization failed");
        std::ofstream gestureFile(directory / (asset(9).asString() + ".gesture"), std::ios::binary);
        gestureFile.write(reinterpret_cast<const char*>(bytes.data()), packer.getCurrentSize());
        require(gestureFile.good(), "R2 actual gesture fixture write failed");
        manifest["agent"]     = agent;
        LLSD             motionCatalog;
        std::set<LLUUID> clips;
        for (const auto& test : motionCases())
        {
            LLSD row;
            row["label"]    = test.label;
            row["source"]   = test.source;
            row["mapped"]   = test.mapped;
            row["female"]   = test.female;
            row["new_walk"] = test.newWalk;
            row["kind"]     = test.kind;
            motionCatalog["cases"].append(row);
            if (test.kind != "emote")
                clips.insert(test.mapped);
        }
        for (const auto& clip : clips)
            motionCatalog["assets"].append(clip);
        const auto    motionPending = directory / "motion-catalog.pending.json";
        std::ofstream motionFile(motionPending);
        motionFile << LlsdToJson(motionCatalog);
        motionFile.close();
        require(!motionFile.fail(), "R2 standard motion catalog write failed");
        std::filesystem::rename(motionPending, directory / "motion-catalog.json");
        const auto    pending = directory / "inventory.pending.json";
        std::ofstream output(pending);
        output << LlsdToJson(manifest);
        output.close();
        require(!output.fail(), "R2 inventory manifest export failed");
        // The HTTP fixture observes either no manifest or the complete generation.
        std::filesystem::rename(pending, directory / "inventory.json");
    }
    void avatarPacket(VSNativeSession& session, bool primitive = false, U32 parent = 0, U8 point = 0, bool rigged = false,
                      bool control = false, bool remote = false, const LLUUID& inventory = LLUUID::null,
                      const LLVector3& velocity = LLVector3::zero)
    {
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("ObjectUpdate");
        message->nextBlock("RegionData");
        message->addU64("RegionHandle", session.handle());
        message->addU16("TimeDilation", 65535);
        message->nextBlock("ObjectData");
        message->addU32("ID", remote ? 982 : primitive ? 981 : 980);
        message->addU8("State", U8((point << 4) | (point >> 4)));
        message->addUUID("FullID", remote ? remoteAvatarID : primitive ? geometryID : session.account().agent);
        message->addU32("CRC", 0);
        message->addU8("PCode", primitive ? LL_PCODE_VOLUME : LL_PCODE_LEGACY_AVATAR);
        message->addU8("Material", 0);
        message->addU8("ClickAction", 0);
        message->addVector3("Scale", primitive ? (parent ? LLVector3(.15f, .15f, .15f) : LLVector3(1, 1, 1)) : LLVector3(.45f, .6f, 1.8f));
        const auto          position = parent ? LLVector3::zero : session.position();
        std::array<F32, 19> pose{};
        const unsigned      offset = primitive ? 0 : 4;
        pose[offset]               = position.mV[0];
        pose[offset + 1]           = position.mV[1];
        pose[offset + 2]           = position.mV[2];
        for (unsigned axis = 0; axis < 3; ++axis)
            pose[offset + 3 + axis] = velocity.mV[axis];
        message->addBinaryData("ObjectData", pose.data(), S32((primitive ? 15 : 19) * sizeof(F32)));
        message->addU32("ParentID", parent);
        message->addU32("UpdateFlags", 0);
        LLVolumeParams params;
        LLVolumeMessage::packVolumeParams(&params, message);
        LLPrimitive textures;
        textures.setNumTEs(primitive ? 1 : TEX_NUM_INDICES);
        textures.packTEMessage(message);
        for (const char* name : { "TextureAnim", "Data", "Text", "MediaURL", "PSBlock" })
            message->addBinaryData(name, nullptr, 0);
        const std::string names = inventory.isNull() ? "" : "AttachItemID STRING RW SV " + inventory.asString() + "\n";
        message->addBinaryData("NameValue", names.data(), S32(names.size()));
        if (rigged)
        {
            std::vector<U8> extra(control ? 34 : 24);
            extra[0] = control ? 2 : 1;
            extra[1] = 0x30;
            extra[3] = 17;
            std::copy(riggedAssetID.mData, riggedAssetID.mData + UUID_BYTES, extra.begin() + 7);
            extra[23] = LL_SCULPT_TYPE_MESH;
            if (control)
            {
                extra[24] = 0x70;
                extra[26] = 4;
                extra[30] = 1;
            }
            message->addBinaryData("ExtraParams", extra.data(), S32(extra.size()));
        }
        else
            message->addBinaryData("ExtraParams", nullptr, 0);
        const U8 colour[4]{ 255, 255, 255, 255 };
        message->addBinaryData("TextColor", colour, 4);
        message->addUUID("Sound", LLUUID::null);
        message->addUUID("OwnerID", session.account().agent);
        message->addF32("Gain", 0);
        message->addU8("Flags", 0);
        message->addF32("Radius", 0);
        message->addU8("JointType", 0);
        message->addVector3("JointPivot", LLVector3::zero);
        message->addVector3("JointAxisOrAnchor", LLVector3::zero);
        require(message->sendReliable(session.host()) > 0, "R2 avatar transport failed");
    }
    static void attachmentRequest(LLMessageSystem* message, void** data)
    {
        auto&      replay  = *reinterpret_cast<Replay*>(data);
        const auto session = VSNativeSession::active();
        if (!session || !session->current(replay.account) || message->getSender() != session->host())
            return;
        LLUUID agent, login, inventory, owner, compound;
        message->getUUID("AgentData", "AgentID", agent);
        message->getUUID("AgentData", "SessionID", login);
        message->getUUID("HeaderData", "CompoundMsgID", compound);
        U8   count, point;
        bool detach;
        message->getU8("HeaderData", "TotalObjects", count);
        message->getBOOL("HeaderData", "FirstDetachAll", detach);
        require(message->getNumberOfBlocks("ObjectData") == 1, "R2 attachment compound packet contains unexpected objects");
        message->getUUID("ObjectData", "ItemID", inventory);
        message->getUUID("ObjectData", "OwnerID", owner);
        message->getU8("ObjectData", "AttachmentPt", point);
        require(agent == replay.account.agent && login == replay.account.session && owner == agent && compound.notNull() && count == 1 &&
                    !detach && inventory == item(12) && point == 0x81,
                "R2 ordinary attachment request lost its account/inventory/point ownership");
        require(!replay.attachmentRezSeen, "R2 ordinary attachment queue duplicated its acknowledged rez request");
        replay.attachmentRezSeen = true;
    }
    static void detachRequest(LLMessageSystem* message, void** data)
    {
        auto&      replay  = *reinterpret_cast<Replay*>(data);
        const auto session = VSNativeSession::active();
        if (!session || !session->current(replay.account) || message->getSender() != session->host())
            return;
        LLUUID agent, inventory;
        message->getUUID("ObjectData", "AgentID", agent);
        message->getUUID("ObjectData", "ItemID", inventory);
        require(agent == replay.account.agent && inventory == item(12), "R2 ordinary detach request lost account/inventory ownership");
        require(!replay.detachSeen, "R2 ordinary attachment queue duplicated its acknowledged detach request");
        replay.detachSeen = true;
    }
    void remoteAppearance(VSNativeSession& session, VSWorldAvatar& source, std::optional<bool> female = {})
    {
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("AvatarAppearance");
        message->nextBlock("Sender");
        message->addUUID("ID", remoteAvatarID);
        message->addBOOL("IsTrial", false);
        message->nextBlock("ObjectData");
        LLPrimitive textures;
        textures.setNumTEs(TEX_NUM_INDICES);
        const auto uploads = session.appearance()->evidence()["bake_uploads"];
        for (const auto& row : llsd::inArray(uploads))
        {
            const auto* baked = LLAvatarAppearance::getDictionary()->getBakedTexture(EBakedTextureIndex(row["index"].asInteger()));
            require(baked, "R2 remote appearance contains an unknown bake channel");
            textures.setTETexture(U8(baked->mTextureIndex), row["asset"].asUUID());
        }
        textures.packTEMessage(message);
        auto&       appearance = source.appearanceOwner();
        const auto* sexParam   = appearance.getVisualParam("male");
        require(sexParam, "R2 remote appearance lacks the shared sex parameter");
        for (auto* param = appearance.getFirstVisualParam(); param; param = appearance.getNextVisualParam())
            if (param->getGroup() == VISUAL_PARAM_GROUP_TWEAKABLE || param->getGroup() == VISUAL_PARAM_GROUP_TRANSMIT_NOT_TWEAKABLE)
            {
                message->nextBlock("VisualParam");
                message->addU8("ParamValue", female && param == sexParam
                                                 ? (*female ? 0 : 255)
                                                 : F32_to_U8(param->getWeight(), param->getMinWeight(), param->getMaxWeight()));
            }
        message->nextBlock("AppearanceData");
        message->addU8("AppearanceVersion", 1);
        message->addS32("CofVersion", 1);
        message->addU32("Flags", 0);
        message->nextBlock("AppearanceHover");
        message->addVector3("HoverHeight", LLVector3(0, 0, .1f));
        require(message->sendReliable(session.host()) > 0, "R2 remote appearance transport failed");
    }
    void remoteAnimation(VSNativeSession& session, const LLUUID& id = animationID, S32 sequence = 1)
    {
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("AvatarAnimation");
        message->nextBlock("Sender");
        message->addUUID("ID", remoteAvatarID);
        if (id.notNull())
        {
            message->nextBlock("AnimationList");
            message->addUUID("AnimID", id);
            message->addS32("AnimSequenceID", sequence);
            message->nextBlock("AnimationSourceList");
            message->addUUID("ObjectID", remoteAvatarID);
        }
        require(message->sendReliable(session.host()) > 0, "R2 remote animation transport failed");
    }
    void killGeometry(VSNativeSession& session, U32 local = 981)
    {
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("KillObject");
        message->nextBlock("ObjectData");
        message->addU32("ID", local);
        require(message->sendReliable(session.host()) > 0, "R2 object retirement transport failed");
    }
    void riggedFixture(VSWorldAvatar& avatar)
    {
        const std::array<U16, 9> positions{ 0, 0, 32768, 65535, 0, 32768, 32768, 65535, 32768 };
        const std::array<U16, 9> normals{ 32768, 32768, 65535, 32768, 32768, 65535, 32768, 32768, 65535 };
        const std::array<U16, 6> uv{ 0, 0, 65535, 0, 32768, 65535 };
        const std::array<U16, 3> indices{ 0, 1, 2 };
        const std::array<U8, 12> weights{ 0, 255, 255, 255, 0, 255, 255, 255, 0, 255, 255, 255 };
        auto                     binary = [](const auto& values)
        {
            const auto* bytes = reinterpret_cast<const U8*>(values.data());
            return LLSD::Binary(bytes, bytes + sizeof(values));
        };
        LLSD face;
        face["Position"]              = binary(positions);
        face["Normal"]                = binary(normals);
        face["TexCoord0"]             = binary(uv);
        face["TriangleList"]          = binary(indices);
        face["Weights"]               = binary(weights);
        face["PositionDomain"]["Min"] = ll_sd_from_vector3(LLVector3(-.2f, -.2f, -.2f));
        face["PositionDomain"]["Max"] = ll_sd_from_vector3(LLVector3(.2f, .2f, .2f));
        for (const char* side : { "Min", "Max" })
        {
            face["TexCoord0Domain"][side].append(side[1] == 'i' ? 0 : 1);
            face["TexCoord0Domain"][side].append(side[1] == 'i' ? 0 : 1);
        }
        LLSD faces = LLSD::emptyArray();
        faces.append(face);
        const auto compressed = zip_llsd(faces);
        LLSD       skin, identity;
        for (unsigned i = 0; i < 16; ++i)
            identity.append(i % 5 == 0 ? 1.f : 0.f);
        skin["joint_names"].append("mShoulderRight");
        skin["inverse_bind_matrix"].append(identity);
        skin["bind_shape_matrix"] = identity;
        auto       alternate      = identity;
        const auto position       = avatar.character().getJoint("mShoulderRight")->getDefaultPosition() + LLVector3(0, .05f, 0);
        for (unsigned axis = 0; axis < 3; ++axis)
            alternate[12 + axis] = position.mV[axis];
        skin["alt_inverse_bind_matrix"].append(alternate);
        skin["pelvis_offset"]                = .01f;
        skin["lock_scale_if_joint_position"] = true;
        const auto skinBytes                 = zip_llsd(skin);
        LLSD       header;
        header["version"] = 1;
        for (const char* lod : { "lowest_lod", "low_lod", "medium_lod", "high_lod" })
        {
            header[lod]["offset"] = 0;
            header[lod]["size"]   = S32(compressed.size());
        }
        header["skin"]["offset"] = S32(compressed.size());
        header["skin"]["size"]   = S32(skinBytes.size());
        std::ofstream output(directory / "rigged.mesh", std::ios::binary);
        LLSDSerialize::toBinary(header, output);
        output.write(compressed.data(), compressed.size());
        output.write(skinBytes.data(), skinBytes.size());
        require(output.good(), "R2 actual rigged mesh asset write failed");
    }
    void objectAnimation(VSNativeSession& session)
    {
        LLMessageSystem* message = gMessageSystem;
        message->newMessage("ObjectAnimation");
        message->nextBlock("Sender");
        message->addUUID("ID", geometryID);
        message->nextBlock("AnimationList");
        message->addUUID("AnimID", animationID);
        message->addS32("AnimSequenceID", 1);
        require(message->sendReliable(session.host()) > 0, "R2 animesh simulator animation transport failed");
    }
    bool tick(VSNativeSession& session, LLSD& report)
    {
        // The enclosing replay may continue its skin/preferences traversal for
        // minutes. The R2 deadline bounds unfinished R2 work, not those later
        // independent UI stages; logout still validates retirement separately.
        if (step == completedStep)
        {
            report["r2_contracts"] = evidence;
            return true;
        }
        report["r2_progress"] = evidence;
        if (logged != step)
        {
            logged = step;
            LL_INFOS("NativeR2Replay") << "Production replay step " << step << LL_ENDL;
        }
        require(session.phase() == VSNativeSession::Phase::Connected, "R2 connected owner was lost");
        auto& world  = gViewerWindow->nativeContext()->world();
        auto* avatar = world.selfAvatar();
        require(avatar && session.appearance(), "R2 actual appearance owner is missing");
        if (!step)
        {
            account   = session.account();
            deadline  = LLFrameTimer::getTotalSeconds() + 180;
            directory = std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY")) / "r2";
            std::filesystem::create_directories(directory);
            fixtures(*avatar, account.agent);
            gMessageSystem->setHandlerFunc("AgentAnimation", animationRequest, reinterpret_cast<void**>(this));
            avatarPacket(session);
            for (const auto folder : { cof, bodyFolder, clothingFolder, objectsFolder })
                if (auto* category = gInventory.getCategory(folder))
                {
                    category->setVersion(LLViewerInventoryCategory::VERSION_UNKNOWN);
                    category->setDescendentCount(-1);
                    category->setFetching(LLViewerInventoryCategory::FETCH_NONE);
                    LLInventoryModelBackgroundFetch::instance().start(folder, true);
                }
            ++step;
            return false;
        }
        require(session.current(account), "R2 changed account during appearance replay");
        sendAnimations(session);
        legacyResponses(session);
        const auto now = LLFrameTimer::getTotalSeconds();
        if (now >= nextProgress)
        {
            nextProgress           = now + 1;
            LLSD progress          = evidence;
            progress["step"]       = S32(step);
            progress["appearance"] = session.appearance()->evidence();
            progress["avatar"]     = avatar->evidence();
            progress["images"]     = gViewerWindow->nativeContext()->resources().evidence();
            std::ofstream output(directory / "progress.xml");
            LLSDSerialize::toPrettyXML(progress, output);
            require(output.good(), "R2 progress evidence write failed");
            report["r2_progress"] = progress;
        }
        require(LLFrameTimer::getTotalSeconds() < deadline, "R2 production replay timed out");
        if (step == 1)
        {
            if (avatar->evidence()["wearables"].asInteger() != 4 || avatar->localBakes().empty())
                return false;
            // Local bakes may finish before the asynchronous inventory fetches.
            // The normal wear route requires a complete COF and source items.
            for (const auto folder : { cof, bodyFolder, clothingFolder, objectsFolder })
                if (!gInventory.isCategoryComplete(folder))
                    return false;
            require(session.appearance()->canWear({ item(6) }, true), "R2 initial shape replacement was not admitted");
            initialSize                    = avatar->appearanceOwner().mBodySize;
            evidence["initial_appearance"] = avatar->evidence();
            evidence["initial_body_size"]  = ll_sd_from_vector3(initialSize);
            LLAppearanceMgr::instance().wearItemOnAvatar(item(6), true, true);
            ++step;
            return false;
        }
        if (step == 2)
        {
            if (!session.appearance()->isWorn(item(6)) || session.appearance()->isWorn(item(1)))
                return false;
            const auto size = avatar->appearanceOwner().mBodySize;
            require(std::abs(size.mV[2] - initialSize.mV[2]) > .01f, "R2 shape action did not change the actual avatar geometry");
            evidence["replaced_body_size"] = ll_sd_from_vector3(size);
            require(LLAppearanceMgr::instance().canAddWearables({ item(5) }, false), "R2 normal native admission route failed");
            LLAppearanceMgr::instance().wearItemOnAvatar(item(5), true, false);
            ++step;
            return false;
        }
        if (step == 3)
        {
            if (!session.appearance()->isWorn(item(5)))
                return false;
            LLAppearanceMgr::instance().wearItemOnAvatar(item(7), true, false);
            LLAppearanceMgr::instance().wearItemOnAvatar(item(10), true, false);
            LLAppearanceMgr::instance().wearItemOnAvatar(item(11), true, false);
            ++step;
            return false;
        }
        if (step == 4)
        {
            const auto worn = vs_avatar_wearable_items(unsigned(LLWearableType::WT_SHIRT));
            if (worn.size() != 2 || !session.appearance()->isWorn(item(10)) || !session.appearance()->isWorn(item(11)))
                return false;
            auto links = LLAppearanceMgr::instance().findCOFItemLinks(item(7));
            require(!links.empty(), "R2 real COF link is missing");
            require(LLAppearanceMgr::instance().moveWearable(links.front(), true), "R2 ordinary layer ordering action failed");
            ++step;
            return false;
        }
        if (step == 5)
        {
            const auto worn = vs_avatar_wearable_items(unsigned(LLWearableType::WT_SHIRT));
            if (worn.size() != 2 || worn.front() != item(7))
                return false;
            evidence["layer_order_changed"] = true;
            LLAppearanceMgr::instance().removeItemFromAvatar(item(5));
            ++step;
            return false;
        }
        if (step == 6)
        {
            if (session.appearance()->isWorn(item(5)))
                return false;
            evidence["ordinary_inventory_actions"] = true;
            session.appearance()->refresh();
            ++step;
            return false;
        }
        if (step == 7)
        {
            const auto appearance = session.appearance()->evidence();
            const auto bakes      = avatar->localBakes();
            if (bakes.empty() || appearance["uploaded_bakes"].asInteger() != S32(bakes.size()) ||
                appearance["pending_bake_uploads"].asInteger() != 0 || ll_U64_from_sd(appearance["appearance_sent_revision"]) == 0)
                return false;
            require(appearance["error"].asString().empty(), "R2 local bake publication reported a failure");
            evidence["bake_capability_upload"] = true;
            evidence["uploaded_appearance"]    = appearance;
            const auto size                    = avatar->appearanceOwner().mBodySize;
            require(session.resetAvatar(account.agent, false), "R2 connected avatar reset failed");
            require((size - avatar->appearanceOwner().mBodySize).lengthSquared() < 1.e-6f, "R2 reset lost the worn avatar shape");
            evidence["avatar_reset"] = true;
            ++step;
            return false;
        }
        if (step == 8)
        {
            evidence["body_lod_publications"] = avatar->qualifyBodyLODs(session.position() + LLVector3(0, -4, 1));
            initialPose    = body(session);
            auto* shoulder = avatar->character().getJoint("mShoulderRight");
            require(shoulder, "R2 avatar shoulder joint is missing");
            initialShoulder = shoulder->getRotation();
            require(session.requestAnimations({ animationID }, true), "R2 normal connected animation request failed");
            ++step;
            return false;
        }
        if (step == 9)
        {
            auto* motion = dynamic_cast<LLKeyframeMotion*>(avatar->character().findMotion(animationID));
            if (!motion || !motion->isLoaded() || !avatar->character().isMotionActive(animationID))
                return false;
            const auto rotation = avatar->character().getJoint("mShoulderRight")->getRotation();
            float      dot      = 0;
            for (unsigned axis = 0; axis < 4; ++axis)
                dot += initialShoulder.mQ[axis] * rotation.mQ[axis];
            if (std::abs(dot) > .999f)
                return false;
            const auto current = body(session);
            bool       changed = false;
            for (const auto& packet : current)
            {
                const auto before = std::find_if(initialPose.begin(), initialPose.end(),
                                                 [&](const auto& old) { return old.faceIndex == packet.faceIndex; });
                if (before == initialPose.end())
                    continue;
                const auto& a = VSWorldRenderer::geometryVertices(before->mesh);
                const auto& b = VSWorldRenderer::geometryVertices(packet.mesh);
                require(a.size() == b.size(), "R2 animation changed stable body topology");
                for (unsigned vertex = 0; vertex < a.size(); ++vertex)
                    changed |=
                        std::abs(a[vertex].x - b[vertex].x) + std::abs(a[vertex].y - b[vertex].y) + std::abs(a[vertex].z - b[vertex].z) >
                        1.e-4f;
            }
            require(changed, "R2 loaded animation did not change visible admitted body vertices");
            evidence["animated_geometry"]      = true;
            evidence["animated_selection"]     = true;
            evidence["animation_asset_loaded"] = animationID;
            require(session.requestAnimations({ animationID }, false), "R2 animation stop request failed");
            initialPose.clear();
            ++step;
            return false;
        }
        if (step == 10)
        {
            avatarPacket(session, true);
            ++step;
            return false;
        }
        if (step == 11)
        {
            const auto object = world.object(geometryID);
            if (!object)
                return false;
            const auto filename = topologyCase < 7 ? "topology-" + std::to_string(topologyCase) + ".gltf"
                                                   : "prepared-" + std::to_string(topologyCase - 2) + ".gltf";
            require(world.loadGLTF(*object, (directory / filename).string()), "R2 normal local glTF scene load failed");
            ++step;
            return false;
        }
        if (step == 12)
        {
            const auto packets = geometry(session);
            if (packets.size() != 1 || packets.front().gltfNode != 1)
                return false;
            const auto&                                mesh     = packets.front().mesh;
            const auto&                                vertices = VSWorldRenderer::geometryVertices(mesh);
            const auto&                                indices  = VSWorldRenderer::geometryIndices(mesh);
            const std::array<std::vector<unsigned>, 7> expected{ { { 0, 1, 2, 3 },
                                                                   { 0, 1, 2, 3 },
                                                                   { 0, 1, 1, 2, 2, 3, 3, 0 },
                                                                   { 0, 1, 1, 2, 2, 3 },
                                                                   { 0, 1, 2 },
                                                                   { 0, 1, 2, 2, 1, 3 },
                                                                   { 0, 1, 2, 0, 2, 3 } } };
            const unsigned                             mode = topologyCase < 7 ? topologyCase : topologyCase - 2;
            if (topologyCase < 7)
            {
                require(indices == expected[mode], "R2 glTF topology oracle differs from published indices");
                require(vertices.size() == (mode == 4 ? 3 : 4), "R2 glTF vertex count differs from accessor");
            }
            require(indices.size() == expected[mode].size(), "R2 prepared glTF changed primitive count");
            const std::array<LLVector3, 4> points{ { { 0, 0, 0 }, { 1, 0, 0 }, { 1, 1, 0 }, { 0, 1, 0 } } };
            for (unsigned index = 0; index < indices.size(); ++index)
            {
                require(indices[index] < vertices.size(), "R2 glTF prepared index exceeds admitted vertices");
                if (topologyCase >= 7)
                    continue;
                const auto  expectedPoint = session.position() + LLVector3(2, 3, 4) + points[expected[mode][index]];
                const auto& vertex        = vertices[indices[index]];
                require((expectedPoint - LLVector3(vertex.x, vertex.y, vertex.z)).lengthSquared() < 1.e-8f,
                        "R2 glTF hierarchy/transform oracle differs from published vertices");
            }
            if (topologyCase >= 7)
                for (unsigned triangle = 0; triangle < indices.size(); triangle += 3)
                {
                    bool matched = false;
                    // Index deduplication may rotate the starting vertex. A
                    // cyclic permutation preserves winding; reversal does not.
                    for (unsigned rotation = 0; rotation < 3; ++rotation)
                    {
                        bool candidate = true;
                        for (unsigned corner = 0; corner < 3; ++corner)
                        {
                            const auto expectedPoint =
                                session.position() + LLVector3(2, 3, 4) + points[expected[mode][triangle + (corner + rotation) % 3]];
                            const auto& vertex = vertices[indices[triangle + corner]];
                            candidate &= (expectedPoint - LLVector3(vertex.x, vertex.y, vertex.z)).lengthSquared() < 1.e-8f;
                        }
                        matched |= candidate;
                    }
                    require(matched, "R2 prepared glTF changed triangle position or winding");
                }
            const auto buffers = VSWorldRenderer::geometryBuffers(mesh);
            require(buffers[0] && buffers[1], "R2 glTF topology has no actual GPU buffers");
            gltfBounds(*world.object(geometryID), mesh);
            evidence["gltf_topology_cases"].append(S32(topologyCase));
            if (++topologyCase < 9)
            {
                step = 11;
                return false;
            }
            evidence["gltf_topologies"] = true;
            initialGLTF                 = mesh;
            require(world.loadGLTF(*world.object(geometryID), (directory / "animated.gltf").string()),
                    "R2 animated glTF scene load failed");
            ++step;
            return false;
        }
        if (step == 13)
        {
            const auto packets = geometry(session);
            if (packets.size() != 1 || packets.front().mesh == initialGLTF)
                return false;
            const auto& before = VSWorldRenderer::geometryVertices(initialGLTF);
            const auto& after  = VSWorldRenderer::geometryVertices(packets.front().mesh);
            if (std::abs(before.front().z - after.front().z) < .01f)
                return false;
            require(before.size() == after.size(), "R2 glTF animation changed stable topology");
            gltfBounds(*world.object(geometryID), packets.front().mesh);
            if (!gltfAnimationCase++)
            {
                evidence["gltf_trs_animation"] = true;
                initialGLTF                    = packets.front().mesh;
                require(world.loadGLTF(*world.object(geometryID), (directory / "skinned.gltf").string()),
                        "R2 skinned glTF scene load failed");
                return false;
            }
            evidence["gltf_skin_animation"] = true;
            evidence["gltf_animation"]      = true;
            require(!world.loadGLTF(*world.object(geometryID), (directory / "invalid.gltf").string()),
                    "R2 invalid glTF scene was accepted");
            // A failed candidate leaves the admitted animated provider current.
            require(!geometry(session).empty(), "R2 rejected glTF candidate erased the admitted generation");
            initialGLTF.reset();
            retiredGLTF = packets.front().mesh;
            require(world.loadGLTF(*world.object(geometryID), (directory / "empty.gltf").string()),
                    "R2 valid empty glTF replacement was rejected");
            ++step;
            return false;
        }
        if (step == 14)
        {
            if (!geometry(session).empty())
                return false;
            require(!world.selectionBounds(*world.object(geometryID)), "R2 removed glTF geometry retained selectable bounds");
            if (!retiredGLTF.expired())
                return false;
            evidence["gltf_empty_replacement"] = true;
            evidence["geometry_replacement"]   = true;
            ++step;
            return false;
        }
        if (step == 15)
        {
            retiredObject = world.object(geometryID);
            require(retiredObject.has_value(), "R2 glTF owner disappeared before explicit retirement");
            killGeometry(session);
            for (const auto& point : avatar->attachmentPoints())
                if (point.hud)
                    hudPoints.push_back(point.index);
            require(hudPoints.size() == 8, "R2 shipped avatar lacks the eight reference HUD attachment points");
            ++step;
            return false;
        }
        if (step == 16)
        {
            if (world.object(geometryID))
                return false;
            require(!world.current(*retiredObject), "R2 killed object retained a current scene handle");
            avatarPacket(session, true, 980, U8(hudPoints[hudCase]));
            ++step;
            return false;
        }
        if (step == 17)
        {
            const auto object = world.object(geometryID);
            if (!object)
                return false;
            const auto packets = geometry(session);
            if (packets.empty())
                return false;
            require(world.isAttachment(geometryID), "R2 simulator attachment lost its avatar ancestry");
            for (const auto& packet : packets)
                require(packet.hud && VSWorldRenderer::geometryBuffers(packet.mesh)[0],
                        "R2 HUD attachment was published as ordinary world geometry");
            const auto bounds = world.selectionBounds(*object);
            require(bounds.has_value(), "R2 HUD attachment selection bounds are absent");
            const float horizontal = ((*bounds)[0][1] + (*bounds)[1][1]) * .5f;
            const float vertical   = ((*bounds)[0][2] + (*bounds)[1][2]) * .5f;
            for (float aspect : { 4.f / 3.f, 16.f / 9.f })
                for (float zoom : { 1.f, .75f })
                {
                    const auto pick = world.pickHUD(-2 * horizontal * zoom / aspect, 2 * vertical * zoom, aspect, zoom);
                    require(pick && pick->hud && pick->face.object.id == geometryID,
                            "R2 HUD projection/zoom pick missed admitted attachment triangles");
                }
            evidence["hud_attachment_points"].append(S32(hudPoints[hudCase]));
            retiredObject = object;
            killGeometry(session);
            if (++hudCase < hudPoints.size())
            {
                step = 16;
                return false;
            }
            evidence["hud_attachments"] = true;
            ++step;
            return false;
        }
        if (step == 18)
        {
            if (world.object(geometryID))
                return false;
            if (!gInventory.getItem(item(12)))
                return false;
            if (!attachmentQueued)
            {
                gMessageSystem->setHandlerFunc("RezMultipleAttachmentsFromInv", attachmentRequest, reinterpret_cast<void**>(this));
                gMessageSystem->setHandlerFunc("DetachAttachmentIntoInv", detachRequest, reinterpret_cast<void**>(this));
                LLAttachmentsMgr::instance().addAttachmentRequest(item(12), 1, true);
                attachmentQueued = true;
                return false;
            }
            if (!attachmentRezSeen)
                return false;
            originalShoulderPosition = avatar->character().getJoint("mShoulderRight")->getPosition();
            riggedFixture(*avatar);
            avatarPacket(session, true, 980, 1, true, false, false, item(12));
            ++step;
            return false;
        }
        if (step == 19)
        {
            if (!session.appearance()->isWorn(item(12)) || session.appearance()->evidence()["pending_attachment_operations"].asInteger() ||
                LLAppearanceMgr::instance().findCOFItemLinks(item(12)).empty())
                return false;
            const auto packets = geometry(session);
            if (packets.empty())
                return false;
            require(packets.size() == 1 && VSWorldRenderer::geometryVertices(packets.front().mesh).size() == 3,
                    "R2 rigged asset did not replace primitive geometry");
            LLVector3 position;
            LLUUID    owner;
            require(avatar->character().getJoint("mShoulderRight")->hasAttachmentPosOverride(position, owner) && owner == riggedAssetID,
                    "R2 fitted attachment did not acquire its real joint override");
            initialGLTF = packets.front().mesh;
            evidence["ordinary_attachment_rez"] = true;
            gltfBounds(*world.object(geometryID), initialGLTF);
            require(session.requestAnimations({ animationID }, true), "R2 rigged animation request failed");
            ++step;
            return false;
        }
        if (step == 20 || step == 23)
        {
            auto* animated = step == 20 ? avatar : world.avatarForObject(geometryID);
            require(animated, "R2 animated mesh has no actual skeleton owner");
            auto* motion = dynamic_cast<LLKeyframeMotion*>(animated->character().findMotion(animationID));
            if (!motion || !motion->isLoaded() || !animated->character().isMotionActive(animationID))
                return false;
            const auto packets = geometry(session);
            if (packets.size() != 1)
                return false;
            const auto& before = VSWorldRenderer::geometryVertices(initialGLTF);
            const auto& after  = VSWorldRenderer::geometryVertices(packets.front().mesh);
            require(before.size() == after.size(), "R2 rigged animation changed topology");
            bool changed = false;
            for (unsigned index = 0; index < before.size(); ++index)
                changed |= std::abs(before[index].x - after[index].x) + std::abs(before[index].y - after[index].y) +
                               std::abs(before[index].z - after[index].z) >
                           .01f;
            if (!changed)
                return false;
            if (step == 20)
            {
                if (!detachQueued)
                {
                    LLAppearanceMgr::instance().removeItemFromAvatar(item(12));
                    detachQueued = true;
                    return false;
                }
                if (!detachSeen)
                    return false;
            }
            gltfBounds(*world.object(geometryID), packets.front().mesh);
            evidence[step == 20 ? "rigged_attachments" : "animesh"] = true;
            if (step == 20)
                require(session.requestAnimations({ animationID }, false), "R2 rigged animation stop failed");
            retiredObject = world.object(geometryID);
            initialGLTF.reset();
            retiredGLTF = packets.front().mesh;
            killGeometry(session);
            ++step;
            return false;
        }
        if (step == 21)
        {
            if (world.object(geometryID) || !retiredGLTF.expired())
                return false;
            if (session.appearance()->isWorn(item(12)) || session.appearance()->evidence()["pending_attachment_operations"].asInteger() ||
                !LLAppearanceMgr::instance().findCOFItemLinks(item(12)).empty())
                return false;
            evidence["ordinary_attachment_detach"] = true;
            require(!world.current(*retiredObject), "R2 detached rigged object remained current");
            LLVector3 position;
            LLUUID    owner;
            require(!avatar->character().getJoint("mShoulderRight")->hasAttachmentPosOverride(position, owner),
                    "R2 detach retained a fitted joint override");
            require((avatar->character().getJoint("mShoulderRight")->getPosition() - originalShoulderPosition).lengthSquared() < 1.e-6f,
                    "R2 fitted detach did not restore the original joint position");
            evidence["fitted_detach_restoration"] = true;
            avatarPacket(session, true, 0, 0, true, true);
            ++step;
            return false;
        }
        if (step == 22)
        {
            const auto packets = geometry(session);
            auto*      control = world.avatarForObject(geometryID);
            if (packets.empty() || !control)
                return false;
            require(control != avatar && control->evidence()["control_avatar"].asBoolean(),
                    "R2 animesh borrowed the self avatar instead of its independent skeleton");
            require(packets.size() == 1 && VSWorldRenderer::geometryVertices(packets.front().mesh).size() == 3,
                    "R2 animesh did not publish its real rigged asset");
            initialGLTF = packets.front().mesh;
            objectAnimation(session);
            ++step;
            return false;
        }
        if (step == 24)
        {
            if (world.object(geometryID) || !retiredGLTF.expired())
                return false;
            originalLegacySetting = gSavedSettings.getBOOL("UploadBakedTexOld");
            legacyEnabled         = true;
            gMessageSystem->setHandlerFunc("AssetUploadRequest", legacyRequest, reinterpret_cast<void**>(this));
            gMessageSystem->setHandlerFunc("SendXferPacket", legacyPacket, reinterpret_cast<void**>(this));
            gSavedSettings.setBOOL("UploadBakedTexOld", true);
            LLAppearanceMgr::instance().wearItemOnAvatar(item(5), true, false);
            ++step;
            return false;
        }
        if (step == 25)
        {
            if (!session.appearance()->isWorn(item(5)) || legacyUploads.empty())
                return false;
            const auto state = session.appearance()->evidence();
            if (state["pending_bake_uploads"].asInteger() || state["uploaded_bakes"].asInteger() != S32(avatar->localBakes().size()))
                return false;
            require(state["error"].asString().empty(), "R2 legacy bake production completion reported failure");
            for (const auto& [id, upload] : legacyUploads)
                require(upload.reply && upload.complete && upload.nextPacket > 0,
                        "R2 legacy bake did not traverse the actual xfer protocol");
            evidence["bake_legacy_upload"] = true;
            gSavedSettings.setBOOL("UploadBakedTexOld", originalLegacySetting);
            ++step;
            return false;
        }
        if (step == 26)
        {
            std::vector<VSWorldAvatar::LocalBake> snapshots;
            for (const auto& bake : avatar->localBakes())
                snapshots.push_back(avatar->localBake(bake.index));
            require(snapshots.size() == 11, "R2 composite readback lacks all body bake publications including the worn skirt");
            vs_r2_capture_bakes(*gViewerWindow->nativeContext(), std::move(snapshots), directory.string(), bakeReadback);
            ++step;
            return false;
        }
        if (step == 27)
        {
            if (!*bakeReadback)
                return false;
            gViewerWindow->nativeContext()->setFrameObserver({});
            evidence["bake_gpu_readbacks"] = true;
            ++step;
            return false;
        }
        if (step == 28)
        {
            const auto uploads = session.appearance()->evidence()["bake_uploads"];
            require(uploads.isArray() && uploads.size() == 11, "R2 codec replay lacks complete bake upload identities");
            const auto& row      = uploads[codecCase];
            const auto  snapshot = avatar->localBake(row["index"].asInteger());
            require(snapshot.color && snapshot.generation == ll_U64_from_sd(row["generation"]),
                    "R2 encoded bake does not match the current compositor generation");
            const auto asset    = row["asset"].asUUID();
            auto       filename = directory / (asset.asString() + ".legacy.j2c");
            if (!std::filesystem::exists(filename))
                filename = directory / (asset.asString() + ".j2c");
            LLPointer<LLImageJ2C> encoded = new LLImageJ2C;
            require(encoded->load(filename.string()) && encoded->getComponents() == 5, "R2 actual wire bake lacks RGBHM codec content");
            encoded->setDiscardLevel(0);
            LLPointer<LLImageRaw> color = new LLImageRaw, mask = new LLImageRaw;
            require(encoded->decodeChannels(color, 0, 0, 4) && color->getComponents() == 4 &&
                        color->getWidth() == snapshot.color->getWidth() && color->getHeight() == snapshot.color->getHeight(),
                    "R2 uploaded bake color decode differs from the compositor extent");
            require(encoded->decodeChannels(mask, 0, 4, 1) && mask->getComponents() == 1 && mask->getWidth() == color->getWidth() &&
                        mask->getHeight() == color->getHeight(),
                    "R2 uploaded bake morph mask is missing");
            unsigned   maximum = 0;
            U64        total   = 0;
            const auto pixels  = unsigned(color->getWidth() * color->getHeight());
            for (unsigned pixel = 0; pixel < pixels; ++pixel)
            {
                for (unsigned channel = 0; channel < 4; ++channel)
                {
                    const auto difference = unsigned(
                        std::abs(int(color->getData()[pixel * 4 + channel]) - int(snapshot.color->getData()[pixel * 4 + channel])));
                    maximum = std::max(maximum, difference);
                    total += difference;
                }
                const auto expected   = snapshot.mask ? snapshot.mask->getData()[pixel] : 255;
                const auto difference = unsigned(std::abs(int(mask->getData()[pixel]) - int(expected)));
                maximum               = std::max(maximum, difference);
                total += difference;
            }
            LLSD comparison;
            comparison["index"]                      = row["index"];
            comparison["asset"]                      = asset;
            comparison["maximum_error"]              = S32(maximum);
            comparison["mean_error"]                 = double(total) / (pixels * 5);
            comparison["maximum_allowed_error"]      = 16;
            comparison["maximum_allowed_mean_error"] = 2;
            evidence["bake_codec_comparisons"].append(comparison);
            require(maximum <= 16 && double(total) / (pixels * 5) <= 2,
                    "R2 lossy wire bake exceeds the fixed color/morph-mask pixel tolerance");
            if (++codecCase < uploads.size())
                return false;
            evidence["bake_pixels"] = true;
            ++step;
            return false;
        }
        if (step == 29)
        {
            if (!gInventory.getItem(item(9)))
                return false;
            LLGestureMgr::instance().activateGesture(item(9));
            ++step;
            return false;
        }
        if (step == 30)
        {
            const auto& active = LLGestureMgr::instance().getActiveGestures();
            const auto  found  = active.find(item(9));
            if (found == active.end() || !found->second)
                return false;
            require(found->second->mSteps.size() == 3, "R2 real gesture asset lost its sequence");
            LLGestureMgr::instance().playGesture(item(9));
            ++step;
            return false;
        }
        if (step == 31)
        {
            if (!serverAnimations.count(animationID) || !avatar->character().isMotionActive(animationID))
                return false;
            require(LLGestureMgr::instance().isGesturePlaying(item(9)), "R2 gesture did not run through the ordinary gesture manager");
            LLGestureMgr::instance().stopGesture(item(9));
            LLGestureMgr::instance().deactivateGesture(item(9));
            evidence["gesture_animation_transport"] = true;
            ++step;
            return false;
        }
        if (step == 32)
        {
            if (AOEngine::instance().getAOFolder().isNull())
                return false;
            AOEngine::instance().addSet("R2 production replay", [](const LLUUID&) {});
            ++step;
            return false;
        }
        if (step == 33)
        {
            auto* set = AOEngine::instance().getSetByName("R2 production replay");
            if (!set || !set->getComplete() || !gInventory.getItem(item(8)))
                return false;
            AOEngine::instance().addAnimation(set, set->getState(AOSet::Standing), gInventory.getItem(item(8)));
            ++step;
            return false;
        }
        if (step == 34)
        {
            auto* set = AOEngine::instance().getSetByName("R2 production replay");
            if (!set || !set->getComplete())
                return false;
            auto* state = set->getState(AOSet::Standing);
            if (state->mAnimations.size() != 1 || !state->mAddQueue.empty() || state->mInventoryUUID.isNull())
                return false;
            require(state->mAnimations.front().mAssetUUID == animationID, "R2 AO inventory link lost its actual animation asset");
            LLInventoryModel::cat_array_t  categories;
            LLInventoryModel::item_array_t links;
            gInventory.collectDescendents(state->mInventoryUUID, categories, links, LLInventoryModel::EXCLUDE_TRASH);
            const auto linked = std::find_if(links.begin(), links.end(), [](const auto& link) { return link->getLinkedUUID() == item(8); });
            // addAnimation exposes an optimistic state row before its ordinary
            // asynchronous inventory callback publishes the real link.
            if (linked == links.end())
                return false;
            require((*linked)->getAssetUUID() == animationID, "R2 AO inventory link resolved to a different animation asset");
            AOEngine::instance().selectSet(set);
            AOEngine::instance().enable(true);
            require(session.requestAnimations({ ANIM_AGENT_STAND }, true), "R2 ordinary stand animation request failed");
            ++step;
            return false;
        }
        if (step == 35)
        {
            if (!serverAnimations.count(animationID) || !avatar->character().isMotionActive(animationID))
                return false;
            const auto* set = AOEngine::instance().getCurrentSet();
            require(set && set->getName() == "R2 production replay" &&
                        AOEngine::instance().getCurrentState()->mCurrentAnimationID == animationID,
                    "R2 standing animation did not use the actual AO state override");
            AOEngine::instance().enable(false);
            require(session.requestAnimations({ ANIM_AGENT_STAND }, false), "R2 AO stand stop failed");
            evidence["ao_inventory_override"] = true;
            ++step;
            return false;
        }
        if (step == 36)
        {
            require(!gAgent.anyControlGrabbed(), "R2 script-control fixture found unowned pre-existing controls");
            gMessageSystem->setHandlerFunc("ForceScriptControlRelease", controlRelease, reinterpret_cast<void**>(this));
            LLMessageSystem* message = gMessageSystem;
            message->newMessage("ScriptControlChange");
            message->nextBlock("Data");
            message->addBOOL("TakeControls", true);
            message->addU32("Controls", 1);
            message->addBOOL("PassToAgent", false);
            require(message->sendReliable(session.host()) > 0, "R2 simulator script-control request failed");
            ++step;
            return false;
        }
        if (step == 37)
        {
            if (!gAgent.isControlGrabbed(0))
                return false;
            gAgent.forceReleaseControls();
            require(!gAgent.anyControlGrabbed(), "R2 ordinary release action did not clear native control ownership");
            ++step;
            return false;
        }
        if (step == 38)
        {
            if (!controlsReleased)
                return false;
            evidence["script_control_transport"] = true;
            evidence["motion_gesture_ao"] =
                evidence["gesture_animation_transport"].asBoolean() && evidence["ao_inventory_override"].asBoolean();
            for (const auto& packet : body(session))
                logoutMeshes.push_back(packet.mesh);
            for (const auto& bake : avatar->localBakes())
                logoutImages.push_back(avatar->localBake(bake.index).image);
            retiredAccountGuard     = session.accountGuard();
            evidence["final_scene"] = world.evidence();
            ++step;
            return false;
        }
        if (step == 39)
        {
            savedFriendsOnly          = gSavedSettings.getBOOL("RenderAvatarFriendsOnly");
            savedComplexity           = gSavedSettings.getU32("RenderAvatarMaxComplexity");
            savedComplexityMode       = gSavedSettings.getS32("RenderAvatarComplexityMode");
            savedNewWalk              = gSavedSettings.getBOOL("UseNewWalkRun");
            savedPhysics              = gSavedSettings.getBOOL("AvatarPhysics");
            savedPhysicsLOD           = gSavedSettings.getF32("RenderAvatarPhysicsLODFactor");
            visibilitySettingsChanged = true;
            gSavedSettings.setBOOL("RenderAvatarFriendsOnly", false);
            gSavedSettings.setU32("RenderAvatarMaxComplexity", 0);
            gSavedSettings.setS32("RenderAvatarComplexityMode", 0);
            avatarPacket(session, false, 0, 0, false, false, true);
            remoteAppearance(session, *avatar);
            ++step;
            return false;
        }
        if (step == 40)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            if (!remote || !remote->evidence()["appearance_known"].asBoolean())
                return false;
            auto       packets = world.packets(session.position() + LLVector3(0, -4, 1), 128, 480, 640);
            const auto visible =
                std::count_if(packets.begin(), packets.end(), [](const auto& packet)
                              { return std::equal(packet.objectIdentity.begin(), packet.objectIdentity.end(), remoteAvatarID.mData); });
            if (!visible)
                return false;
            auto current = body(session, &remoteAvatarID);
            if (current.size() != MESH_ID_NUM_INDICES)
                return false;
            require(std::all_of(current.begin(), current.end(), [](const auto& packet) { return bool(packet.image); }),
                    "R2 remote avatar did not use decoded server bakes");
            evidence["remote_body_parts"] = LLSD::emptyArray();
            for (const auto& packet : current)
                evidence["remote_body_parts"].append(S32(packet.faceIndex));
            initialPose    = std::move(current);
            remoteHeight   = remote->appearanceOwner().getVisualParamWeight(33);
            remoteShoulder = remote->character().getJoint("mShoulderRight")->getRotation();
            remoteAnimation(session);
            ++step;
            return false;
        }
        if (step == 41)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 remote avatar retired before animation qualification");
            auto* motion = dynamic_cast<LLKeyframeMotion*>(remote->character().findMotion(animationID));
            if (!motion || !motion->isLoaded() || !remote->character().isMotionActive(animationID))
                return false;
            const auto rotation = remote->character().getJoint("mShoulderRight")->getRotation();
            F32        dot      = 0;
            for (unsigned axis = 0; axis < 4; ++axis)
                dot += remoteShoulder.mQ[axis] * rotation.mQ[axis];
            if (std::abs(dot) > .999f)
                return false;
            const auto current = body(session, &remoteAvatarID);
            bool       changed = false;
            for (const auto& packet : current)
            {
                const auto before = std::find_if(initialPose.begin(), initialPose.end(),
                                                 [&](const auto& old) { return old.faceIndex == packet.faceIndex; });
                require(before != initialPose.end(), "R2 remote animation lost a received body branch");
                const auto& previous = VSWorldRenderer::geometryVertices(before->mesh);
                const auto& next     = VSWorldRenderer::geometryVertices(packet.mesh);
                require(previous.size() == next.size(), "R2 remote animation changed stable body topology");
                for (unsigned vertex = 0; vertex < next.size(); ++vertex)
                    changed |= std::abs(previous[vertex].x - next[vertex].x) + std::abs(previous[vertex].y - next[vertex].y) +
                                   std::abs(previous[vertex].z - next[vertex].z) >
                               1.e-4f;
            }
            require(changed, "R2 remote animation did not change admitted body vertices");
            initialPose.clear();
            evidence["remote_animation_selection"] = true;
            gSavedSettings.setU32("RenderAvatarMaxComplexity", 1);
            ++step;
            return false;
        }
        if (step == 42)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 remote avatar retired before jellydoll qualification");
            if (remote->visibility() != VSWorldAvatar::Visibility::Jellydoll)
                return false;
            const auto current = body(session, &remoteAvatarID);
            require(std::all_of(current.begin(), current.end(),
                                [](const auto& packet) { return !packet.image && packet.fullbright && !packet.alpha && !packet.masked; }),
                    "R2 jellydoll retained ordinary body materials");
            gSavedSettings.setBOOL("RenderAvatarFriendsOnly", true);
            ++step;
            return false;
        }
        if (step == 43)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 remote avatar retired before invisible qualification");
            if (remote->visibility() != VSWorldAvatar::Visibility::Invisible)
                return false;
            const auto token = world.object(remoteAvatarID);
            require(token && !world.selectionBounds(*token), "R2 invisible remote retained selectable body geometry");
            gSavedSettings.setBOOL("RenderAvatarFriendsOnly", false);
            gSavedSettings.setU32("RenderAvatarMaxComplexity", 0);
            ++step;
            return false;
        }
        if (step == 44)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 remote avatar retired before visibility restoration");
            if (remote->visibility() != VSWorldAvatar::Visibility::Normal)
                return false;
            require(std::abs(remote->appearanceOwner().getVisualParamWeight(33) - remoteHeight) < 1.e-5f,
                    "R2 jellydoll restoration lost the actual received shape");
            if (!remote->character().isMotionActive(animationID))
                return false;
            const auto current = body(session, &remoteAvatarID);
            for (const auto& packet : current)
                logoutMeshes.push_back(packet.mesh);
            evidence["remote_visibility_restoration"] = true;
            ++step;
            return false;
        }
        if (step == 45)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 motion matrix lost its actual remote character");
            if (motionCase < motionCases().size())
            {
                const auto& test                = motionCases()[motionCase];
                evidence["motion_case_pending"] = test.label;
                if (!motionPhase)
                {
                    if (test.label != "male-sit")
                    {
                        remoteAnimation(session, LLUUID::null);
                        motionPhase = 3;
                        return false;
                    }
                    gSavedSettings.setBOOL("UseNewWalkRun", test.newWalk);
                    remoteAppearance(session, *avatar, test.female);
                    motionPhase = 1;
                    return false;
                }
                if (motionPhase == 3)
                {
                    // Retire the old clip under its original saved remap policy.
                    // The male-sit case deliberately changes sex while sitting.
                    const LLUUID previous = motionCase ? motionCases()[motionCase - 1].mapped : animationID;
                    if (!remote->signaledAnimations().empty() || remote->character().isMotionActive(previous))
                        return false;
                    gSavedSettings.setBOOL("UseNewWalkRun", test.newWalk);
                    remoteAppearance(session, *avatar, test.female);
                    motionPhase = 1;
                    return false;
                }
                if (motionPhase == 1)
                {
                    if ((remote->appearanceOwner().getSex() == SEX_FEMALE) != test.female)
                        return false;
                    if (test.label == "male-sit")
                    {
                        require(!remote->character().isMotionActive(ANIM_AGENT_SIT_FEMALE),
                                "R2 sex change retained its previous mapped sit motion");
                        if (!remote->character().isMotionActive(ANIM_AGENT_SIT))
                            return false;
                    }
                    remoteAnimation(session, test.source, S32(motionCase + 2));
                    motionTime  = 0;
                    motionPhase = 2;
                    return false;
                }
                auto* motion = remote->character().findMotion(test.mapped);
                if (!motion || !remote->character().isMotionActive(test.mapped))
                    return false;
                auto* keyframe = dynamic_cast<LLKeyframeMotion*>(motion);
                if (test.kind != "emote" && (!keyframe || !keyframe->isLoaded()))
                    return false;
                if (!motionTime)
                    motionTime = LLFrameTimer::getTotalSeconds();
                if (LLFrameTimer::getTotalSeconds() - motionTime < .12)
                    return false;
                require((test.kind != "stand" || dynamic_cast<LLKeyframeStandMotion*>(motion)) &&
                            (test.kind != "walk" || dynamic_cast<LLKeyframeWalkMotion*>(motion)) &&
                            (test.kind != "fall" || dynamic_cast<LLKeyframeFallMotion*>(motion)) &&
                            (test.kind != "emote" || dynamic_cast<LLEmote*>(motion)),
                        "R2 motion ID did not use its actual registered specialized factory");
                if (test.kind == "emote")
                {
                    auto* param = remote->character().getVisualParam(motion->getName().c_str());
                    require(param && param->getWeight() > param->getMinWeight() + 1.e-5f,
                            "R2 emote did not update its real facial morph parameter");
                }
                require(body(session, &remoteAvatarID).size() == MESH_ID_NUM_INDICES, "R2 specialized motion lost an admitted body branch");
                LLSD row;
                row["label"]    = test.label;
                row["source"]   = test.source;
                row["mapped"]   = test.mapped;
                row["female"]   = test.female;
                row["new_walk"] = test.newWalk;
                row["kind"]     = test.kind;
                evidence["specialized_motion_cases"].append(row);
                ++motionCase;
                motionPhase = 0;
                return false;
            }
            evidence["specialized_motions"] = true;
            ++step;
            return false;
        }
        if (step == 46)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 physics fixture lost its real character");
            gSavedSettings.setBOOL("AvatarPhysics", true);
            gSavedSettings.setF32("RenderAvatarPhysicsLODFactor", 1);
            for (unsigned index = 0; index < physicsWeights.size(); ++index)
                physicsWeights[index] = remote->appearanceOwner().getVisualParamWeight(S32(1202 + index));
            initialPose = body(session, &remoteAvatarID);
            remoteAnimation(session, LLUUID::null);
            avatarPacket(session, false, 0, 0, false, false, true, LLUUID::null, LLVector3(0, 0, 5));
            motionTime = LLFrameTimer::getTotalSeconds();
            ++step;
            return false;
        }
        if (step == 47)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote && remote->character().isMotionActive(ANIM_AGENT_PHYSICS_MOTION), "R2 native physics controller is not active");
            if (LLFrameTimer::getTotalSeconds() - motionTime < .2)
                return false;
            bool changed = false;
            LLSD values;
            for (unsigned index = 0; index < physicsWeights.size(); ++index)
            {
                const F32 value = remote->appearanceOwner().getVisualParamWeight(S32(1202 + index));
                require(std::isfinite(value), "R2 physics produced a nonfinite morph weight");
                values.append(value);
                changed |= std::abs(value - physicsWeights[index]) > 1.e-4f;
            }
            evidence["physics_weights"] = values;
            if (!changed)
                return false;
            body(session, &remoteAvatarID);
            initialPose.clear();
            evidence["physics_morphs"] = true;
            avatarPacket(session, false, 0, 0, false, false, true);
            remoteAnimation(session, ANIM_AGENT_SIT, 100);
            ++step;
            return false;
        }
        if (step == 48)
        {
            avatarPacket(session, false, 900, 0, false, false, true);
            ++step;
            return false;
        }
        if (step == 49)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 seated avatar lost its owner");
            if (!remote->sitting())
                return false;
            const auto expected =
                remote->character().getCharacterPosition() + LLVector3(0, 0, .1f) * remote->character().getCharacterRotation();
            require((remote->appearanceOwner().getRootJoint()->getWorldPosition() - expected).lengthSquared() < 1.e-6f,
                    "R2 object sit did not use its parent-relative hover transform");
            body(session, &remoteAvatarID);
            avatarPacket(session, false, 0, 0, false, false, true);
            remoteAnimation(session, ANIM_AGENT_SIT_GROUND_CONSTRAINED, 101);
            ++step;
            return false;
        }
        if (step == 50)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 ground-sitting avatar lost its owner");
            if (!remote->sitting() || !remote->signaledAnimations().count(ANIM_AGENT_SIT_GROUND_CONSTRAINED))
                return false;
            auto expected = remote->character().getCharacterPosition() + LLVector3(0, 0, .1f);
            expected.mV[VZ] -= .5f * remote->appearanceOwner().mBodySize.mV[VZ] - remote->appearanceOwner().getPelvisToFoot();
            require((remote->appearanceOwner().getRootJoint()->getWorldPosition() - expected).lengthSquared() < 1.e-6f,
                    "R2 ground sit dropped or applied its hover offset twice");
            body(session, &remoteAvatarID);
            remoteAnimation(session, ANIM_AGENT_STAND, 102);
            ++step;
            return false;
        }
        if (step == 51)
        {
            auto* remote = world.avatarForObject(remoteAvatarID);
            require(remote, "R2 standing avatar lost its owner");
            if (remote->sitting() || !remote->character().isMotionActive(ANIM_AGENT_STAND))
                return false;
            for (const auto& packet : body(session, &remoteAvatarID))
                logoutMeshes.push_back(packet.mesh);
            evidence["sitting_ground_hover"] = true;
            killGeometry(session, 982);
            ++step;
            return false;
        }
        if (step == 52)
        {
            if (world.avatarForObject(remoteAvatarID) || world.object(remoteAvatarID))
                return false;
            evidence["remote_avatar_retirement"] = true;
            restoreVisibilitySettings();
            for (const auto& packet : body(session))
                logoutMeshes.push_back(packet.mesh);
            evidence["final_scene"] = world.evidence();
            ++step;
        }
        require(step == completedStep, "R2 replay reached completion without all connected stages");
        report["r2_contracts"] = evidence;
        std::ofstream output(directory / "contracts.xml");
        LLSDSerialize::toPrettyXML(evidence, output);
        require(output.good(), "R2 connected production evidence write failed");
        return true;
    }
    void retired(VSNativeSession& session, LLSD& report)
    {
        auto& context = *gViewerWindow->nativeContext();
        context.wait();
        const auto scene = context.world().evidence();
        require(!session.current(account) && retiredAccountGuard && !retiredAccountGuard(),
                "R2 logout retained a current account/callback owner");
        require(scene["owners"].size() == 0 && scene["neighbours"].size() == 0 && scene["assets"]["requests"].asInteger() == 0 &&
                    ll_U64_from_sd(scene["assets"]["resident_bytes"]) == 0 && scene["owned_object_slots"].asInteger() == 0 &&
                    scene["owned_avatar_slots"].asInteger() == 0,
                "R2 logout retained scene/asset/skeleton owners");
        for (const auto& mesh : logoutMeshes)
            require(mesh.expired(), "R2 logout retained an admitted avatar geometry generation");
        for (const auto& image : logoutImages)
            require(image.expired(), "R2 logout retained an admitted compositor image generation");
        require(!gAgent.anyControlGrabbed(), "R2 logout retained script controls");
        evidence["logout_retirement"] = true;
        evidence["retired_scene"]     = scene;
        report["r2_contracts"]        = evidence;
        std::ofstream output(directory / "contracts.xml");
        LLSDSerialize::toPrettyXML(evidence, output);
        require(output.good(), "R2 complete retirement evidence write failed");
    }
};
std::unique_ptr<Replay> replay;
} // namespace
bool vs_r2_replay_tick(VSNativeSession& session, LLSD& report)
{
    if (!std::getenv("VS_VULKAN_REPLAY_R2"))
        return true;
    if (!replay)
        replay = std::make_unique<Replay>();
    try
    {
        return replay->tick(session, report);
    }
    catch (const std::exception& failure)
    {
        LLSD diagnostic             = replay->evidence;
        diagnostic["step"]          = S32(replay->step);
        diagnostic["topology_case"] = S32(replay->topologyCase);
        diagnostic["failure"]       = failure.what();
        if (!replay->directory.empty())
        {
            std::ofstream output(replay->directory / "failure.xml");
            LLSDSerialize::toPrettyXML(diagnostic, output);
        }
        LL_WARNS("NativeR2Replay") << "R2 production failure at step " << replay->step << ": " << failure.what() << LL_ENDL;
        throw;
    }
}
void vs_r2_replay_cleanup()
{ replay.reset(); }
void vs_r2_replay_retired(VSNativeSession& session, LLSD& report)
{
    if (replay)
    {
        replay->retired(session, report);
        replay.reset();
    }
}
