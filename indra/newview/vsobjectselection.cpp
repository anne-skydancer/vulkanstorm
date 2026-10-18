// Preserve reference ObjectSelect/ObjectDeselect and properties-family protocols.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectselection.h"
#include "vseditselection.h"
#include "vseditdelete.h"
#include "vsregioncontext.h"
#include "llinventorymodel.h"
#include "llhudeffecttrail.h"
#include "llhudmanager.h"
#include "llaudioengine.h"
#include "llviewerstats.h"
#include "object_flags.h"
#include "fscommon.h"
#include "lleditmenuhandler.h"
#include "vslinkactions.h"
#include "llnotificationsutil.h"
#include "vsregionlimits.h"
#include "llmeshrepository.h"
#include "rlvactions.h"
#include "vstaskinventory.h"
#include "vsobjectattention.h"
#include "vsworldavatar.h"
#include "llagentcamera.h"
#include "llcharacter.h"
#include "llhudeffectpointat.h"
#include "llhudeffectlookat.h"
#include "vsselectionpolicy.h"
#include "vsselectedobjectpermissions.h"
#include "vsobjectcontext.h"
#include "vsobjectinteraction.h"
#include "llselectmgr.h"
#include "llfloatertools.h"
#include "lltoolmgr.h"
#include "vsmultipleupdate.h"
#include "vstexturescaleselection.h"
#include "llviewerobject.h"
#include "lltextureentry.h"
#include "llviewercontrol.h"
#include "llagent.h"
#include "llattachmentsmgr.h"
#include "rlvlocks.h"
#include "rlvhandler.h"
#include "llviewernetwork.h"
#include "vsnativesession.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include "llframetimer.h"
#include "message.h"
#include <boost/signals2/signal.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <utility>
namespace
{
bool sameObject(const VSWorldScene::ObjectHandle& a,const VSWorldScene::ObjectHandle& b)
{
    return a.host==b.host && a.id==b.id && a.local==b.local && a.region==b.region &&
        a.generation==b.generation && a.kind==b.kind && a.account==b.account && a.accountGeneration==b.accountGeneration;
}
VSWorldScene* scene()
{
    return gViewerWindow && gViewerWindow->isNativeVulkan() && gViewerWindow->nativeContext() ?
        &gViewerWindow->nativeContext()->world() : nullptr;
}
std::size_t propertyBytes(const VSObjectProperties& value)
{
    // Wire strings and packed UUIDs are protocol bounded before publication.
    return sizeof(value)+value.name.size()+value.desc.size()+value.touch_name.size()+value.sit_name.size()+
        4+value.texture_ids.size()*sizeof(LLUUID);
}
}
struct VSObjectSelection::Impl
{
    struct Node
    {
        VSWorldScene::ObjectHandle object;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
        std::optional<Publication> publication;
        bool pending=true;
        F64 retryAt=0;
        NodeMetadata metadata;
        std::shared_ptr<SavedMaterials> materials = std::make_shared<SavedMaterials>();
        std::shared_ptr<SavedColors> colors;
        std::shared_ptr<SavedTransform> transform;
        std::shared_ptr<VSEditNode> editProjection;
    };
    struct Request
    {
        VSWorldScene::ObjectHandle object;
        U32 flags=0;
        std::function<bool()> current;
        FamilyCallback callback;
        F64 deadline=0;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::weak_ptr<VSNativeSession> session;
    struct AvatarOverride
    {
        VSWorldScene::ObjectHandle object;LLVector3 position;LLQuaternion rotation;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::map<LLUUID,AvatarOverride> avatarOverrides;
    std::vector<Node> nodes;
    std::vector<Node> hover;
    struct GridReference{VSWorldScene::ObjectHandle object;std::shared_ptr<const VSResourceBudget::Reservation> residency;};
    std::vector<GridReference> gridObjects;
    std::optional<VSWorldScene::ObjectHandle> primary,hoverRoot,hoverPrimary;
    U64 hoverRevision=1;
    LLBBox bounds;
    std::optional<LLBBox> savedSelectionBounds;
    LLVector3d selectionCenter;
    bool geometryKnown=false,showSelection=false;
    std::vector<LLAnimPauseRequest> pauses;
    bool forceSelection=false,allowSelectAvatar=false,retired=false;
    // One shared owner marker per account incarnation. Real UI consumers hold
    // this same marker; account retirement invalidates all old leases at once.
    std::shared_ptr<const void> selectionLease=std::make_shared<const U8>(0);
    std::vector<Request> families;
    struct Highlight
    {
        VSWorldScene::ObjectHandle object;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::vector<Highlight> highlights;
    std::vector<Highlight> rectangleHits;
    std::vector<Node> rectangleNodes;
    U64 rectangleRevision=1;
    std::function<bool()> highlightCurrent;
    U64 highlightRevision=1;
    boost::signals2::signal<void()> changed;
    boost::signals2::scoped_connection materialUpdates;
    boost::signals2::signal<void(const LLUUID&, S32)> materialChanged;
    U64 revision=1;
    explicit Impl(std::weak_ptr<VSNativeSession> value):session(std::move(value)){}
    bool current(const VSWorldScene::ObjectHandle& token)const
    {
        const auto owner=session.lock();
        auto* world=scene();
        if (retired || !owner || owner->phase()!=VSNativeSession::Phase::Connected || !world) return false;
        const auto identity=owner->account();
        return token.account==identity.agent && token.accountGeneration==identity.generation &&
            owner->current(identity) && world->current(token);
    }
    std::optional<Publication> publish(const VSWorldScene::ObjectHandle& token,VSObjectProperties value)
    {
        auto* world=scene();
        if(!current(token) || !world)return {};
        auto reservation=world->reserveSpatialBytes(propertyBytes(value));
        if(!reservation)return {};
        return Publication{token,std::make_shared<const VSObjectProperties>(std::move(value)),std::move(reservation)};
    }
    void sendSelection(const char* name,const std::vector<VSWorldScene::ObjectHandle>& objects)
    {
        const auto owner=session.lock();
        if(!owner || !gMessageSystem)return;
        const auto account=owner->account();
        std::map<LLHost,std::vector<U32>> regions;
        for(const auto& object:objects)if(current(object))regions[object.host].push_back(object.local);
        for(const auto& [host,locals]:regions)
        {
            std::size_t index=0;
            while(index<locals.size() && owner->current(account))
            {
                gMessageSystem->newMessage(name);
                gMessageSystem->nextBlock("AgentData");
                gMessageSystem->addUUID("AgentID",account.agent);
                gMessageSystem->addUUID("SessionID",account.session);
                S32 count=0;
                do
                {
                    gMessageSystem->nextBlock("ObjectData");
                    gMessageSystem->addU32("ObjectLocalID",locals[index++]);
                    ++count;
                }
                while(index<locals.size() && count<254 && !gMessageSystem->isSendFull(nullptr));
                gMessageSystem->sendReliable(host);
            }
        }
    }
};
VSObjectSelection::VSObjectSelection(std::weak_ptr<VSNativeSession> session)
    :mImpl(std::make_shared<Impl>(std::move(session)))
{
    if (auto* world = scene())
        mImpl->materialUpdates = world->observeMaterialUpdates([weak = std::weak_ptr<Impl>(mImpl)](const auto& token, S32 side)
        {
            const auto state = weak.lock();
            if (!state || !state->current(token)) return;
            for (const auto& node : state->nodes)
                if (sameObject(node.object, token))
                {
                    state->materialChanged(token.id, side);
                    if (state->current(token)) state->changed();
                    return;
                }
        });
}
VSObjectSelection::~VSObjectSelection(){reset();}
VSObjectSelection::SelectionTypeState VSObjectSelection::selectionType() const
{
    for(const auto& node:mImpl->nodes)
        if(mImpl->current(node.object))
        {
            auto* world=scene();const auto row=world?world->objectAdmission(node.object):std::nullopt;
            if(!row)return {true,{}};
            return {true,row->hud?SelectionType::HUD:row->attachment?SelectionType::Attachment:SelectionType::World};
        }
    return {};
}

bool VSObjectSelection::canSelect(const VSWorldScene::ObjectHandle& object,bool forceSelection,bool ignoreSelectOwned) const
{
    const auto owner=mImpl->session.lock();
    auto* world=scene();
    if(!owner || !world || !mImpl->current(object))return false;
    // Reference force overrides permission/type/orphan checks after live identity.
    if(forceSelection)return true;
    const auto row=world->objectAdmission(object);
    if(!row)return false;
    struct Object
    {
        const VSWorldScene::ObjectAdmission& row;
        const VSNativeSession& session;
        const LLHost& host;
        bool present()const{return true;}
        bool dead()const{return false;} // checked against the real scene/account owner above
        bool youOwner()const{return VSObjectContext::ownedByAgent(row.rootUpdateFlags);}
        bool permission(U32 flag)const
        {
            return vs_object_you_owner((row.rootUpdateFlags&flag)!=0,[]
            {return LLGridManager::getInstance()->isInSLBeta() && gAgent.getGodLevel()>=GOD_MAINTENANCE;});
        }
        bool move()const{return permission(FLAGS_OBJECT_MOVE);}
        bool copy()const{return permission(FLAGS_OBJECT_COPY);}
        bool permanent()const{return (row.updateFlags&FLAGS_AFFECTS_NAVMESH) && host!=session.host() && !gAgent.isGodlike();}
        bool orphaned()const{return row.orphaned;}
        bool avatar()const{return row.pcode==LL_PCODE_LEGACY_AVATAR;}
        U8 pcode()const{return row.pcode;}
        ESelectType selectionType()const{return row.hud?SELECT_TYPE_HUD:row.attachment?SELECT_TYPE_ATTACHMENT:SELECT_TYPE_WORLD;}
    };
    struct Selection
    {
        const VSObjectSelection& owner;
        mutable std::optional<SelectionTypeState> cached;
        const SelectionTypeState& state()const
        {
            if(!cached)cached=owner.selectionType();
            return *cached;
        }
        bool hasSelection()const{return state().selected;}
        std::optional<ESelectType> type()const
        {
            const auto& value=state();
            if(!value.type)return {};
            return *value.type==SelectionType::HUD?SELECT_TYPE_HUD:
                *value.type==SelectionType::Attachment?SELECT_TYPE_ATTACHMENT:SELECT_TYPE_WORLD;
        }
    };
    return vs_can_select_object(Object{*row,*owner,object.host},forceSelection,ignoreSelectOwned,Selection{*this,{}},
        LLViewerObject::LL_VO_SURFACE_PATCH,[](const char* setting){return gSavedSettings.getBOOL(setting);});
}
bool VSObjectSelection::select(const std::vector<VSWorldScene::ObjectHandle>& objects)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world)return false;
    std::vector<Impl::Node> next;
    std::set<LLUUID> identities;
    for(const auto& object:objects)
    {
        if(!state->current(object) || !identities.insert(object.id).second)return false;
        const auto old=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
        if(old!=state->nodes.end())next.push_back(*old);
        else
        {
            auto reservation=world->reserveSpatialBytes(sizeof(Impl::Node));
            if(!reservation)return false;
            Impl::Node candidate{object,std::move(reservation),{},true,LLFrameTimer::getTotalSeconds()+5};
            const auto faces=world->faceCount(object);
            if(!faces)return false;
            candidate.metadata.faceMask=*faces?0xffffffffu:0;
            next.push_back(std::move(candidate));
        }
    }
    std::vector<VSWorldScene::ObjectHandle> removed,added;
    for(const auto& old:state->nodes)
        if(!identities.count(old.object.id) || std::none_of(next.begin(),next.end(),[&](const auto& node){return sameObject(node.object,old.object);}))
            removed.push_back(old.object);
    for(const auto& node:next)
        if(std::none_of(state->nodes.begin(),state->nodes.end(),[&](const auto& old){return sameObject(old.object,node.object);}))added.push_back(node.object);
    // Check the actual linked-family freeze before committing selection. A
    // current local object may still have a retired or cyclic parent chain.
    for(const auto& node:next)
        if(!world->freeze(node.object,true))
        {
            for(const auto& candidate:next)world->freeze(candidate.object,false);
            for(const auto& previous:state->nodes)world->freeze(previous.object,true);
            return false;
        }
    for(const auto& object:removed)world->freeze(object,false);
    // Removing one member can thaw another selected member's linked family.
    for(const auto& node:next)world->freeze(node.object,true);
    state->nodes=std::move(next);
    state->primary=objects.empty()?std::nullopt:std::optional<VSWorldScene::ObjectHandle>(objects.front());
    ++state->revision;
    state->sendSelection("ObjectDeselect",removed);
    state->sendSelection("ObjectSelect",added);
    state->changed();
    return true;
}
std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::selected()const
{
    std::vector<VSWorldScene::ObjectHandle> result;
    for(const auto& node:mImpl->nodes)if(mImpl->current(node.object))result.push_back(node.object);
    return result;
}
std::optional<VSWorldScene::ObjectHandle> VSObjectSelection::firstRootObject(bool nonRootOK) const
{
    auto* world=scene();if(!world)return {};
    const auto state=mImpl;
    for(const auto& node:state->nodes)
    {
        if(node.metadata.individual || !state->current(node.object))continue;
        const auto root=world->rootObject(node.object,true);
        if(root && sameObject(*root,node.object))return node.object;
    }
    if(nonRootOK)
        for(const auto& node:state->nodes)if(state->current(node.object))return node.object;
    return {};
}
S32 VSObjectSelection::rootObjectCount() const
{
    auto* world=scene();if(!world)return 0;
    S32 count=0;
    for(const auto& node:mImpl->nodes)
    {
        if(node.metadata.individual || !mImpl->current(node.object))continue;
        const auto root=world->rootObject(node.object,true);
        if(root && sameObject(*root,node.object))++count;
    }
    return count;
}
S32 VSObjectSelection::objectCount() const
{
    S32 count=0;
    for(const auto& node:mImpl->nodes)if(mImpl->current(node.object))++count;
    return count;
}
bool VSObjectSelection::selectionAllPCode(U8 code) const
{
    auto* world=scene();if(!world)return true;
    bool result=true;
    for(const auto& node:mImpl->nodes)
    {
        if(!mImpl->current(node.object))continue;
        const auto object=world->objectAdmission(node.object);
        if(object)result=(object->pcode==code)&&result;
    }
    return result;
}
bool VSObjectSelection::selectGetGroup(LLUUID& result) const
{
    result.setNull();
    auto* world=scene();if(!world)return true;
    const bool parts=gSavedSettings.getBOOL("EditLinkedParts");
    bool first=true;
    for(const auto& node:mImpl->nodes)
    {
        if(!mImpl->current(node.object))continue;
        const bool valid=!node.pending && node.publication.has_value();
        if(parts)
        {
            if(!valid)continue; // Original valid_iterator, not root filter.
        }
        else
        {
            const auto root=world->rootObject(node.object,true);
            if(!root || !sameObject(*root,node.object))continue;
            if(!valid)break; // Original checkMatchingNode stops on invalid root.
        }
        const auto& group=node.publication->properties->group_id;
        if(first){result=group;first=false;}
        else if(result!=group)return false;
    }
    return true;
}

std::optional<VSObjectSelection::Publication> VSObjectSelection::properties(const VSWorldScene::ObjectHandle& object)const
{
    if(!mImpl->current(object))return {};
    for(const auto& node:mImpl->nodes)if(sameObject(node.object,object))return node.pending ? std::nullopt : node.publication;
    return {};
}
std::function<bool()> VSObjectSelection::selectionGuard()const
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    const auto account=owner ? owner->accountGuard() : std::function<bool()>([]{return false;});
    const auto revision=state->revision;
    return [weak=std::weak_ptr<Impl>(state),account,revision]
    {
        const auto state=weak.lock();
        return state && account() && state->revision==revision && !state->nodes.empty() &&
            std::all_of(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return state->current(node.object);});
    };
}
boost::signals2::connection VSObjectSelection::changed(const std::function<void()>& callback)
{return mImpl->changed.connect(callback);}
bool VSObjectSelection::requestFamily(const VSWorldScene::ObjectHandle& object,U32 flags,FamilyCallback callback)
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    auto* world=scene();
    if(!owner || !world || !gMessageSystem || !callback || !state->current(object))return false;
    auto reservation=world->reserveSpatialBytes(sizeof(Impl::Request));
    if(!reservation)return false;
    const auto account=owner->accountGuard();
    const auto guard=world->guard(object);
    const bool pending=std::any_of(state->families.begin(),state->families.end(),[&](const auto& request)
    {return sameObject(request.object,object) && request.flags==flags && request.current();});
    state->families.push_back({object,flags,[account,guard]{return account() && guard();},std::move(callback),
        LLFrameTimer::getTotalSeconds()+60,std::move(reservation)});
    if(!pending)
    {
        const auto identity=owner->account();
        gMessageSystem->newMessage("RequestObjectPropertiesFamily");
        gMessageSystem->nextBlock("AgentData");
        gMessageSystem->addUUID("AgentID",identity.agent);
        gMessageSystem->addUUID("SessionID",identity.session);
        gMessageSystem->nextBlock("ObjectData");
        gMessageSystem->addU32("RequestFlags",flags);
        gMessageSystem->addUUID("ObjectID",object.id);
        gMessageSystem->sendReliable(object.host);
    }
    return true;
}
bool VSObjectSelection::beginHighlightRequest(std::function<bool()> current)
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    if(!owner || owner->phase()!=VSNativeSession::Phase::Connected || !current)return false;
    const auto account=owner->accountGuard();
    const auto previous=state->highlightRevision;
    if(!account() || !current() || state->highlightRevision!=previous)return false;
    state->highlights.clear();
    state->highlightCurrent=[account,current=std::move(current)]{return account() && current();};
    const auto request=state->highlightCurrent;
    const auto revision=++state->highlightRevision;
    state->changed();
    return state->highlightRevision==revision && request() && state->highlightRevision==revision;
}

bool VSObjectSelection::highlight(const std::vector<VSWorldScene::ObjectHandle>& objects,bool replace)
{
    const auto state=mImpl;
    auto* world=scene();
    const auto request=state->highlightCurrent;
    const auto revision=state->highlightRevision;
    if(!world || !request || !request() || state->highlightRevision!=revision)return false;
    std::vector<Impl::Highlight> next=replace ? std::vector<Impl::Highlight>() : state->highlights;
    for(const auto& object:objects)
    {
        if(!state->current(object))return false;
        if(std::any_of(next.begin(),next.end(),[&](const auto& node){return sameObject(node.object,object);}))continue;
        auto reservation=world->reserveSpatialBytes(sizeof(Impl::Highlight));
        if(!reservation)return false;
        next.push_back({object,std::move(reservation)});
    }
    state->highlights=std::move(next);
    state->changed();
    return true;
}
std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::highlights()const
{
    const auto state=mImpl;
    std::vector<VSWorldScene::ObjectHandle> result;
    const auto request=state->highlightCurrent;
    const auto revision=state->highlightRevision;
    if(request && request() && state->highlightRevision==revision)
        for(const auto& node:state->highlights)if(state->current(node.object))result.push_back(node.object);
    return result;
}
void VSObjectSelection::clearHighlights()
{
    const auto state=mImpl;
    state->highlights.clear();state->highlightCurrent={};++state->highlightRevision;
    state->changed();
}
void VSObjectSelection::receive(LLMessageSystem& message)
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    const std::string name=message.getMessageName();
    if(!owner || !owner->admit(name,message.getSender()))return;
    const auto account=owner->accountGuard();
    if(name=="ForceObjectSelect")
    {
        auto* world=scene();
        const auto request=state->highlightCurrent;
        const auto revision=state->highlightRevision;
        if(!world || !request || !request() || state->highlightRevision!=revision)return;
        bool replace=false;message.getBOOL("Header","ResetList",replace);
        const S32 count=message.getNumberOfBlocks("Data");
        if(count<0 || count>255)return;
        std::vector<VSWorldScene::ObjectHandle> objects;
        for(S32 block=0;block<count;++block)
        {
            S32 local=0;message.getS32("Data","LocalID",local,block);
            if(auto object=world->object(message.getSender(),U32(local)))objects.push_back(*object);
        }
        highlight(objects,replace);
    }
    else if(name=="ObjectProperties")
    {
        const S32 count=message.getNumberOfBlocks("ObjectData");
        if(count<0 || count>255)return;
        bool changed=false;
        for(S32 block=0;block<count && account();++block)
        {
            LLUUID id;message.getUUID("ObjectData","ObjectID",id,block);
            const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node)
            {return node.object.id==id && node.object.host==message.getSender() && state->current(node.object);});
            if(found==state->nodes.end())continue;
            auto decoded=vs_read_object_properties(message,block);
            if(!decoded)continue;
            const bool saveMaterials = found->pending || !found->publication || found->publication->properties->inv_serial != decoded->inv_serial;
            const S16 previousSerial = found->publication ? found->publication->properties->inv_serial : 0;
            const S16 receivedSerial = decoded->inv_serial;
            const auto object = found->object;
            auto publication=state->publish(found->object,std::move(*decoded));
            if(!publication)
            {
                found->pending=true;
                found->retryAt=LLFrameTimer::getTotalSeconds()+5;
                changed=true;
                continue;
            }
            found->publication=std::move(publication);
            found->pending=false;
            if (saveMaterials && captureSavedMaterials(object,found->publication->properties))
            {
                // Authenticated rollback is recorded before the independent BP
                // display cache, matching the shared ObjectProperties sequence.
                if(gSavedSettings.getBOOL("FSShowSelectedInBlinnPhong"))hideGLTFMaterial();
                else showGLTFMaterial();
            }

            changed=true;
            if (previousSerial != receivedSerial)
                if (auto* inventory = owner->taskInventory(); inventory && inventory->state(object).serial != receivedSerial)
                    inventory->dirty(object);

        }
        if(changed && account())state->changed();
    }
    else if(name=="ObjectPropertiesFamily")
    {
        auto decoded=vs_read_object_properties_family(message);
        std::vector<Impl::Request> ready;
        for(auto request=state->families.begin();request!=state->families.end();)
            if(request->object.id==decoded.id && request->object.host==message.getSender() && request->flags==decoded.request_flags)
            {ready.push_back(std::move(*request));request=state->families.erase(request);}
            else ++request;
        for(const auto& request:ready)
            if(account() && request.current())request.callback(state->publish(request.object,decoded));
    }
}
void VSObjectSelection::tick()
{
    const auto state=mImpl;
    const auto size=state->nodes.size();
    state->nodes.erase(std::remove_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node)
        {return !state->current(node.object);}),state->nodes.end());
    const auto rectangleSize=state->rectangleHits.size()+state->rectangleNodes.size();
    state->rectangleHits.erase(std::remove_if(state->rectangleHits.begin(),state->rectangleHits.end(),[&](const auto& node)
        {return !state->current(node.object);}),state->rectangleHits.end());
    state->rectangleNodes.erase(std::remove_if(state->rectangleNodes.begin(),state->rectangleNodes.end(),[&](const auto& node)
        {return !state->current(node.object);}),state->rectangleNodes.end());
    const bool rectangleChanged=rectangleSize!=state->rectangleHits.size()+state->rectangleNodes.size();
    if(rectangleChanged)++state->rectangleRevision;
    bool highlightsChanged=rectangleChanged;
    const auto highlightRequest=state->highlightCurrent;
    const auto highlightRevision=state->highlightRevision;
    const bool highlightActive=highlightRequest && highlightRequest();
    if(state->highlightRevision!=highlightRevision)return;
    if(!highlightActive)
    {
        highlightsChanged=highlightsChanged || !state->highlights.empty();
        state->highlights.clear();state->highlightCurrent={};++state->highlightRevision;
    }
    else
    {
        const auto count=state->highlights.size();
        state->highlights.erase(std::remove_if(state->highlights.begin(),state->highlights.end(),[&](const auto& node)
            {return !state->current(node.object);}),state->highlights.end());
        highlightsChanged=highlightsChanged || count!=state->highlights.size();
    }
    if(state->hoverRoot && !state->current(*state->hoverRoot))
    {
        state->hover.clear();state->hoverRoot.reset();state->hoverPrimary.reset();++state->hoverRevision;
    }
    else if(state->hoverRoot)
    {
        const auto found=std::find_if(state->hover.begin(),state->hover.end(),[&](const auto& node){return sameObject(node.object,*state->hoverRoot);});
        if(found!=state->hover.end() && found->pending && found->retryAt<=LLFrameTimer::getTotalSeconds())requestHoverProperties();
    }
    std::vector<Impl::Request> expired;
    const auto now=LLFrameTimer::getTotalSeconds();
    std::vector<VSWorldScene::ObjectHandle> retry;
    for(auto& node:state->nodes)
        if(node.pending && node.retryAt<=now)
        {node.retryAt=now+5;retry.push_back(node.object);}
    state->sendSelection("ObjectSelect",retry);
    for(auto request=state->families.begin();request!=state->families.end();)
        if(!request->current() || request->deadline<now)
        {expired.push_back(std::move(*request));request=state->families.erase(request);}
        else ++request;
    if(size!=state->nodes.size()){++state->revision;state->changed();}
    else if(highlightsChanged)state->changed();
    for(const auto& request:expired)if(request.current())request.callback({});
}
void VSObjectSelection::reset()
{
    const auto state=mImpl;
    if(state->retired)return;
    state->retired=true;
    if(LLSelectMgr::instanceExists() && LLEditMenuHandler::gEditMenuHandler==LLSelectMgr::getInstance())LLEditMenuHandler::gEditMenuHandler=nullptr;
    if(auto* world=scene())for(const auto& node:state->nodes)world->setSelectionState(node.object,false);
    state->hover.clear();state->hoverRoot.reset();state->hoverPrimary.reset();state->primary.reset();++state->hoverRevision;
    state->pauses.clear();state->geometryKnown=false;state->showSelection=false;state->selectionCenter.clearVec();state->bounds=LLBBox();
    state->avatarOverrides.clear();
    state->gridObjects.clear();
    state->rectangleHits.clear();state->rectangleNodes.clear();++state->rectangleRevision;
    state->selectionLease.reset();
    state->nodes.clear();state->families.clear();state->highlights.clear();state->highlightCurrent={};++state->highlightRevision;++state->revision;
}

std::optional<VSObjectSelection::NodeMetadata> VSObjectSelection::node(const VSWorldScene::ObjectHandle& object) const
{
    const auto state=mImpl;
    if(!state->current(object))return {};
    const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(found==state->nodes.end())return {};
    auto result=found->metadata;
    result.primary=state->primary && sameObject(*state->primary,object);
    return result;
}
bool VSObjectSelection::contains(const VSWorldScene::ObjectHandle& object,S32 face) const
{
    const auto selected=node(object);
    auto* world=scene();
    if(!selected || !world)return false;
    const auto faces=world->faceCount(object);
    if(!faces)return false;
    if(face==-1)
    {
        if(selected->faceMask==0xffffffffu)return true;
        // The reference's zero-face object has all of its zero faces selected.
        for(U32 index=0;index<*faces;++index)
            if(index>=32 || !(selected->faceMask&(1u<<index)))return false;
        return true;
    }
    return face>=0 && U32(face)<*faces && face<32 && (selected->faceMask&(1u<<face));
}
std::optional<VSObjectSelection::Publication> VSObjectSelection::findProperties(const VSWorldScene::ObjectHandle& object) const
{
    // No hover fallback: LLSelectMgr::findObjectPermissions searches valid
    // selected nodes only. UI hover metadata never widens media permissions.
    return properties(object);
}
void VSObjectSelection::convertTransient()
{
    const auto state=mImpl;
    bool any=false;
    for(auto& node:state->nodes)if(node.metadata.transient){node.metadata.transient=false;any=true;}
    if(any){++state->revision;state->changed();}
}
void VSObjectSelection::setTransient(const VSWorldScene::ObjectHandle& object,bool transient)
{
    const auto state=mImpl;
    if(!state->current(object))return;
    const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(found!=state->nodes.end() && found->metadata.transient!=transient)
    {found->metadata.transient=transient;++state->revision;state->changed();}
}

bool VSObjectSelection::selectOnly(const VSWorldScene::ObjectHandle& object,S32 face,S32 gltfNode,S32 gltfPrimitive)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world || !state->current(object))return false;
    const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(found!=state->nodes.end())
    {
        // Original selectObjectOnly does not change TE selection or motion for
        // an existing node. selectGLTFNode ignores a negative node index.
        state->primary=object;
        if(gltfNode>=0){found->metadata.gltfNode=gltfNode;found->metadata.gltfPrimitive=gltfPrimitive;}
        ++state->revision;
        bindEditMenuHandler();refreshSelectionGeometry();updateAttention();state->changed();
        return true;
    }
    if(!canSelect(object,state->forceSelection))return false;
    const auto faces=world->faceCount(object);
    if(!faces || (*faces>0 && face!=-1 && (face<0 || face>=32)))return false;
    auto reservation=world->reserveSpatialBytes(sizeof(Impl::Node));
    if(!reservation)return false;
    auto next=state->nodes;
    Impl::Node node{object,std::move(reservation),{},true,LLFrameTimer::getTotalSeconds()+5};
    node.metadata.individual=true;
    node.metadata.faceMask=*faces?face==-1?0xffffffffu:1u<<face:0;
    node.metadata.lastFace=face==-1?0:face;
    if(gltfNode>=0){node.metadata.gltfNode=gltfNode;node.metadata.gltfPrimitive=gltfPrimitive;}
    next.insert(next.begin(),std::move(node));
    // All allocations and identity checks precede the first scene mutation.
    if(!state->current(object) || !world->setSelectionState(object,true))return false;
    world->initializeSelectionMotion(object);
    state->nodes=std::move(next);
    state->primary=object;
    ++state->revision;
    const auto revision=state->revision;
    state->sendSelection("ObjectSelect",{object});
    if(!state->current(object) || state->revision!=revision)return false;
    bindEditMenuHandler();refreshSelectionGeometry();updateAttention();state->changed();
    return state->current(object);
}
bool VSObjectSelection::selectFamily(const VSWorldScene::ObjectHandle& object,bool addToEnd,bool ignoreSelectOwned)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world || !state->current(object))return false;
    if(node(object))
    {
        state->primary=object;
        ++state->revision;
        bindEditMenuHandler();refreshSelectionGeometry();updateAttention();state->changed();
        return true;
    }
    if(!canSelect(object,state->forceSelection,ignoreSelectOwned))return false;
    const auto family=world->linkedFamily(object,true,true);
    if(!family || family->empty())return false;
    const auto owner=state->session.lock();
    if(!owner)return false;
    const auto account=owner->account();
    auto next=state->nodes;
    std::vector<VSWorldScene::ObjectHandle> added;
    for(const auto& member:*family)
    {
        if(!state->current(member))return false;
        if(member.id==account.agent && !state->allowSelectAvatar)continue;
        auto existing=std::find_if(next.begin(),next.end(),[&](const auto& node){return sameObject(node.object,member);});
        if(existing!=next.end())
        {
            existing->metadata.transient=false;
            continue;
        }
        const auto faces=world->faceCount(member);
        if(!faces)return false;
        auto residency=world->reserveSpatialBytes(sizeof(Impl::Node));
        if(!residency)return false;
        Impl::Node candidate{member,std::move(residency),{},true,LLFrameTimer::getTotalSeconds()+5};
        candidate.metadata.faceMask=*faces?0xffffffffu:0;
        if(addToEnd)next.push_back(std::move(candidate));
        else next.insert(next.begin(),std::move(candidate));
        added.push_back(member);
    }
    // Reference addNode inserts each DFS element at the front. Preserve that
    // reverse-DFS node/packet order; add_to_end keeps forward DFS instead.
    for(const auto& member:added)
        if(!state->current(member))return false;
    for(const auto& member:added)world->setSelectionState(member,true);
    state->nodes=std::move(next);
    state->primary=object;
    ++state->revision;
    const auto committed=state->revision;
    std::vector<VSWorldScene::ObjectHandle> all;
    for(const auto& selected:state->nodes)all.push_back(selected.object);
    state->sendSelection("ObjectSelect",all);
    if(state->revision!=committed || !owner->current(account))return false;
    world->initializeSelectionMotion(family->front());
    if(gSavedSettings.getBOOL("EditLinkedParts"))
    {
        gSavedSettings.setBOOL("EditLinkedParts",false);
        if(state->retired || !owner->current(account))return false;
        promoteSelectionToRoot();
        return !state->retired && owner->current(account);
    }
    if(state->revision!=committed || !owner->current(account))return false;
    bindEditMenuHandler();refreshSelectionGeometry();updateAttention();state->changed();
    return owner->current(account);
}
bool VSObjectSelection::deselectOnly(const VSWorldScene::ObjectHandle& object,bool sendToSimulator)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world || !state->current(object))return false;
    const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(found==state->nodes.end())return true;
    const auto revision=state->revision;
    world->initializeDeselectionMotion(object);
    if(sendToSimulator)state->sendSelection("ObjectDeselect",{object});
    // Sending may synchronously retire the account or replace its model.
    if(!state->current(object) || state->revision!=revision)return false;
    const auto current=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(current==state->nodes.end())return true;
    world->setSelectionState(object,false);
    state->nodes.erase(current);
    ++state->revision;
    refreshSelectionGeometry();updateAttention();state->changed();
    return true;
}
bool VSObjectSelection::deselectAll()
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world)return false;
    if(state->nodes.empty())return true;
    const auto owner=state->session.lock();
    if(!owner)return false;
    const auto account=owner->account();
    const auto revision=state->revision;
    std::vector<VSWorldScene::ObjectHandle> all;
    for(const auto& node:state->nodes)
        if(state->current(node.object))
        {
            world->initializeDeselectionMotion(node.object);
            all.push_back(node.object);
        }
    state->sendSelection("ObjectDeselect",all);
    if(!owner->current(account) || state->revision!=revision)return false;
    for(const auto& object:all)world->setSelectionState(object,false);
    state->nodes.clear();
    state->primary.reset();
    ++state->revision;
    refreshSelectionGeometry();updateAttention();state->changed();
    return owner->current(account);
}
std::shared_ptr<const void> VSObjectSelection::retainSelection() const
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    return !state->retired && owner && owner->phase()==VSNativeSession::Phase::Connected?state->selectionLease:std::shared_ptr<const void>();
}
bool VSObjectSelection::deselectFamily(const VSWorldScene::ObjectHandle& object,bool sendToSimulator,bool includeEntireObject)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world || !state->current(object))return false;
    if(!node(object))return true;
    // Reference false uses getRoot (including an avatar parent); true uses
    // getRootEdit (stopping before the avatar), then both traverse AllChildren.
    const auto root=world->rootObject(object,includeEntireObject);
    if(!root)return false;
    const auto family=world->linkedFamily(*root,false,true,VSWorldScene::FamilyTraversal::AllChildren);
    if(!family)return false;
    const auto owner=state->session.lock();
    if(!owner)return false;
    const auto account=owner->account();
    for(const auto& member:*family)
    {
        const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,member);});
        if(found==state->nodes.end())continue;
        world->setSelectionState(member,false);
        state->nodes.erase(found);
    }
    ++state->revision;
    const auto revision=state->revision;
    // Reference remove() refreshes center/dialogs before transmitting; its
    // send_to_sim=false path does not zero velocities or send extra packets.
    refreshSelectionGeometry();updateAttention();state->changed();
    if(!owner->current(account) || state->revision!=revision)return false;
    if(!sendToSimulator)return true;
    for(const auto& member:*family)world->initializeDeselectionMotion(member);
    state->sendSelection("ObjectDeselect",*family);
    return owner->current(account);
}
bool VSObjectSelection::setForceSelection(bool value)
{
    std::swap(mImpl->forceSelection,value);
    return value;
}
bool VSObjectSelection::setAllowSelectAvatar(bool value)
{
    std::swap(mImpl->allowSelectAvatar,value);
    return value;
}
bool VSObjectSelection::allowSelectAvatar() const{return mImpl->allowSelectAvatar;}
void VSObjectSelection::validateSelection()
{
    const auto objects=selected();
    for(const auto& object:objects)if(!canSelect(object))deselectOnly(object);
}
void VSObjectSelection::promoteSelectionToRoot()
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    auto* world=scene();
    if(!owner || !world || state->retired)return;
    const auto lifetime=owner->retainObjectSelection();
    const auto account=owner->accountGuard();
    bool changed=false;
    std::vector<VSWorldScene::ObjectHandle> roots;
    for(const auto& node:state->nodes)
    {
        changed|=node.metadata.individual;
        const auto root=world->rootObject(node.object,true);
        if(!root)return;
        if(std::none_of(roots.begin(),roots.end(),[&](const auto& old){return sameObject(old,*root);}))roots.push_back(*root);
    }
    if(!changed)return;
    if(!deselectAll())return;
    for(const auto& root:roots)
    {
        if(!account() || state->retired)return;
        selectFamily(root);
    }
}
void VSObjectSelection::demoteSelectionToIndividuals()
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    auto* world=scene();
    if(!owner || !world || state->retired)return;
    const auto lifetime=owner->retainObjectSelection();
    const auto account=owner->accountGuard();
    std::vector<VSWorldScene::ObjectHandle> objects;
    for(const auto& node:state->nodes)
    {
        if(node.metadata.individual)continue;
        const auto root=world->rootObject(node.object,true);
        if(!root || !sameObject(*root,node.object))continue;
        const auto family=world->linkedFamily(node.object,false,true);
        if(!family)return;
        objects.insert(objects.end(),family->begin(),family->end());
    }
    if(objects.empty() || !deselectAll())return;
    for(const auto& object:objects)
    {
        if(!account() || state->retired)return;
        selectOnly(object);
    }
}

bool VSObjectSelection::setHoverObject(const std::optional<VSWorldScene::ObjectHandle>& object,S32 face)
{
    const auto state=mImpl;
    auto* world=scene();
    const auto owner=state->session.lock();
    if(!world || !owner)return false;
    const auto identity=owner->account();
    const auto row=object && state->current(*object)?world->objectAdmission(*object):std::nullopt;
    if(!object || !row || object->id==identity.agent || row->pcode==LLViewerObject::LL_VO_SURFACE_PATCH)
    {
        state->hover.clear();state->hoverRoot.reset();state->hoverPrimary.reset();
        ++state->hoverRevision;
        return !object || bool(row);
    }
    const auto root=world->rootObject(*object,true);
    if(!root)return false;
    if(state->hoverRoot && sameObject(*state->hoverRoot,*root))
    {
        state->hoverPrimary=*object;
        return true;
    }
    const auto family=world->linkedFamily(*root,false,true);
    if(!family)return false;
    std::vector<Impl::Node> next;
    for(const auto& member:*family)
    {
        if(!state->current(member))continue;
        auto residency=world->reserveSpatialBytes(sizeof(Impl::Node));
        if(!residency)return false;
        Impl::Node candidate{member,std::move(residency),{},true,0};
        // Unlike full selection, the original hover controller calls selectTE
        // even for -1, which is ignored rather than selecting all faces.
        if(face>=0 && face<32)
        {
            candidate.metadata.faceMask=1u<<face;
            candidate.metadata.lastFace=face;
        }
        next.push_back(std::move(candidate));
    }
    state->hover=std::move(next);
    state->hoverRoot=*root;
    state->hoverPrimary=*object;
    ++state->hoverRevision;
    requestHoverProperties();
    return true;
}
void VSObjectSelection::requestHoverProperties()
{
    const auto state=mImpl;
    if(!state->hoverRoot || !state->current(*state->hoverRoot))return;
    const auto root=*state->hoverRoot;
    const auto revision=state->hoverRevision;
    const auto weak=std::weak_ptr<Impl>(state);
    for(auto& node:state->hover)
        if(sameObject(node.object,root))node.retryAt=LLFrameTimer::getTotalSeconds()+5;
    requestFamily(root,0,[weak,root,revision](const std::optional<Publication>& publication)
    {
        const auto state=weak.lock();
        if(!state || state->hoverRevision!=revision || !state->current(root))return;
        const auto found=std::find_if(state->hover.begin(),state->hover.end(),[&](const auto& node){return sameObject(node.object,root);});
        if(found==state->hover.end())return;
        found->pending=!publication;
        found->publication=publication;
        found->retryAt=LLFrameTimer::getTotalSeconds()+5;
        state->changed();
    });
}
std::optional<VSObjectSelection::Publication> VSObjectSelection::hoverProperties(const VSWorldScene::ObjectHandle& object) const
{
    const auto state=mImpl;
    if(!state->current(object))return {};
    const auto found=std::find_if(state->hover.begin(),state->hover.end(),[&](const auto& node){return sameObject(node.object,object);});
    return found==state->hover.end() || found->pending?std::nullopt:found->publication;
}

void VSObjectSelection::refreshSelectionGeometry()
{
    const auto state=mImpl;
    auto* world=scene();
    state->pauses.clear();
    state->geometryKnown=false;
    state->showSelection=false;
    state->selectionCenter.clearVec();
    state->bounds=LLBBox();
    if(!world)return;
    const auto objects=selected();
    if(objects.empty())
    {
        if(gAgentCamera.mHUDTargetZoom!=1.f)
        {gAgentCamera.mHUDTargetZoom=1.f;gAgentCamera.mHUDCurZoom=1.f;}
        state->geometryKnown=true;
        return;
    }
    const auto type=selectionType();
    if(!type.type)return;
    if(*type.type!=SelectionType::HUD && world->object(gAgent.getID()) && gAgentCamera.mHUDTargetZoom!=1.f)
    {gAgentCamera.mHUDTargetZoom=1.f;gAgentCamera.mHUDCurZoom=1.f;}
    auto firstRoot=objects.front();
    for(const auto& node:state->nodes)
    {
        if(node.metadata.individual || !state->current(node.object))continue;
        const auto root=world->rootObject(node.object,true);
        if(root && sameObject(*root,node.object)){firstRoot=node.object;break;}
    }
    const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    const auto rootFocus=world->focusObject(firstRoot,origin);
    if(!rootFocus || !rootFocus->current())return;
    LLBBox bounds;
    bounds.addBBoxAgent(rootFocus->bounds());
    bool show=false;
    for(const auto& object:objects)
    {
        const auto focus=world->focusObject(object,origin);
        if(!focus || !focus->current())return;
        const auto root=world->rootObject(object,true);
        if(!root)return;
        const auto children=world->directChildren(*root);
        if(!children)return;
        const bool seat=std::any_of(children->begin(),children->end(),[](const auto& child){return child.id==gAgent.getID();});
        if(*type.type==SelectionType::World && !seat && !focus->avatar())show=true;
        bounds.addBBoxAgent(focus->bounds());
    }
    // Reference computes one oriented union initialized by the first root's
    // official bbox. It never averages object origins or axis-aligns the set.
    state->bounds=bounds;
    state->selectionCenter=gAgent.getPosGlobalFromAgent(bounds.getCenterAgent());
    state->showSelection=show;
    state->geometryKnown=true;
    for(const auto& object:objects)
    {
        const auto row=world->objectAdmission(object);
        if(!row)return;
        if(row->attachment && !row->hud)
        {
            const auto root=world->rootObject(object,false);
            if(root)
            {
                const auto avatar=world->avatarForObject(root->id);
                if(avatar)state->pauses.push_back(avatar->character().requestPause());
            }
        }
        const auto focus=world->focusObject(object,origin);
        if(focus && focus->animated() && focus->controlAvatar())
            if(const auto avatar=world->avatarForObject(object.id))state->pauses.push_back(avatar->character().requestPause());
    }
}
std::optional<LLBBox> VSObjectSelection::boundingBox()const
{
    return mImpl->geometryKnown?std::optional<LLBBox>(mImpl->bounds):std::nullopt;
}
LLVector3d VSObjectSelection::centerGlobal()const{return mImpl->selectionCenter;}
bool VSObjectSelection::showSelection()const{return mImpl->geometryKnown && mImpl->showSelection;}
void VSObjectSelection::updateAttention()
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    if(!owner || !state->geometryKnown)return;
    auto* attention=owner->objectAttention();
    if(!attention)return;
    auto* world=scene();
    if(!world)return;
    if(!state->showSelection || selected().empty())
    {
        attention->point(POINTAT_TARGET_CLEAR);
        attention->look(LOOKAT_TARGET_CLEAR);
        return;
    }
    const auto& pick=gViewerWindow->getLastPick();
    auto object=world->object(pick.mObjectID);
    LLVector3 offset;
    if(object && node(*object))
    {
        const auto row=world->objectAdmission(*object);
        const bool wasHUD=pick.mPickHUD && row && !row->hud;
        const auto focus=world->focusObject(*object,gAgent.getPosGlobalFromAgent(LLVector3::zero));
        if(!wasHUD && focus && focus->current())
        {
            offset.setVec(pick.mObjectOffset);
            offset.rotVec(~focus->rotation());
        }
        else object.reset();
    }
    else object.reset();
    if(!object)object=selected().front();
    attention->point(POINTAT_TARGET_SELECT,object,offset);
    attention->look(LOOKAT_TARGET_SELECT,object,offset);
}

void VSObjectSelection::sendSelect()
{
    // Resubscribe the genuine current selected simulator objects. The shared
    // wire producer rechecks exact current account/circuit/object identity.
    const auto state=mImpl;
    state->sendSelection("ObjectSelect",selected());
}

bool VSObjectSelection::allowOperationOnNode(const VSWorldScene::ObjectHandle& token,
                                            PermissionBit operation,U64 groupProxyPower) const
{
    const auto publication=findProperties(token);
    if (!publication || !publication->properties) return false;
    const auto source=VSObjectContext::resolve(token.id);
    struct Peer
    {
        VSObjectContext source;
        bool isDead() const { return !source.current || !source.current(); }
        bool isAttachment() const { return source.attachment; }
        bool permTransfer() const
        {
            // Reference aggregate transfer permission comes from root-edit,
            // including its developer god-mode policy, evaluated only on demand.
#ifdef HACKED_GODLIKE_VIEWER
            return true;
#else
# ifdef TOGGLE_HACKED_GODLIKE_VIEWER
            if (LLGridManager::instance().isInSLBeta() && gAgent.getGodLevel()>=GOD_MAINTENANCE) return true;
# endif
            const auto root=VSObjectContext::resolve(source.rootEditID);
            return root && root->current() && (root->flags & FLAGS_OBJECT_TRANSFER)!=0;
#endif
        }
    };
    const Peer object{source.value_or(VSObjectContext{})};
    const LLPermissions permissions=publication->properties->permissions();
    return vs_selected_allow_operation(source ? &object : nullptr,&permissions,operation,groupProxyPower,gAgent);
}

std::optional<VSWorldScene::ObjectHandle> VSObjectSelection::primaryObject() const
{
    const auto state=mImpl;
    for(const auto& node:state->nodes)
        if(node.metadata.primary && state->current(node.object))return node.object;
    return {};
}

std::shared_ptr<VSObjectSelection::SavedMaterials> VSObjectSelection::savedMaterials(const VSWorldScene::ObjectHandle& object) const
{
    if (!mImpl->current(object)) return {};
    const auto found = std::find_if(mImpl->nodes.begin(), mImpl->nodes.end(),
        [&](const auto& node) { return sameObject(node.object, object); });
    return found != mImpl->nodes.end() ? found->materials : nullptr;
}

boost::signals2::connection VSObjectSelection::materialsChanged(const std::function<void(const LLUUID&, S32)>& callback)
{
    return mImpl->materialChanged.connect(callback);
}

void VSObjectSelection::updateSelectionCenter()
{refreshSelectionGeometry();updateAttention();}

bool VSObjectSelection::selectGetAllValid() const
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    if(state->retired || !owner || owner->phase()!=VSNativeSession::Phase::Connected)return false;
    for(const auto& node:state->nodes)
        if(!state->current(node.object) || node.pending || !node.publication)return false;
    return true;
}

void VSObjectSelection::sendMultipleUpdate(U32 type)
{
    if(type==UPD_NONE)return;
    const auto state=mImpl;const auto owner=state->session.lock();auto* world=scene();
    if(!owner || !world || !gMessageSystem)return;
    const auto account=owner->account();const auto selection=selectionGuard();
    const bool rootsOnly=!gSavedSettings.getBOOL("EditLinkedParts") && !LLSelectMgr::getInstance()->getTEMode();
    if(rootsOnly)type|=UPD_LINKED_SETS;
    // Preserve the original roots-first sequence, including the non-individual
    // root filter in SEND_ONLY_ROOTS. Never group/reorder across simulator hosts.
    std::vector<VSWorldScene::ObjectHandle> ordered;
    for(unsigned pass=0;pass<(rootsOnly?1u:2u);++pass)
        for(const auto& node:state->nodes)
        {
            if(!state->current(node.object))continue;
            const auto root=world->objectIsRootEdit(node.object);if(!root)continue;
            const bool isRoot=*root;
            if(rootsOnly?(!node.metadata.individual&&isRoot):(pass==0?isRoot:!isRoot))ordered.push_back(node.object);
        }
    LLHost host;S32 count=0;LLMessageSystem* system=gMessageSystem;auto& messages=*system;
    for(const auto& token:ordered)
    {
        if(!owner->current(account) || !selection()){if(count)messages.clearMessage();return;}
        const auto transform=world->objectLocalTransform(token);if(!transform)continue;
        if(count && (host!=token.host || messages.isSendFull(nullptr) || count>=254))
        {
            messages.sendReliable(host);count=0;
            if(!owner->current(account) || !selection())return;
        }
        if(!count)
        {
            host=token.host;messages.newMessageFast(_PREHASH_MultipleObjectUpdate);
            messages.nextBlockFast(_PREHASH_AgentData);messages.addUUIDFast(_PREHASH_AgentID,account.agent);messages.addUUIDFast(_PREHASH_SessionID,account.session);
        }
        struct Object
        {
            const VSWorldScene::LocalTransform& value;U32 local;
            U32 localID()const{return local;}
            const LLVector3& position()const{return value.position;}
            LLQuaternion rotation()const{return value.rotation;}
            const LLVector3& scale()const{return value.scale;}
        }object{*transform,token.local};
        vs_pack_multiple_update(messages,object,U8(type));++count;
    }
    if(count)
    {
        if(owner->current(account) && selection())messages.sendReliable(host);
        else messages.clearMessage();
    }
}

std::shared_ptr<VSObjectSelection::SavedTransform> VSObjectSelection::savedTransform(const VSWorldScene::ObjectHandle& token) const
{
    const auto state=mImpl;if(!state->current(token))return {};
    for(auto& node:state->nodes)
        if(sameObject(node.object,token))
        {
            if(node.transform)return node.transform;
            auto* world=scene();if(!world)return {};
            auto residency=world->reserveSpatialBytes(sizeof(SavedTransform)+64);if(!residency)return {};
            auto value=std::make_shared<SavedTransform>();value->residency=std::move(residency);node.transform=value;return value;
        }
    return {};
}

void VSObjectSelection::sendDetach()
{
    const auto state = mImpl;
    auto* world = scene();
    const auto type = selectionType();
    if (!world || !type.selected || !type.type || *type.type == SelectionType::World) return;
    std::vector<VSWorldScene::ObjectHandle> roots;
    const auto attachments = world->attachments();
    for (const auto& node : state->nodes)
    {
        if (node.metadata.individual || !state->current(node.object)) continue;
        const auto root = world->rootObject(node.object, true);
        if (!root || !sameObject(*root, node.object)) continue;
        const auto attachment = std::find_if(attachments.begin(), attachments.end(),
            [&](const auto& entry) { return sameObject(entry.object, *root); });
        if (attachment == attachments.end()) continue;
        // Same whole-selection RLVa veto as RlvSelectHasLockedAttach. Never
        // partially detach a family when one selected root is locked.
        if (rlv_handler_t::isEnabled() && gRlvAttachmentLocks.hasLockedAttachmentPoint(RLV_LOCK_REMOVE) &&
            (gRlvAttachmentLocks.isLockedAttachmentPoint(attachment->point, RLV_LOCK_REMOVE) ||
             gRlvAttachmentLocks.getAttachObjLocks().count(root->id))) return;
        roots.push_back(*root);
    }
    state->sendSelection("ObjectDetach", roots);
    for (const auto& root : roots)
    {
        if (!state->current(root)) continue;
        const auto item = world->objectInventoryItem(root);
        if (item) LLAttachmentsMgr::instance().onDetachRequested(*item);
    }
}

bool VSObjectSelection::saveUnselectedChildrenPosition(const VSWorldScene::ObjectHandle& token,std::vector<LLVector3>& positions)
{
    auto* world=scene();auto saved=savedTransform(token);if(!world || !saved)return false;
    if(!positions.empty())return true;
    auto children=world->directChildren(token);if(!children)return false;
    auto residency=world->reserveSpatialBytes(children->size()*(sizeof(VSWorldScene::ObjectHandle)+sizeof(LLVector3))+64);
    if(!residency)return false;
    std::vector<VSWorldScene::ObjectHandle> handles;std::vector<LLVector3> values;
    handles.reserve(children->size());values.reserve(children->size());
    for(const auto& child:*children)
    {
        if(node(child))continue;
        const auto pose=world->objectEditTransform(child);if(!pose)return false;
        handles.push_back(child);values.push_back(pose->position);
    }
    if(!mImpl->current(token))return false;
    saved->positionChildren=std::move(handles);saved->positionResidency=std::move(residency);positions=std::move(values);return true;
}
bool VSObjectSelection::saveUnselectedChildrenRotation(const VSWorldScene::ObjectHandle& token,std::vector<LLQuaternion>& rotations)
{
    auto* world=scene();auto saved=savedTransform(token);if(!world || !saved)return false;
    auto children=world->directChildren(token);if(!children)return false;
    auto residency=world->reserveSpatialBytes(children->size()*(sizeof(VSWorldScene::ObjectHandle)+sizeof(LLQuaternion))+64);
    if(!residency)return false;
    std::vector<VSWorldScene::ObjectHandle> handles;std::vector<LLQuaternion> values;
    handles.reserve(children->size());values.reserve(children->size());
    for(const auto& child:*children)
    {
        if(node(child))continue;
        const auto pose=world->objectEditTransform(child);if(!pose)return false;
        handles.push_back(child);values.push_back(pose->rotation);
    }
    if(!mImpl->current(token))return false;
    // The reference appends to the caller's rotation array. Keep that offset,
    // while child identities prevent a replacement local ID inheriting a drag.
    saved->rotationBase=rotations.size();rotations.insert(rotations.end(),values.begin(),values.end());
    saved->rotationChildren=std::move(handles);saved->rotationResidency=std::move(residency);return true;
}
bool VSObjectSelection::resetChildrenRotationAndPosition(const VSWorldScene::ObjectHandle& token,const std::vector<LLQuaternion>& rotations,const std::vector<LLVector3>& positions)
{
    auto* world=scene();auto saved=savedTransform(token);if(!world || !saved)return false;
    auto pose=world->objectEditTransform(token);auto children=world->directChildren(token);if(!pose || !children)return false;
    const LLQuaternion inverse=~pose->rotation;const LLVector3 offset=pose->position;
    struct Change{VSWorldScene::ObjectHandle token;VSWorldScene::LocalTransform pose;};std::vector<Change> changes;
    auto residency=world->reserveSpatialBytes(saved->positionChildren.size()*sizeof(Change)+64);if(!residency)return false;
    changes.reserve(saved->positionChildren.size());
    for(std::size_t index=0;index<saved->positionChildren.size();++index)
    {
        const auto& child=saved->positionChildren[index];if(node(child))continue;
        if(std::none_of(children->begin(),children->end(),[&](const auto& current){return sameObject(current,child);}))continue;
        auto current=world->objectLocalTransform(child);if(!current)continue;
        const auto rotation=std::find_if(saved->rotationChildren.begin(),saved->rotationChildren.end(),[&](const auto& current){return sameObject(current,child);});
        if(index>=positions.size() || rotation==saved->rotationChildren.end())return false;
        const auto rotationIndex=saved->rotationBase+std::size_t(rotation-saved->rotationChildren.begin());if(rotationIndex>=rotations.size())return false;
        current->position=(positions[index]-offset)*inverse;current->rotation=rotations[rotationIndex]*inverse;
        if(!current->position.isFinite() || !current->rotation.isFinite())return false;
        changes.push_back({child,*current});
    }
    if(!mImpl->current(token))return false;
    for(const auto& change:changes)if(!world->previewLocalTransform(change.token,change.pose,3))return false;
    return true;
}
bool VSObjectSelection::resetChildrenPosition(const VSWorldScene::ObjectHandle& token,const LLVector3& offset,bool simplified,bool skipAvatarChild)
{
    auto* world=scene();if(!world || !mImpl->current(token))return false;
    const auto pose=world->objectLocalTransform(token);const auto children=world->directChildren(token);if(!pose || !children)return false;
    LLQuaternion rotation=pose->rotation;
    if(!simplified)
    {
        // Native rendered attachment transforms include the actual joint's
        // world rotation, matching the reference attachment-point branch.
        const auto rendered=world->focusObject(token,gAgent.getPosGlobalFromAgent(LLVector3::zero));if(!rendered || !rendered->current())return false;
        rotation=rendered->renderRotation;
    }
    const LLVector3 childOffset=offset*~rotation;
    struct Change{VSWorldScene::ObjectHandle token;VSWorldScene::LocalTransform pose;};std::vector<Change> changes;
    auto residency=world->reserveSpatialBytes(children->size()*sizeof(Change)+64);if(!residency)return false;
    changes.reserve(children->size());
    for(const auto& child:*children)
    {
        if(node(child) || (skipAvatarChild && child.kind==VSWorldContracts::OwnerKind::Avatar))continue;
        auto current=world->objectLocalTransform(child);if(!current)return false;
        current->position+=childOffset;if(!current->position.isFinite())return false;changes.push_back({child,*current});
    }
    if(!mImpl->current(token))return false;
    for(const auto& change:changes)if(!world->previewLocalTransform(change.token,change.pose,1))return false;
    return true;
}

bool VSObjectSelection::rebuildObject(const VSWorldScene::ObjectHandle& token)
{
    auto* world=scene();return world && mImpl->current(token) && world->rebuildObject(token);
}

void VSObjectSelection::overrideObjectUpdates()
{
    const auto state=mImpl;auto* world=scene();if(!world || state->retired)return;
    for(const auto& node:state->nodes)
    {
        const auto& saved=node.transform;
        if(!saved || !state->current(node.object))continue;
        U32 fields=0;
        if(!saved->mLastPositionLocal.isExactlyZero())fields|=UPD_POSITION;
        if(saved->mLastRotation!=LLQuaternion())fields|=UPD_ROTATION;
        if(!saved->mLastScale.isExactlyZero())fields|=UPD_SCALE;
        if(!fields)continue;
        // Reuse the actual shared Build permission owner, retaining the
        // original permMove/permanent-enforced policy, including god flags.
        const auto edit=editNode(node.object);const auto* object=edit?edit->getObject():nullptr;
        if(!object || !object->permMove() || object->isPermanentEnforced())continue;
        auto pose=world->objectLocalTransform(node.object);if(!pose)continue;
        U32 changed=0;
        if((fields&UPD_POSITION) && pose->position!=saved->mLastPositionLocal){pose->position=saved->mLastPositionLocal;changed|=UPD_POSITION;}
        if((fields&UPD_ROTATION) && pose->rotation!=saved->mLastRotation){pose->rotation=saved->mLastRotation;changed|=UPD_ROTATION;}
        if((fields&UPD_SCALE) && pose->scale!=saved->mLastScale){pose->scale=saved->mLastScale;changed|=UPD_SCALE;}
        if(changed)world->previewLocalTransform(node.object,*pose,changed);
    }
}

bool VSObjectSelection::saveSelectedObjectTransform()
{
    const auto state=mImpl;auto* world=scene();if(!world || state->retired)return false;
    if(state->nodes.empty())return true;
    const auto current=selectionGuard();const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    struct Capture{VSWorldScene::ObjectHandle object;std::shared_ptr<SavedTransform> saved;VSWorldScene::EditorTransform pose;bool gltf=false;};
    auto residency=world->reserveSpatialBytes(state->nodes.size()*sizeof(Capture)+64);if(!residency)return false;
    std::vector<Capture> captures;captures.reserve(state->nodes.size());
    for(const auto& node:state->nodes)
    {
        if(!state->current(node.object))continue;
        const auto pose=world->editorTransform(node.object,node.metadata.gltfNode,origin);if(!pose)return false;
        auto saved=savedTransform(node.object);if(!saved)return false;
        captures.push_back({node.object,std::move(saved),*pose,node.metadata.gltfNode!=-1});
    }
    // Capture ratios before publishing scalar drag state. Refusal must prevent
    // the operation, rather than letting a scale reuse a previous drag's UVs.
    for(const auto& capture:captures)
        if(!capture.gltf && !vs_capture_selected_texture_ratios(*this,capture.object))return false;
    if(!current())return false;
    for(const auto& capture:captures)
    {
        auto& saved=*capture.saved;saved.mSavedPositionLocal=capture.pose.localPosition;
        saved.mSavedPositionGlobal=capture.pose.globalPosition;saved.mSavedRotation=capture.pose.rotation;saved.mSavedScale=capture.pose.scale;
        if(capture.gltf)saved.mLastMoveLocal.setZero();
    }
    state->savedSelectionBounds=state->bounds;return true;
}
std::optional<LLBBox> VSObjectSelection::savedSelectionBBox() const
{
    return mImpl->retired?std::nullopt:mImpl->savedSelectionBounds;
}

std::shared_ptr<VSEditNode> VSObjectSelection::editNode(const VSWorldScene::ObjectHandle& token)
{
    const auto state=mImpl;auto* world=scene();const auto owner=state->session.lock();
    if(!world || !owner || !state->current(token))return {};
    const auto selection=owner->retainObjectSelection();if(selection.get()!=this)return {};
    auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,token);});
    if(found==state->nodes.end())return {};
    const auto publication=properties(token);
    const auto fields=publication?publication->properties:std::shared_ptr<const VSObjectProperties>{};
    if(found->editProjection && found->editProjection->matches(fields,found->metadata.individual))return found->editProjection;
    const auto residency=world->reserveSpatialBytes(sizeof(VSEditNode)+(fields?fields->name.size()+fields->desc.size():0)+128);
    if(!residency)return {};
    auto node=std::make_shared<VSEditNode>(selection,token);node->mResidency=residency;
    found->editProjection=node;return node;
}

bool VSObjectSelection::resetObjectOverrides()
{
    const auto state=mImpl;auto* world=scene();if(!world || state->retired)return false;
    if(state->allowSelectAvatar)
    {
        auto scratch=world->reserveSpatialBytes((state->avatarOverrides.size()+state->nodes.size())*(sizeof(Impl::AvatarOverride)+sizeof(LLUUID)+64));
        if(!scratch)return false;
        auto next=state->avatarOverrides;
        for(const auto& node:state->nodes)
        {
            if(!node.transform || !state->current(node.object) || node.object.kind!=VSWorldContracts::OwnerKind::Avatar)continue;
            const auto root=world->objectIsRootEdit(node.object);if(!root || !*root)continue;
            const auto old=next.find(node.object.id);
            if(old!=next.end() && !state->current(old->second.object))next.erase(old);
            if(next.find(node.object.id)!=next.end())continue;
            auto residency=world->reserveSpatialBytes(sizeof(Impl::AvatarOverride)+sizeof(LLUUID)+64);if(!residency)return false;
            next.emplace(node.object.id,Impl::AvatarOverride{node.object,node.transform->mLastPositionLocal,node.transform->mLastRotation,std::move(residency)});
        }
        state->avatarOverrides=std::move(next);
    }
    for(auto& node:state->nodes)
        if(node.transform)
        {
            node.transform->mLastPositionLocal.setVec(0,0,0);node.transform->mLastRotation=LLQuaternion();node.transform->mLastScale.setVec(0,0,0);
        }
    return true;
}
void VSObjectSelection::resetAvatarOverrides(){mImpl->avatarOverrides.clear();}
void VSObjectSelection::overrideAvatarUpdates()
{
    const auto state=mImpl;if(state->avatarOverrides.empty())return;
    auto* world=scene();
    if(!world || state->retired || !state->allowSelectAvatar || !gFloaterTools)
    {state->avatarOverrides.clear();return;}
    if(!gFloaterTools->getVisible() && state->nodes.empty() && LLToolMgr::instanceExists())
    {
        const auto toolset=LLToolMgr::instance().getCurrentToolset();
        if(toolset && toolset->isShowFloaterTools() && toolset->isToolSelected(0))
        {state->avatarOverrides.clear();return;}
    }
    for(auto& node:state->nodes)
    {
        if(!state->current(node.object) || node.object.kind!=VSWorldContracts::OwnerKind::Avatar)continue;
        const auto root=world->objectIsRootEdit(node.object);if(!root || !*root)continue;
        const auto old=state->avatarOverrides.find(node.object.id);if(old==state->avatarOverrides.end())continue;
        if(!sameObject(old->second.object,node.object)){state->avatarOverrides.erase(old);continue;}
        auto saved=savedTransform(node.object);if(!saved)continue;
        if(saved->mLastPositionLocal.isExactlyZero())saved->mLastPositionLocal=old->second.position;
        if(saved->mLastRotation==LLQuaternion())saved->mLastRotation=old->second.rotation;
        state->avatarOverrides.erase(old);
    }
    for(auto old=state->avatarOverrides.begin();old!=state->avatarOverrides.end();)
    {
        const auto& value=old->second;
        if(!state->current(value.object)){old=state->avatarOverrides.erase(old);continue;}
        auto pose=world->objectLocalTransform(value.object);if(!pose){++old;continue;}
        U32 fields=0;
        if(!value.position.isExactlyZero() && pose->position!=value.position){pose->position=value.position;fields|=UPD_POSITION;}
        if(value.rotation!=LLQuaternion() && pose->rotation!=value.rotation){pose->rotation=value.rotation;fields|=UPD_ROTATION;}
        if(fields)world->previewLocalTransform(value.object,*pose,fields);
        ++old;
    }
}

bool VSObjectSelection::sendEditCommand(const char* name,SendOrder order,const std::function<void(LLMessageSystem&)>& header,const std::function<void(LLMessageSystem&,const VSWorldScene::ObjectHandle&)>& body)
{
    const auto state=mImpl;const auto owner=state->session.lock();auto* world=scene();
    if(!owner || !world || !gMessageSystem || state->retired)return false;
    const auto account=owner->account();const auto current=selectionGuard();
    const bool singleAvatar=state->allowSelectAvatar && state->nodes.size()==1 &&
        state->current(state->nodes.front().object) && state->nodes.front().object.kind==VSWorldContracts::OwnerKind::Avatar;
    // Same sendListToRegions override retirement, including the single-avatar
    // debug-edit exception. Reset owns any persisted avatar pose before send.
    auto residency=world->reserveSpatialBytes(state->nodes.size()*sizeof(VSWorldScene::ObjectHandle)+64);if(!residency)return false;
    if(!singleAvatar && !resetObjectOverrides())return false;
    std::vector<VSWorldScene::ObjectHandle> ordered;ordered.reserve(state->nodes.size());
    const unsigned passes=(order==SendOrder::ChildrenFirst || order==SendOrder::RootsFirst)?2:1;
    for(unsigned pass=0;pass<passes;++pass)
        for(const auto& node:state->nodes)
        {
            if(!state->current(node.object))continue;
            if(order==SendOrder::Individuals){ordered.push_back(node.object);continue;}
            const auto root=world->objectIsRootEdit(node.object);if(!root)continue;
            const bool admit=order==SendOrder::RootsOnly?(!node.metadata.individual && *root):
                order==SendOrder::RootsFirst?(pass==0?*root:!*root):(pass==0?!*root:*root);
            if(admit)ordered.push_back(node.object);
        }
    LLMessageSystem* system=gMessageSystem;auto& messages=*system;LLHost host;S32 count=0;bool sent=false;
    const bool link=std::string(name)=="ObjectLink";std::optional<VSWorldScene::ObjectHandle> linksetRoot;
    for(const auto& token:ordered)
    {
        if(!owner->current(account) || !current() || !state->current(token)){if(count)messages.clearMessage();return false;}
        if(count && (host!=token.host || messages.isSendFull(nullptr) || count>=254))
        {
            const bool sameRegion=host==token.host;
            messages.sendReliable(host);count=0;sent=true;
            if(!owner->current(account) || !current())return false;
            if(!sameRegion)linksetRoot.reset();
        }
        if(!count)
        {
            host=token.host;messages.newMessage(name);header(messages);
            if(!owner->current(account) || !current()){messages.clearMessage();return false;}
            if(linksetRoot)
            {
                if(!state->current(*linksetRoot)){messages.clearMessage();return false;}
                body(messages,*linksetRoot);++count;
            }
        }
        if(!owner->current(account) || !current() || !state->current(token)){messages.clearMessage();return false;}
        if(link && !linksetRoot)linksetRoot=token;
        body(messages,token);++count;
    }
    if(count)
    {
        if(!owner->current(account) || !current()){messages.clearMessage();return false;}
        messages.sendReliable(host);sent=true;
    }
    return sent;
}

namespace
{
// Thin actual model peer. No legacy object, node or permission stand-ins.
struct VSLinkManager
{
    VSEditSelection selection;
    VSObjectSelection& source;
    VSEditSelection* getSelection(){return &selection;}
    bool selectGetAllRootsValid()
    {
        for(auto it=selection.root_begin();it!=selection.root_end();++it)if(!(*it)->mValid)return false;
        return true;
    }
    bool selectGetRootsModify(){return selection.selectGetRootsModify();}
    bool selectGetRootsNonPermanentEnforced(){return selection.selectGetRootsNonPermanentEnforced();}
    bool selectGetOwner(LLUUID& id,std::string& name){return selection.selectGetOwner(id,name);}
    bool selectGetSameRegion()
    {
        if(selection.isEmpty())return true;
        const auto first=selection.getFirstNode();if(!first || !first->getObject())return false;
        const auto host=first->source.host;
        for(auto it=selection.root_begin();it!=selection.root_end();++it)
            if(!(*it)->mValid || !(*it)->getObject() || (*it)->source.host!=host)return false;
        return true;
    }
    void sendLink(){source.sendLink();}
    void sendDelink(){source.sendDelink();}
};
bool vs_link_sitting_selection(VSEditSelection& selection)
{
    if(!rlv_handler_t::isEnabled() || RlvActions::canStand())return false;
    auto* world=scene();if(!world)return false;
    for(auto it=selection.root_begin();it!=selection.root_end();++it)
    {
        const auto children=world->directChildren((*it)->source);
        if(children && std::any_of(children->begin(),children->end(),[](const auto& child){return child.id==gAgentID;}))return true;
    }
    // getFirstRootNode(functor,true) falls back to non-root nodes whenever no
    // root nodes pass the predicate. isChild means direct child.
    for(auto* node:selection)
    {
        const auto root=world->rootObject(node->source,true);
        const auto children=root ? world->directChildren(*root) : std::nullopt;
        if(children && std::any_of(children->begin(),children->end(),[](const auto& child){return child.id==gAgentID;}))return true;
    }
    return false;
}
bool vs_link_animated_selection(VSEditSelection& selection)
{
    auto* world=scene();const auto owner=VSNativeSession::active();if(!world || !owner)return false;
    S32 animated=0;F32 triangles=0;
    for(auto it=selection.root_begin();it!=selection.root_end();++it)
    {
        auto* object=(*it)->getObject();if(!object)continue;
        if(object->isAnimatedObject())++animated;
        // recursiveGetEstTrianglesMax excludes child avatars, but unlike the
        // selection-family helper it still traverses a debug-selected root
        // avatar's actual non-avatar children.
        std::vector<VSWorldScene::ObjectHandle> pending{(*it)->source};std::set<U32> visited;
        while(!pending.empty())
        {
            const auto token=pending.back();pending.pop_back();
            if(!visited.insert(token.local).second)return false;
            const auto params=world->objectVolumeParams(token);
            if(params && (params->getSculptType()&LL_SCULPT_TYPE_MASK)==LL_SCULPT_TYPE_MESH)
                triangles+=gMeshRepo.getEstTrianglesMax(params->getSculptID());
            const auto children=world->directChildren(token);if(!children)return false;
            // Preserve reference summation order (floating-point triangle
            // estimates are not associative).
            for(auto child=children->rbegin();child!=children->rend();++child)
            {
                const auto metadata=world->objectAdmission(*child);if(!metadata)return false;
                if(metadata->pcode!=LL_PCODE_LEGACY_AVATAR)pending.push_back(*child);
            }
        }
    }
    if(!animated)return true;
    const auto features=owner->simulatorFeatures(owner->host());
    const S32 maximum=features && features->has("AnimatedObjects") ? (*features)["AnimatedObjects"]["AnimatedObjectMaxTris"].asInteger() : 0;
    return triangles<=maximum;
}
}
bool VSObjectSelection::enableLinkObjects() const
{
    VSLinkManager manager{{},const_cast<VSObjectSelection&>(*this)};
    if(!manager.selection.guard()())return false;
    bool result=false;
    if(!gSavedSettings.getBOOL("EditLinkedParts") && manager.selectGetAllRootsValid() && manager.selection.getRootObjectCount()>=2)
        for(auto it=manager.selection.root_begin();it!=manager.selection.root_end();++it)
        {
            auto* object=(*it)->getObject();auto* root=object ? object->getRootEdit() : nullptr;
            if(object && object->permModify() && !object->isPermanentEnforced() && (!root || !root->isPermanentEnforced())){result=true;break;}
        }
    if(!vs_link_animated_selection(manager.selection))result=false;
    return result && !vs_link_sitting_selection(manager.selection);
}
bool VSObjectSelection::enableUnlinkObjects() const
{
    VSLinkManager manager{{},const_cast<VSObjectSelection&>(*this)};
    if(!manager.selection.guard()())return false;
    VSEditObject* first=nullptr;
    for(auto* node:manager.selection)if(auto* object=node->getObject();object && object->permModify()){first=object;break;}
    auto* root=first ? first->getRootEdit() : nullptr;
    const bool result=manager.selectGetAllRootsValid() && first && !first->isAttachment() && !first->isPermanentEnforced() && (!root || !root->isPermanentEnforced());
    return result && !vs_link_sitting_selection(manager.selection);
}
bool VSObjectSelection::linkObjects()
{
    VSLinkManager manager{{},*this};if(!manager.selection.guard()())return false;
    return vs_link_objects(manager);
}
bool VSObjectSelection::unlinkObjects()
{
    VSLinkManager manager{{},*this};if(!manager.selection.guard()())return false;
    const auto owner=mImpl->session.lock();if(!owner)return false;
    const auto current=selectionGuard();const auto account=owner->accountGuard();
    const auto lease=retainSelection();const auto weak=std::weak_ptr<VSNativeSession>(owner);
    return vs_unlink_objects(manager,[weak,current,account,lease]
    {
        const auto completed=std::make_shared<bool>(false);
        LLNotificationsUtil::add("ConfirmUnlink",LLSD(),LLSD(),[weak,current,account,lease,completed](const LLSD& notification,const LLSD& response)
        {
            if(std::exchange(*completed,true))return;
            if(!account() || !current())return;
            const auto owner=weak.lock();if(!owner)return;
            if(LLNotificationsUtil::getSelectedOption(notification,response)!=1)
                if(auto* selection=owner->existingObjectSelection())selection->sendDelink();
        });
    });
}
void VSObjectSelection::sendLink()
{
    if(!objectCount())return;
    sendEditCommand("ObjectLink",SendOrder::RootsOnly,[](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addU32Fast(_PREHASH_ObjectLocalID,token.local);});
}
void VSObjectSelection::sendDelink()
{
    if(!objectCount())return;
    VSEditSelection selection;const auto current=selection.guard();if(!current())return;
    for(auto* node:selection)
    {
        auto* object=node->getObject();
        if(object && object->permModify() && object->getPhysicsShapeType()==LLViewerObject::PHYSICS_SHAPE_NONE)
        {
            object->setPhysicsShapeType(LLViewerObject::PHYSICS_SHAPE_CONVEX_HULL);object->updateFlags();
            if(!current())return;
        }
    }
    sendEditCommand("ObjectDelink",SendOrder::Individuals,[](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addU32Fast(_PREHASH_ObjectLocalID,token.local);});
}

namespace
{
bool vs_edit_delete_or_return(VSEditSelection& selection)
{
    if(!gRlvHandler.hasBehaviour(RLV_BHVR_EDIT) && !gRlvHandler.hasBehaviour(RLV_BHVR_REZ) &&
       !gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT) && !gRlvHandler.hasException(RLV_BHVR_EDITOBJ))return true;
    if(!selection.getRootObjectCount())return false;
    auto* world=scene();if(!world)return false;
    for(auto it=selection.root_begin();it!=selection.root_end();++it)
    {
        auto* object=(*it)->getObject();if(!object)return false;
        const auto context=VSObjectContext::resolve((*it)->source);if(!context || !context->current() || !RlvActions::canEdit(*context))return false;
        auto* root=object->getRootEdit();
        const bool groupOwned=root && (root->flags()&FLAGS_OBJECT_GROUP_OWNED);
        if((gRlvHandler.hasBehaviour(RLV_BHVR_EDIT) || gRlvHandler.hasBehaviour(RLV_BHVR_REZ)) &&
           (object->permYouOwner() || object->permModify() || groupOwned))return false;
        if(gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT) && world->selfAvatar())
        {
            const auto rootToken=world->rootObject((*it)->source,true);
            const auto children=rootToken?world->directChildren(*rootToken):std::nullopt;
            if(children && std::any_of(children->begin(),children->end(),[](const auto& child){return child.id==gAgentID;}))return false;
        }
    }
    return true;
}
struct VSEditDeleteManager
{
    VSEditSelection view;
    VSObjectSelection& source;
    std::function<void()> beforeConvert;
    VSEditSelection* getSelection(){return &view;}
    bool canDeleteOrReturn(){return vs_edit_delete_or_return(view);}
    void deselectAll(){source.deselectAll();}
    void convertTransient(){if(beforeConvert)beforeConvert();source.convertTransient();}
};
}
void VSObjectSelection::bindEditMenuHandler()
{
    if(!mImpl->retired && objectCount())LLEditMenuHandler::gEditMenuHandler=LLSelectMgr::getInstance();
}
bool VSObjectSelection::canUndo() const
{
    VSEditSelection selection;if(!selection.guard()())return false;
    for(auto* node:selection)if(auto* object=node->getObject();object && (object->permModify() || (object->permMove() && !object->isPermanentEnforced())))return true;
    return false;
}
bool VSObjectSelection::canRedo() const
{
    VSEditSelection selection;if(!selection.guard()())return false;
    for(auto* node:selection)if(auto* object=node->getObject();object && object->permModify())return true;
    return false;
}
bool VSObjectSelection::canDoDelete() const
{
    VSEditSelection selection;if(!selection.guard()())return false;
    bool enabled=false;
    for(auto* node:selection)
        if(auto* object=node->getObject();object && !object->isPermanentEnforced() &&
           (object->permModify() || object->permYouOwner() || !object->task().permAnyOwner()) && !object->isAttachment())
        {enabled=contains(node->source);break;}
    return enabled && (!rlv_handler_t::isEnabled() || vs_edit_delete_or_return(selection));
}
bool VSObjectSelection::canDuplicate() const
{
    VSEditSelection selection;if(!selection.guard()())return false;
    bool enabled=false;
    for(auto* node:selection)if(auto* object=node->getObject();object && object->permCopy() && !object->isAttachment()){enabled=true;break;}
    return enabled && (!rlv_handler_t::isEnabled() || vs_edit_delete_or_return(selection));
}
void VSObjectSelection::undo()
{
    const LLUUID group=gAgent.getGroupID();
    sendEditCommand("Undo",gSavedSettings.getBOOL("EditLinkedParts")?SendOrder::ChildrenFirst:SendOrder::RootsOnly,[group](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);message.addUUIDFast(_PREHASH_GroupID,group);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addUUIDFast(_PREHASH_ObjectID,token.id);});
}
void VSObjectSelection::redo()
{
    const LLUUID group=gAgent.getGroupID();
    sendEditCommand("Redo",gSavedSettings.getBOOL("EditLinkedParts")?SendOrder::ChildrenFirst:SendOrder::RootsOnly,[group](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);message.addUUIDFast(_PREHASH_GroupID,group);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addUUIDFast(_PREHASH_ObjectID,token.id);});
}
void VSObjectSelection::selectDelete()
{
    VSEditDeleteManager manager{{},*this};if(!manager.view.guard()())return;
    const auto owner=mImpl->session.lock();if(!owner)return;
    // Bind this source revision. The one intentional convertTransient revision
    // is predicted before notification; any reentrant replacement invalidates it.
    const auto account=owner->accountGuard();const auto lease=retainSelection();
    const auto state=mImpl;const auto weak=std::weak_ptr<VSNativeSession>(owner);
    const auto once=std::make_shared<bool>(false);
    const auto expected=std::make_shared<U64>(state->revision);
    manager.beforeConvert=[state,expected]
    {
        const bool changes=std::any_of(state->nodes.begin(),state->nodes.end(),[](const auto& node){return node.metadata.transient;});
        *expected=state->revision+(changes?1:0);
    };
    vs_delete_selection(manager,[weak,account,lease,state,once,expected](const LLSD& notification,const LLSD& response)
    {
        if(std::exchange(*once,true))return false;
        if(!account() || state->retired || state->revision!=*expected)return false;
        const auto owner=weak.lock();if(!owner)return false;
        if(LLNotification::getSelectedOption(notification,response)==0)
            if(auto* selection=owner->existingObjectSelection())selection->deleteConfirmed();
        return false;
    });

}
void VSObjectSelection::deleteConfirmed()
{
    const auto current=selectionGuard();if(!current())return;
    const auto count=objectCount();const auto center=centerGlobal();const auto type=selectionType();
    const LLUUID trash=gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH);
    const bool sent=sendEditCommand("DeRezObject",SendOrder::RootsOnly,[trash](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);
        message.nextBlockFast(_PREHASH_AgentBlock);message.addUUIDFast(_PREHASH_GroupID,gAgent.getGroupID());message.addU8Fast(_PREHASH_Destination,U8(DRD_TRASH));message.addUUIDFast(_PREHASH_DestinationID,trash);
        LLUUID transaction;transaction.generate();message.addUUIDFast(_PREHASH_TransactionID,transaction);message.addU8Fast(_PREHASH_PacketCount,1);message.addU8Fast(_PREHASH_PacketNumber,1);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addU32Fast(_PREHASH_ObjectLocalID,token.local);});
    if(!sent || !current())return;
    if(type.type!=SelectionType::HUD)
    {
        auto* effect=static_cast<LLHUDEffectSpiral*>(LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_POINT,true));
        if(effect->setNativeEndpoints(LLUUID::null,LLUUID::null))
        {effect->setPositionGlobal(center);effect->setColor(LLColor4U(gAgent.getEffectColor()));effect->setDuration(.5f+count/64.f);}
        else effect->markDead();
    }
    if(gAudiop && gSavedSettings.getBOOL("PlayModeUISndObjectDelete"))gAudiop->triggerSound(LLUUID(gSavedSettings.getString("UISndObjectDelete")),gAgentID,1.f,LLAudioEngine::AUDIO_TYPE_UI);
    gAgentCamera.setLookAt(LOOKAT_TARGET_CLEAR);
    add(LLStatViewer::DELETE_OBJECT,count);
}
void VSObjectSelection::selectDuplicate(const LLVector3& offset,bool selectCopy)
{
    VSEditSelection selection;const auto current=selection.guard();if(!current())return;
    if(selection.isAttachment() || (rlv_handler_t::isEnabled() && !vs_edit_delete_or_return(selection)))
    {make_ui_sound("UISndInvalidOp");return;}
    if(!canDuplicate())
    {
        auto* node=selection.getFirstRootNode();if(!node)node=selection.getFirstNode();
        if(node){LLSD args;args["OBJ_NAME"]=node->mName;LLNotificationsUtil::add("NoCopyPermsNoObject",args);return;}
    }
    // The reference node already owns these fields. Admit equivalent retained
    // native records before sending an irreversible simulator mutation.
    if(!selectCopy)for(auto it=selection.root_begin();it!=selection.root_end();++it)
        if(!savedTransform((*it)->source))return;
    const LLUUID group=FSCommon::getGroupForRezzing();
    const bool sent=sendEditCommand("ObjectDuplicate",SendOrder::RootsOnly,[group,offset,selectCopy](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);message.addUUIDFast(_PREHASH_GroupID,group);
        message.nextBlockFast(_PREHASH_SharedData);message.addVector3Fast(_PREHASH_Offset,offset);message.addU32Fast(_PREHASH_DuplicateFlags,selectCopy?FLAGS_CREATE_SELECTED:0);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addU32Fast(_PREHASH_ObjectLocalID,token.local);});
    if(!sent || !current())return;
    if(selectCopy)deselectAll();
    else for(auto it=selection.root_begin();it!=selection.root_end();++it)
    {
        const auto saved=savedTransform((*it)->source);if(!saved)return;
        saved->mDuplicated=true;saved->mDuplicatePos=(*it)->getObject()->getPositionGlobal();saved->mDuplicateRot=(*it)->getObject()->getRotation();
    }
}


void VSObjectSelection::repeatDuplicate()
{
    VSEditSelection initial;const auto account=mImpl->session.lock();if(!account || !initial.guard()())return;
    if(initial.isAttachment()){make_ui_sound("UISndInvalidOp");return;}
    const auto live=account->accountGuard();
    std::vector<VSWorldScene::ObjectHandle> remove;
    for(auto it=initial.root_begin();it!=initial.root_end();++it)
    {
        const auto saved=savedTransform((*it)->source);if(!saved)return;
        if(!saved->mDuplicated)remove.push_back((*it)->source);
    }
    for(const auto& token:remove){deselectFamily(token);if(!live())return;}
    VSEditSelection selection;const auto current=selection.guard();if(!current())return;
    const LLUUID group=FSCommon::getGroupForRezzing();
    const bool sent=sendEditCommand("ObjectDuplicate",SendOrder::RootsOnly,[group](auto& message)
    {
        message.nextBlockFast(_PREHASH_AgentData);message.addUUIDFast(_PREHASH_AgentID,gAgentID);message.addUUIDFast(_PREHASH_SessionID,gAgentSessionID);message.addUUIDFast(_PREHASH_GroupID,group);
        message.nextBlockFast(_PREHASH_SharedData);message.addVector3Fast(_PREHASH_Offset,LLVector3::zero);message.addU32Fast(_PREHASH_DuplicateFlags,0);
    },[](auto& message,const auto& token){message.nextBlockFast(_PREHASH_ObjectData);message.addU32Fast(_PREHASH_ObjectLocalID,token.local);});
    if(!sent || !current())return;
    for(auto it=selection.root_begin();it!=selection.root_end();++it)
    {
        const auto saved=savedTransform((*it)->source);if(!saved || !saved->mDuplicated)continue;
        auto* object=(*it)->getObject();
        const LLQuaternion rotation=object->getRotation();const LLQuaternion delta=~saved->mDuplicateRot*rotation;
        const LLQuaternion nextRotation=rotation*delta;const LLVector3d position=object->getPositionGlobal();
        const LLVector3d nextPosition=position+((position-saved->mDuplicatePos)*delta);
        saved->mDuplicatePos=position;saved->mDuplicateRot=rotation;
        auto* world=scene();if(!world)return;
        auto pose=world->objectLocalTransform((*it)->source);if(!pose)return;
        const auto region=vs_region_context((*it)->source.host);if(!region)return;
        // Same selected root global-to-region conversion as setPositionGlobal.
        pose->position=LLVector3(nextPosition-region->getOriginGlobal());pose->rotation=nextRotation;
        if(!world->previewLocalTransform((*it)->source,*pose,UPD_POSITION|UPD_ROTATION))return;
        if(!current())return;
    }
    sendMultipleUpdate(UPD_ROTATION|UPD_POSITION);
}


bool VSObjectSelection::addGridObject(const VSWorldScene::ObjectHandle& token)
{
    auto* world=scene();const auto state=mImpl;
    if(!world || !state->current(token))return false;
    const auto children=world->directChildren(token);if(!children)return false;
    auto residency=world->reserveSpatialBytes(sizeof(Impl::GridReference)*(children->size()+1));
    if(!residency)return false;
    auto objects=state->gridObjects;objects.push_back({token,residency});
    for(const auto& child:*children)objects.push_back({child,residency});
    if(!state->current(token) || std::any_of(children->begin(),children->end(),[&](const auto& child){return !state->current(child);}))return false;
    state->gridObjects=std::move(objects);return true;
}
void VSObjectSelection::clearGridObjects()
{mImpl->gridObjects.clear();}
bool VSObjectSelection::getGrid(S32 mode,LLVector3& origin,LLQuaternion& rotation,LLVector3& scale,bool forSnapGuides)
{
    auto* world=scene();const auto state=mImpl;const auto owner=state->session.lock();
    if(!world || state->retired || !owner || !owner->accountGuard()())return false;
    state->gridObjects.erase(std::remove_if(state->gridObjects.begin(),state->gridObjects.end(),[&](const auto& object){return !state->current(object.object);}),state->gridObjects.end());
    const auto agentOrigin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    LLVector3 nextOrigin=origin,nextScale=scale;LLQuaternion nextRotation=rotation;
    if(mode==GRID_MODE_LOCAL && objectCount())
    {
        const auto bounds=savedSelectionBBox();if(!bounds)return false;
        nextOrigin=bounds->getCenterAgent();nextScale=bounds->getExtentLocal()*.5f;nextRotation=bounds->getRotation();
    }
    else if(mode==GRID_MODE_REF_OBJECT && !state->gridObjects.empty())
    {
        const auto first=world->focusObject(state->gridObjects.front().object,agentOrigin);if(!first || !first->current())return false;
        const auto selected=node(state->gridObjects.front().object);
        const auto saved=selected?savedTransform(state->gridObjects.front().object):nullptr;
        if(!forSnapGuides && selected && !saved)return false;
        nextRotation=(!forSnapGuides && saved)?saved->mSavedRotation:first->renderRotation;
        std::optional<std::array<LLVector3,2>> extents;
        for(const auto& entry:state->gridObjects)
        {
            const auto& token=entry.object;
            const auto bounds=world->publishedObjectBounds(token,agentOrigin);if(!bounds)continue;
            if(!extents)extents=bounds;
            else for(S32 axis=0;axis<3;++axis)
            {(*extents)[0].mV[axis]=llmin((*extents)[0].mV[axis],(*bounds)[0].mV[axis]);(*extents)[1].mV[axis]=llmax((*extents)[1].mV[axis],(*bounds)[1].mV[axis]);}
        }
        if(extents){nextOrigin=((*extents)[0]+(*extents)[1])*.5f;nextScale=((*extents)[1]-(*extents)[0])*.5f;}
    }
    else
    {
        nextOrigin.clearVec();nextRotation.loadIdentity();
        const auto first=firstRootObject(true);const auto admission=first?world->objectAdmission(*first):std::nullopt;
        const auto type=admission && admission->hud?SelectionType::HUD:admission && admission->attachment?SelectionType::Attachment:SelectionType::World;
        if(type==SelectionType::Attachment)
        {
            const auto attachment=world->objectAttachment(*first);
            const auto avatar=world->avatarForObject(first->id);
            if(attachment && avatar)
            {
                LLVector3 position;LLQuaternion orientation;
                if(avatar->attachmentTransform(attachment->point,position,orientation))
                {
                    const auto context=vs_region_context(first->host);if(!context)return false;
                    nextOrigin=position+LLVector3(context->getOriginGlobal()-agentOrigin);nextRotation=orientation;
                    nextScale=LLVector3(1,1,1)*gSavedSettings.getF32("GridResolution");
                }
            }
        }
        else nextScale=LLVector3(1,1,1)*(type==SelectionType::HUD?llmin(gSavedSettings.getF32("GridResolution"),.5f):gSavedSettings.getF32("GridResolution"));
    }
    if(!nextOrigin.isFinite() || !nextScale.isFinite() || !nextRotation.isFinite())return false;
    origin=nextOrigin;rotation=nextRotation;scale=nextScale;return true;
}

bool VSObjectSelection::refreshSavedTextures()
{
    const auto state=mImpl;auto* world=scene();const auto owner=state->session.lock();
    if(!world || !owner || state->retired)return false;
    const auto current=selectionGuard();if(!current())return false;
    std::vector<VSWorldScene::ObjectHandle> objects;
    for(auto& node:state->nodes)
    {
        if(!state->current(node.object))return false;
        node.pending=true;node.retryAt=LLFrameTimer::getTotalSeconds()+5;
        objects.push_back(node.object);
    }
    // Keep the old authorized rollback snapshot until its authenticated response
    // arrives, as the reference keeps LLSelectNode::mSavedTextures while invalid.
    state->sendSelection("ObjectSelect",objects);
    return current();
}

bool VSObjectSelection::saveColors(const VSWorldScene::ObjectHandle& object,bool shiny)
{
    const auto state=mImpl;auto* world=scene();if(!world || !state->current(object))return false;
    auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    if(found==state->nodes.end())return false;
    const auto old=found->colors;
    const auto count=world->textureEntryCount(object);if(count<0)return false;
    auto saved=std::make_shared<SavedColors>();
    const std::size_t preserved=old?(shiny?old->colors.size():old->shiny.size()):0;
    saved->residency=world->reserveSpatialBytes(sizeof(SavedColors)+(std::size_t(count)+preserved)*sizeof(LLColor4));
    if(!saved->residency)return false;
    if(old){if(shiny)saved->colors=old->colors;else saved->shiny=old->shiny;}
    auto& values=shiny?saved->shiny:saved->colors;values.reserve(count);
    for(S32 side=0;side<count;++side)
    {
        LLTextureEntry entry;if(!world->copyMaterialEntry(object,U32(side),entry))return false;
        const auto material=entry.getMaterialParams();
        values.push_back(shiny?(material?LLColor4(material->getSpecularLightColor()):LLColor4::white):entry.getColor());
    }
    if(!state->current(object))return false;
    found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,object) && node.colors==old;});
    if(found==state->nodes.end())return false;
    found->colors=std::move(saved);return true;
}
std::shared_ptr<const VSObjectSelection::SavedColors> VSObjectSelection::savedColors(const VSWorldScene::ObjectHandle& object) const
{
    if(!mImpl->current(object))return {};
    const auto found=std::find_if(mImpl->nodes.begin(),mImpl->nodes.end(),[&](const auto& node){return sameObject(node.object,object);});
    return found!=mImpl->nodes.end()?found->colors:nullptr;
}

bool VSObjectSelection::captureSavedMaterials(const VSWorldScene::ObjectHandle& object,
    const std::shared_ptr<const VSObjectProperties>& properties)
{
    const auto state=mImpl;auto* world=scene();const auto owner=state->session.lock();
    if(!world || !owner || !properties || !state->current(object))return false;
    const auto account=owner->accountGuard();if(!account())return false;
    auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node)
        {return sameObject(node.object,object) && node.publication && node.publication->properties==properties;});
    if(found==state->nodes.end())return false;
    const auto savedOwner=found->materials;
    const auto row=world->objectAdmission(object);if(!row)return false;
    const auto& aggregate=VSObjectContext::ownedByAgent(row->rootUpdateFlags)?properties->ag_texture_perms_owner:properties->ag_texture_perms;
    const auto copy=aggregate.getValue(PERM_COPY),transfer=aggregate.getValue(PERM_TRANSFER);
    if((copy!=LLAggregatePermissions::AP_EMPTY && copy!=LLAggregatePermissions::AP_ALL) ||
       (transfer!=LLAggregatePermissions::AP_EMPTY && transfer!=LLAggregatePermissions::AP_ALL))return false;
    const auto faceCount=world->textureEntryCount(object);if(faceCount<0)return false;
    SavedMaterials saved;
    const std::size_t charge=sizeof(SavedMaterials)+properties->texture_ids.size()*sizeof(LLUUID)+
        std::size_t(faceCount)*(sizeof(LLUUID)+sizeof(LLPointer<LLGLTFMaterial>)+sizeof(LLGLTFMaterial));
    saved.residency=world->reserveSpatialBytes(charge);
    if(!saved.residency){savedOwner->ready=false;return false;}
    saved.textures=properties->texture_ids;
    for(S32 side=0;side<faceCount;++side)
    {
        LLTextureEntry entry;if(!world->copyMaterialEntry(object,U32(side),entry))return false;
        saved.ids.push_back(world->renderMaterialID(object,U32(side)));
        const auto* value=entry.getGLTFMaterialOverride();
        saved.overrides.emplace_back(value?new LLGLTFMaterial(*value):nullptr);
    }
    if(!account() || !state->current(object))return false;
    found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node)
        {return sameObject(node.object,object) && node.materials==savedOwner && node.publication && node.publication->properties==properties;});
    if(found==state->nodes.end())return false;
    saved.ready=true;*savedOwner=std::move(saved);return true;
}

std::optional<VSWorldScene::ObjectHandle> VSObjectSelection::hoverObject() const
{
    const auto state=mImpl;
    for(const auto& node:state->hover)if(state->current(node.object))return node.object;
    return {};
}

bool VSObjectSelection::hideGLTFMaterial()
{
    const auto current=selectionGuard();auto* world=scene();if(!world || !current())return false;
    for(const auto& token:selected())
    {
        if(!current())return false;
        const auto object=VSMaterialObject::find(token);if(!object || !object.permModify())continue;
        if(!world->setLegacyMaterialPreview(token,true))return false;
    }
    return current();
}
bool VSObjectSelection::showGLTFMaterial()
{
    const auto current=selectionGuard();auto* world=scene();if(!world || !current())return false;
    for(const auto& object:selected())
    {
        if(!current() || !world->setLegacyMaterialPreview(object,false))return false;
    }
    return current();
}

bool VSObjectSelection::setFaceSelected(const VSWorldScene::ObjectHandle& object,S32 face,bool selected)
{
    const auto state=mImpl;
    if(face<0 || face>=SELECT_MAX_TES || !state->current(object))return false;
    for(auto& node:state->nodes)
        if(sameObject(node.object,object))
        {
            const U32 mask=U32(1)<<face;
            const U32 next=selected ? node.metadata.faceMask|mask : node.metadata.faceMask&~mask;
            if(next==node.metadata.faceMask && node.metadata.lastFace==face)return true;
            node.metadata.faceMask=next;node.metadata.lastFace=face;
            const auto revision=++state->revision;state->changed();return state->current(object) && state->revision==revision;
        }
    return false;
}

// The rectangle and simulator ForceObjectSelect selections are distinct owners.
bool VSObjectSelection::updateRectangle(const LLCamera& camera,bool grow,bool shrink,bool subtract,
                                      bool limit,F32 distanceSquared,const LLVector3& avatar,
                                      const std::function<bool()>& operation)
{
    const auto state=mImpl;auto* world=scene();
    const auto owner=state->session.lock();
    if(!world || !owner || !operation)return false;
    const auto account=owner->accountGuard();const auto serial=state->rectangleRevision;
    const auto current=[&]{return !state->retired && account() && operation() && state->rectangleRevision==serial;};
    if(!current())return false;
    const auto origin=gAgent.getPosGlobalFromAgent(LLVector3::zero);
    const auto candidates=grow?world->selectionCandidates(camera,origin):std::optional<VSWorldScene::SelectionCandidates>();
    if(grow && !candidates)return false;
    auto scratch=world->reserveSpatialBytes((state->rectangleHits.size()+(candidates?candidates->objects.size():0))*sizeof(Impl::Highlight)+64);
    if(!scratch)return false;
    auto next=state->rectangleHits;
    const auto sphere=[&](const VSWorldScene::ObjectHandle& token)->S32
    {
        const auto frame=world->focusObject(token,origin);
        // LLVOVolume::updateRadius gives its drawable getScale().length().
        return frame?camera.sphereInFrustum(frame->renderPosition,frame->rawScale.length()):0;
    };
    if(shrink)
        next.erase(std::remove_if(next.begin(),next.end(),[&](const auto& hit)
        {
            if(!state->current(hit.object))return true;
            const auto row=world->objectAdmission(hit.object);
            if(!row || row->pcode!=LL_PCODE_VOLUME || row->attachment)return false;
            const S32 result=sphere(hit.object);
            return result==0 || (result==1 && !world->objectVertsVisible(hit.object,camera,LLSelectMgr::sRectSelectInclusive,origin));
        }),next.end());
    if(grow)for(const auto& token:candidates->objects)
    {
        if(!current())return false;
        const auto row=world->objectAdmission(token);
        if(!row || row->pcode!=LL_PCODE_VOLUME || row->attachment || (subtract && !node(token)))continue;
        const auto frame=world->focusObject(token,origin);if(!frame)continue;
        if(limit && dist_vec_squared(frame->renderPosition,avatar)>distanceSquared)continue;
        if(RlvActions::isRlvEnabled())
        {
            const auto source=VSObjectContext::resolve(token);
            if(!source || !RlvActions::canEdit(*source))continue;
        }
        const S32 result=sphere(token);
        if(!result || (result==1 && !world->objectVertsVisible(token,camera,LLSelectMgr::sRectSelectInclusive,origin)))continue;
        // Exact highlightObjectOnly filters (not canSelect: locked-only is not
        // a raw rectangle filter, and roots get canSelect at node expansion).
        const bool move=vs_object_you_owner((row->rootUpdateFlags&FLAGS_OBJECT_MOVE)!=0,[]
            {return LLGridManager::getInstance()->isInSLBeta() && gAgent.getGodLevel()>=GOD_MAINTENANCE;});
        const bool copy=vs_object_you_owner((row->rootUpdateFlags&FLAGS_OBJECT_COPY)!=0,[]
            {return LLGridManager::getInstance()->isInSLBeta() && gAgent.getGodLevel()>=GOD_MAINTENANCE;});
        const bool permanent=(row->updateFlags&FLAGS_AFFECTS_NAVMESH) && token.host!=owner->host() && !gAgent.isGodlike();
        if((gSavedSettings.getBOOL("SelectOwnedOnly") && !VSObjectContext::ownedByAgent(row->rootUpdateFlags)) ||
           (gSavedSettings.getBOOL("SelectMovableOnly") && (!move || permanent)) ||
           (!copy && gSavedSettings.getBOOL("FSSelectCopyableOnly")) ||
           ((row->rootUpdateFlags&FLAGS_OBJECT_GROUP_OWNED) && !gSavedSettings.getBOOL("FSSelectIncludeGroupOwned")))continue;
        if(std::any_of(next.begin(),next.end(),[&](const auto& hit){return sameObject(hit.object,token);}))continue;
        auto residency=world->reserveSpatialBytes(sizeof(Impl::Highlight));if(!residency)return false;
        next.push_back({token,std::move(residency)});
    }
    // Reconcile persistent highlight nodes, retaining already-owned node state.
    // Reference adds only immediate children and then the root LAST.
    const bool linked=!gSavedSettings.getBOOL("EditLinkedParts");
    std::vector<VSWorldScene::ObjectHandle> roots;
    for(const auto& hit:next)
    {
        const auto root=linked?world->rootObject(hit.object,false):std::optional<VSWorldScene::ObjectHandle>(hit.object);
        if(root && std::none_of(roots.begin(),roots.end(),[&](const auto& old){return sameObject(old,*root);}))roots.push_back(*root);
    }
    std::vector<Impl::Node> nodes;
    auto nodesScratch=world->reserveSpatialBytes((state->rectangleNodes.size()+next.size())*sizeof(Impl::Node)+64);
    if(!nodesScratch)return false;
    for(const auto& old:state->rectangleNodes)
    {
        if(!state->current(old.object))continue;
        const auto root=linked?world->rootObject(old.object,false):std::optional<VSWorldScene::ObjectHandle>(old.object);
        if(root && std::any_of(roots.begin(),roots.end(),[&](const auto& value){return sameObject(value,*root);}))nodes.push_back(old);
    }
    const auto append=[&](const VSWorldScene::ObjectHandle& token)->bool
    {
        if(std::any_of(nodes.begin(),nodes.end(),[&](const auto& node){return sameObject(node.object,token);}))return true;
        if(!canSelect(token,state->forceSelection))return true;
        const auto faces=world->faceCount(token);if(!faces)return false;
        auto residency=world->reserveSpatialBytes(sizeof(Impl::Node));if(!residency)return false;
        Impl::Node candidate{token,std::move(residency),{},true,LLFrameTimer::getTotalSeconds()+5};
        candidate.metadata.faceMask=*faces?0xffffffffu:0;candidate.metadata.individual=!linked;
        nodes.push_back(std::move(candidate));return true;
    };
    for(const auto& root:roots)
    {
        if(std::any_of(nodes.begin(),nodes.end(),[&](const auto& node){return sameObject(node.object,root);}))continue;
        if(!canSelect(root,state->forceSelection))continue;
        if(linked)
        {
            const auto children=world->directChildren(root);if(!children)return false;
            for(const auto& child:*children)if(!append(child))return false;
        }
        if(!append(root))return false;
    }
    if(!current())return false;
    state->rectangleHits=std::move(next);state->rectangleNodes=std::move(nodes);
    const auto accepted=++state->rectangleRevision;
    state->changed();
    return !state->retired && account() && operation() && state->rectangleRevision==accepted;
}

bool VSObjectSelection::commitRectangle(bool subtract,const std::function<bool()>& operation)
{
    const auto state=mImpl;const auto owner=state->session.lock();auto* world=scene();
    if(!owner || !world || !operation)return false;
    const auto account=owner->accountGuard();
    if(state->retired || !account() || !operation())return false;
    if(subtract)
    {
        const auto hits=state->rectangleHits;
        const bool linked=!gSavedSettings.getBOOL("EditLinkedParts");
        for(const auto& hit:hits)
        {
            if(!account() || !operation())return false;
            if(!state->current(hit.object))continue;
            if(!linked)deselectOnly(hit.object);
            else if(const auto root=world->rootObject(hit.object,false);root && node(*root))deselectFamily(*root);
        }
        clearRectangle();return account();
    }
    if(state->rectangleNodes.empty())return false;
    auto next=state->nodes;
    std::vector<VSWorldScene::ObjectHandle> added;
    auto scratch=world->reserveSpatialBytes((next.size()+state->rectangleNodes.size())*(sizeof(Impl::Node)+sizeof(VSWorldScene::ObjectHandle))+64);
    if(!scratch)return false;
    for(const auto& highlighted:state->rectangleNodes)
    {
        const auto& token=highlighted.object;
        if(!canSelect(token,state->forceSelection) || node(token))continue;
        auto residency=world->reserveSpatialBytes(sizeof(Impl::Node));if(!residency)return false;
        auto copy=highlighted;copy.residency=std::move(residency);
        next.insert(next.begin(),std::move(copy));added.push_back(token);
    }
    if(!account() || !operation())return false;
    for(const auto& token:added)if(!state->current(token))return false;
    // Unlike selectOnly/selectFamily, original highlighted selection DOES NOT
    // reset velocity/rotation. Preserve that distinction in the actual owner.
    for(const auto& token:added)world->setSelectionState(token,true);
    state->nodes=std::move(next);state->primary.reset();++state->revision;
    const auto revision=state->revision;
    sendSelect();
    if(!account() || state->revision!=revision)return false;
    for(const auto& token:added)
    {
        if(!account() || state->revision!=revision)return false;
        if(world->objectIsRootEdit(token)==std::optional<bool>(true))requestFamily(token,0,[](const auto&){});
    }
    clearRectangle();
    if(!account() || state->revision!=revision)return false;
    refreshSelectionGeometry();
    if(!account() || state->revision!=revision)return false;
    saveSelectedObjectTransform();
    if(!account() || state->revision!=revision)return false;
    updateAttention();bindEditMenuHandler();
    if(account())state->changed();
    return account();
}
void VSObjectSelection::clearRectangle()
{
    const auto state=mImpl;
    const bool changed=!state->rectangleHits.empty() || !state->rectangleNodes.empty();
    state->rectangleHits.clear();state->rectangleNodes.clear();++state->rectangleRevision;
    if(changed && !state->retired)state->changed();
}
std::vector<VSWorldScene::ObjectHandle> VSObjectSelection::rectangleHighlights()const
{
    std::vector<VSWorldScene::ObjectHandle> result;
    for(const auto& node:mImpl->rectangleNodes)if(mImpl->current(node.object))result.push_back(node.object);
    return result;
}

bool VSObjectSelection::seedSelectedProperties(const VSWorldScene::ObjectHandle& token,const VSObjectProperties& properties)
{
    const auto state=mImpl;
    const auto owner=state->session.lock();
    if (!owner || state->retired || !state->current(token) || properties.id != token.id || !properties.complete) return false;
    const auto current=owner->accountGuard();
    const auto found=std::find_if(state->nodes.begin(),state->nodes.end(),[&](const auto& node){return sameObject(node.object,token);});
    if (found == state->nodes.end()) return false;
    auto publication=state->publish(token,properties);
    if (!publication) return false;
    found->publication=std::move(publication);
    found->pending=false;
    state->changed();
    return current() && state->current(token);
}

bool VSObjectSelection::visitSelected(const std::function<bool(const VSWorldScene::ObjectHandle&,const NodeMetadata&)>& visitor) const
{
    const auto state=mImpl;const auto owner=state->session.lock();
    if(!owner || state->retired || !visitor)return false;
    const auto account=owner->accountGuard();const auto revision=state->revision;
    for(const auto& node:state->nodes)
    {
        if(!account() || state->revision!=revision)return false;
        if(state->current(node.object) && !visitor(node.object,node.metadata))return false;
        // The callback may synchronously mutate/retire the actual vector. Do
        // not increment its iterator until the real owner/version is checked.
        if(!account() || state->retired || state->revision!=revision)return false;
    }
    return account();
}
