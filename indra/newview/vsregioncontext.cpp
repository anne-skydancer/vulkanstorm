// Authenticated simulator CPU values for shared UI/services. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsregioncontext.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llworld.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llregionhandle.h"
#include "llregionflags.h"
#include "llviewerparceloverlay.h"
#include "llparcel.h"
#include "lltrans.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
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
bool VSRegionContext::getRestrictPushObject() const { return getRegionFlag(REGION_FLAGS_RESTRICT_PUSHOBJECT); }
std::string VSRegionContext::getSimAccessString() const { return LLViewerRegion::accessToString(mAccess); }
std::string VSRegionContext::getLocalizedSimProductName() const
{
    std::string localized;
    return LLTrans::findString(localized,mProductName) ? localized : mProductName;
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
    result.mFlags=region->getRegionFlags();result.mProtocols=region->getRegionProtocols();result.mProtocolsKnown=true;
    result.mAccess=region->getSimAccess();result.mWaterHeight=region->getWaterHeight();result.mWaterKnown=true;
    result.mBillableFactor=region->getBillableFactor();result.mBillableKnown=true;
    result.mObjectCapacity=region->getMaxTasks();result.mObjectCapacityKnown=true;
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
        result.mBillableFactor=region.metadata.billableFactor;result.mBillableKnown=region.metadata.billableKnown;
        // LLViewerRegion starts at 15000 until SimStats supplies the simulator limit.
        result.mObjectCapacity=region.objectCapacityKnown?region.objectCapacity:15000;result.mObjectCapacityKnown=region.objectCapacityKnown;
        result.mEstateManager=region.estateManager;result.mEstateManagerKnown=region.estateManagerKnown;
        result.mProductName=region.metadata.productName;result.mProductKnown=region.metadata.productKnown;
        const auto weak=std::weak_ptr<VSNativeSession>(owner);
        // Do not retain coarse locations/parcel bitmaps inside every UI callback.
        VSNativeSession::RegionSnapshot token;
        token.account=region.account;token.host=host;token.id=region.id;token.handle=region.handle;token.circuit=region.circuit;
        result.mCurrent=[weak,token]()
        {const auto current=weak.lock();return current && current->regionReady() && current->current(token);};
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
