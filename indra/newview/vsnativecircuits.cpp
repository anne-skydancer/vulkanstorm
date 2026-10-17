// Native simulator circuit/region ownership. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#include "llviewerwindow.h"
#include "llviewercontrol.h"
#include "llframetimer.h"
#include "llsdutil.h"
#include "lluri.h"
#include "llcorehttputil.h"
#include "llcoros.h"
#include "lleventpoll.h"
#include "workqueue.h"
#include "message.h"
#include <utility>
namespace
{
bool circuitURL(const std::string& url)
{
    return url.size()<=4096 && (url.compare(0,8,"https://")==0 || url.compare(0,7,"http://")==0) && !LLURI(url).hostName().empty();
}
}
U64 VSNativeSession::circuitGeneration(const LLHost& host) const
{
    if (mShutdown || mPhase==Phase::Disconnected || mPhase==Phase::Login) return 0;
    const auto region=mCircuits.find(host);
    return region==mCircuits.end()?0:region->second.incarnation;
}
bool VSNativeSession::enableNeighbour(const LLHost& host,U64 handle,U32 width,U32 height)
{
    if (!host.isOk() || width<16 || height<16 || width>65536 || height>65536 || !gMessageSystem) return false;
    if (host==mHost) return handle==mHandle;
    const auto existing=mCircuits.find(host);
    if (existing!=mCircuits.end() && existing->second.handle!=handle) retireNeighbour(host);
    auto found=mCircuits.find(host);
    if (found!=mCircuits.end())
    {
        if (gViewerWindow && gViewerWindow->nativeContext() &&
            !gViewerWindow->nativeContext()->world().openRegion(host,handle,width,height,found->second.incarnation)) return false;
        found->second.width=width;found->second.height=height;return true;
    }
    if (mCircuits.size()>=65) return false;
    auto& region=mCircuits[host];region.handle=handle;region.width=width;region.height=height;
    region.incarnation=mNextCircuitGeneration++;region.deadline=LLFrameTimer::getTotalSeconds()+60;
    if (gViewerWindow && gViewerWindow->nativeContext() &&
        !gViewerWindow->nativeContext()->world().openRegion(host,handle,width,height,region.incarnation))
    { mCircuits.erase(host);return false; }
    gMessageSystem->enableCircuit(host,true);
    gMessageSystem->newMessage("UseCircuitCode");gMessageSystem->nextBlock("CircuitCode");
    gMessageSystem->addU32("Code",mCircuit);gMessageSystem->addUUID("SessionID",mSession);gMessageSystem->addUUID("ID",mAgent);
    struct Delivery { std::weak_ptr<VSNativeSession> owner;LLHost host;U64 account,circuit; };
    auto* delivery=new Delivery{weak_from_this(),host,mGeneration,region.incarnation};
    gMessageSystem->sendReliable(host,3,false,F32Seconds(5),[](void** userdata,S32 result)
    {
        std::unique_ptr<Delivery> delivery(reinterpret_cast<Delivery*>(userdata));
        LL::WorkQueue::getInstance("mainloop")->post([weak=delivery->owner,host=delivery->host,
            account=delivery->account,circuit=delivery->circuit,result]()
        { if (auto owner=weak.lock()) owner->neighbourCircuitResult(host,account,circuit,result); });
    },reinterpret_cast<void**>(delivery));
    return true;
}
void VSNativeSession::retireNeighbour(const LLHost& host)
{
    if (host==mHost) return;
    auto found=mCircuits.find(host);if (found==mCircuits.end()) return;
    auto cancel=std::move(found->second.cancel);mCircuits.erase(found);
    if (cancel) cancel();
    if (gViewerWindow && gViewerWindow->nativeContext()) gViewerWindow->nativeContext()->world().retireRegion(host);
    if (gMessageSystem)
    {
        gMessageSystem->newMessage("AgentQuitCopy");gMessageSystem->nextBlock("AgentData");
        gMessageSystem->addUUID("AgentID",mAgent);gMessageSystem->addUUID("SessionID",mSession);
        gMessageSystem->nextBlock("FuseBlock");gMessageSystem->addU32("ViewerCircuitCode",mCircuit);gMessageSystem->sendMessage(host);
        gMessageSystem->disableCircuit(host);gMessageSystem->mCircuitInfo.removeCircuitData(host);
    }
}
void VSNativeSession::neighbourCircuitResult(const LLHost& host,U64 account,U64 circuit,S32 result)
{
    if (mShutdown || (mPhase!=Phase::Connecting && mPhase!=Phase::Connected) || account!=mGeneration || host==mHost || circuitGeneration(host)!=circuit) return;
    if (result) { retireNeighbour(host);return; }
    auto& region=mCircuits.at(host);region.acknowledged=true;
    if (region.handshake) region.deadline=0;
}
bool VSNativeSession::regionEvent(const std::string& name,const LLSD& body,const LLHost& sender,U64 account,U64 circuit)
{
    if (mShutdown || account!=mGeneration || !circuit || circuitGeneration(sender)!=circuit ||
        (mPhase!=Phase::Connecting && mPhase!=Phase::Connected) || !body.isMap()) return false;
    if (name=="EnableSimulator")
    {
        if (sender!=mHost) return false;
        const LLSD& rows=body.has("SimulatorInfo")?body["SimulatorInfo"]:body["Simulators"];
        if (!rows.isArray() || rows.size()>64) return false;
        bool accepted=true;
        for (const auto& row:llsd::inArray(rows))
        {
            if (!row.isMap() || row["Handle"].asBinary().size()!=8 || row["IP"].asBinary().size()!=4 ||
                !row["Port"].isInteger() || row["Port"].asInteger()<1 || row["Port"].asInteger()>65535) return false;
            const U64 handle=ll_U64_from_sd(row["Handle"]);
            if (gSavedSettings.getBOOL("FSDisableNeighbourRegionConnections") && handle!=mHandle) continue;
            const LLHost destination(ll_ipaddr_from_sd(row["IP"]),row["Port"].asInteger());
            const U32 width=row.has("RegionSizeX")?U32(row["RegionSizeX"].asInteger()):256;
            const U32 height=row.has("RegionSizeY")?U32(row["RegionSizeY"].asInteger()):256;
            accepted=enableNeighbour(destination,handle,width?width:256,height?height:256) && accepted;
        }
        return accepted;
    }
    if (name=="EstablishAgentCommunication")
    {
        if (sender!=mHost || body["agent-id"].asUUID()!=mAgent) return false;
        const LLHost destination(body["sim-ip-and-port"].asString());
        const std::string seed=body["seed-capability"].asString();
        auto found=mCircuits.find(destination);
        if (found==mCircuits.end() || destination==mHost || !circuitURL(seed)) return false;
        auto cancel=std::exchange(found->second.cancel,{});if (cancel) cancel();
        const auto weak=weak_from_this();const U64 incarnation=found->second.incarnation;
        LLCoros::instance().launch("nativeNeighbourCapabilities",[weak,destination,seed,account,incarnation]()
        { if (auto owner=weak.lock()) owner->neighbourSeedCoro(destination,seed,account,incarnation); });
        return true;
    }
    return false;
}
void VSNativeSession::neighbourSeedCoro(LLHost host,std::string seed,U64 account,U64 incarnation)
{
    if (mShutdown || (mPhase!=Phase::Connecting && mPhase!=Phase::Connected) || account!=mGeneration || host==mHost || circuitGeneration(host)!=incarnation) return;
    auto adapter=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeNeighbourSeed",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    auto& region=mCircuits.at(host);const U64 requestRevision=++region.requestRevision;
    region.cancel=[weak=std::weak_ptr<LLCoreHttpUtil::HttpCoroutineAdapter>(adapter)]()
        { if (auto request=weak.lock()) request->cancelSuspendedOperation(); };
    auto request=std::make_shared<LLCore::HttpRequest>();auto options=std::make_shared<LLCore::HttpOptions>();
    options->setTransferTimeout(15);options->setRetries(2);
    LLSD wanted = LLSD::emptyArray();
    for (const char* name :
         { "EventQueueGet", "GetMesh", "GetMesh2", "GetTexture", "ViewerAsset", "RenderMaterials", "ModifyMaterialParams", "ObjectMedia", "ObjectMediaNavigate", "UpdateAvatarAppearance", "UntrustedSimulatorMessage" })
        wanted.append(name);
    const LLSD result=adapter->postAndSuspend(request,seed,wanted,options);
    const auto weak=weak_from_this();
    LL::WorkQueue::getInstance("mainloop")->post([weak,host,account,incarnation,requestRevision,result,adapter]()
    {
        auto owner=weak.lock();
        if (!owner || owner->mShutdown || (owner->mPhase!=Phase::Connecting && owner->mPhase!=Phase::Connected) ||
            owner->mGeneration!=account || owner->mHost==host || owner->circuitGeneration(host)!=incarnation) return;
        auto& region=owner->mCircuits.at(host);if (region.requestRevision!=requestRevision) return;region.cancel={};
        region.seedFailed=!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]) || !result.isMap();
        if (region.seedFailed) return;
        region.capabilities=LLSD::emptyMap();
        for (const char* name :
             { "EventQueueGet", "GetMesh", "GetMesh2", "GetTexture", "ViewerAsset", "RenderMaterials", "ModifyMaterialParams", "ObjectMedia", "ObjectMediaNavigate", "UpdateAvatarAppearance", "UntrustedSimulatorMessage" })
            if (circuitURL(result[name].asString())) region.capabilities[name]=result[name];
        if (circuitURL(region.capabilities["EventQueueGet"].asString())) region.poll=std::make_unique<LLEventPoll>(region.capabilities["EventQueueGet"].asString(),host);
    });
}

std::string VSNativeSession::capability(const LLHost& host,const std::string& name) const
{
    if(host==mHost) return capability(name);
    const auto found=mCircuits.find(host);
    return found!=mCircuits.end() && found->second.acknowledged ? found->second.capabilities[name].asString() : std::string();
}
