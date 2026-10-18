// Native object/face values; no GL viewer-object or drawable stand-ins.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsprimmediatarget.h"
#include "vsmediapolicy.h"
#include "vsnativesession.h"
#include "vsobjectcontext.h"
#include "vsobjectselection.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llviewermedia.h"
#include "llagent.h"
namespace
{
VSWorldScene* mediaScene()
{
    return gViewerWindow && gViewerWindow->isNativeVulkan() && gViewerWindow->nativeContext()
        ? &gViewerWindow->nativeContext()->world():nullptr;
}
}
VSPrimMediaTarget::VSPrimMediaTarget(std::weak_ptr<VSNativeSession> session,VSWorldScene::ObjectHandle object,S32 face,const LLUUID& media)
    :mSession(std::move(session)),mObject(std::move(object)),mFace(face),mMedia(media){}
std::optional<VSPrimMediaTarget> VSPrimMediaTarget::resolve(const LLUUID& id,S32 face)
{
    auto* world=mediaScene();const auto session=VSNativeSession::active();
    if(!world || !session || session->phase()!=VSNativeSession::Phase::Connected || face<0)return {};
    const auto object=world->object(id);
    if(!object || face>=world->textureEntryCount(*object))return {};
    VSPrimMediaTarget target(session,*object,face,world->mediaTexture(*object,U32(face)));return target.current()?std::optional<VSPrimMediaTarget>(std::move(target)):std::nullopt;
}
bool VSPrimMediaTarget::current() const
{
    auto* world=mediaScene();
    const auto session=mSession.lock();
    if(!world || !session || session->phase()!=VSNativeSession::Phase::Connected || !world->current(mObject))return false;
    const auto account=session->account();
    if(!session->current(account) || mObject.account!=account.agent || mObject.accountGeneration!=account.generation)return false;
    const auto admission=world->objectAdmission(mObject);
    return admission && !admission->orphaned && mFace<world->textureEntryCount(mObject) &&
        world->mediaTexture(mObject,U32(mFace))==mMedia;
}
std::function<bool()> VSPrimMediaTarget::guard() const
{return [target=*this]{return target.current();};}
const LLUUID& VSPrimMediaTarget::id() const{return mObject.id;}
S32 VSPrimMediaTarget::face() const{return mFace;}
S32 VSPrimMediaTarget::faceCount() const
{return current()?mediaScene()->textureEntryCount(mObject):0;}
LLUUID VSPrimMediaTarget::mediaID() const{return mMedia;}
const LLMediaEntry* VSPrimMediaTarget::metadata() const
{return current()?mediaScene()->mediaEntry(mObject,U32(mFace)):nullptr;}
LLViewerMediaImpl* VSPrimMediaTarget::media() const
{return current()?LLViewerMedia::getInstance()->getMediaImplFromTextureID(mMedia):nullptr;}
bool VSPrimMediaTarget::hud() const
{
    if(!current())return false;
    const auto row=mediaScene()->objectAdmission(mObject);return row && row->hud;
}
bool VSPrimMediaTarget::permission(bool interact) const
{
    return vs_has_media_permission(metadata(),interact,[this]
    {
        const auto owner=mSession.lock();if(!owner || !current())return false;
        // Match findObjectPermissions: only actual selected properties confer
        // the group context, never an unselected hover or an invented group.
        const auto* selection=owner->objectSelection();
        const auto properties=selection?selection->findProperties(mObject):std::nullopt;
        return properties && gAgent.isInGroup(properties->properties->group_id);
    },[this]
    {
        if(!current())return false;
        const auto row=mediaScene()->objectAdmission(mObject);
        return row && VSObjectContext::ownedByAgent(row->rootUpdateFlags);
    });
}
std::optional<std::pair<LLUUID,LLUUID>> VSPrimMediaTarget::hoverOwners() const
{
    const auto owner=mSession.lock();
    if(!owner || !current())return {};
    const auto* selection=owner->objectSelection();
    const auto properties=selection?selection->hoverProperties(mObject):std::nullopt;
    if(!properties)return {};
    return std::pair(properties->properties->owner_id,properties->properties->group_id);
}
std::optional<LLVector3d> VSPrimMediaTarget::position() const
{
    if(!current())return {};
    const auto object=VSObjectContext::resolve(mObject.id);
    return object && object->current()?std::optional<LLVector3d>(object->position):std::nullopt;
}
std::optional<LLBBox> VSPrimMediaTarget::bounds() const
{
    if(!current())return {};
    const auto frame=mediaScene()->focusObject(mObject,gAgent.getPosGlobalFromAgent(LLVector3::zero));
    return frame?std::optional<LLBBox>(frame->bounds()):std::nullopt;
}
std::optional<std::array<LLVector3,8>> VSPrimMediaTarget::faceBounds() const
{return current()?mediaScene()->mediaFaceBounds(mObject,U32(mFace),gAgent.getPosGlobalFromAgent(LLVector3::zero)):std::nullopt;}
bool VSPrimMediaTarget::selectFace() const
{
    const auto owner=mSession.lock();
    if(owner && current())owner->objectSelection();
    const auto selection=owner && current()?owner->retainObjectSelection():nullptr;
    return selection && selection->selectOnly(mObject,mFace);
}
void VSPrimMediaTarget::deselectAll()
{
    if(const auto owner=VSNativeSession::active())
        if(const auto selection=owner->retainObjectSelection())selection->deselectAll();
}
std::shared_ptr<const void> VSPrimMediaTarget::retainSelection() const
{
    const auto owner=mSession.lock();
    const auto* selection=owner && current()?owner->objectSelection():nullptr;
    return selection?selection->retainSelection():nullptr;
}
