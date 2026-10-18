// Reference CPU movement inputs from real renderer-owned scene objects. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsagentnavigation.h"
#include "llvoavatarself.h"
#include "llavatarappearance.h"
#include "llviewerobjectlist.h"
#include "llworld.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsobjectcontext.h"
#include "vsregioncontext.h"
#include "vsworldavatar.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "llregionhandle.h"
namespace
{
VSWorldAvatar* avatar(const std::shared_ptr<VSNativeSession>& session)
{
    return session && session->regionReady() && gViewerWindow && gViewerWindow->nativeContext()
               ? gViewerWindow->nativeContext()->world().selfAvatar()
               : nullptr;
}
} // namespace
#endif
VSAgentNavigation::VSAgentNavigation()
{
#if VS_NATIVE_VULKAN
    mNative = VSNativeSession::active();
    if (mNative)
    {
        const auto token = mNative->account();
        mAgent           = token.agent;
        mSession         = token.session;
        mGeneration      = token.generation;
    }
#endif
}
bool VSAgentNavigation::native() const
{
#if VS_NATIVE_VULKAN
    return bool(mNative);
#else
    return false;
#endif
}
bool VSAgentNavigation::valid() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
        return current() && avatar(mNative);
#endif
    return isAgentAvatarValid();
}
bool VSAgentNavigation::inAir() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        const auto* current = valid() ? avatar(mNative) : nullptr;
        return current && current->inAir();
    }
#endif
    return gAgentAvatarp->mInAir;
}
F32 VSAgentNavigation::pelvisToFoot() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        auto* current = valid() ? avatar(mNative) : nullptr;
        return current ? current->appearanceOwner().getPelvisToFoot() : 0.f;
    }
#endif
    return gAgentAvatarp->getPelvisToFoot();
}
LLVector3d VSAgentNavigation::avatarPosition() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
        return from_region_handle(mNative->handle()) + LLVector3d(mNative->position());
#endif
    return gAgentAvatarp->getPositionGlobal();
}
std::optional<LLVector3d> VSAgentNavigation::objectPosition(const LLUUID& id) const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        if (!current())
            return {};
        const auto object = VSObjectContext::resolve(id);
        return object && object->current && object->current() ? std::optional<LLVector3d>(object->position) : std::nullopt;
    }
#endif
    const auto* object = gObjectList.findObject(id);
    return object ? std::optional<LLVector3d>(object->getPositionGlobal()) : std::nullopt;
}
F32 VSAgentNavigation::stepHeight(const LLVector3d& a, const LLVector3d& b, LLVector3d& intersection) const
{
#if VS_NATIVE_VULKAN
    if (mNative)
    {
        // LLAgent passes a NULL avatar to LLWorld::resolveStepHeightGlobal:
        // the reference uses terrain, not the avatar's collision foot plane.
        const auto region = vs_region_context(a);
        if (!region || !region->current() || !current())
        {
            intersection = .5f * (a + b);
            return .5f;
        }
        const F32 length = F32((a - b).length());
        if (length == 0)
        {
            intersection = a;
            return 0;
        }
        const auto height = vs_region_height(a);
        if (!height)
        {
            intersection = .5f * (a + b);
            return .5f;
        }
        intersection         = a;
        intersection.mdV[VZ] = *height;
        return F32(a.mdV[VZ] - intersection.mdV[VZ]) / length;
    }
#endif
    LLVector3       normal;
    LLViewerObject* object;
    return LLWorld::getInstance()->resolveStepHeightGlobal(nullptr, a, b, intersection, normal, &object);
}
std::function<bool()> VSAgentNavigation::accountGuard() const
{
#if VS_NATIVE_VULKAN
    if (mNative)
        return mNative->accountGuard();
#endif
    return {};
}

#if VS_NATIVE_VULKAN
bool VSAgentNavigation::current() const
{ return mNative && mNative->regionReady() && mNative->current(VSNativeSession::AccountHandle{ mAgent, mSession, mGeneration }); }
#endif

void VSAgentNavigation::face(const LLVector3& direction) const
{
#if VS_NATIVE_VULKAN
    if (mNative && current())
        mNative->faceBody(direction);
#endif
}
