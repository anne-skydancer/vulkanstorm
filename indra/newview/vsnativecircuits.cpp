// Native simulator circuit/region ownership. LGPL-2.1, like the viewer.
#include "llviewerprecompiledheaders.h"
#include "vsnativesession.h"
#include "vscapabilities.h"
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
#include "llcallingcard.h"
#include "llregionhandle.h"
#include "llregioninfomodel.h"
#include "llagent.h"
#include <utility>
namespace
{
bool circuitURL(const std::string& url)
{
    return url.size()<=4096 && (url.compare(0,8,"https://")==0 || url.compare(0,7,"http://")==0) && !LLURI(url).hostName().empty();
}
}
// Native CPU spatial snapshots; no legacy region or scene ownership.
VSParcelOverlay::Snapshot VSNativeSession::parcelOverlay(const LLHost& host) const
{
    if(mShutdown || mPhase!=Phase::Connected)return {};
    const auto found=mCircuits.find(host);
    return found!=mCircuits.end() && found->second.acknowledged && found->second.handshake ? found->second.parcelOverlay : nullptr;
}
std::optional<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshot(const LLHost& host,bool includeSpatial) const
{
    if(mShutdown || mPhase!=Phase::Connected)return {};
    const auto found=mCircuits.find(host);
    if(found==mCircuits.end() || !found->second.acknowledged || !found->second.handshake)return {};
    const auto& region=found->second;
        RegionSnapshot row;
        row.account=account();row.host=host;row.handle=region.handle;row.circuit=region.incarnation;
        row.width=region.width;row.height=region.height;row.id=region.id;row.owner=region.owner;
        row.name=region.name;row.flags=region.flags;row.access=region.access;
        row.estateManager=region.estateManager;row.estateManagerKnown=region.estateManagerKnown;
        row.metadata=region.metadata;
        row.objectCapacity=region.objectCapacity;row.objectCapacityKnown=region.objectCapacityKnown;
        row.waterHeight=region.waterHeight;row.waterKnown=region.waterKnown;row.current=host==mHost;
        if(includeSpatial){row.coarse=region.coarse.avatars;row.parcelOverlay=region.parcelOverlay;}
    return row;
}
std::vector<VSNativeSession::RegionSnapshot> VSNativeSession::regionSnapshots(bool includeSpatial) const
{
    std::vector<RegionSnapshot> result;
    if(mShutdown || mPhase!=Phase::Connected)return result;
    result.reserve(mCircuits.size());
    for(const auto& [host,region]:mCircuits)
        if(auto row=regionSnapshot(host,includeSpatial))result.push_back(std::move(*row));
    return result;
}
bool VSNativeSession::current(const RegionSnapshot& snapshot) const
{
    if(mShutdown || mPhase!=Phase::Connected || !current(snapshot.account))return false;
    const auto found=mCircuits.find(snapshot.host);
    return found!=mCircuits.end() && found->second.acknowledged && found->second.handshake &&
        found->second.incarnation==snapshot.circuit && found->second.handle==snapshot.handle &&
        found->second.id==snapshot.id;
}
std::optional<std::pair<LLHost,VSCoarseSample<LLUUID>>> VSNativeSession::coarseAvatarLocation(const LLUUID& id) const
{
    if(mShutdown || mPhase!=Phase::Connected || id.isNull())return {};
    for(const auto& [host,region]:mCircuits)
        if(region.acknowledged && region.handshake)
            for(const auto& avatar:region.coarse.avatars)
                if(avatar.id==id)return std::make_pair(host,avatar);
    return {};
}
std::vector<VSCoarseSample<LLUUID>> VSNativeSession::coarseAvatarLocations() const
{
    std::vector<VSCoarseSample<LLUUID>> result;
    if(mShutdown || mPhase!=Phase::Connected)return result;
    for(const auto& [host,region]:mCircuits)
        if(region.acknowledged && region.handshake)
            result.insert(result.end(),region.coarse.avatars.begin(),region.coarse.avatars.end());
    return result;
}
bool VSNativeSession::receiveParcelOverlay(const LLHost& host,U64 account,U64 incarnation,S32 sequence,const std::vector<U8>& bytes)
{
    const auto found=mCircuits.find(host);
    if(mShutdown || mPhase!=Phase::Connected || account!=mGeneration || !incarnation ||
       found==mCircuits.end() || found->second.incarnation!=incarnation ||
       !found->second.acknowledged || !found->second.handshake || !gViewerWindow || !gViewerWindow->nativeContext())return false;
    auto& world=gViewerWindow->nativeContext()->world();
    const auto next=VSParcelOverlay::updated(found->second.parcelOverlay,found->second.width,found->second.height,
        sequence,bytes.data(),bytes.size(),[&world](std::size_t amount){return world.reserveSpatialBytes(amount);});
    if(!next)return false;
    found->second.parcelOverlay=next;
    return true;
}

bool VSNativeSession::applyRegionStats(const LLHost& host,U64 account,U64 incarnation,U64 flags,U32 capacity)
{
    const auto found=mCircuits.find(host);
    if(mShutdown || mPhase!=Phase::Connected || mChangingRegion || account!=mGeneration || host!=mHost || !incarnation ||
       found==mCircuits.end() || found->second.incarnation!=incarnation ||
       !found->second.acknowledged || !found->second.handshake)return false;
    mFlags=flags;
    found->second.flags=flags;found->second.objectCapacity=capacity;found->second.objectCapacityKnown=true;
    return true;
}

bool VSNativeSession::receiveCoarse(const LLSD& body,const LLHost& host,U64 account,U64 incarnation,bool legacyUDP)
{
    const auto found=mCircuits.find(host);
    if(mShutdown || mPhase!=Phase::Connected || account!=mGeneration || !incarnation ||
        found==mCircuits.end() || found->second.incarnation!=incarnation ||
        !found->second.acknowledged || !found->second.handshake)return false;
    if(!body.isMap() || !body["Location"].isArray() || body["Location"].size()>4096 ||
        !body["Index"].isArray() || body["Index"].size()!=1 ||
        !body["Index"][0]["You"].isInteger() || !body["Index"][0]["Prey"].isInteger())return false;
    const bool identified=body.has("AgentData");
    if(identified && (!body["AgentData"].isArray() || body["AgentData"].size()!=body["Location"].size()))return false;
    std::vector<VSCoarseInput<LLUUID>> rows;
    for(S32 i=0;i<body["Location"].size();++i)
    {
        const auto& location=body["Location"][i];
        if(!location.isMap() || !location["X"].isInteger() || !location["Y"].isInteger() || !location["Z"].isInteger())return false;
        if(identified && !body["AgentData"][i]["AgentID"].isUUID())return false;
        rows.push_back({identified?body["AgentData"][i]["AgentID"].asUUID():LLUUID::null,
            location["X"].asInteger(),location["Y"].asInteger(),location["Z"].asInteger()});
    }
    const auto origin=from_region_handle(found->second.handle);
    VSCoarseSnapshot<LLUUID> snapshot;
    if(!vs_decode_coarse(rows,body["Index"][0]["You"].asInteger(),body["Index"][0]["Prey"].asInteger(),
        found->second.width,found->second.height,{origin.mdV[0],origin.mdV[1],origin.mdV[2]},legacyUDP,snapshot))return false;
    found->second.coarse=std::move(snapshot);
    if(found->second.coarse.prey)
    {
        const auto& position=*found->second.coarse.prey;
        LLAvatarTracker::instance().setTrackedCoarseLocation(LLVector3d(position[0],position[1],position[2]));
    }
    return true;
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
    updateInterestListMode();
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
    const LLSD wanted=vs_native_capability_names();
    const LLSD result=adapter->postAndSuspend(request,seed,wanted,options);
    const auto weak=weak_from_this();
    LL::WorkQueue::getInstance("mainloop")->post([weak,host,account,incarnation,requestRevision,result,adapter,wanted]()
    {
        auto owner=weak.lock();
        if (!owner || owner->mShutdown || (owner->mPhase!=Phase::Connecting && owner->mPhase!=Phase::Connected) ||
            owner->mGeneration!=account || owner->mHost==host || owner->circuitGeneration(host)!=incarnation) return;
        auto& region=owner->mCircuits.at(host);if (region.requestRevision!=requestRevision) return;region.cancel={};
        region.seedFailed=!LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]) || !result.isMap();
        if (region.seedFailed) return;
        region.featuresRequest.reset();
        region.featuresReceived=false;
        region.simulatorFeatures=LLSD::emptyMap();
        region.capabilitiesReady=true;
        region.capabilities=LLSD::emptyMap();
        const LLSD& requestedCapabilities=wanted;
        for (auto requested=requestedCapabilities.beginArray();requested!=requestedCapabilities.endArray();++requested)
        {
            const std::string name=requested->asString();
            if (circuitURL(result[name].asString())) region.capabilities[name]=result[name];
        }
        if (circuitURL(region.capabilities["EventQueueGet"].asString())) region.poll=std::make_unique<LLEventPoll>(region.capabilities["EventQueueGet"].asString(),host);
        owner->updateInterestListMode();
        const std::string features=region.capabilities["SimulatorFeatures"].asString();
        if (!features.empty()) LLCoros::instance().launch("nativeNeighbourFeatures", [weak,host,features,account,incarnation]
            { neighbourFeaturesCoro(weak,host,features,account,incarnation); });
    });
}

std::string VSNativeSession::capability(const LLHost& host,const std::string& name) const
{
    if(host==mHost) return capability(name);
    const auto found=mCircuits.find(host);
    return found!=mCircuits.end() && found->second.acknowledged ? found->second.capabilities[name].asString() : std::string();
}

// Existing shared RegionInfo parser, authenticated by the actual native account/circuit.
bool VSNativeSession::applyRegionInfo(LLMessageSystem& message)
{
    if(message.getMessageName()!=std::string("RegionInfo") || !regionReady() || message.getSender()!=mHost)return false;
    const auto region=mCircuits.find(mHost);
    if(region==mCircuits.end() || !region->second.acknowledged || !region->second.handshake)return false;
    LLUUID agent,session;
    message.getUUID("AgentData","AgentID",agent);message.getUUID("AgentData","SessionID",session);
    if(agent!=mAgent || session!=mSession)return false;
    F32 billable,water;
    message.getF32("RegionInfo","BillableFactor",billable);message.getF32("RegionInfo","WaterHeight",water);
    if(!std::isfinite(billable) || !std::isfinite(water))return false;
    const LLHost sender=message.getSender();
    const U64 epoch=mRegionEpoch;
    const auto accountCurrent=accountGuard();
    const U64 incarnation=region->second.incarnation;
    auto& model=LLRegionInfoModel::instance();
    model.update(&message,false);
    // Publish actual CPU region values before model observers inspect permissions.
    const bool wasFlying=gAgent.getFlying();
    mName=model.mSimName;mAccess=model.mSimAccess;mFlags=model.mRegionFlags;
    mWaterHeight=model.mWaterHeight;mWaterKnown=true;
    auto& circuit=region->second;
    circuit.name=mName;circuit.access=mAccess;circuit.flags=mFlags;
    circuit.waterHeight=mWaterHeight;circuit.waterKnown=true;
    circuit.metadata.billableFactor=model.mBillableFactor;circuit.metadata.billableKnown=true;
    if(!model.mSimType.empty()){circuit.metadata.productName=model.mSimType;circuit.metadata.productKnown=true;}
    if(wasFlying)gAgent.setFlying(gAgent.canFly());
    if(!accountCurrent() || mRegionEpoch!=epoch || mHost!=sender || circuitGeneration(sender)!=incarnation)return false;
    model.mUpdateSignal();
    // An observer may synchronously change account/region or close the session.
    return accountCurrent() && mRegionEpoch==epoch && mHost==sender && regionReady() && circuitGeneration(sender)==incarnation;
}

bool VSNativeSession::publishRegionOwner(const LLHost& host,U64 incarnation,const LLUUID& owner)
{
    if(!regionReady() || host!=mHost || circuitGeneration(host)!=incarnation)return false;
    const auto found=mCircuits.find(host);
    if(found==mCircuits.end() || !found->second.acknowledged || !found->second.handshake)return false;
    found->second.owner=owner;
    return true;
}

bool VSNativeSession::capabilitiesReceived(const LLHost& host) const
{
    if (mShutdown || (mPhase!=Phase::Connecting && mPhase!=Phase::Connected)) return false;
    if (host==mHost) return mSeedReady;
    const auto found=mCircuits.find(host);
    return found!=mCircuits.end() && found->second.acknowledged && found->second.capabilitiesReady;
}
std::optional<LLSD> VSNativeSession::simulatorFeatures(const LLHost& host) const
{
    if (mShutdown || (mPhase!=Phase::Connecting && mPhase!=Phase::Connected)) return {};
    if (host==mHost) return mSimulatorFeaturesReceived ? std::optional<LLSD>(mSimulatorFeatures) : std::nullopt;
    const auto found=mCircuits.find(host);
    return found!=mCircuits.end() && found->second.acknowledged && found->second.featuresReceived
        ? std::optional<LLSD>(found->second.simulatorFeatures) : std::nullopt;
}
void VSNativeSession::neighbourFeaturesCoro(std::weak_ptr<VSNativeSession> weak,LLHost host,std::string url,
                                           U64 account,U64 incarnation)
{
    auto adapter=std::make_shared<LLCoreHttpUtil::HttpCoroutineAdapter>("nativeNeighbourFeatures",LLCore::HttpRequest::DEFAULT_POLICY_ID);
    auto request=std::make_shared<LLCore::HttpRequest>();
    auto options=std::make_shared<LLCore::HttpOptions>();
    options->setTransferTimeout(15);options->setRetries(0);
    auto token=std::make_shared<RegionCircuit::FeaturesRequest>();
    token->cancel=[requestOwner=std::weak_ptr<LLCoreHttpUtil::HttpCoroutineAdapter>(adapter)]
        { if (const auto request=requestOwner.lock()) request->cancelSuspendedOperation(); };
    {
        const auto owner=weak.lock();
        if (!owner || owner->mShutdown || owner->mGeneration!=account || owner->circuitGeneration(host)!=incarnation) return;
        owner->mCircuits.at(host).featuresRequest=token;
    }
    // The circuit owns cancellation; a suspended request does not keep a retired
    // account or region alive. Publish only the same accepted circuit/request.
    const std::weak_ptr<RegionCircuit::FeaturesRequest> identity=token;
    token.reset();
    for (unsigned attempt=0;attempt<2;++attempt)
    {
        const LLSD result=adapter->getAndSuspend(request,url,options);
        const auto owner=weak.lock();
        if (!owner || owner->mShutdown || owner->mGeneration!=account ||
            owner->circuitGeneration(host)!=incarnation) return;
        auto& circuit=owner->mCircuits.at(host);
        const auto current=identity.lock();
        if (!current || circuit.featuresRequest!=current) return;
        if (LLCoreHttpUtil::HttpCoroutineAdapter::getStatusFromLLSD(result["http_result"]) && result.isMap())
        {
            current->cancel={};
            circuit.featuresRequest.reset();
            circuit.simulatorFeatures=result;
            circuit.featuresReceived=true;
            if (host==owner->mHost) owner->applySimulatorFeatures(result,account,owner->mRegionEpoch);
            return;
        }
    }
    if (const auto owner=weak.lock();owner && owner->mGeneration==account && owner->circuitGeneration(host)==incarnation)
    {
        auto& circuit=owner->mCircuits.at(host);
        if (const auto current=identity.lock();current && circuit.featuresRequest==current)
        { current->cancel={};circuit.featuresRequest.reset(); }
    }
}

bool VSNativeSession::setRegionVoiceEnabled(const LLHost& host,U64 incarnation,bool enabled)
{
    if (!regionReady() || circuitGeneration(host)!=incarnation) return false;
    const auto found=mCircuits.find(host);
    if (found==mCircuits.end() || !found->second.acknowledged || !found->second.handshake) return false;
    auto& flags=found->second.flags;
    if (enabled) flags|=REGION_FLAGS_ALLOW_VOICE;
    else flags&=~U64(REGION_FLAGS_ALLOW_VOICE);
    if (host==mHost) mFlags=flags;
    return true;
}

std::optional<std::size_t> VSNativeSession::coarseAvatarCount(const LLHost& host) const
{
    if(mShutdown || mPhase!=Phase::Connected)return {};
    const auto found=mCircuits.find(host);
    if(found==mCircuits.end() || !found->second.acknowledged || !found->second.handshake)return {};
    return found->second.coarse.avatars.size();
}


void VSNativeSession::updateInterestListMode()
{
    if(mShutdown || mPhase!=Phase::Connected || mChangingRegion)return;
    const auto mode=gAgent.getInterestListMode();
    if(mode!="default" && mode!="360")return;
    const auto weak=weak_from_this();const auto account=mGeneration;
    for(auto& [host,region]:mCircuits)
    {
        if(!region.acknowledged || !region.handshake || !region.capabilitiesReady || region.interestListMode==mode)continue;
        const auto url=capability(host,"InterestList");if(url.empty())continue;
        region.interestListMode=mode;const auto circuit=region.incarnation;
        LLSD body;body["mode"]=mode;
        LLCoreHttpUtil::HttpCoroutineAdapter::callbackHttpPost(url,LLCore::HttpRequest::DEFAULT_POLICY_ID,body,
            [weak,host,account,circuit](const LLSD& result)
            {
                const auto owner=weak.lock();
                if(!owner || owner->mShutdown || owner->mGeneration!=account || owner->circuitGeneration(host)!=circuit)return;
                LL_DEBUGS("360Capture")<<"InterestList capability responded: "<<result<<LL_ENDL;
            });
    }
}
