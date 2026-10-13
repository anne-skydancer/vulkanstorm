// Native immutable CPU mesh assets. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsworldassets.h"
#include "vsnativesession.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llsdserialize.h"
#include "llvolumemgr.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include "workqueue.h"
#include <zlib.h>
#include <array>
#include <sstream>
#include <map>
#include <limits>
#include <cmath>
#include <utility>
namespace
{
constexpr size_t wireLimit = 16 * 1024 * 1024;
constexpr size_t expansionLimit = 32 * 1024 * 1024;
constexpr size_t cacheLimit = 128 * 1024 * 1024;
using Key = std::pair<LLUUID, S32>;
bool inflatedBound(const U8* data, size_t size)
{
    z_stream stream{};
    if (size > std::numeric_limits<uInt>::max() || inflateInit(&stream) != Z_OK) return false;
    stream.next_in = const_cast<Bytef*>(data); stream.avail_in = static_cast<uInt>(size);
    std::array<U8, 16384> output{};
    int result = Z_OK;
    do
    {
        stream.next_out = output.data(); stream.avail_out = static_cast<uInt>(output.size());
        result = inflate(&stream, Z_NO_FLUSH);
        if (stream.total_out > expansionLimit) { inflateEnd(&stream); return false; }
    } while (result == Z_OK);
    const bool valid = result == Z_STREAM_END && stream.avail_in == 0;
    inflateEnd(&stream); return valid;
}
bool section(const LLSD& header, const char* name, size_t base, size_t total,
             size_t& offset, size_t& length)
{
    const LLSD& value = header[name];
    if (!value.isMap() || !value["offset"].isInteger() || !value["size"].isInteger()) return false;
    const S32 start = value["offset"].asInteger(), count = value["size"].asInteger();
    if (start < 0 || count <= 0 || base > total) return false;
    offset = base + static_cast<size_t>(start); length = static_cast<size_t>(count);
    return offset >= base && offset <= total && length <= total - offset;
}
bool meshVersionAllowed(const LLSD& header)
{
    // LLMeshHeader::fromLLSD converts missing legacy version to zero.
    return header["version"].asInteger() <= 999;
}
S32 meshLOD(const LLSD& header, S32 requested, size_t base, size_t total,
            size_t& offset, size_t& length)
{
    static const char* names[] = {"lowest_lod", "low_lod", "medium_lod", "high_lod"};
    if (requested < 0 || requested > 3) return -1;
    for (const char* name : names)
    {
        if (!header.has(name)) continue;
        const auto& value = header[name];
        if (!value.isMap() || !value["size"].isInteger() || value["size"].asInteger() < 0) return -1;
        size_t start = 0, size = 0;
        if (value["size"].asInteger() > 0 && !section(header, name, base, total, start, size)) return -1;
    }
    for (S32 actual = requested; actual >= 0; --actual)
        if (section(header, names[actual], base, total, offset, length)) return actual;
    for (S32 actual = requested + 1; actual < 4; ++actual)
        if (section(header, names[actual], base, total, offset, length)) return actual;
    return -1;
}
}
std::shared_ptr<const VSWorldAssets::Geometry> VSWorldAssets::decode(
    const LLVolumeParams& params, S32 lod, const LLSD::Binary& bytes, std::string& error)
{
    error.clear();
    auto fail = [&error](const char* reason) -> std::shared_ptr<const Geometry>
    { error = reason; return {}; };
    if (lod < 0 || lod > 3 || bytes.empty() || bytes.size() > wireLimit) return fail("Mesh payload/LOD exceeds admission bounds");
    llssize count = static_cast<llssize>(bytes.size()), prefix = 0;
    char* data = strip_deprecated_header(reinterpret_cast<char*>(const_cast<U8*>(bytes.data())), count, &prefix);
    std::istringstream input(std::string(data, static_cast<size_t>(count)), std::ios::binary);
    LLSD header;
    if (!LLSDSerialize::fromBinary(header, input, count) || !header.isMap() || input.tellg() < 0)
        return fail("Invalid binary mesh header");
    if (!meshVersionAllowed(header))
        return fail("Unsupported mesh version");
    const size_t base = static_cast<size_t>(prefix) + static_cast<size_t>(input.tellg());
    size_t offset = 0, length = 0;
    const S32 actual = meshLOD(header, lod, base, bytes.size(), offset, length);
    if (actual < 0) return fail("No valid complete mesh visual LOD");
    if (!inflatedBound(bytes.data() + offset, length)) return fail("Invalid or oversized mesh LOD expansion");
    auto result = std::make_shared<Geometry>(); result->lod = actual;
    result->volume = new LLVolume(params, LLVolumeLODGroup::getVolumeScaleFromDetail(actual));
    if (!result->volume->unpackVolumeFaces(const_cast<U8*>(bytes.data()) + offset, static_cast<S32>(length)))
        return fail("Shared mesh face decoder rejected LOD");
    const S32 faces = result->volume->getNumVolumeFaces();
    if (faces < 1 || faces > 8) return fail("Mesh face count outside supported bounds");
    for (S32 index = 0; index < faces; ++index)
    {
        const auto& face = result->volume->getVolumeFace(index);
        if (face.mNumVertices < 0 || face.mNumVertices > 65536 || face.mNumIndices < 0 || face.mNumIndices > 196608 || face.mNumIndices % 3 != 0)
            return fail("Mesh geometry exceeds face budget");
        if ((face.mNumVertices && (!face.mPositions || !face.mNormals || !face.mTexCoords)) || (face.mNumIndices && !face.mIndices))
            return fail("Mesh geometry arrays absent");
        for (S32 vertex = 0; vertex < face.mNumVertices; ++vertex)
        {
            const auto* position = face.mPositions[vertex].getF32ptr();
            const auto* normal = face.mNormals[vertex].getF32ptr();
            for (S32 axis = 0; axis < 3; ++axis)
                if (!std::isfinite(position[axis]) || !std::isfinite(normal[axis])) return fail("Nonfinite mesh geometry");
            if (!std::isfinite(face.mTexCoords[vertex].mV[0]) || !std::isfinite(face.mTexCoords[vertex].mV[1]))
                return fail("Nonfinite mesh texture coordinates");
        }
        result->bytes += static_cast<size_t>(face.mNumVertices) * (sizeof(LLVector4a) * 4 + sizeof(LLVector2))
                       + static_cast<size_t>(face.mNumIndices) * sizeof(U16);
        for (S32 i = 0; i < face.mNumIndices; ++i)
            if (face.mIndices[i] >= face.mNumVertices) return fail("Mesh index references absent vertex");
    }
    if (header.has("skin") && header["skin"]["size"].asInteger() > 0)
    {
        if (!section(header, "skin", base, bytes.size(), offset, length) || !inflatedBound(bytes.data() + offset, length))
            return fail("Invalid or oversized mesh skin metadata");
        if (LLUZipHelper::unzip_llsd(result->skin, bytes.data() + offset, static_cast<S32>(length)) != LLUZipHelper::ZR_OK || !result->skin.isMap())
            return fail("Shared skin metadata decoder rejected asset");
        result->bytes += expansionLimit; // Conservative bound for retained LLSD metadata.
    }
    if (result->bytes > cacheLimit) return fail("Mesh exceeds CPU cache budget");
    result->volume->setMeshAssetLoaded(true);
    return result;
}
struct VSWorldAssets::Impl
{
    struct Entry
    {
        LLVolumeParams params;
        State state = State::Pending;
        std::string error;
        std::shared_ptr<const Geometry> geometry;
        U64 revision = 0, touched = 0;
        unsigned attempts = 0;
        double retryAt = 0;
    };
    std::map<Key, Entry> entries;
    std::map<Key, LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t> requests;
    std::weak_ptr<VSNativeSession> owner;
    U64 generation = 0, epoch = 0;
    VSResourceBudget budget{cacheLimit};
    U64 nextRevision = 1, clock = 0, evictions = 0, replacements = 0;
    bool retired = false;
    bool evict(const Key* preserve = nullptr)
    {
        auto victim = entries.end();
        for (auto it = entries.begin(); it != entries.end(); ++it)
        {
            if ((preserve && it->first == *preserve) || requests.count(it->first) ||
                it->second.state == State::Pending ||
                (it->second.geometry && it->second.geometry.use_count() != 1)) continue;
            if (victim == entries.end() || it->second.touched < victim->second.touched) victim = it;
        }
        if (victim == entries.end()) return false;
        entries.erase(victim); ++evictions; return true;
    }
};
VSWorldAssets::VSWorldAssets() : mImpl(std::make_shared<Impl>()) {}
VSWorldAssets::~VSWorldAssets() { reset(); mImpl->retired = true; }
void VSWorldAssets::reset()
{
    auto requests = std::exchange(mImpl->requests, {});
    mImpl->entries.clear(); mImpl->owner.reset();
    for (auto& request : requests) request.second->cancelSuspendedOperation();
}
void VSWorldAssets::replace(const LLUUID& id)
{
    for (auto& [key, entry] : mImpl->entries)
    {
        if (key.first != id) continue;
        auto request = mImpl->requests.find(key);
        LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t adapter;
        if (request != mImpl->requests.end()) { adapter = request->second; mImpl->requests.erase(request); }
        entry.geometry.reset(); entry.error.clear(); entry.state = State::Pending;
        entry.revision = mImpl->nextRevision++; entry.attempts = 0; entry.retryAt = 0;
        ++mImpl->replacements;
        if (adapter) adapter->cancelSuspendedOperation();
    }
}
void VSWorldAssets::synchronizeOwner(const std::shared_ptr<VSNativeSession>& owner)
{
    if (mImpl->owner.lock() != owner || mImpl->generation != owner->generation())
    {
        reset(); mImpl->owner = owner; mImpl->generation = owner->generation();
        mImpl->epoch = owner->regionEpoch(); return;
    }
    if (mImpl->epoch == owner->regionEpoch()) return;
    // UUID-addressed decoded mesh data remains immutable within an account.
    // Crossing changes the authorized HTTP endpoint, not the asset identity.
    // Cancel/reissue unfinished deliveries while retaining completed assets.
    auto requests = std::exchange(mImpl->requests, {});
    mImpl->epoch = owner->regionEpoch();
    for (auto& [key, entry] : mImpl->entries)
    {
        if (entry.state == State::Ready) continue;
        entry.revision = mImpl->nextRevision++; entry.state = State::Pending;
        entry.attempts = 0; entry.retryAt = 0; entry.error.clear();
    }
    for (auto& request : requests) request.second->cancelSuspendedOperation();
}
VSWorldAssets::State VSWorldAssets::request(const LLVolumeParams& params, S32 lod)
{
    const auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected) { reset(); return State::Unavailable; }
    synchronizeOwner(owner);
    if (!owner->regionReady()) return State::Pending;
    if (lod < 0 || lod > 3 || params.getSculptID().isNull() || (params.getSculptType() & LL_SCULPT_TYPE_MASK) != LL_SCULPT_TYPE_MESH)
        return State::Unavailable;
    const Key key{params.getSculptID(), lod};
    auto found = mImpl->entries.find(key);
    if (found != mImpl->entries.end()) { found->second.touched = ++mImpl->clock; return found->second.state; }
    if (mImpl->entries.size() >= 1024 && !mImpl->evict()) return State::Unavailable;
    Impl::Entry entry; entry.params = params; entry.revision = mImpl->nextRevision++;
    entry.touched = ++mImpl->clock; mImpl->entries.emplace(key, std::move(entry));
    return State::Pending;
}
std::shared_ptr<const VSWorldAssets::Geometry> VSWorldAssets::geometry(const LLUUID& id, S32 lod) const
{
    const auto owner = VSNativeSession::active();
    if (!owner || owner != mImpl->owner.lock() || owner->generation() != mImpl->generation || owner->regionEpoch() != mImpl->epoch || owner->phase() != VSNativeSession::Phase::Connected) return {};
    auto found = mImpl->entries.find({id, lod});
    return found == mImpl->entries.end() ? nullptr : found->second.geometry;
}
std::string VSWorldAssets::error(const LLUUID& id, S32 lod) const
{
    auto found = mImpl->entries.find({id, lod}); return found == mImpl->entries.end() ? "Mesh not requested" : found->second.error;
}
void VSWorldAssets::pump()
{
    auto owner = VSNativeSession::active();
    if (!owner || owner->phase() != VSNativeSession::Phase::Connected) { reset(); return; }
    synchronizeOwner(owner);
    if (!owner->regionReady()) return;
    std::string cap = owner->capability("GetMesh2");
    if (cap.empty()) cap = owner->capability("GetMesh");
    if (cap.empty()) cap = owner->capability("ViewerAsset");
    for (auto& item : mImpl->entries)
    {
        // A transport error may be retried, but malformed assets and exhausted
        // residency are terminal until replacement or ordinary cache eviction.
        if (item.second.state == State::Error && item.second.retryAt > 0 &&
            LLFrameTimer::getTotalSeconds() >= item.second.retryAt)
        { item.second.state = State::Pending; item.second.retryAt = 0; }
        if (item.second.state != State::Pending || mImpl->requests.count(item.first)) continue;
        if (cap.empty()) { item.second.state = State::Unavailable; item.second.error = "No authenticated mesh capability"; continue; }
        if (mImpl->requests.size() >= 4) break;
        const auto key = item.first; const auto params = item.second.params;
        auto adapter = std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeWorldMesh", LLCore::HttpRequest::DEFAULT_POLICY_ID);
        mImpl->requests.emplace(key, adapter);
        ++item.second.attempts;
        const std::weak_ptr<Impl> weak = mImpl; const std::weak_ptr<VSNativeSession> session = owner;
        const U64 generation = mImpl->generation, epoch = mImpl->epoch, revision = item.second.revision;
        const std::string url = cap + (cap.find('?') == std::string::npos ? "?" : "&") + "mesh_id=" + key.first.asString();
        LLCoros::instance().launch("nativeWorldMesh", [weak, session, adapter, key, params, url, generation, epoch, revision]()
        {
            auto options = std::make_shared<LLCore::HttpOptions>(); options->setTransferTimeout(20); options->setRetries(0);
            options->setMaximumResponseBodyBytes(wireLimit);
            auto headers = std::make_shared<LLCore::HttpHeaders>(); headers->append("Range", "bytes=0-16777215");
            // A range prefix is usable only when all advertised visual LODs
            // and skin sections lie inside it; decode rejects truncation.
            // Physics sections are intentionally not decoded here.
            auto request = std::make_shared<LLCore::HttpRequest>();
            const LLSD response = adapter->getRawAndSuspend(request, url, options, headers);
            // Coroutine completion runs outside the viewer's main coroutine.
            // Decode and publish only when the mainloop executes this delivery,
            // after checking account, region, request and asset generation again.
            LL::WorkQueue::getInstance("mainloop")->post(
                [weak, session, adapter, key, params, generation, epoch, revision, response]()
                {
                    auto state = weak.lock(); const auto owner = session.lock();
                    if (!state || state->retired || !owner || VSNativeSession::active() != owner ||
                        owner->generation() != generation || owner->regionEpoch() != epoch ||
                        owner->phase() != VSNativeSession::Phase::Connected) return;
                    auto pending = state->requests.find(key);
                    if (pending == state->requests.end() || pending->second != adapter) return;
                    auto found = state->entries.find(key);
                    if (found == state->entries.end() || found->second.revision != revision) return;
                    state->requests.erase(pending);
                    auto& entry = found->second; entry.state = State::Error;
                    const auto status = LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(response["http_result"]);
                    if (!status)
                    {
                        entry.error = "Mesh HTTP request failed";
                        if (status.isRetryable() && entry.attempts < 3) entry.retryAt = LLFrameTimer::getTotalSeconds() + 2 * entry.attempts;
                        return;
                    }
                    std::shared_ptr<const Geometry> decoded;
                    try { decoded = decode(params, key.second, response[LLCoreHttpUtil::HttpCoroutineAdapter::HTTP_RESULTS_RAW].asBinary(), entry.error); }
                    catch (const std::exception& e) { entry.error = e.what(); }
                    if (!decoded) return;
                    auto reservation = state->budget.reserve(decoded->bytes);
                    while (!reservation && state->evict(&key)) reservation = state->budget.reserve(decoded->bytes);
                    if (!reservation) { entry.error = "Mesh CPU cache budget exhausted"; return; }
                    auto admitted = std::make_shared<Geometry>(*decoded);
                    admitted->generation = revision; admitted->residency = std::move(reservation);
                    entry.geometry = std::move(admitted); entry.state = State::Ready; entry.error.clear();
                });
        });
    }
}
LLSD VSWorldAssets::evidence() const
{
    LLSD result;
    result["entries"] = S32(mImpl->entries.size()); result["requests"] = S32(mImpl->requests.size());
    result["resident_bytes"] = ll_sd_from_U64(mImpl->budget.used());
    result["budget_bytes"] = ll_sd_from_U64(mImpl->budget.limit());
    result["evictions"] = ll_sd_from_U64(mImpl->evictions);
    result["replacements"] = ll_sd_from_U64(mImpl->replacements);
    return result;
}



