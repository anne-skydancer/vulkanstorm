// Native task inventory uses the shared item deserializers, not GL scene objects.
// LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vstaskinventory.h"
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
        Result result{request->object, serial, success, std::move(contents)};
        auto callbacks = std::move(request->callbacks);
        requests.erase(id); // listeners may synchronously request the next revision
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
    auto requests = std::move(mImpl->requests);
    mImpl->requests.clear();
    for (const auto& [id, request] : requests)
        if (request.transfer && gXferManager) gXferManager->abortRequestById(request.transfer, -1);
}
bool VSTaskInventory::request(const VSWorldScene::ObjectHandle& object, Callback callback)
{
    auto owner = mImpl->session.lock();
    if (!mImpl->active || !owner || !callback || !owner->regionReady() || !gViewerWindow ||
        !gViewerWindow->nativeContext() || !gViewerWindow->nativeContext()->world().current(object)) return false;
    const auto account = owner->accountGuard();
    const auto circuit = owner->circuitGeneration(object.host);
    const auto objectCurrent = gViewerWindow->nativeContext()->world().guard(object);
    if (!circuit || object.account != owner->account().agent || object.accountGeneration != owner->generation()) return false;
    const auto regions = owner->regionSnapshots();
    const bool authenticated = std::any_of(regions.begin(), regions.end(), [&](const auto& region)
        { return region.host == object.host && region.circuit == circuit; });
    if (!authenticated) return false;
    const auto current = [weak=mImpl->session, account, objectCurrent, host=object.host, circuit]
    {
        auto session = weak.lock();
        return session && account() && objectCurrent() && session->circuitGeneration(host) == circuit;
    };
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
    mImpl->requests.emplace(object.id, std::move(request));
    const std::string capability = owner->capability(object.host, "RequestTaskInventory");
    if (!capability.empty())
    {
        const std::string url = capability + "?task_id=" + object.id.asString();
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
    const auto found = impl->requests.find(id);
    if (found == impl->requests.end()) return;
    const U64 ticket = found->second.ticket;
    auto* request = impl->find(id, ticket);
    if (!request || request->object.host != message.getSender() || request->receiving) return;
    message.getS16("InventoryData", "Serial", serial);
    message.getString("InventoryData", "Filename", filename);
    if (filename != LLDir::getScrubbedFileName(filename) || filename == "." || filename == ".." || (!filename.empty() && filename.size() < 4) || filename.size() > 256)
    { impl->finish(id, ticket, false, 0); return; }
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
    std::vector<std::pair<LLUUID, U64>> expired;
    for (const auto& [id, request] : impl->requests)
        if (!request.current() || LLFrameTimer::getTotalSeconds() > request.deadline)
            expired.emplace_back(id, request.ticket);
    for (const auto& [id, ticket] : expired)
    {
        const auto found = impl->requests.find(id);
        if (found == impl->requests.end() || found->second.ticket != ticket) continue;
        const U64 transfer = found->second.transfer;
        impl->finish(id, ticket, false, 0);
        // An invalid owner is intentionally not delivered, but must be retired.
        if (auto stale = impl->requests.find(id); stale != impl->requests.end() && stale->second.ticket == ticket)
            impl->requests.erase(stale);
        if (transfer && gXferManager) gXferManager->abortRequestById(transfer, -1);
    }
}
