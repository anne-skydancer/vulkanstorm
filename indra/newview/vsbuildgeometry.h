// Reference Geometry controller services over genuine renderer owners. LGPL-2.1.
#pragma once
#include "vsbuildvolume.h"
#include "vsregioncontext.h"
#include "llworld.h"
#include "llagent.h"
#include "llviewerregion.h"
#include "llmanip.h"
#include <type_traits>
#include <utility>

template<class Action>
bool vs_build_complete(Action&& action)
{
    if constexpr (std::is_void_v<std::invoke_result_t<Action>>)
    {
        action();
        return true;
    }
    else
        return bool(action());
}
inline std::vector<LLVector3>* vs_build_saved_children(LLViewerObject* object)
{ return &object->mUnselectedChildrenPositions; }
inline bool vs_build_rebuild(LLViewerObject* object)
{
    LLManip::rebuild(object);
    return true;
}
inline std::optional<VSRegionContext> vs_build_region(LLViewerObject* object)
{ return object && object->getRegion() ? vs_region_context(object->getRegion()->getHost()) : std::nullopt; }
inline F32 vs_build_min_height(LLViewerObject* object, const LLVector3d& point)
{ return LLWorld::getInstance()->getMinAllowedZ(object, point); }
#if VS_NATIVE_VULKAN
inline std::vector<LLVector3>* vs_build_saved_children(VSEditObject* object)
{
    const auto saved = object->transformState();
    return saved ? &saved->mUnselectedChildrenPositions : nullptr;
}
inline bool vs_build_rebuild(VSEditObject* object)
{
    const auto owner     = VSNativeSession::active();
    auto*      selection = owner ? owner->existingObjectSelection() : nullptr;
    return selection && selection->rebuildObject(object->task().nativeObject());
}
inline std::optional<VSRegionContext> vs_build_region(VSEditObject* object)
{ return object ? vs_region_context(object->task().getHost()) : std::nullopt; }
inline F32 vs_build_min_height(VSEditObject* object, const LLVector3d& point)
{ return vs_region_height(point).value_or(0.f) - .5f * object->getScale().length(); }
#endif
inline bool vs_build_region_valid(const LLVector3d& point)
{ return vs_region_context(point).has_value(); }
inline F32 vs_build_height_agent(const LLVector3& point)
{ return vs_region_height(gAgent.getPosGlobalFromAgent(point)).value_or(0.f); }
inline void vs_build_clamp_attachments()
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        auto* context = gViewerWindow->nativeContext();
        if (!context)
            return;
        std::set<LLUUID> visited;
        for (const auto& attachment : context->world().attachments())
        {
            VSEditObject object(attachment.object.id);
            auto*        root = object.getRootEdit();
            if (!root || !visited.insert(root->getID()).second)
                continue;
            auto      position = root->getPosition();
            const F32 distance = position.normVec();
            position *= llmin(distance, MAX_ATTACHMENT_DIST);
            root->setPosition(position);
        }
        return;
    }
#endif
    if (gAgentAvatarp)
        gAgentAvatarp->clampAttachmentPositions();
}
