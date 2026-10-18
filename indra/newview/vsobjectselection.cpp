// Preserve reference ObjectSelect/ObjectDeselect and properties-family protocols.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectselection.h"
#include "vsobjectattention.h"
#include "vsworldavatar.h"
#include "llagentcamera.h"
#include "llcharacter.h"
#include "llhudeffectpointat.h"
#include "llhudeffectlookat.h"
#include "vsselectionpolicy.h"
#include "vsobjectcontext.h"
#include "vsobjectinteraction.h"
#include "llselectmgr.h"
#include "llviewerobject.h"
#include "llviewercontrol.h"
#include "llagent.h"
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
    std::vector<Node> nodes;
    std::vector<Node> hover;
    std::optional<VSWorldScene::ObjectHandle> primary,hoverRoot,hoverPrimary;
    U64 hoverRevision=1;
    LLBBox bounds;
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
    std::function<bool()> highlightCurrent;
    U64 highlightRevision=1;
    boost::signals2::signal<void()> changed;
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
    :mImpl(std::make_shared<Impl>(std::move(session))){}
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
            changed=true;
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
    bool highlightsChanged=false;
    const auto highlightRequest=state->highlightCurrent;
    const auto highlightRevision=state->highlightRevision;
    const bool highlightActive=highlightRequest && highlightRequest();
    if(state->highlightRevision!=highlightRevision)return;
    if(!highlightActive)
    {
        highlightsChanged=!state->highlights.empty();
        state->highlights.clear();state->highlightCurrent={};++state->highlightRevision;
    }
    else
    {
        const auto count=state->highlights.size();
        state->highlights.erase(std::remove_if(state->highlights.begin(),state->highlights.end(),[&](const auto& node)
            {return !state->current(node.object);}),state->highlights.end());
        highlightsChanged=count!=state->highlights.size();
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
    if(auto* world=scene())for(const auto& node:state->nodes)world->setSelectionState(node.object,false);
    state->hover.clear();state->hoverRoot.reset();state->hoverPrimary.reset();state->primary.reset();++state->hoverRevision;
    state->pauses.clear();state->geometryKnown=false;state->showSelection=false;state->selectionCenter.clearVec();state->bounds=LLBBox();
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
        refreshSelectionGeometry();updateAttention();state->changed();
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
    refreshSelectionGeometry();updateAttention();state->changed();
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
        refreshSelectionGeometry();updateAttention();state->changed();
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
    refreshSelectionGeometry();updateAttention();state->changed();
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
