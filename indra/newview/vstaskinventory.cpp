// Native task inventory uses the shared item deserializers, not GL scene objects.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vstaskinventory.h"
#include "vstaskinventorycontroller.h"
#include "vsobjectselection.h"
#include "vsobjectcontext.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "llviewerwindow.h"
#include "llviewerinventory.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llxfermanager.h"
#include "llfile.h"
#include "lldir.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include <sstream>
#include "message.h"
#include <map>
#include <algorithm>
#include <utility>

struct VSTaskInventory::Impl : std::enable_shared_from_this<Impl>
{
    struct Model
    {
        VSWorldScene::ObjectHandle object;
        std::function<bool()> current;
        std::unique_ptr<LLInventoryObject::object_list_t> contents;
        S16 serial = 0, expected = 0;
        bool known = false, dirty = false;
        U64 revision = 0;
        struct ItemRevision { LLSD fields; U64 revision = 0; };
        std::map<LLUUID, ItemRevision> items;
        F64 retryDue = 0;
    };
    std::map<LLUUID, std::shared_ptr<Model>> models;
    U64 nextRevision = 0;
    boost::signals2::signal<void(const Result&)> changed;
    static bool same(const VSWorldScene::ObjectHandle& a, const VSWorldScene::ObjectHandle& b)
    {
        return a.host == b.host && a.id == b.id && a.local == b.local && a.region == b.region &&
            a.generation == b.generation && a.account == b.account && a.accountGeneration == b.accountGeneration;
    }
    std::shared_ptr<Model> model(const VSWorldScene::ObjectHandle& object) const
    {
        const auto found = models.find(object.id);
        return active && found != models.end() && same(found->second->object, object) && found->second->current()
            ? found->second : nullptr;
    }
    LLInventoryObject* objectIn(const Model& model, const LLUUID& id) const
    {
        if (model.contents)
            for (const auto& item : *model.contents) if (item->getUUID() == id) return item;
        return nullptr;
    }
    bool publish(const std::shared_ptr<Model>& model)
    {
        if (!active || !model->current()) return false;
        std::map<LLUUID, Model::ItemRevision> revisions;
        if (model->contents)
            for (const auto& object : *model->contents)
            {
                const auto* item = dynamic_cast<const LLViewerInventoryItem*>(object.get());
                if (!item) continue;
                const LLSD fields = item->asLLSD();
                const auto old = model->items.find(item->getUUID());
                revisions.emplace(item->getUUID(), Model::ItemRevision{fields,
                    old != model->items.end() && old->second.fields == fields ? old->second.revision : ++nextRevision});
            }
        model->items = std::move(revisions);
        model->revision = ++nextRevision;
        Result result{model->object, U16(model->serial), model->known,
            model->contents ? *model->contents : LLInventoryObject::object_list_t{}};
        changed(result); // Controllers may synchronously retire this account/model.
        return active && model->current();
    }
    std::shared_ptr<Model> ensure(const VSWorldScene::ObjectHandle& object)
    {
        if (const auto existing = model(object)) return existing;
        const auto owner = session.lock();
        if (!active || !owner || !owner->regionReady() || !gViewerWindow || !gViewerWindow->nativeContext() ||
            !gViewerWindow->nativeContext()->world().current(object)) return {};
        const U64 circuit = owner->circuitGeneration(object.host);
        if (!circuit || object.account != owner->account().agent || object.accountGeneration != owner->generation()) return {};
        const auto regions = owner->regionSnapshots();
        if (std::none_of(regions.begin(), regions.end(), [&](const auto& region)
            { return region.host == object.host && region.circuit == circuit; })) return {};
        const auto account = owner->accountGuard();
        const auto objectCurrent = gViewerWindow->nativeContext()->world().guard(object);
        auto value = std::make_shared<Model>(); value->object = object;
        value->current = [weak = session, account, objectCurrent, host = object.host, circuit]
        {
            const auto owner = weak.lock();
            return owner && account() && objectCurrent() && owner->circuitGeneration(host) == circuit;
        };
        models[object.id] = value;
        return value;
    }
    struct Peer
    {
        Impl& service;
        std::shared_ptr<Model> model;
        std::optional<LLPermissions> selectedPermissions;
        explicit Peer(Impl& owner, std::shared_ptr<Model> value) : service(owner), model(std::move(value))
        {
            if (auto session = service.session.lock())
                if (auto* selection = session->objectSelection())
                    if (const auto property = selection->findProperties(model->object))
                        selectedPermissions = property->properties->permissions();
        }
        const LLUUID& id() const { return model->object.id; }
        U32 localID() const { return model->object.local; }
        const LLHost& host() const { return model->object.host; }
        bool hasRegion() const { return model->object.host.isOk(); }
        LLInventoryObject::object_list_t* inventory() const { return model->contents.get(); }
        S16& expectedSerial() { return model->expected; }
        LLInventoryObject* getInventoryObject(const LLUUID& id) { return service.objectIn(*model, id); }
        LLViewerInventoryItem* getInventoryItemByAsset(const LLUUID& asset)
        {
            if (model->contents)
                for (const auto& object : *model->contents)
                    if (auto* item = dynamic_cast<LLViewerInventoryItem*>(object.get()); item && item->getAssetUUID() == asset) return item;
            return nullptr;
        }
        void deleteInventoryItem(const LLUUID& id)
        {
            if (model->contents)
            {
                for (auto i = model->contents->begin(); i != model->contents->end(); ++i)
                    if ((*i)->getUUID() == id) { model->contents->erase(i); return; }
                doInventoryCallback();
            }
        }
        void doInventoryCallback() { service.publish(model); }
        bool permYouOwner() const
        {
            const auto context = VSObjectContext::resolve(id());
            return context && context->current() && context->youOwner;
        }
        LLPermissions* objectPermissions() { return selectedPermissions ? &*selectedPermissions : nullptr; }
        void doUpdateInventory(LLPointer<LLViewerInventoryItem>& item, U8 key, bool is_new)
        { vs_do_update_task_inventory(*this, item, key, is_new); }
    };
    struct Request
    {
        VSWorldScene::ObjectHandle object;
        std::function<bool()> current;
        std::vector<Callback> callbacks;
        U64 ticket = 0, transfer = 0;
        U16 serial = 0;
        bool receiving = false;
        F64 deadline = 0;
    };
    struct File
    {
        std::weak_ptr<Impl> owner;
        LLUUID object;
        U64 ticket = 0;
        std::string local;
    };
    std::weak_ptr<VSNativeSession> session;
    std::map<LLUUID, Request> requests;
    U64 nextTicket = 0;
    bool active = true;

    Request* find(const LLUUID& id, U64 ticket)
    {
        const auto found = requests.find(id);
        return active && found != requests.end() && found->second.ticket == ticket && found->second.current()
            ? &found->second : nullptr;
    }
    void finish(const LLUUID& id, U64 ticket, bool success, U16 serial,
                LLInventoryObject::object_list_t contents = {})
    {
        auto* request = find(id, ticket);
        if (!request) return;
        const auto current = request->current;
        const auto token = request->object;
        auto callbacks = std::move(request->callbacks);
        requests.erase(id); // Observers may request the next real revision.
        if (success)
        {
            auto model = this->model(token);
            if (model)
            {
                const bool potentially_stale = S16(serial) < model->expected;
                model->serial = model->expected = S16(serial);
                model->known = true;
                model->contents = std::make_unique<LLInventoryObject::object_list_t>(std::move(contents));
                if (potentially_stale) model->retryDue = LLFrameTimer::getTotalSeconds() + 1.;
                if (!publish(model)) success = false;
                if (model->contents) contents = *model->contents;
            }
            else success = false;
        }
        Result result{token, serial, success, std::move(contents)};

        for (const auto& callback : callbacks)
            if (active && current()) callback(result);
    }
    static void fetch(std::weak_ptr<Impl> weak, LLUUID id, U64 ticket, std::string url)
    {
        LLCoreHttpUtil::HttpCoroutineAdapter adapter("NativeTaskInventory", LLCore::HttpRequest::DEFAULT_POLICY_ID);
        auto request = std::make_shared<LLCore::HttpRequest>();
        const LLSD response = adapter.getAndSuspend(request, url);
        auto owner = weak.lock();
        if (!owner || !owner->find(id, ticket)) return;
        const auto status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(
            response[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS]);
        if (status.getType() == 304)
        {
            const auto pending = owner->find(id, ticket);
            const auto model = pending ? owner->model(pending->object) : nullptr;
            if (!model || !model->known || !model->contents) { owner->finish(id, ticket, false, 0); return; }
            owner->finish(id, ticket, true, U16(model->serial), *model->contents);
            return;
        }
        if (!status || !response["contents"].isArray() || !response["inventory_serial"].isInteger())
        {
            owner->finish(id, ticket, false, 0);
            return;
        }
        LLInventoryObject::object_list_t contents;
        // The shared task inventory model synthesizes this category too.
        contents.push_front(new LLInventoryObject(id, LLUUID::null, LLAssetType::AT_CATEGORY, "Contents"));
        for (const auto& row : llsd::inArray(response["contents"]))
        {
            LLPointer<LLViewerInventoryItem> item = new LLViewerInventoryItem;
            if (!row.has("item_id") || !item->unpackMessage(row))
            {
                owner->finish(id, ticket, false, 0);
                return;
            }
            contents.push_front(item);
        }
        owner->finish(id, ticket, true, U16(response["inventory_serial"].asInteger()), std::move(contents));
    }
    static void fileReceived(void** data, S32 error, LLExtStat)
    {
        std::unique_ptr<File> file(reinterpret_cast<File*>(data));
        auto owner = file->owner.lock();
        auto* request = owner ? owner->find(file->object, file->ticket) : nullptr;
        if (!request || error)
        {
            LLFile::remove(file->local);
            if (request) owner->finish(file->object, file->ticket, false, 0);
            return;
        }
        const U16 serial = request->serial;
        LLInventoryObject::object_list_t contents;
        llifstream input(file->local);
        bool valid = input.good();
        std::string line;
        U32 unknown = 0;
        while (valid && std::getline(input, line))
        {
            std::istringstream tokens(line);
            std::string keyword;
            if (!(tokens >> keyword)) continue;
            LLPointer<LLInventoryObject> item;
            if (keyword == "inv_item") item = new LLViewerInventoryItem;
            else if (keyword == "inv_object") item = new LLInventoryObject;
            else
            {
                if (++unknown > 25) valid = false;
                continue;
            }
            valid = item->importLegacyStream(input);
            if (item->getType() == LLAssetType::AT_CATEGORY) item->rename("Contents");
            if (valid) contents.push_front(item);
        }
        valid = valid && !input.bad();
        input.close();
        LLFile::remove(file->local);
        owner->finish(file->object, file->ticket, valid, serial, std::move(contents));
    }
};

VSTaskInventory::VSTaskInventory(std::weak_ptr<VSNativeSession> session) : mImpl(std::make_shared<Impl>())
{ mImpl->session = std::move(session); }
VSTaskInventory::~VSTaskInventory() { reset(); }
void VSTaskInventory::reset()
{
    mImpl->active = false;
    mImpl->models.clear();
    mImpl->changed.disconnect_all_slots();
    auto requests = std::move(mImpl->requests);
    mImpl->requests.clear();
    for (const auto& [id, request] : requests)
        if (request.transfer && gXferManager) gXferManager->abortRequestById(request.transfer, -1);
}
bool VSTaskInventory::request(const VSWorldScene::ObjectHandle& object, Callback callback)
{
    const auto owner = mImpl->session.lock();
    const auto model = mImpl->ensure(object);
    if (!owner || !model || !callback) return false;
    const auto current = model->current;
    auto found = mImpl->requests.find(object.id);
    if (found != mImpl->requests.end())
    {
        if (found->second.current() && found->second.object.host == object.host &&
            found->second.object.local == object.local && found->second.object.generation == object.generation &&
            found->second.object.accountGeneration == object.accountGeneration)
        {
            found->second.callbacks.push_back(std::move(callback));
            return true;
        }
        const U64 transfer = found->second.transfer;
        mImpl->requests.erase(found);
        if (transfer && gXferManager) gXferManager->abortRequestById(transfer, -1);
    }
    Impl::Request request;
    request.object = object;
    request.current = current;
    request.callbacks.push_back(std::move(callback));
    request.ticket = ++mImpl->nextTicket;
    request.deadline = LLFrameTimer::getTotalSeconds() + 30;
    const U64 ticket = request.ticket;
    mImpl->model(object)->dirty = false;
    mImpl->requests.emplace(object.id, std::move(request));
    const std::string capability = owner->capability(object.host, "RequestTaskInventory");
    if (!capability.empty())
    {
        std::string url = capability + "?task_id=" + object.id.asString();
        if (const auto model = mImpl->model(object); model && model->known && model->serial && model->contents)
            url += "&inventory_serial=" + std::to_string(model->serial);
        mImpl->requests.at(object.id).receiving = true; // capability owns this revision
        const std::weak_ptr<Impl> weak = mImpl;
        LLCoros::instance().launch("NativeTaskInventory", [weak, id=object.id, ticket, url]
            { Impl::fetch(weak, id, ticket, url); });
        return true;
    }
    if (owner->requestTaskInventory(object)) return true;
    mImpl->requests.erase(object.id);
    return false;
}
void VSTaskInventory::receive(LLMessageSystem& message)
{
    const auto impl = mImpl;
    if (std::string_view(message.getMessageName()) != "ReplyTaskInventory") return;
    LLUUID id;
    S16 serial = 0;
    std::string filename;
    message.getUUID("InventoryData", "TaskID", id);
    auto found = impl->requests.find(id);
    message.getS16("InventoryData", "Serial", serial);
    const auto owner = impl->session.lock();
    if (const auto cached = impl->models.find(id); cached != impl->models.end() && cached->second->current() &&
        cached->second->object.host == message.getSender() && owner &&
        !owner->capability(message.getSender(), "RequestTaskInventory").empty())
        cached->second->expected = serial;
    if (found == impl->requests.end())
    {
        const auto model = impl->models.find(id);
        if (model == impl->models.end() || !model->second->current() || model->second->object.host != message.getSender()) return;
        if (!request(model->second->object, [](const Result&) {})) return;
        found = impl->requests.find(id);
        if (found == impl->requests.end() || found->second.receiving) return;
    }
    const U64 ticket = found->second.ticket;
    auto* request = impl->find(id, ticket);
    if (!request || request->object.host != message.getSender() || request->receiving) return;
    message.getS16("InventoryData", "Serial", serial);
    message.getString("InventoryData", "Filename", filename);
    if (filename != LLDir::getScrubbedFileName(filename) || filename == "." || filename == ".." || (!filename.empty() && filename.size() < 4) || filename.size() > 256)
    { impl->finish(id, ticket, false, 0); return; }
    if (const auto model = impl->model(request->object))
    {
        if (serial < model->expected)
        {
            const bool repeated = serial == model->serial;
            model->serial = serial;
            if (repeated) model->expected = serial;
            model->retryDue = LLFrameTimer::getTotalSeconds() + (repeated ? 5. : 1.);
            impl->finish(id, ticket, false, U16(serial));
            return;
        }
        model->serial = model->expected = serial;
    }
    request->serial = U16(serial);
    if (filename.empty())
    {
        LLInventoryObject::object_list_t contents;
        contents.push_front(new LLInventoryObject(id, LLUUID::null, LLAssetType::AT_CATEGORY, "Contents"));
        impl->finish(id, ticket, true, U16(serial), std::move(contents));
        return;
    }
    if (!gXferManager) { impl->finish(id, ticket, false, 0); return; }
    request->receiving = true;
    auto* file = new Impl::File{impl, id, ticket,
        gDirUtilp->getExpandedFilename(LL_PATH_CACHE, LLUUID::generateNewID().asString() + ".task")};
    const U64 transfer = gXferManager->requestFile(file->local, filename, LL_PATH_CACHE, message.getSender(),
        true, &Impl::fileReceived, reinterpret_cast<void**>(file), LLXferManager::HIGH_PRIORITY);
    // requestFile may complete synchronously: never retain the old entry pointer.
    if (auto* pending = impl->find(id, ticket))
    {
        pending->transfer = transfer;
        if (!transfer) impl->finish(id, ticket, false, 0);
    }
}
void VSTaskInventory::tick()
{
    const auto impl = mImpl;
    std::vector<VSWorldScene::ObjectHandle> refresh;
    for (auto model = impl->models.begin(); model != impl->models.end();)
    {
        if (!model->second->current()) { model = impl->models.erase(model); continue; }
        if (model->second->retryDue && LLFrameTimer::getTotalSeconds() >= model->second->retryDue)
        {
            model->second->retryDue = 0;
            refresh.push_back(model->second->object);
        }
        ++model;
    }
    for (const auto& token : refresh) request(token, [](const Result&) {});
    std::vector<std::pair<LLUUID, U64>> expired;
    for (const auto& [id, request] : impl->requests)
        if (!request.current() || LLFrameTimer::getTotalSeconds() > request.deadline)
            expired.emplace_back(id, request.ticket);
    for (const auto& [id, ticket] : expired)
    {
        const auto found = impl->requests.find(id);
        if (found == impl->requests.end() || found->second.ticket != ticket) continue;
        const U64 transfer = found->second.transfer;
        const auto token = found->second.object;
        const bool retired = !found->second.current();
        const auto callbacks = retired ? std::move(found->second.callbacks) : std::vector<Callback>{};
        impl->finish(id, ticket, false, 0);
        // An invalid owner is intentionally not delivered, but must be retired.
        if (auto stale = impl->requests.find(id); stale != impl->requests.end() && stale->second.ticket == ticket)
            impl->requests.erase(stale);
        if (transfer && gXferManager) gXferManager->abortRequestById(transfer, -1);
        if (retired)
            if (const auto owner = impl->session.lock(); owner && owner->account().agent == token.account &&
                owner->generation() == token.accountGeneration)
            {
                const auto account = owner->accountGuard();
                const Result failure{token, 0, false, {}};
                for (const auto& callback : callbacks)
                    if (impl->active && account()) callback(failure);
            }
    }
}

std::optional<VSTaskInventory::Result> VSTaskInventory::snapshot(const VSWorldScene::ObjectHandle& token) const
{
    const auto model = mImpl->model(token);
    if (!model || !model->known || !model->contents) return {};
    return Result{token, U16(model->serial), true, *model->contents};
}
LLPointer<LLViewerInventoryItem> VSTaskInventory::item(const VSWorldScene::ObjectHandle& token, const LLUUID& id) const
{
    const auto model = mImpl->model(token);
    return model ? dynamic_cast<LLViewerInventoryItem*>(mImpl->objectIn(*model, id)) : nullptr;
}
LLPointer<LLInventoryObject> VSTaskInventory::object(const VSWorldScene::ObjectHandle& token, const LLUUID& id) const
{
    const auto model = mImpl->model(token);
    return model ? mImpl->objectIn(*model, id) : nullptr;
}
std::function<bool()> VSTaskInventory::guard(const VSWorldScene::ObjectHandle& token, const LLUUID& id) const
{
    const auto model = mImpl->model(token);
    if (!model) return [] { return false; };
    const auto entry = model->items.find(id);
    if (id.notNull() && entry == model->items.end()) return [] { return false; };
    const U64 revision = id.isNull() ? 0 : entry->second.revision;
    return [weak = std::weak_ptr<Impl>(mImpl), token, id, revision]
    {
        const auto service = weak.lock(); const auto model = service ? service->model(token) : nullptr;
        if (!model) return false;
        if (id.isNull()) return true;
        const auto item = model->items.find(id);
        if (item == model->items.end() || item->second.revision != revision) return false;
        const auto* value = dynamic_cast<const LLViewerInventoryItem*>(service->objectIn(*model, id));
        return value && value->asLLSD() == item->second.fields;
    };
}
boost::signals2::connection VSTaskInventory::observe(const VSWorldScene::ObjectHandle& token, Callback callback)
{
    const auto owner = mImpl->session.lock();
    const auto account = owner ? owner->accountGuard() : std::function<bool()>([] { return false; });
    return mImpl->changed.connect([token, callback, account](const Result& result)
    { if (account() && Impl::same(token, result.object)) callback(result); });
}
bool VSTaskInventory::update(const VSWorldScene::ObjectHandle& token, LLViewerInventoryItem* item, U8 key, bool is_new, bool local_only)
{
    const auto model = mImpl->ensure(token);
    if (!model || !item || !gMessageSystem) return false;
    Impl::Peer peer{*mImpl, model};
    if (local_only)
        vs_update_task_inventory_local(peer, item, key);
    else vs_update_task_inventory(peer, item, key, is_new);
    return mImpl->active && model->current();
}
bool VSTaskInventory::saveScript(const VSWorldScene::ObjectHandle& token, const LLViewerInventoryItem* item, bool running, bool is_new)
{
    const auto model = mImpl->ensure(token);
    if (!model || !item || !gMessageSystem) return false;
    Impl::Peer peer{*mImpl, model}; vs_save_task_script(peer, item, running, is_new);
    return mImpl->active && model->current();
}
bool VSTaskInventory::move(const VSWorldScene::ObjectHandle& token, const LLUUID& folder, const LLUUID& id)
{
    const auto model = mImpl->ensure(token);
    if (!model || !gMessageSystem) return false;
    Impl::Peer peer{*mImpl, model}; vs_move_task_inventory(peer, folder, id);
    return mImpl->publish(model);
}
bool VSTaskInventory::remove(const VSWorldScene::ObjectHandle& token, const LLUUID& id)
{
    const auto model = mImpl->ensure(token);
    if (!model || !gMessageSystem) return false;
    Impl::Peer peer{*mImpl, model}; vs_remove_task_inventory(peer, id);
    return mImpl->publish(model);
}
bool VSTaskInventory::completeAsset(const VSWorldScene::ObjectHandle& token, const LLUUID& id, const LLUUID& asset,
    const std::function<bool()>& current)
{
    if (!current()) return false;
    const auto existing = item(token, id);
    if (!existing) return false;
    if (existing->getAssetUUID() == asset) return current();
    const auto model = mImpl->model(token);
    if (!model) return false;
    Impl::Peer peer{*mImpl, model};
    vs_update_task_inventory_asset(peer, existing, asset);
    return mImpl->active && model->current();
}

VSTaskInventory::State VSTaskInventory::state(const VSWorldScene::ObjectHandle& token) const
{
    const auto model = mImpl->model(token);
    if (!model) return {};
    const auto request = mImpl->requests.find(token.id);
    return State{request != mImpl->requests.end() && Impl::same(request->second.object, token) && request->second.current(),
        model->dirty, model->serial};
}
void VSTaskInventory::publishLocalChanges(const VSWorldScene::ObjectHandle& token)
{
    notify(token);
}
void VSTaskInventory::notify(const VSWorldScene::ObjectHandle& token)
{
    if (const auto model = mImpl->model(token)) mImpl->publish(model);
}
void VSTaskInventory::dirty(const VSWorldScene::ObjectHandle& token)
{
    const auto model = mImpl->model(token);
    if (!model) return;
    // The native model owns real retained snapshots. Invalidating publication
    // drops this owner's list without invalidating copies held by a controller.
    model->contents.reset(); model->known = false; model->dirty = true;
    mImpl->publish(model);
}
