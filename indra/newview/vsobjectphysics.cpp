// Uses the same GetObjectPhysicsData fields and 256-object request bound as
// LLViewerObjectList. Values remain unknown until a real simulator reply.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsobjectphysics.h"
#include "vsobjecteditparams.h"
#include "vsobjectselection.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llselectmgr.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llsdutil.h"
#include <map>

struct VSObjectPhysics::Impl : std::enable_shared_from_this<Impl>
{
    struct Entry
    {
        VSWorldScene::ObjectHandle object;
        std::function<bool()>      current;
    };
    struct Batch
    {
        std::vector<Entry>                          entries;
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter;
    };
    std::weak_ptr<VSNativeSession>           session;
    std::map<LLUUID, Entry>                  queued;
    std::map<LLUUID, std::shared_ptr<Batch>> pending;
    bool                                     active = true;
    static VSWorldScene*                     world()
    { return gViewerWindow && gViewerWindow->nativeContext() ? &gViewerWindow->nativeContext()->world() : nullptr; }
    static void finish(const Entry& entry, const LLSD* data)
    {
        auto* scene = world();
        if (!scene || !entry.current())
            return;
        const auto record = scene->editParameters(entry.object);
        if (!record)
            return;
        auto value      = record->physics;
        value.pending   = false;
        value.requested = false;
        if (data && data->has("PhysicsShapeType"))
        {
            value.shape   = U8((*data)["PhysicsShapeType"].asInteger());
            value.unknown = false;
            if (data->has("Density"))
            {
                value.density     = F32((*data)["Density"].asReal());
                value.friction    = F32((*data)["Friction"].asReal());
                value.restitution = F32((*data)["Restitution"].asReal());
                value.gravity     = F32((*data)["GravityMultiplier"].asReal());
            }
        }
        scene->setPhysicsProperties(entry.object, value);
    }
    void fetch(std::shared_ptr<Batch> batch, std::string url)
    {
        auto request       = std::make_shared<LLCore::HttpRequest>();
        LLSD post          = LLSD::emptyMap();
        post["object_ids"] = LLSD::emptyArray();
        for (const auto& entry : batch->entries)
            post["object_ids"].append(entry.object.id);
        const LLSD result = batch->adapter->postAndSuspend(request, url, post);
        if (!active)
            return;
        const auto status =
            LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
        const bool success = bool(status) && !result.has("error");
        for (const auto& entry : batch->entries)
        {
            const auto i = pending.find(entry.object.id);
            if (i == pending.end() || i->second != batch)
                continue;
            pending.erase(i);
            const std::string key = entry.object.id.asString();
            finish(entry, success && result.has(key) ? &result[key] : nullptr);
        }
        dialog_refresh_all();
    }
};
VSObjectPhysics::VSObjectPhysics(std::weak_ptr<VSNativeSession> owner) : mImpl(std::make_shared<Impl>())
{ mImpl->session = std::move(owner); }
VSObjectPhysics::~VSObjectPhysics()
{ reset(); }
void VSObjectPhysics::request(const VSWorldScene::ObjectHandle& object)
{
    const auto state = mImpl;
    const auto owner = state->session.lock();
    auto*      scene = Impl::world();
    if (!state->active || !owner || !owner->regionReady() || !scene || !scene->current(object))
        return;
    const auto record = scene->editParameters(object);
    if (!record || !record->physics.unknown)
        return;
    const auto circuit = owner->circuitGeneration(object.host);
    if (!circuit || !owner->admit("ObjectPhysicsProperties", object.host))
        return;
    const auto  account = owner->accountGuard(), source = scene->guard(object);
    Impl::Entry entry{ object, [weak = state->session, account, source, host = object.host, circuit]
                       {
                           const auto owner = weak.lock();
                           return owner && account() && source() && owner->circuitGeneration(host) == circuit;
                       } };
    const auto pending = state->pending.find(object.id);
    if (pending != state->pending.end())
    {
        for (const auto& old : pending->second->entries)
            if (old.object.id == object.id && old.current())
                return;
        state->pending.erase(pending);
    }
    state->queued[object.id] = std::move(entry);
    auto value               = record->physics;
    value.requested          = true;
    scene->setPhysicsProperties(object, value);
}
void VSObjectPhysics::tick()
{
    const auto state = mImpl;
    const auto owner = state->session.lock();
    auto*      scene = Impl::world();
    if (!state->active || !owner || !scene || state->queued.empty())
        return;
    auto        batch = std::make_shared<Impl::Batch>();
    LLHost      host;
    std::string url;
    for (auto i = state->queued.begin(); i != state->queued.end();)
    {
        if (!i->second.current())
        {
            i = state->queued.erase(i);
            continue;
        }
        const auto candidate = owner->capability(i->second.object.host, "GetObjectPhysicsData");
        if (candidate.empty())
        {
            ++i;
            continue;
        }
        if (batch->entries.empty())
        {
            host = i->second.object.host;
            url  = candidate;
        }
        if (i->second.object.host != host)
        {
            ++i;
            continue;
        }
        auto entry = i->second;
        i          = state->queued.erase(i);
        if (state->pending.count(entry.object.id))
            continue;
        const auto record = scene->editParameters(entry.object);
        if (!record)
            continue;
        auto value    = record->physics;
        value.pending = true;
        scene->setPhysicsProperties(entry.object, value);
        batch->entries.push_back(entry);
        state->pending[entry.object.id] = batch;
        if (batch->entries.size() == 256)
            break;
    }
    if (batch->entries.empty())
        return;
    batch->adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeObjectPhysics", LLCore::HttpRequest::DEFAULT_POLICY_ID);
    LLCoros::instance().launch("nativeObjectPhysics", [state, batch, url] { state->fetch(batch, url); });
}
void VSObjectPhysics::receive(const LLSD& body, const LLHost& host)
{
    const auto state = mImpl;
    const auto owner = state->session.lock();
    auto*      scene = Impl::world();
    if (!state->active || !owner || !scene || !owner->admit("ObjectPhysicsProperties", host))
        return;
    const auto selection = owner->retainObjectSelection();
    if (!selection)
        return;
    for (const auto& data : llsd::inArray(body["ObjectData"]))
    {
        const U32 local = U32(data["LocalID"].asInteger());
        for (const auto& object : selection->selected())
            if (object.host == host && object.local == local)
            {
                const auto current = scene->guard(object);
                Impl::finish({ object, current }, &data);
                state->queued.erase(object.id);
                state->pending.erase(object.id);
                break;
            }
    }
    dialog_refresh_all();
}
void VSObjectPhysics::reset()
{
    const auto state = mImpl;
    if (!state || !state->active)
        return;
    state->active = false;
    state->queued.clear();
    auto pending = std::move(state->pending);
    state->pending.clear();
    std::set<std::shared_ptr<Impl::Batch>> batches;
    for (const auto& [id, batch] : pending)
        batches.insert(batch);
    for (const auto& batch : batches)
        if (batch->adapter)
            batch->adapter->cancelSuspendedOperation();
}
