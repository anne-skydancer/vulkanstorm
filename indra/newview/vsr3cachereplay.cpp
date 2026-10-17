// Exercise ordinary shared fetch/cache workers through real application shutdown. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsr3cachereplay.h"
#include "llappviewer.h"
#include "lltexturefetch.h"
#include "lltexturecache.h"
#include "llimagej2c.h"
#include "llframetimer.h"
#include "llsdserialize.h"
#include "llsdutil.h"
#include "vsuiimageprovider.h"
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
namespace
{
const LLUUID readID("a3000000-0000-0000-0000-000000000003");
const LLUUID writeID("a3000000-0000-0000-0000-000000000002");
void         require(bool value, const char* message)
{
    if (!value)
        throw std::runtime_error(message);
}
struct ShutdownWrite : LLTextureCache::WriteResponder
{
    std::filesystem::path    root;
    LLPointer<LLImageJ2C>    encoded;
    LLPointer<LLImageRaw>    raw;
    LLTextureCache::handle_t handle = LLTextureCache::nullHandle();
    explicit ShutdownWrite(const std::filesystem::path& folder) : root(folder)
    {
        encoded = new LLImageJ2C;
        raw     = new LLImageRaw;
        require(encoded->load((root / "r3/texture.j2c").string()) && encoded->decode(raw, 0),
                "Pending cache shutdown source decode failed");
    }
    void completed(bool success) override
    {
        // LLTextureCache::update invokes responders with its worker-list mutex
        // unlocked. Retire the ordinary writer through its public completion API.
        const auto cache   = LLAppViewer::getTextureCache();
        const bool retired = cache && cache->writeComplete(handle);
        LLSD       record;
        record["completed"]      = true;
        record["success"]        = success;
        record["writer_retired"] = retired;
        record["id"]             = writeID;
        std::ofstream out(root / "r3/pending-cache-write.xml");
        LLSDSerialize::toPrettyXML(record, out);
    }
};
struct State
{
    bool   started  = false;
    double deadline = 0;
    U64    before   = 0;
};
State state;
} // namespace
bool vs_r3_cache_shutdown_tick(LLSD& report)
{
    const auto root  = std::filesystem::path(std::getenv("VS_VULKAN_DIAGNOSTIC_REPLAY"));
    auto*      fetch = LLAppViewer::getTextureFetch();
    auto*      cache = LLAppViewer::getTextureCache();
    require(fetch && cache && !cache->isReadOnly(), "Pending cache shutdown requires the real writable cache");
    if (!state.started)
    {
        std::string url(std::getenv("VS_VULKAN_REPLAY_LOGIN"));
        require(url.starts_with("http://127.0.0.1:") && url.ends_with("/login"), "Pending cache shutdown requires loopback transport");
        url.resize(url.size() - 6);
        url += "/asset?texture_id=" + readID.asString();
        state.before = fetch->getTotalNumHTTPRequests();
        require(fetch->createRequest(FTT_DEFAULT, url, readID, LLHost(), 100000, 1024, 1024, 4, 0, true, true) >= 0,
                "Pending shared fetch request was refused");
        state.started  = true;
        state.deadline = LLFrameTimer::getTotalSeconds() + 20;
        return false;
    }
    require(LLFrameTimer::getTotalSeconds() < state.deadline, "Pending cache shutdown HTTP observation timed out");
    if (!std::filesystem::exists(root / "r3/pending-cache-read.requested"))
        return false;
    const U64 additional = fetch->getTotalNumHTTPRequests() - state.before;
    require(additional == 1, "Pending shared fetch unexpectedly retried");
    LLPointer<ShutdownWrite> writer = new ShutdownWrite(root);
    writer->handle                  = cache->writeToCache(writeID, writer->encoded->getData(), writer->encoded->getDataSize(),
                                                          writer->encoded->getDataSize(), writer->raw, 0, writer.get());
    require(writer->handle != LLTextureCache::nullHandle() && cache->getWriter(writer->handle),
            "Pending cache shutdown did not queue an ordinary writer");
    auto& row                       = report["r3_shutdown"];
    row["http_pending"]             = true;
    row["write_queued"]             = true;
    row["additional_http_requests"] = S32(additional);
    row["write_id"]                 = writeID;
    row["read_id"]                  = readID;
    row["write_completion"]         = "r3/pending-cache-write.xml";
    row["read_observation"]         = "r3/pending-cache-read.requested";
    row["pending_workers"]          = S32(cache->getPending());
    row["ordinary_cleanup"]         = true;
    return true;
}
bool vs_r3_cache_restart_tick(LLSD& report)
{
    if (!std::getenv("VS_VULKAN_REPLAY_PENDING_CACHE_WRITE_RESTART") || report.has("r3_shutdown_restart"))
        return true;
    vs_request_world_image(writeID, 1024);
    if (vs_ui_image_discard(writeID) != 0)
        return false;
    const auto cache = vs_ui_texture_cache_evidence(true);
    for (const auto& row : llsd::inArray(cache["images"]))
        if (row["id"].asUUID() == writeID)
        {
            report["r3_shutdown_restart"] = row;
            return true;
        }
    return false;
}
