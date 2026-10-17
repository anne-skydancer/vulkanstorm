// Preserve reference ObjectSelect/ObjectDeselect and properties-family protocols.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectselection.h"
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
    std::vector<Request> families;
    struct Highlight
    {
        VSWorldScene::ObjectHandle object;
        std::shared_ptr<const VSResourceBudget::Reservation> residency;
    };
    std::vector<Highlight> highlights;
    std::function<bool()> highlightCurrent;
    boost::signals2::signal<void()> changed;
    U64 revision=1;
    explicit Impl(std::weak_ptr<VSNativeSession> value):session(std::move(value)){}
    bool current(const VSWorldScene::ObjectHandle& token)const
    {
        const auto owner=session.lock();
        auto* world=scene();
        if (!owner || owner->phase()!=VSNativeSession::Phase::Connected || !world) return false;
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
            next.push_back({object,std::move(reservation),{},true,LLFrameTimer::getTotalSeconds()+5});
        }
    }
    std::vector<VSWorldScene::ObjectHandle> removed,added;
    for(const auto& old:state->nodes)
        if(!identities.count(old.object.id) || std::none_of(next.begin(),next.end(),[&](const auto& node){return sameObject(node.object,old.object);}))
            removed.push_back(old.object);
    for(const auto& node:next)
        if(std::none_of(state->nodes.begin(),state->nodes.end(),[&](const auto& old){return sameObject(old.object,node.object);}))added.push_back(node.object);
    state->nodes=std::move(next);
    ++state->revision;
    for(const auto& object:removed)world->freeze(object,false);
    for(const auto& node:state->nodes)world->freeze(node.object,true);
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
    if(!owner || owner->phase()!=VSNativeSession::Phase::Connected || !current || !current())return false;
    const auto account=owner->accountGuard();
    state->highlights.clear();
    state->highlightCurrent=[account,current=std::move(current)]{return account() && current();};
    state->changed();
    return state->highlightCurrent && state->highlightCurrent();
}
bool VSObjectSelection::highlight(const std::vector<VSWorldScene::ObjectHandle>& objects,bool replace)
{
    const auto state=mImpl;
    auto* world=scene();
    if(!world || !state->highlightCurrent || !state->highlightCurrent())return false;
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
    if(state->highlightCurrent && state->highlightCurrent())
        for(const auto& node:state->highlights)if(state->current(node.object))result.push_back(node.object);
    return result;
}
void VSObjectSelection::clearHighlights()
{
    const auto state=mImpl;
    state->highlights.clear();state->highlightCurrent={};
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
        if(!world || !state->highlightCurrent || !state->highlightCurrent())return;
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
    if(!state->highlightCurrent || !state->highlightCurrent())
    {
        highlightsChanged=!state->highlights.empty();
        state->highlights.clear();state->highlightCurrent={};
    }
    else
    {
        const auto count=state->highlights.size();
        state->highlights.erase(std::remove_if(state->highlights.begin(),state->highlights.end(),[&](const auto& node)
            {return !state->current(node.object);}),state->highlights.end());
        highlightsChanged=count!=state->highlights.size();
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
    if(auto* world=scene())for(const auto& node:state->nodes)world->freeze(node.object,false);
    state->nodes.clear();state->families.clear();state->highlights.clear();state->highlightCurrent={};++state->revision;
}
