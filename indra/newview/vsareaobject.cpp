// Actual Area Search object peers; the original query/filter controller is shared.
#include "llviewerprecompiledheaders.h"
#include "vsareaobject.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llworld.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llvoavatarself.h"
#include "llselectmgr.h"
#include "fsassetblacklist.h"
#include "lltoolmgr.h"
#include "lltool.h"
#include "llassettype.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#include "rlvcommon.h"
#include "vsnativeim.h"
#include "message.h"
#if VS_NATIVE_VULKAN
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "vsnativesession.h"
#include "vseditselection.h"
#include "vsobjectattention.h"
#endif

namespace
{
#if VS_NATIVE_VULKAN
VSWorldScene* area_scene()
{
    auto* context = gViewerWindow && gViewerWindow->isNativeVulkan() ? gViewerWindow->nativeContext() : nullptr;
    return context ? &context->world() : nullptr;
}
#endif
}

struct VSAreaObject::State
{
    LLPointer<LLViewerObject> reference;
    VSObjectContext source;
    std::function<bool()> account;
#if VS_NATIVE_VULKAN
    std::unique_ptr<VSEditObject> native;
#endif
};
VSAreaRegion VSAreaRegion::current()
{
    const auto region = vs_current_region_context();
    return region ? VSAreaRegion(*region) : VSAreaRegion{};
}
std::vector<VSAreaRegion> VSAreaRegion::instances()
{
    std::vector<VSAreaRegion> result;
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        if (const auto owner = VSNativeSession::active())
            for (const auto& source : owner->regionSnapshots(false))
                if (const auto region = vs_region_context(source.host)) result.emplace_back(*region);
        return result;
    }
#endif
    for (auto* source : LLWorld::instance().getRegionList())
        if (const auto region = vs_region_context(source->getHost())) result.emplace_back(*region);
    return result;
}
void VSAreaRegion::getNeighboringRegions(std::vector<VSAreaRegion>& result) const
{
    if (!*this) return;
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto origin = mRegion->getOriginGlobal();
        const auto maxX = origin.mdV[VX] + mRegion->getWidth();
        const auto maxY = origin.mdV[VY] + mRegion->getHeight();
        for (const auto& region : instances())
        {
            if (region == *this) continue;
            const auto start = region->getOriginGlobal();
            const auto endX = start.mdV[VX] + region->getWidth(), endY = start.mdV[VY] + region->getHeight();
            const bool xTouch=start.mdV[VX]==maxX || endX==origin.mdV[VX];
            const bool yTouch=start.mdV[VY]==maxY || endY==origin.mdV[VY];
            const bool xOverlap=start.mdV[VX]<=maxX && endX>=origin.mdV[VX];
            const bool yOverlap=start.mdV[VY]<=maxY && endY>=origin.mdV[VY];
            if ((xTouch && yOverlap) || (yTouch && xOverlap)) result.push_back(region);
        }
        return;
    }
#endif
    if (auto* region = LLWorld::instance().getRegion(mRegion->getHost()))
    {
        std::vector<LLViewerRegion*> neighbors;
        region->getNeighboringRegions(neighbors);
        for (auto* source : neighbors)
            if (const auto context = vs_region_context(source->getHost())) result.emplace_back(*context);
    }
}
void VSAreaRegion::sendReliableMessage() const
{
    if (*this && gMessageSystem) gMessageSystem->sendReliable(mRegion->getHost());
}
VSAreaObject::VSAreaObject(LLViewerObject* object)
{
    if (!object || object->isDead()) return;
    mState = std::make_shared<State>();
    mState->reference = object;
    mState->source = VSObjectContext::fromLegacy(object);
    mState->account = vs_native_im_guard();
}
VSAreaObject::operator bool() const
{
    return mState && mState->account && mState->account() && mState->source.current && mState->source.current();
}
VSAreaObject VSAreaObject::find(const LLUUID& id)
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto* scene = area_scene();
        const auto token = scene ? scene->object(id) : std::nullopt;
        return token ? find(*token) : VSAreaObject{};
    }
#endif
    return VSAreaObject(gObjectList.findObject(id));
}
#if VS_NATIVE_VULKAN
VSAreaObject VSAreaObject::find(const VSWorldScene::ObjectHandle& token)
{
    const auto context = VSObjectContext::resolve(token);
    if (!context || !context->current()) return {};
    VSAreaObject result;
    result.mState = std::make_shared<State>();
    result.mState->source = *context;
    result.mState->account = vs_native_im_guard();
    result.mState->native = std::make_unique<VSEditObject>(token);
    return result;
}
const VSWorldScene::ObjectHandle& VSAreaObject::token() const { return mState->native->task().nativeObject(); }
#endif
std::vector<VSAreaObject> VSAreaObject::instances()
{
    std::vector<VSAreaObject> result;
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        if (const auto* scene = area_scene())
            for (const auto& row : scene->objectSnapshots())
            {
                auto object = find(row.object);
                if (object) result.push_back(std::move(object));
            }
        return result;
    }
#endif
    for (S32 index=0;index<gObjectList.getNumObjects();++index)
    {
        VSAreaObject object(gObjectList.getObject(index));
        if (object) result.push_back(std::move(object));
    }
    return result;
}
bool VSAreaObject::native() const
{
#if VS_NATIVE_VULKAN
    return mState && bool(mState->native);
#else
    return false;
#endif
}
LLViewerObject* VSAreaObject::legacy() const { return *this && !native() ? mState->reference.get() : nullptr; }
std::optional<VSObjectContext> VSAreaObject::context() const
{
    if (!*this) return {};
    if (auto* object=legacy()) return VSObjectContext::fromLegacy(object);
#if VS_NATIVE_VULKAN
    return VSObjectContext::resolve(token());
#else
    return {};
#endif
}
std::function<bool()> VSAreaObject::guard() const { return [source=*this] { return bool(source); }; }
const LLUUID& VSAreaObject::getID() const { return mState ? mState->source.id : LLUUID::null; }
U32 VSAreaObject::getLocalID() const { return *this ? mState->source.localID : 0; }
VSAreaRegion VSAreaObject::getRegion() const
{
    const auto region = *this ? vs_region_context(mState->source.host) : std::nullopt;
    return region ? VSAreaRegion(*region) : VSAreaRegion{};
}
U8 VSAreaObject::getPCode() const { return *this ? mState->source.pcode : 0; }
bool VSAreaObject::canSelect() const
{
    if (auto* object = legacy()) return object->mbCanSelect;
#if VS_NATIVE_VULKAN
    if (*this && area_scene()) return area_scene()->objectSelectable(token()).value_or(false);
#endif
    return false;
}
bool VSAreaObject::isAvatar() const { return getPCode() == LL_PCODE_LEGACY_AVATAR; }
bool VSAreaObject::isAttachment() const { return *this && mState->source.attachment; }
bool VSAreaObject::isHUDAttachment() const { return *this && mState->source.hud; }
LLVector3d VSAreaObject::getPositionGlobal() const
{
    if (auto* object = legacy()) return object->getPositionGlobal();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->getPositionGlobal();
#endif
    return {};
}
LLVector3 VSAreaObject::getPositionAgent() const { return gAgent.getPosAgentFromGlobal(getPositionGlobal()); }
LLBBox VSAreaObject::getBoundingBoxAgent() const
{
    if (auto* object = legacy()) return object->getBoundingBoxAgent();
#if VS_NATIVE_VULKAN
    if (*this && area_scene())
        if (const auto source = area_scene()->focusObject(token(),gAgent.getPosGlobalFromAgent(LLVector3::zero))) return source->bounds();
#endif
    return {};
}

bool VSAreaObject::isRoot() const
{
    if (auto* object = legacy()) return object->isRoot();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->isRoot();
#endif
    return false;
}

bool VSAreaObject::isRootEdit() const
{
    if (auto* object = legacy()) return object->isRootEdit();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->isRootEdit();
#endif
    return false;
}

bool VSAreaObject::isReflectionProbe() const
{
    if (auto* object = legacy()) return object->mReflectionProbe.notNull();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->isReflectionProbe();
#endif
    return false;
}

bool VSAreaObject::flagUsePhysics() const
{
    if (auto* object = legacy()) return object->flagUsePhysics();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->flagUsePhysics();
#endif
    return false;
}

bool VSAreaObject::flagTemporaryOnRez() const
{
    if (auto* object = legacy()) return object->flagTemporaryOnRez();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->flagTemporaryOnRez();
#endif
    return false;
}

bool VSAreaObject::flagPhantom() const
{
    if (auto* object = legacy()) return object->flagPhantom();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->flagPhantom();
#endif
    return false;
}

bool VSAreaObject::flagHandleTouch() const
{
    if (auto* object = legacy()) return object->flagHandleTouch();
#if VS_NATIVE_VULKAN
    if (*this) return (mState->native->flags() & FLAGS_HANDLE_TOUCH) != 0;
#endif
    return false;
}

U8 VSAreaObject::getClickAction() const
{
    if (auto* object = legacy()) return object->getClickAction();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->getClickAction();
#endif
    return 0;
}

U8 VSAreaObject::getNumTEs() const
{
    if (auto* object = legacy()) return object->getNumTEs();
#if VS_NATIVE_VULKAN
    if (*this) return U8(mState->native->getNumTEs());
#endif
    return 0;
}

LLTextureEntry* VSAreaObject::getTE(U8 index) const
{
    if (auto* object = legacy()) return object->getTE(index);
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->getTE(index);
#endif
    return nullptr;
}

F32 VSAreaObject::getLinksetCost() const
{
    if (auto* object = legacy()) return object->getLinksetCost();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->getLinksetCost();
#endif
    return 0;
}

S32 VSAreaObject::numChildren() const
{
    if (auto* object = legacy()) return S32(object->numChildren());
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->numChildren();
#endif
    return 0;
}

bool VSAreaObject::permYouOwner() const
{
    if (auto* object = legacy()) return object->permYouOwner();
#if VS_NATIVE_VULKAN
    if (*this) return mState->native->permYouOwner();
#endif
    return false;
}

bool VSAreaObject::canTouch() const
{
    if (auto* object = legacy()) return RlvActions::canTouch(object);
    const auto source = context();
    return source && RlvActions::canTouch(*source);
}
bool VSAreaObject::canSit() const
{
    if (auto* object = legacy()) return RlvActions::canSit(object);
    const auto source = context();
    return source && RlvActions::canSit(*source);
}
bool VSAreaObject::canEdit() const
{
    if (auto* object = legacy()) return RlvActions::canEdit(object);
    const auto source = context();
    return source && RlvActions::canEdit(*source);
}
bool VSAreaObject::canDeleteOrReturn() const
{
    if (auto* object = legacy()) return rlvCanDeleteOrReturn(object);
#if VS_NATIVE_VULKAN
    if (!*this || !canEdit()) return false;
    const auto* source = mState->native.get();
    const auto* root = source->getRootEdit();
    const bool groupOwned = root && (root->flags() & FLAGS_OBJECT_GROUP_OWNED);
    return ((!gRlvHandler.hasBehaviour(RLV_BHVR_EDIT) && !gRlvHandler.hasBehaviour(RLV_BHVR_REZ)) ||
            (!source->permYouOwner() && !source->permModify() && !groupOwned)) &&
           (!gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT) || !source->task().agentSittingOnRoot());
#else
    return false;
#endif
}
bool VSAreaObject::selectFamily(bool add) const
{
    if (auto* object = legacy())
    {
        if (!add) LLSelectMgr::instance().deselectAll();
        LLSelectMgr::instance().selectObjectAndFamily(object);
        return LLSelectMgr::instance().getSelection()->findNode(object) != nullptr;
    }
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active();owner && *this)
    {
        auto* selection=owner->objectSelection();
        if (!add) selection->deselectAll();
        return *this && selection->selectFamily(token(),add);
    }
#endif
    return false;
}
void VSAreaObject::lookAt() const
{
    if (auto* object = legacy()) { gAgentCamera.setLookAt(LOOKAT_TARGET_SELECT,object);return; }
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active();owner && *this)
        if (auto* attention = owner->objectAttention()) attention->look(U8(LOOKAT_TARGET_SELECT),token());
#endif
}

bool VSAreaObject::sameSource(const VSAreaObject& other) const
{
    if (!*this || !other || native() != other.native()) return false;
    if (!native()) return legacy() == other.legacy();
#if VS_NATIVE_VULKAN
    const auto& a=token();const auto& b=other.token();
    return a.host==b.host && a.local==b.local && a.region==b.region && a.generation==b.generation &&
           a.account==b.account && a.accountGeneration==b.accountGeneration;
#else
    return false;
#endif
}

VSAreaObject VSAreaObject::find(const LLHost& host,const LLUUID& id)
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        auto* scene=area_scene();
        const auto source=scene?scene->object(host,id):std::nullopt;
        return source?find(*source):VSAreaObject{};
    }
#endif
    VSAreaObject source(gObjectList.findObject(id));
    return source && source.getRegion() && source.getRegion()->getHost()==host ? source : VSAreaObject{};
}
void VSAreaObject::derender(const std::string& name) const
{
#if VS_NATIVE_VULKAN
    const auto owner=VSNativeSession::active();
    if (!native() || !*this || !owner || !area_scene() || getID()==gAgentID) return;
    const auto region=getRegion();
    FSAssetBlacklist::instance().addNewItemToBlacklist(getID(),name,region?region->getName():std::string{},LLAssetType::AT_OBJECT);
    if (!*this) return;
    // Native blacklist observer queues retirement until this original
    // deselect/store/kill sequence has delivered ObjectDeselect.
    owner->objectSelection()->deselectOnly(token());
    gObjectList.addDerenderedItem(getID(),true);
    auto* tool = LLToolMgr::instance().getCurrentTool();
    const LLUUID editing = tool ? tool->getEditingObjectID() : LLUUID::null;
    const LLUUID object_id = getID();
    area_scene()->derender(token());
    if (tool && editing == object_id) tool->stopEditing();
#endif
}
