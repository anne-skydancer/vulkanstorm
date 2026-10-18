// Typed Build volume queries over genuine renderer-owned objects. LGPL-2.1.
#pragma once
#include "llvovolume.h"
#include "llvoavatarself.h"
#include "llagentbenefits.h"
#include "llviewerwindow.h"
#if VS_NATIVE_VULKAN
#include "vseditselection.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#endif
inline LLViewerObject* vs_build_child(const LLPointer<LLViewerObject>& object)
{ return object.get(); }
inline LLViewerObject* vs_build_parent(LLViewerObject* object)
{ return static_cast<LLViewerObject*>(object->getParent()); }
#if VS_NATIVE_VULKAN
inline VSEditObject* vs_build_child(VSEditObject* object)
{ return object; }
inline VSEditObject* vs_build_parent(VSEditObject* object)
{ return object->getParent(); }
#endif
inline LLVOVolume* vs_build_volume(LLViewerObject* object)
{ return object && object->getPCode() == LL_PCODE_VOLUME ? static_cast<LLVOVolume*>(object) : nullptr; }
inline bool vs_build_has_volume(LLViewerObject* object)
{ return object->getVolume() != nullptr; }
inline LLVolumeParams vs_build_volume_params(LLViewerObject* object)
{ return object->getVolume()->getParams(); }
#if VS_NATIVE_VULKAN
inline VSEditObject* vs_build_volume(VSEditObject* object)
{ return object && object->getPCode() == LL_PCODE_VOLUME ? object : nullptr; }
inline bool vs_build_has_volume(VSEditObject* object)
{
    return object && object->getPCode() == LL_PCODE_VOLUME &&
           gViewerWindow->nativeContext()->world().objectVolumeParams(object->task().nativeObject()).has_value();
}
inline LLVolumeParams vs_build_volume_params(VSEditObject* object)
{
    const auto value = gViewerWindow->nativeContext()->world().objectVolumeParams(object->task().nativeObject());
    if (!value)
        throw std::logic_error("Retired Build volume source");
    return *value;
}
#endif
inline bool vs_build_can_attach_animated(U32 count = 1)
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        auto* context = gViewerWindow->nativeContext();
        if (!context)
            return false;
        std::set<LLUUID> roots;
        for (const auto& attachment : context->world().attachments())
        {
            VSEditObject object(attachment.object.id);
            if (auto* root = object.getRootEdit(); root && root->isAnimatedObject())
                roots.insert(root->getID());
        }
        return roots.size() + count <= U32(LLAgentBenefitsMgr::current().getAnimatedObjectLimit());
    }
#endif
    return gAgentAvatarp && gAgentAvatarp->canAttachMoreAnimatedObjects(count);
}
