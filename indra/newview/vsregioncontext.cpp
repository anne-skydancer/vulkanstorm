// Authenticated simulator CPU values for shared UI/services. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsregioncontext.h"
#include "llcallbacklist.h"
#include "llavatarappearancedefines.h"
#include "llviewernetwork.h"
#include "llbbox.h"
#include "vsparcelbounds.h"
#include "vsregionclip.h"
#include "vsregionlimits.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llworld.h"
#include "llsurface.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llregionhandle.h"
#include "llregionflags.h"
#include "llviewerparceloverlay.h"
#include "llparcel.h"
#include "lltrans.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#endif
namespace
{
bool nativeRegions()
{
#if VS_NATIVE_VULKAN
    return gViewerWindow ? gViewerWindow->isNativeVulkan() :
        (LLAppViewer::instance() && LLAppViewer::instance()->isNativeVulkanLogin());
#else
    return false;
#endif
}
}
bool VSRegionContext::pointInRegionGlobal(const LLVector3d& global) const
{
    const auto local=global-mOrigin;
    return local.mdV[VX]>=0 && local.mdV[VY]>=0 && local.mdV[VX]<mWidth && local.mdV[VY]<mHeight;
}
std::optional<U8> VSRegionContext::parcelFlagsAt(const LLVector3d& global) const
{
    if(!current() || !pointInRegionGlobal(global) || !mParcelCell)return {};
    const auto local=getPosRegionFromGlobal(global);
    return mParcelCell(S32(local.mV[VY]/PARCEL_GRID_STEP_METERS),S32(local.mV[VX]/PARCEL_GRID_STEP_METERS));
}
std::optional<U8> VSRegionContext::parcelLineFlags(S32 row,S32 column) const
{
    if(row<0 || column<0 || F64(column)*PARCEL_GRID_STEP_METERS>=mWidth || F64(row)*PARCEL_GRID_STEP_METERS>=mHeight ||
       !current() || !mParcelCell)return {};
    const auto cell=mParcelCell(row,column);
    return cell ? std::optional<U8>(*cell & (PARCEL_WEST_LINE|PARCEL_SOUTH_LINE)) : std::nullopt;
}
bool VSRegionContext::canManageEstate() const
{
    if(!current())return false;
    const auto latest=vs_region_context(mHost);
    return latest && *latest==*this && (gAgent.isGodlike() || latest->isEstateManager() || gAgent.getID()==latest->getOwner());
}
bool VSRegionContext::getAllowDamage() const { return getRegionFlag(REGION_FLAGS_ALLOW_DAMAGE); }
bool VSRegionContext::getBlockFly() const { return getRegionFlag(REGION_FLAGS_BLOCK_FLY); }
bool VSRegionContext::isVoiceEnabled() const { return getRegionFlag(REGION_FLAGS_ALLOW_VOICE); }
bool VSRegionContext::setVoiceEnabled(bool enabled) const
{ return current() && mSetVoiceEnabled && mSetVoiceEnabled(enabled); }
bool VSRegionContext::getRestrictPushObject() const { return getRegionFlag(REGION_FLAGS_RESTRICT_PUSHOBJECT); }
std::string VSRegionContext::getSimAccessString() const { return LLViewerRegion::accessToString(mAccess); }
std::string VSRegionContext::getLocalizedSimProductName() const
{
    // Missing native RegionInfo metadata has the same localized unknown label
    // as the shared region-information controller, not a lookup of a raw sentinel.
    if(!mProductKnown)return LLTrans::getString("land_type_unknown");
    std::string localized;
    return LLTrans::findString(localized,mProductName) ? localized : mProductName;
}
bool VSRegionContext::capabilitiesReceived() const
{
    return current() && mCapabilitiesReceived && mCapabilitiesReceived();
}
bool VSRegionContext::simulatorFeaturesReceived() const
{
    return current() && mSimulatorFeatures && mSimulatorFeatures().has_value();
}
void VSRegionContext::getSimulatorFeatures(LLSD& features) const
{
    features = LLSD::emptyMap();
    if (current() && mSimulatorFeatures)
        if (const auto value = mSimulatorFeatures()) features = *value;
}
std::vector<std::string> VSRegionContext::getStunServers() const
{
    LLSD features;
    getSimulatorFeatures(features);
    std::vector<std::string> result;
    if (features.has("stun-servers")) LLStringUtil::getTokens(features["stun-servers"].asString(), result, ",");
    return result;
}
std::string VSRegionContext::getCapability(const std::string& name) const
{
    return current() && mCapability ? mCapability(name) : std::string();
}
std::optional<VSRegionContext> VSRegionContext::legacy(LLViewerRegion* region)
{
    if(!region)return {};
    VSRegionContext result;
    result.mHost=region->getHost();result.mID=region->getRegionID();result.mHandle=region->getHandle();
    result.mOwner=region->getOwner();result.mName=region->getName();result.mOrigin=region->getOriginGlobal();
    result.mWidth=region->getWidth();result.mHeight=region->getWidth();
    result.mCentralBakeVersion=region->getCentralBakeVersion();
    result.mLegacyMaxBakes=region->getRegionMaxBakes();
    result.mFlags=region->getRegionFlags();result.mProtocols=region->getRegionProtocols();result.mProtocolsKnown=true;
    result.mAccess=region->getSimAccess();result.mWaterHeight=region->getWaterHeight();result.mWaterKnown=true;
    result.mBillableFactor=region->getBillableFactor();result.mBillableKnown=true;
    result.mObjectCapacity=region->getMaxTasks();result.mObjectCapacityKnown=true;
    result.mDynamicPathfinding=region->dynamicPathfindingEnabled();
    result.mEstateManager=region->isEstateManager();result.mEstateManagerKnown=true;
    result.mProductName=region->getLocalizedSimProductName();result.mProductKnown=true;
    const auto host=result.mHost;
    const auto id=result.mID;
    const auto handle=result.mHandle;
    const auto agent=gAgent.getID(),session=gAgent.getSessionID();
    result.mCurrent=[host,id,handle,agent,session]()
    {
        if(nativeRegions() || gDisconnected || gAgent.getID()!=agent || gAgent.getSessionID()!=session)return false;
        auto* current=LLWorld::instance().getRegion(host);
        return current && current->getRegionID()==id && current->getHandle()==handle;
    };
    result.mSetVoiceEnabled=[host](bool enabled)
    {
        auto* region=LLWorld::instance().getRegion(host);
        if (!region) return false;
        region->setRegionFlag(REGION_FLAGS_ALLOW_VOICE,enabled);return true;
    };
    result.mCapabilitiesReceived=[host]
    {
        const auto* region=LLWorld::instance().getRegion(host);
        return region && region->capabilitiesReceived();
    };
    result.mSimulatorFeatures=[host]()->std::optional<LLSD>
    {
        const auto* region=LLWorld::instance().getRegion(host);
        if (!region || !region->simulatorFeaturesReceived()) return {};
        LLSD features;region->getSimulatorFeatures(features);return features;
    };
    result.mCapability=[host](const std::string& name)
    {
        auto* current=LLWorld::instance().getRegion(host);
        return current ? current->getCapability(name) : std::string();
    };
    result.mParcelCell=[host](S32 row,S32 column)->std::optional<U8>
    {
        const auto* current=LLWorld::instance().getRegion(host);
        const auto* overlay=current ? current->getParcelOverlay() : nullptr;
        if(!overlay || row<0 || column<0)return {};
        const auto edge=S32(current->getWidth()/PARCEL_GRID_STEP_METERS);
        if(row>=edge || column>=edge)return {};
        return overlay->getOwnership()[std::size_t(row)*edge+column];
    };
    return result;
}
#if VS_NATIVE_VULKAN
std::optional<VSRegionContext> VSRegionContext::native(const LLHost& host)
{
    const auto owner=VSNativeSession::active();
    if(!owner || !owner->regionReady())return {};
    const auto snapshot=owner->regionSnapshot(host,false);
    if(!snapshot)return {};
    const auto& region=*snapshot;
        VSRegionContext result;
        result.mHost=host;result.mID=region.id;result.mOwner=region.owner;result.mHandle=region.handle;
        result.mName=region.name;result.mOrigin=from_region_handle(region.handle);
        result.mWidth=F32(region.width);result.mHeight=F32(region.height);result.mFlags=region.flags;
        result.mAccess=region.access;result.mWaterHeight=region.waterHeight;result.mWaterKnown=region.waterKnown;
        result.mProtocols=region.metadata.protocols;result.mProtocolsKnown=region.metadata.protocolsKnown;
        // Same handshake protocol bit and initial legacy-bake value as LLViewerRegion.
        result.mCentralBakeVersion=region.metadata.protocolsKnown ? S32(region.metadata.protocols & 1) : 0;
        result.mBillableFactor=region.metadata.billableFactor;result.mBillableKnown=region.metadata.billableKnown;
        // LLViewerRegion starts at 15000 until SimStats supplies the simulator limit.
        result.mObjectCapacity=region.objectCapacityKnown?region.objectCapacity:15000;result.mObjectCapacityKnown=region.objectCapacityKnown;
        result.mDynamicPathfinding=host==owner->host() &&
            owner->simulatorFeatures()["DynamicPathfindingEnabled"].asBoolean();
        result.mEstateManager=region.estateManager;result.mEstateManagerKnown=region.estateManagerKnown;
        result.mProductName=region.metadata.productName;result.mProductKnown=region.metadata.productKnown;
        const auto weak=std::weak_ptr<VSNativeSession>(owner);
        // Do not retain coarse locations/parcel bitmaps inside every UI callback.
        VSNativeSession::RegionSnapshot token;
        token.account=region.account;token.host=host;token.id=region.id;token.handle=region.handle;token.circuit=region.circuit;
        result.mCurrent=[weak,token]()
        {const auto current=weak.lock();return current && current->regionReady() && current->current(token);};
        result.mSetVoiceEnabled=[weak,host,circuit=region.circuit](bool enabled)
        { const auto current=weak.lock();return current && current->setRegionVoiceEnabled(host,circuit,enabled); };
        result.mCapabilitiesReceived=[weak,host]
        { const auto current=weak.lock();return current && current->capabilitiesReceived(host); };
        result.mSimulatorFeatures=[weak,host]()->std::optional<LLSD>
        { const auto current=weak.lock();return current ? current->simulatorFeatures(host) : std::nullopt; };
        result.mCapability=[weak,host](const std::string& name)
        {const auto current=weak.lock();return current ? current->capability(host,name) : std::string();};
    result.mParcelCell=[weak,host](S32 row,S32 column)->std::optional<U8>
    {
        const auto current=weak.lock();const auto overlay=current ? current->parcelOverlay(host) : nullptr;
        return overlay && row>=0 && column>=0 ? overlay->cell(U32(column),U32(row)) : std::nullopt;
    };
    return result;
}
#endif
std::optional<VSRegionContext> vs_region_context(const LLHost& host)
{
#if VS_NATIVE_VULKAN
    if(nativeRegions())return VSRegionContext::native(host);
#endif
    return VSRegionContext::legacy(LLWorld::instance().getRegion(host));
}
std::optional<VSRegionContext> vs_region_context(const LLVector3d& global)
{
#if VS_NATIVE_VULKAN
    if(nativeRegions())
    {
        const auto owner=VSNativeSession::active();if(!owner || !owner->regionReady())return {};
        for(const auto& region:owner->regionSnapshots(false))
        {
            const auto local=global-from_region_handle(region.handle);
            if(local.mdV[VX]>=0 && local.mdV[VY]>=0 && local.mdV[VX]<region.width && local.mdV[VY]<region.height)
                return VSRegionContext::native(region.host);
        }
        return {};
    }
#endif
    return VSRegionContext::legacy(LLWorld::instance().getRegionFromPosGlobal(global));
}
std::optional<VSRegionContext> vs_region_context(const LLUUID& id)
{
#if VS_NATIVE_VULKAN
    if(nativeRegions())
    {
        const auto owner=VSNativeSession::active();if(!owner || !owner->regionReady())return {};
        for(const auto& region:owner->regionSnapshots(false))if(region.id==id)return VSRegionContext::native(region.host);
        return {};
    }
#endif
    return VSRegionContext::legacy(LLWorld::instance().getRegionFromID(id));
}
std::optional<VSRegionContext> vs_current_region_context()
{
#if VS_NATIVE_VULKAN
    if(nativeRegions())
    {
        const auto owner=VSNativeSession::active();
        return owner ? vs_region_context(owner->host()) : std::nullopt;
    }
#endif
    const auto* region=gAgent.getRegion();
    return region ? vs_region_context(region->getHost()) : std::nullopt;
}

std::vector<VSRegionContext> vs_region_contexts()
{
    std::vector<VSRegionContext> result;
#if VS_NATIVE_VULKAN
    if(nativeRegions())
    {
        const auto session=VSNativeSession::active();if(!session)return result;
        for(const auto& source:session->regionSnapshots(false))
            if(const auto region=vs_region_context(source.host))result.push_back(*region);
        return result;
    }
#endif
    for(const auto* source:LLWorld::instance().getRegionList())
        if(source)if(const auto region=vs_region_context(source->getHost()))result.push_back(*region);
    return result;
}

bool vs_publish_region_owner(const VSRegionContext& context,const LLUUID& owner)
{
    if(!context.current())return false;
#if VS_NATIVE_VULKAN
    if(nativeRegions())
    {
        const auto session=VSNativeSession::active();
        return session && session->publishRegionOwner(context.getHost(),session->circuitGeneration(context.getHost()),owner);
    }
#endif
    auto* region=LLWorld::instance().getRegion(context.getHost());
    if(!region || region->getRegionID()!=context.getRegionID())return false;
    region->setOwner(owner);
    return true;
}

bool VSRegionContext::getAllowEnvironmentOverride() const
{
    return getRegionFlag(REGION_FLAGS_ALLOW_ENVIRONMENT_OVERRIDE);
}

std::optional<F32> vs_region_height(const LLVector3d& global)
{
    const auto region=vs_region_context(global);
    if(!region || !region->current())return {};
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        auto* context=gViewerWindow->nativeContext();if(!context)return {};
        const auto field=context->world().terrainField(region->getHost());
        if(!field || !field->current || !field->current() || !field->referenceHeight)return {};
        const auto local=region->getPosRegionFromGlobal(global);
        const auto result=field->referenceHeight(local.mV[VX],local.mV[VY]);
        return region->current() && field->current()?result:std::nullopt;
    }
#endif
    const auto owner=LLWorld::instance().getRegion(region->getHost());
    if(!owner || owner->getRegionID()!=region->getRegionID())return {};
    return owner->getLand().resolveHeightGlobal(global);
}

LLVector3d vs_clip_visible_regions(const LLVector3d& start,const LLVector3d& end)
{
    struct Regions
    {
        bool contains(const LLVector3d& point)const{return bool(vs_region_context(point));}
        std::optional<VSRegionContext> region(const LLVector3d& point)const{return vs_region_context(point);}
        F32 maxHeight()const{return vs_region_limits().getRegionMaxHeight();}
    } regions;
    return vs_clip_regions(start,end,regions);
}


bool VSRegionContext::objectsCrossParcel(const std::vector<LLBBox>& boxes) const
{
    if (!current()) return false;
    return vs_parcel_bounds_cross(boxes, mWidth, mHeight,
        [this](S32 row, S32 column) { return parcelLineFlags(row, column).value_or(0); });
}

void VSRegionContext::whenCapabilitiesReceived(std::function<void(const LLUUID&)> callback, std::function<bool()> scopeCurrent) const
{
    if (!current() || !callback || (scopeCurrent && !scopeCurrent())) return;
    if (capabilitiesReceived()) { callback(mID); return; }
    if (!nativeRegions())
    {
        if (auto* region = LLWorld::instance().getRegion(mHost))
            region->setCapabilitiesReceivedCallback([owner = *this, callback, scopeCurrent](const LLUUID& id, LLViewerRegion*)
            { if (owner.current() && (!scopeCurrent || scopeCurrent())) callback(id); });
        return;
    }
    // Native seed completion has the same current-circuit semantics as the
    // legacy one-shot callback. The ordinary CPU idle queue retires it as soon
    // as the captured region incarnation disappears or delivers its seed.
    doOnIdleRepeating([region = *this, callback = std::move(callback), scopeCurrent]
    {
        if (!region.current() || (scopeCurrent && !scopeCurrent())) return true;
        if (!region.capabilitiesReceived()) return false;
        callback(region.getRegionID());
        return true;
    });
}

S32 VSRegionContext::getRegionMaxBakes() const
{
    if(mLegacyMaxBakes)return *mLegacyMaxBakes;
    using namespace LLAvatarAppearanceDefines;
#ifdef OPENSIM
    LLSD features;getSimulatorFeatures(features);
    return LLGridManager::getInstance()->isInSecondLife() ||
        (features.has("BakesOnMeshEnabled") && features["BakesOnMeshEnabled"].asBoolean()) ? BAKED_NUM_INDICES : BAKED_LEFT_ARM;
#else
    return BAKED_NUM_INDICES;
#endif
}
