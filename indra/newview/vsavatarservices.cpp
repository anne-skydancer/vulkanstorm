// Backend-neutral CPU avatar access for ordinary viewer services. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsavatarservices.h"
#include "llagent.h"
#include "llviewercontrol.h"
#include "llvoavatarself.h"
#include "llagentwearables.h"
#include "llinventorymodel.h"
#include "llviewerjointattachment.h"
#include "llviewerobjectlist.h"
#include "llworld.h"
#include <cmath>
#include <set>
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "vsworldavatar.h"
namespace
{
VSWorldAvatar* nativeAvatar()
{
    const auto session = VSNativeSession::active();
    if (!session || session->phase() != VSNativeSession::Phase::Connected || !gViewerWindow || !gViewerWindow->nativeContext())
        return nullptr;
    return gViewerWindow->nativeContext()->world().selfAvatar();
}
} // namespace
#endif
LLCharacter* vs_avatar_character()
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        const auto avatar = nativeAvatar();
        return avatar ? &avatar->character() : nullptr;
    }
#endif
    return isAgentAvatarValid() ? gAgentAvatarp.get() : nullptr;
}
LLAvatarAppearance* vs_avatar_appearance()
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        const auto avatar = nativeAvatar();
        return avatar ? &avatar->appearanceOwner() : nullptr;
    }
#endif
    return isAgentAvatarValid() ? gAgentAvatarp.get() : nullptr;
}
const std::map<LLUUID, S32>& vs_avatar_signaled_animations()
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        static const std::map<LLUUID, S32> empty;
        const auto                         avatar = nativeAvatar();
        return avatar ? avatar->signaledAnimations() : empty;
    }
#endif
    static const std::map<LLUUID, S32> empty;
    return isAgentAvatarValid() ? gAgentAvatarp->mSignaledAnimations : empty;
}
const std::multimap<LLUUID, LLUUID>& vs_avatar_animation_sources()
{
    static const std::multimap<LLUUID, LLUUID> empty;
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        const auto avatar = nativeAvatar();
        return avatar ? avatar->animationSources() : empty;
    }
#endif
    return isAgentAvatarValid() ? gAgentAvatarp->mAnimationSources : empty;
}
LLUUID vs_avatar_root_id()
{
#if VS_NATIVE_VULKAN
    if (const auto session = VSNativeSession::active())
        return gViewerWindow && gViewerWindow->nativeContext() ? gViewerWindow->nativeContext()->world().rootID(session->account().agent)
                                                               : LLUUID::null;
#endif
    if (isAgentAvatarValid())
        if (const auto* root = dynamic_cast<LLViewerObject*>(gAgentAvatarp->getRoot()))
            return root->getID();
    return LLUUID::null;
}
bool vs_avatar_animation_from_seat(const LLUUID& source, const LLUUID& seat)
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
        return gViewerWindow && gViewerWindow->nativeContext() && !gViewerWindow->nativeContext()->world().isAttachment(source) &&
               gViewerWindow->nativeContext()->world().rootID(source) == seat;
#endif
    const auto* object = gObjectList.findObject(source);
    if (!object || object->isAttachment())
        return false;
    const auto* root = dynamic_cast<const LLViewerObject*>(object->getRoot());
    return root && root->getID() == seat;
}
bool vs_avatar_stop_motion(const LLUUID& id, bool immediate)
{
    const auto character = vs_avatar_character();
    return character && character->LLCharacter::stopMotion(id, immediate);
}

std::vector<LLUUID> vs_avatar_wearable_items(unsigned type)
{
    if (type >= unsigned(LLWearableType::WT_COUNT))
        return {};
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        const auto avatar = nativeAvatar();
        return avatar ? avatar->wearableItems(type) : std::vector<LLUUID>{};
    }
#endif
    std::vector<LLUUID> result;
    const auto          wearableType = LLWearableType::EType(type);
    for (U32 index = 0; index < gAgentWearables.getWearableCount(wearableType); ++index)
        result.push_back(gAgentWearables.getWearableItemID(wearableType, index));
    return result;
}

std::vector<std::pair<unsigned, std::string>> vs_avatar_attachment_points()
{
    std::vector<std::pair<unsigned, std::string>> result;
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        if (const auto avatar = nativeAvatar())
            for (const auto& point : avatar->attachmentPoints())
                result.emplace_back(point.index, point.name);
        return result;
    }
#endif
    if (isAgentAvatarValid())
        for (const auto& [index, point] : gAgentAvatarp->mAttachmentPoints)
            if (point)
                result.emplace_back(unsigned(index), point->getName());
    return result;
}

std::vector<VSAvatarAttachment> vs_avatar_attachments()
{
    std::vector<VSAvatarAttachment> result;
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        if (gViewerWindow && gViewerWindow->nativeContext())
            for (const auto& attachment : gViewerWindow->nativeContext()->world().attachments())
                result.push_back({ attachment.point, attachment.object.id, attachment.item });
        return result;
    }
#endif
    if (isAgentAvatarValid())
        for (const auto& [point, slot] : gAgentAvatarp->mAttachmentPoints)
            if (slot)
                for (const auto& object : slot->mAttachedObjects)
                    if (object)
                        result.push_back({ unsigned(point), object->getID(), object->getAttachmentItemID() });
    return result;
}

bool vs_avatar_attachment_name(const LLUUID& item, std::string& name)
{
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
    {
        const auto linked = gInventory.getLinkedItemID(item);
        for (const auto& attachment : vs_avatar_attachments())
            if (attachment.item == linked)
                for (const auto& [point, label] : vs_avatar_attachment_points())
                    if (point == attachment.point)
                    {
                        name = label;
                        return true;
                    }
        name = "Unknown";
        return false;
    }
#endif
    return isAgentAvatarValid() && gAgentAvatarp->getAttachedPointName(item, name);
}

bool vs_avatar_precise_location(const LLUUID& id,LLVector3d& position)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=VSNativeSession::active();
        if(!owner || !owner->regionReady() || !gViewerWindow->nativeContext())return false;
        // Precise object updates refresh tracking timers. Cached coarse samples
        // retain their original freshness timeout in the shared tracking owner.
        for(const auto& avatar:gViewerWindow->nativeContext()->world().avatarLocations())
            if(avatar.id==id){position=avatar.position;return position.isFinite();}
        return false;
    }
#endif
    const auto object=gObjectList.findObject(id);
    if(!object || object->isDead() || !object->isAvatar())return false;
    position=object->getPositionGlobal();return position.isFinite();
}

std::vector<VSAvatarLocation> vs_avatar_locations(const LLVector3d& relativeTo, F32 radius)
{
    std::vector<VSAvatarLocation> result;
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto session = VSNativeSession::active();
        if (!session || !session->regionReady() || !gViewerWindow->nativeContext() || !relativeTo.isFinite() ||
            !std::isfinite(radius) || radius < 0) return result;
        std::vector<VSCoarseSample<LLUUID>> precise;
        for(const auto& avatar:gViewerWindow->nativeContext()->world().avatarLocations())
            precise.push_back({avatar.id,{avatar.position.mdV[0],avatar.position.mdV[1],avatar.position.mdV[2]},true});
        for(const auto& avatar:vs_merge_coarse(precise,session->coarseAvatarLocations(),
            std::array<double,3>{relativeTo.mdV[0],relativeTo.mdV[1],relativeTo.mdV[2]},F64(radius)))
            result.push_back({avatar.id,LLVector3d(avatar.position[0],avatar.position[1],avatar.position[2])});
        return result;
    }
#endif
    uuid_vec_t ids; std::vector<LLVector3d> positions;
    LLWorld::getInstance()->getAvatars(&ids, &positions, relativeTo, radius);
    for (std::size_t i = 0; i < ids.size(); ++i) result.push_back({ids[i], positions[i]});
    return result;
}

bool vs_avatar_nearby_location(const LLUUID& id, LLVector3d& position)
{
    const auto origin = gAgent.getPositionGlobal();
    // Match the ordinary radar's user-selected distance policy. The provider
    // retains coarse altitude and circuit freshness; missing is not position 0.
    const F32 radius = gSavedSettings.getBOOL("LimitRadarByRange")
        ? gSavedSettings.getF32("NearMeRange") : F32_MAX;
    for (const auto& avatar : vs_avatar_locations(origin, radius))
        if (avatar.id == id) { position = avatar.position; return true; }
    return false;
}
