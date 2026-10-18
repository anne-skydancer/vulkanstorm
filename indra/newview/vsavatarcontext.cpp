// Shared avatar moderation resolves actual owners; no stand-in legacy region.
#include "llviewerprecompiledheaders.h"
#include "vsavatarcontext.h"
#include "llviewerregion.h"
#include "llviewerwindow.h"
#include "llworld.h"
#include "llparcel.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"
#endif
// Preserve the exact reference lazy object-to-coarse lookup on the legacy peer.
extern bool getRegionAndPosGlobalFromAgentID(const LLUUID&,const LLViewerRegion**,LLVector3d*);
std::optional<VSAvatarContext> VSAvatarContext::resolve(const LLUUID& id)
{
    if(id.isNull())return {};
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=VSNativeSession::active();auto* context=gViewerWindow->nativeContext();
        if(!owner || !owner->regionReady() || !context)return {};
        auto& world=context->world();
        if(const auto object=world.object(id))
        {
            if(object->kind!=VSWorldContracts::OwnerKind::Avatar)return {};
            const auto pose=world.objectTransform(*object);const auto region=vs_region_context(object->host);
            if(pose && region)
            {
                VSAvatarContext result;result.mID=id;result.mPosition=pose->position;result.mHost=object->host;result.mNative=region;
                const auto account=owner->accountGuard();const auto objectCurrent=world.guard(*object);
                result.mCurrent=[account,region,objectCurrent]{return account() && region->current() && objectCurrent();};
                if(result.current())return result;
            }
            return {};
        }
        if(const auto coarse=owner->coarseAvatarLocation(id))
        {
            const auto region=vs_region_context(coarse->first);if(!region)return {};
            VSAvatarContext result;result.mID=id;result.mPosition=LLVector3d(coarse->second.position.data());
            result.mAltitudeKnown=coarse->second.altitudeKnown;result.mHost=coarse->first;result.mNative=region;
            const auto weak=std::weak_ptr<VSNativeSession>(owner);const auto account=owner->account();const auto host=coarse->first;
            result.mCurrent=[weak,account,region,host,id]
            {
                const auto current=weak.lock();if(!current || !current->current(account) || !region->current())return false;
                const auto latest=current->coarseAvatarLocation(id);return latest && latest->first==host;
            };
            if(result.current())return result;
        }
        return {};
    }
#endif
    const LLViewerRegion* region=nullptr;LLVector3d position;
    if(!getRegionAndPosGlobalFromAgentID(id,&region,&position) || !region)return {};
    VSAvatarContext result;result.mID=id;result.mPosition=position;result.mHost=region->getHost();result.mLegacy=region;
    const LLHost host=result.mHost;const LLUUID regionID=region->getRegionID();
    result.mCurrent=[host,region,regionID]
    {
        if(!LLWorld::instanceExists())return false;
        const auto current=LLWorld::instance().getRegion(host);
        return current==region && current->getRegionID()==regionID;
    };
    return result;
}
LLUUID VSAvatarContext::getOwner()const
{
    if(!current())return LLUUID::null;
    if(mLegacy)return mLegacy->getOwner();
    const auto current=vs_region_context(mHost);return current?current->getOwner():LLUUID::null;
}
bool VSAvatarContext::isEstateManager()const
{
    if(!current())return false;
    if(mLegacy)return mLegacy->isEstateManager();
    const auto currentRegion=vs_region_context(mHost);return currentRegion && currentRegion->isEstateManager();
}
bool VSAvatarContext::isOwnedSelf()const
{
    if(!current())return false;
    if(mLegacy)return mLegacy->isOwnedSelf(mLegacy->getPosRegionFromGlobal(mPosition));
    const auto flags=mNative->parcelFlagsAt(mPosition);return flags && (*flags&PARCEL_COLOR_MASK)==PARCEL_SELF;
}
bool VSAvatarContext::isOwnedGroup()const
{
    if(!current())return false;
    if(mLegacy)return mLegacy->isOwnedGroup(mLegacy->getPosRegionFromGlobal(mPosition));
    const auto flags=mNative->parcelFlagsAt(mPosition);return flags && (*flags&PARCEL_COLOR_MASK)==PARCEL_GROUP;
}
