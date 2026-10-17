// The reference RenderMaterials response and LLMaterial decoder, without LLWorld. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsmaterialassets.h"
#include "vsnativesession.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llsdserialize.h"
#include "workqueue.h"
#include "llframetimer.h"
#include <zlib.h>
#include <map>
#include <array>
namespace
{
bool boundedMaterialData(const LLSD::Binary& bytes)
{
    if (bytes.empty() || bytes.size()>1024*1024) return false;
    z_stream stream{};
    if (inflateInit(&stream)!=Z_OK) return false;
    stream.next_in=const_cast<U8*>(bytes.data()); stream.avail_in=static_cast<uInt>(bytes.size());
    std::array<U8,16384> buffer{};
    int status=Z_OK;
    do {
        stream.next_out=buffer.data();stream.avail_out=static_cast<uInt>(buffer.size());
        status=inflate(&stream,Z_NO_FLUSH);
        if (stream.total_out>4*1024*1024) {inflateEnd(&stream);return false;}
    } while(status==Z_OK);
    const bool valid=status==Z_STREAM_END && stream.avail_in==0;
    inflateEnd(&stream);return valid;
}
}
struct VSMaterialAssets::Impl
{
    LLHost host;
    std::map<LLMaterialID,LLMaterialPtr> materials;
    LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t request;
    double retryAt=0;
    bool retired=false;
    explicit Impl(const LLHost& value):host(value) {}
};
VSMaterialAssets::VSMaterialAssets(const LLHost& host):mImpl(std::make_shared<Impl>(host)) {}
VSMaterialAssets::~VSMaterialAssets() {reset();mImpl->retired=true;}
void VSMaterialAssets::reset()
{
    auto request=std::exchange(mImpl->request,{});
    mImpl->materials.clear();mImpl->retryAt=0;
    if (request) request->cancelSuspendedOperation();
}
LLMaterialPtr VSMaterialAssets::get(const LLMaterialID& id)
{
    if (id.isNull()) return {};
    if (const auto found=mImpl->materials.find(id);found!=mImpl->materials.end()) return found->second;
    const auto session=VSNativeSession::active();
    if (!session || !session->regionReady() || mImpl->request || LLFrameTimer::getTotalSeconds()<mImpl->retryAt) return {};
    const auto url=session->capability(mImpl->host,"RenderMaterials");
    if (url.empty()) return {};
    mImpl->retryAt=LLFrameTimer::getTotalSeconds()+5;
    const auto guard=session->accountGuard();
    const auto circuit=session->circuitGeneration(mImpl->host);
    const auto peer=mImpl->host;
    auto request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeMaterials",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    mImpl->request=request;
    const std::weak_ptr<Impl> weak=mImpl;
    const std::weak_ptr<VSNativeSession> owner=session;
    LLCoros::instance().launch("nativeMaterials",[weak,owner,guard,circuit,peer,url,request]()
    {
        auto http=std::make_shared<LLCore::HttpRequest>();
        auto options=std::make_shared<LLCore::HttpOptions>();options->setTransferTimeout(15);options->setRetries(0);
        options->setMaximumResponseBodyBytes(2*1024*1024);
        LLSD result=request->getAndSuspend(http,url,options);
        LL::WorkQueue::getInstance("mainloop")->post([weak,owner,guard,circuit,peer,request,result]()
        {
            const auto state=weak.lock();const auto session=owner.lock();
            if (!state || state->retired || state->request!=request) return;
            state->request.reset();
            if (!guard() || !session || session->circuitGeneration(peer)!=circuit) return;
            if (!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"])) return;
            const auto& zipped=result["Zipped"].asBinary();
            if (!boundedMaterialData(zipped)) return;
            LLSD data;
            if (LLUZipHelper::unzip_llsd(data,zipped.data(),S32(zipped.size()))!=LLUZipHelper::ZR_OK ||
                !data.isArray() || data.size()>4096) return;
            std::map<LLMaterialID,LLMaterialPtr> candidate;
            for (const auto& row:llsd::inArray(data))
            {
                if (!row["ID"].isBinary() || row["ID"].asBinary().size()!=16 || !row["Material"].isMap()) return;
                candidate.emplace(LLMaterialID(row["ID"].asBinary()),new LLMaterial(row["Material"]));
            }
            state->materials=std::move(candidate);
        });
    });
    return {};
}
