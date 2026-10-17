// Use actual native attachments and the same simulator messages as the GL peer.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsbridgeadapter.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llviewerinventory.h"
#include "llprimitive.h"
#include "llselectmgr.h"
#include "llvolumemessage.h"
#include "lltextureentry.h"
#include "llviewertexture.h"
#include "llagent.h"
#include "llendianswizzle.h"
#include "object_flags.h"
#include "message.h"
#include <algorithm>
#include <array>
#include <utility>
namespace
{
constexpr U8 bridgePoint = 31;
bool sameObject(const VSWorldScene::ObjectHandle& a, const VSWorldScene::ObjectHandle& b)
{
    return a.host == b.host && a.id == b.id && a.local == b.local && a.region == b.region &&
        a.generation == b.generation && a.account == b.account && a.accountGeneration == b.accountGeneration;
}
}
VSBridgeAdapter::VSBridgeAdapter(std::weak_ptr<VSNativeSession> session, Attach attach, Detach detach)
    : mSession(std::move(session)), mAttach(std::move(attach)), mDetach(std::move(detach)) {}
void VSBridgeAdapter::reset() { mAttachments.clear(); }
void VSBridgeAdapter::tick()
{
    auto session = mSession.lock();
    if (!session || !session->regionReady() || !gViewerWindow || !gViewerWindow->nativeContext()) return;
    const auto current = session->accountGuard();
    std::map<LLUUID, VSWorldScene::Attachment> next;
    for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
        if (attachment.item.notNull()) next.try_emplace(attachment.item, attachment);
    auto previous = std::move(mAttachments);
    mAttachments = next;
    // Deliver snapshots, not iterators into controller-owned state: lifecycle
    // callbacks can synchronously recreate the bridge or retire this account.
    const auto attach = mAttach;
    const auto detach = mDetach;
    for (const auto& [item, old] : previous)
        if (!next.count(item) && current() && detach) detach(item, old.point);
    for (const auto& [item, value] : next)
    {
        const auto old = previous.find(item);
        if ((old == previous.end() || !sameObject(old->second.object, value.object) || old->second.point != value.point) &&
            current() && attach) attach(value);
    }
}
std::optional<VSWorldScene::Attachment> VSBridgeAdapter::attachment(const LLUUID& item) const
{
    auto session = mSession.lock();
    if (!session || !session->regionReady() || !gViewerWindow || !gViewerWindow->nativeContext()) return {};
    auto& world = gViewerWindow->nativeContext()->world();
    for (const auto& value : world.attachments())
        if (value.item == item && value.point == bridgePoint && value.object.host == session->host() && world.current(value.object))
            return value;
    return {};
}
std::function<bool()> VSBridgeAdapter::guard(const LLUUID& item) const
{
    auto session = mSession.lock();
    const auto value = attachment(item);
    if (!session || !value) return [] { return false; };
    const auto account = session->accountGuard();
    const U64 region = session->regionEpoch();
    const auto object = gViewerWindow->nativeContext()->world().guard(value->object);
    return [weak=mSession, account, region, object]
    {
        auto session = weak.lock();
        return session && account() && session->regionReady() && session->regionEpoch() == region && object();
    };
}
bool VSBridgeAdapter::inspect(const LLUUID& item, VSTaskInventory::Callback callback)
{
    auto session = mSession.lock();
    const auto value = attachment(item);
    auto* inventory = session ? session->taskInventory() : nullptr;
    return value && inventory && inventory->request(value->object, std::move(callback));
}
bool VSBridgeAdapter::configure(const LLUUID& item)
{
    const auto value = attachment(item);
    auto session = mSession.lock();
    if (!session || !value || !gMessageSystem) return false;
    auto& world = gViewerWindow->nativeContext()->world();
    const auto snapshot = world.objectSnapshot(value->object);
    if (!snapshot || snapshot->pcode != LL_PCODE_VOLUME) return false;
    const auto current = guard(item);
    // Copy the currently published face metadata before sending any mutation.
    // Each shared entry retains material/UV policy; only its image is changed.
    LLPrimitive material;
    std::vector<LLTextureEntry> faces;
    for (U32 index = 0; index < 255; ++index)
    {
        const auto face = world.face(value->object, index);
        if (!face) break;
        LLTextureEntry entry;
        if (!world.copyTextureEntry(*face, entry)) return false;
        entry.setID(IMG_INVISIBLE);
        faces.push_back(entry);
    }
    if (faces.empty()) return false;
    material.setNumTEs(U8(faces.size()));
    for (U8 index = 0; index < faces.size(); ++index) material.setTE(index, faces[index]);
    LLProfileParams profile(LL_PCODE_PROFILE_CIRCLE, .230f, .250f, .95f);
    LLPathParams path(LL_PCODE_PATH_CIRCLE, .2f, .22f, 0.f, 350.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, .05f, .05f);
    path.setRevolutions(1.f);
    LLVolumeParams volume(profile, path);
    auto* message = static_cast<LLMessageSystem*>(gMessageSystem);
    const auto start = [&](const char* name)
    {
        message->newMessage(name);
        message->nextBlock("AgentData");
        message->addUUID("AgentID", gAgentID);
        message->addUUID("SessionID", gAgentSessionID);
    };
    const auto send = [&] { return current() && message->sendReliable(value->object.host) > 0; };
    start("ObjectShape");
    message->nextBlock("ObjectData");
    message->addU32("ObjectLocalID", value->object.local);
    LLVolumeMessage::packVolumeParams(&volume, message);
    if (!send()) return false;
    const LLVector3 position(2.f, 2.f, 2.f), scale(.01f, .01f, .01f);
    std::array<U8, 24> bytes;
    htolememcpy(bytes.data(), position.mV, MVT_LLVector3, 12);
    htolememcpy(bytes.data() + 12, scale.mV, MVT_LLVector3, 12);
    start("MultipleObjectUpdate");
    message->nextBlock("ObjectData");
    message->addU32("ObjectLocalID", value->object.local);
    message->addU8("Type", UPD_POSITION | UPD_SCALE | UPD_LINKED_SETS);
    message->addBinaryData("Data", bytes.data(), S32(bytes.size()));
    if (!send()) return false;
    start("ObjectImage");
    message->nextBlock("ObjectData");
    message->addU32("ObjectLocalID", value->object.local);
    message->addString("MediaURL", snapshot->mediaVersion);
    material.packTEMessage(message);
    if (!send()) return false;
    start("ObjectFlagUpdate");
    message->addU32("ObjectLocalID", value->object.local);
    message->addBOOL("UsePhysics", (snapshot->updateFlags & FLAGS_USE_PHYSICS) != 0);
    message->addBOOL("IsTemporary", true);
    message->addBOOL("IsPhantom", (snapshot->updateFlags & FLAGS_PHANTOM) != 0);
    message->addBOOL("CastsShadows", false); // Same unused protocol value as LLViewerObject::updateFlags.
    return send();
}
bool VSBridgeAdapter::saveScript(const LLUUID& attachmentItem, const LLViewerInventoryItem& script)
{
    const auto value = attachment(attachmentItem);
    if (!value || !gMessageSystem) return false;
    const auto current = guard(attachmentItem);
    if (!current()) return false;
    LLPointer<LLViewerInventoryItem> taskItem = new LLViewerInventoryItem(script.getUUID(), value->object.id,
        script.getPermissions(), script.getAssetUUID(), script.getType(), script.getInventoryType(), script.getName(),
        script.getDescription(), script.getSaleInfo(), script.getFlags(), script.getCreationDate());
    taskItem->setTransactionID(script.getTransactionID());
    auto* message = static_cast<LLMessageSystem*>(gMessageSystem);
    message->newMessage("RezScript");
    message->nextBlock("AgentData");
    message->addUUID("AgentID", gAgentID);
    message->addUUID("SessionID", gAgentSessionID);
    message->addUUID("GroupID", gAgent.getGroupID());
    message->nextBlock("UpdateBlock");
    message->addU32("ObjectLocalID", value->object.local);
    message->addBOOL("Enabled", true);
    message->nextBlock("InventoryBlock");
    taskItem->packMessage(message);
    return current() && message->sendReliable(value->object.host) > 0;
}
