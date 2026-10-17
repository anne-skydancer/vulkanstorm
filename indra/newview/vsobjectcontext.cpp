// Adapt actual renderer-peer object owners to the existing UI policy.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectcontext.h"
#include "vsobjectinteraction.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llagent.h"
#include "llviewernetwork.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#endif

bool VSObjectContext::ownedByAgent(U32 rootFlags)
{
#ifdef HACKED_GODLIKE_VIEWER
    return true;
#else
#ifdef TOGGLE_HACKED_GODLIKE_VIEWER
    if (LLGridManager::getInstance()->isInSLBeta() && gAgent.getGodLevel()>=GOD_MAINTENANCE)
        return true;
#endif
    return (rootFlags & FLAGS_OBJECT_YOU_OWNER)!=0;
#endif
}

VSObjectContext VSObjectContext::fromLegacy(const LLViewerObject* object)
{
    VSObjectContext value;
    if (!object || object->isDead()) return value;
    auto* root=object->getRootEdit();
    value.id=object->getID();value.rootEditID=root->getID();value.rootID=static_cast<const LLViewerObject*>(object->getRoot())->getID();
    value.localID=object->getLocalID();value.flags=root->getFlags();value.pcode=object->getPCode();
    value.material=object->getMaterial();value.clickAction=vs_object_click_action(object->getClickAction(),root->getClickAction(),object->isAttachment());
    value.position=object->getPositionGlobal();value.attachment=object->isAttachment();
    value.hud=object->isHUDAttachment();value.youOwner=object->permYouOwner();
    if (object->getRegion()) value.host=object->getRegion()->getHost();
    // The identity remains owned by the real reference object list. Reparenting
    // invalidates pending actions even when the object's UUID itself survives.
    const LLPointer<LLViewerObject> retained=const_cast<LLViewerObject*>(object);
    const auto agent=gAgent.getID(),session=gAgent.getSessionID();
    value.current=[retained,agent,session,id=value.id,rootEdit=value.rootEditID,rootID=value.rootID,host=value.host]()
    {
        return gAgent.getID()==agent && gAgent.getSessionID()==session && retained.notNull() && !retained->isDead() &&
            gObjectList.findObject(id)==retained.get() && retained->getRootEdit()->getID()==rootEdit &&
            static_cast<const LLViewerObject*>(retained->getRoot())->getID()==rootID && retained->getRegion() && retained->getRegion()->getHost()==host;
    };
    return value;
}

std::optional<VSObjectContext> VSObjectContext::resolve(const LLUUID& id)
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=VSNativeSession::active();
        auto* context=gViewerWindow->nativeContext();
        if (!owner || owner->phase()!=VSNativeSession::Phase::Connected || !context) return {};
        auto& world=context->world();
        const auto handle=world.object(id);
        if (!handle) return {};
        const auto row=world.objectSnapshot(*handle);
        if (!row) return {};
        const auto root=world.object(row->rootID),rootEdit=world.object(row->rootEditID);
        if (!root || !rootEdit) return {};
        VSObjectContext value;
        value.id=id;value.rootEditID=row->rootEditID;value.rootID=row->rootID;value.host=handle->host;
        value.localID=handle->local;value.flags=row->rootUpdateFlags;value.pcode=row->pcode;value.material=row->material;
        value.clickAction=vs_object_click_action(row->clickAction,row->rootClickAction,row->attachment);
        value.position=row->position;value.attachment=row->attachment;value.hud=row->hud;
        value.youOwner=ownedByAgent(row->rootUpdateFlags);
        const auto account=owner->account();
        const auto objectGuard=world.guard(*handle),rootGuard=world.guard(*root),editGuard=world.guard(*rootEdit);
        value.current=[weak=std::weak_ptr<VSNativeSession>(owner),account,objectGuard,rootGuard,editGuard,
                       token=*handle,rootID=row->rootID,editID=row->rootEditID]()
        {
            const auto owner=weak.lock();
            if (!owner || !owner->current(account) || owner->phase()!=VSNativeSession::Phase::Connected ||
                !objectGuard() || !rootGuard() || !editGuard() || !gViewerWindow || !gViewerWindow->nativeContext()) return false;
            const auto now=gViewerWindow->nativeContext()->world().objectSnapshot(token);
            return now && now->rootID==rootID && now->rootEditID==editID;
        };
        return value;
    }
#endif
    const auto* object=gObjectList.findObject(id);
    if (!object || object->isDead()) return {};
    return fromLegacy(object);
}
