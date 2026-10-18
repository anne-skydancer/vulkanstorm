// The reference RenderMaterials response and LLMaterial decoder, without LLWorld. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsmaterialassets.h"
#include "vsnativesession.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "llsdserialize.h"
#include "workqueue.h"
#include "llframetimer.h"
#include "llviewerwindow.h"
#include "vsvulkancontext.h"
#include <utility>
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
    std::map<std::pair<U32,U8>,std::shared_ptr<PreparedPut>> pending;
    std::map<U64,LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t> writes;
    double nextPut=0;
    U64 nextWrite=0,epoch=1;
    bool loaded=false,retired=false;
    explicit Impl(const LLHost& value):host(value) {}
};
struct VSMaterialAssets::PreparedPut
{
    VSWorldScene::ObjectHandle object;
    U8 face=0;
    LLMaterial material;
    std::function<bool()> current;
    std::shared_ptr<const void> residency;
    std::weak_ptr<Impl> owner;
};
VSMaterialAssets::VSMaterialAssets(const LLHost& host):mImpl(std::make_shared<Impl>(host)) {}
VSMaterialAssets::~VSMaterialAssets() {reset();mImpl->retired=true;}
void VSMaterialAssets::reset()
{
    ++mImpl->epoch;
    auto request=std::exchange(mImpl->request,{});
    auto writes=std::exchange(mImpl->writes,{});
    mImpl->pending.clear();mImpl->materials.clear();mImpl->retryAt=mImpl->nextPut=0;mImpl->loaded=false;
    for(auto& [id,write]:writes)if(write)write->cancelSuspendedOperation();
    if (request) request->cancelSuspendedOperation();
}
LLMaterialPtr VSMaterialAssets::get(const LLMaterialID& id)
{
    if (id.isNull()) return {};
    if (const auto found=mImpl->materials.find(id);found!=mImpl->materials.end()) return found->second;
    requestAll();return {};
}
bool VSMaterialAssets::loaded() const{return mImpl->loaded;}
const std::map<LLMaterialID,LLMaterialPtr>& VSMaterialAssets::materials() const{return mImpl->materials;}
void VSMaterialAssets::requestAll()
{
    const auto session=VSNativeSession::active();
    if (!session || !session->regionReady() || mImpl->request || LLFrameTimer::getTotalSeconds()<mImpl->retryAt) return;
    const auto url=session->capability(mImpl->host,"RenderMaterials");
    if (url.empty()) return;
    mImpl->retryAt=LLFrameTimer::getTotalSeconds()+5;
    const auto guard=session->accountGuard();
    const auto circuit=session->circuitGeneration(mImpl->host);
    const auto peer=mImpl->host;
    auto request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeMaterials",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    mImpl->request=request;mImpl->loaded=false;
    const std::weak_ptr<Impl> weak=mImpl;
    const std::weak_ptr<VSNativeSession> owner=session;
    LLCoros::instance().launch("nativeMaterials",[weak,owner,guard,circuit,peer,url,request]()
    {
        const auto initial=weak.lock();const auto initialOwner=owner.lock();
        if(!initial || initial->retired || initial->request!=request || !guard() || !initialOwner || initialOwner->circuitGeneration(peer)!=circuit)
        {if(initial && initial->request==request)initial->request.reset();return;}
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
            state->materials=std::move(candidate);state->loaded=true;
        });
    });
}

std::shared_ptr<VSMaterialAssets::PreparedPut> VSMaterialAssets::preparePut(const VSWorldScene::ObjectHandle& token,U8 face,const LLMaterial& material)
{
    const auto session=VSNativeSession::active();
    if(mImpl->retired || !session || token.host!=mImpl->host || !gViewerWindow || !gViewerWindow->nativeContext())return {};
    auto& world=gViewerWindow->nativeContext()->world();
    if(!world.current(token) || face>=world.textureEntryCount(token) || session->capability(token.host,"RenderMaterials").empty())return {};
    const auto account=session->accountGuard();const auto circuit=session->circuitGeneration(token.host);
    if(!account() || !circuit)return {};
    auto residency=world.reserveSpatialBytes(sizeof(PreparedPut)+sizeof(LLMaterial)+64);if(!residency)return {};
    const auto source=world.guard(token);const std::weak_ptr<VSNativeSession> owner=session;
    auto value=std::make_shared<PreparedPut>();value->object=token;value->face=face;value->material=material;value->residency=std::move(residency);value->owner=mImpl;
    const auto epoch=mImpl->epoch;const std::weak_ptr<Impl> state=mImpl;
    value->current=[owner,account,circuit,host=token.host,source,state,epoch]
    {const auto session=owner.lock();const auto active=state.lock();return active && !active->retired && active->epoch==epoch && session && account() && session->circuitGeneration(host)==circuit && source();};
    if(!value->current())return {};
    return value;
}
bool VSMaterialAssets::put(const std::shared_ptr<PreparedPut>& value)
{
    if(!value || value->owner.lock()!=mImpl || mImpl->retired || !value->current())return false;
    mImpl->pending.insert_or_assign({value->object.local,value->face},value);return true;
}
bool VSMaterialAssets::put(const VSWorldScene::ObjectHandle& token,U8 face,const LLMaterial& material)
{
    return put(preparePut(token,face,material));
}
void VSMaterialAssets::tick()
{
    const auto state=mImpl;const auto session=VSNativeSession::active();
    if(state->retired || !session || state->pending.empty())return;
    for(auto item=state->pending.begin();item!=state->pending.end();)
        if(!item->second->current())item=state->pending.erase(item);else ++item;
    if(state->pending.empty() || LLFrameTimer::getTotalSeconds()<state->nextPut || !session->capabilitiesReceived(state->host))return;
    const auto url=session->capability(state->host,"RenderMaterials");if(url.empty())return;
    U32 maximum=50;
    if(const auto features=session->simulatorFeatures(state->host);features && (*features)["MaxMaterialsPerTransaction"].isInteger())
    {
        const S32 value=(*features)["MaxMaterialsPerTransaction"].asInteger();
        if(value<=0 || value>4096)return; // Refuse invalid/unbounded advertised protocol limits.
        maximum=U32(value);
    }
    LLSD faces=LLSD::emptyArray();std::vector<std::shared_ptr<PreparedPut>> accepted;accepted.reserve(llmin<std::size_t>(maximum,state->pending.size()));
    for(auto item=state->pending.begin();item!=state->pending.end() && accepted.size()<maximum;)
    {
        if(!item->second->current()){item=state->pending.erase(item);continue;}
        LLSD face=LLSD::emptyMap();face["ID"]=static_cast<LLSD::Integer>(item->second->object.local);face["Face"]=static_cast<LLSD::Integer>(item->second->face);
        if(!item->second->material.isNull())face["Material"]=item->second->material.asLLSD();
        faces.append(face);accepted.push_back(item->second);item=state->pending.erase(item);
    }
    if(accepted.empty())return;
    LLSD data=LLSD::emptyMap();data["FullMaterialsPerFace"]=faces;
    const auto zipped=zip_llsd(data);if(zipped.empty())return;
    LLSD body=LLSD::emptyMap();body["Zipped"]=LLSD::Binary(zipped.begin(),zipped.end());
    state->nextPut=LLFrameTimer::getTotalSeconds()+1.;
    const U64 serial=++state->nextWrite;
    auto request=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeMaterialPut",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    state->writes[serial]=request;const std::weak_ptr<Impl> weak=state;
    LLCoros::instance().launch("nativeMaterialPut",[weak,request,serial,url,body,accepted=std::move(accepted)]
    {
        const auto active=weak.lock();
        if(!active || active->retired || active->writes.find(serial)==active->writes.end())return;
        for(const auto& value:accepted)if(!value->current()){active->writes.erase(serial);return;}
        auto http=std::make_shared<LLCore::HttpRequest>();auto options=std::make_shared<LLCore::HttpOptions>();
        options->setTransferTimeout(15);options->setRetries(0);options->setMaximumResponseBodyBytes(2*1024*1024);
        LLSD result=request->putAndSuspend(http,url,body,options);
        LL::WorkQueue::getInstance("mainloop")->post([weak,request,serial,result,accepted]
        {
            const auto state=weak.lock();if(!state || state->retired)return;
            const auto write=state->writes.find(serial);if(write==state->writes.end() || write->second!=request)return;
            state->writes.erase(write);
            for(const auto& value:accepted)if(!value->current())return;
            if(!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]))return;
            const auto& bytes=result["Zipped"].asBinary();if(!boundedMaterialData(bytes))return;
            LLSD rows;if(LLUZipHelper::unzip_llsd(rows,bytes.data(),S32(bytes.size()))!=LLUZipHelper::ZR_OK || !rows.isArray() || rows.size()>4096)return;
            for(const auto& row:llsd::inArray(rows))
                if(!row["ID"].isInteger() || !row["Face"].isInteger() || !row["MaterialID"].isBinary() || row["MaterialID"].asBinary().size()!=16)return;
            // Original PUT response only validates acknowledgements. The actual
            // authenticated ObjectUpdate publishes the simulator's material ID.
        });
    });
}
