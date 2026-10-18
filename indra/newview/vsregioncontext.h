// Authenticated simulator CPU values for shared UI/services. LGPL-2.1.
#pragma once
#include "llhost.h"
#include "lluuid.h"
#include "llsd.h"
#include "v3dmath.h"
#include "v3math.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>
class LLViewerRegion;
class LLBBox;
class VSNativeSession;
class VSRegionContext
{
public:
    bool current() const { return mCurrent && mCurrent(); }
    const LLHost& getHost() const { return mHost; }
    const LLUUID& getRegionID() const { return mID; }
    U64 getHandle() const { return mHandle; }
    const LLUUID& getOwner() const { return mOwner; }
    const std::string& getName() const { return mName; }
    const LLVector3d& getOriginGlobal() const { return mOrigin; }
    LLVector3d getPosGlobalFromRegion(const LLVector3& local) const { return mOrigin+LLVector3d(local); }
    LLVector3 getPosRegionFromGlobal(const LLVector3d& global) const { return LLVector3(global-mOrigin); }
    F32 getWidth() const { return mWidth; }
    F32 getHeight() const { return mHeight; }
    bool pointInRegionGlobal(const LLVector3d& global) const;
    std::optional<U8> parcelFlagsAt(const LLVector3d& global) const;
    std::optional<U8> parcelLineFlags(S32 row,S32 column) const;
    bool objectsCrossParcel(const std::vector<LLBBox>& boxes) const;
    U64 getRegionFlags() const { return mFlags; }
    bool getRegionFlag(U64 flag) const { return (mFlags&flag)!=0; }
    U64 getRegionProtocols() const { return mProtocols; }
    bool getRegionProtocol(U64 protocol) const { return (mProtocols&protocol)!=0; }
    bool operator==(const VSRegionContext& other) const { return mHost==other.mHost && mID==other.mID && mHandle==other.mHandle; }
    bool operator!=(const VSRegionContext& other) const { return !(*this==other); }
    bool protocolsKnown() const { return mProtocolsKnown; }
    S32 getCentralBakeVersion() const { return mCentralBakeVersion; }
    S32 getRegionMaxBakes() const;
    U8 getSimAccess() const { return mAccess; }
    std::string getSimAccessString() const;
    F32 getWaterHeight() const { return mWaterHeight; }
    bool waterKnown() const { return mWaterKnown; }
    F32 getBillableFactor() const { return mBillableFactor; }
    bool billableKnown() const { return mBillableKnown; }
    U32 getMaxTasks() const { return mObjectCapacity; }
    bool objectCapacityKnown() const { return mObjectCapacityKnown; }
    bool isEstateManager() const { return mEstateManagerKnown && mEstateManager; }
    bool estateManagerKnown() const { return mEstateManagerKnown; }
    bool canManageEstate() const;
    bool dynamicPathfindingEnabled() const { return mDynamicPathfinding; }
    bool getAllowEnvironmentOverride() const;
    bool getAllowDamage() const;
    bool getBlockFly() const;
    bool isVoiceEnabled() const;
    bool setVoiceEnabled(bool) const;
    bool getRestrictPushObject() const;
    std::string getLocalizedSimProductName() const;
    bool productKnown() const { return mProductKnown; }
    bool capabilitiesReceived() const;
    void whenCapabilitiesReceived(std::function<void(const LLUUID&)>, std::function<bool()> = {}) const;
    bool simulatorFeaturesReceived() const;
    void getSimulatorFeatures(LLSD&) const;
    std::vector<std::string> getStunServers() const;
    std::string getCapability(const std::string& name) const;
    bool isCapabilityAvailable(const std::string& name) const { return !getCapability(name).empty(); }
private:
    friend std::optional<VSRegionContext> vs_region_context(const LLHost&);
    friend std::optional<VSRegionContext> vs_region_context(const LLVector3d&);
    friend std::optional<VSRegionContext> vs_region_context(const LLUUID&);
    static std::optional<VSRegionContext> legacy(LLViewerRegion*);
#if VS_NATIVE_VULKAN
    static std::optional<VSRegionContext> native(const LLHost&);
#endif
    LLHost mHost;
    LLUUID mID,mOwner;
    U64 mHandle=0,mFlags=0,mProtocols=0;
    LLVector3d mOrigin;
    F32 mWidth=0,mHeight=0,mWaterHeight=0,mBillableFactor=1.f;
    U32 mObjectCapacity=0;
    U8 mAccess=0;
    S32 mCentralBakeVersion=0;
    std::optional<S32> mLegacyMaxBakes;
    std::string mName,mProductName="unknown";
    bool mDynamicPathfinding=false;
    bool mWaterKnown=false,mBillableKnown=false,mProtocolsKnown=false,mObjectCapacityKnown=false;
    bool mEstateManager=false,mEstateManagerKnown=false,mProductKnown=false;
    std::function<bool()> mCurrent;
    std::function<std::optional<U8>(S32,S32)> mParcelCell;
    std::function<std::string(const std::string&)> mCapability;
    std::function<bool()> mCapabilitiesReceived;
    std::function<bool(bool)> mSetVoiceEnabled;
    std::function<std::optional<LLSD>()> mSimulatorFeatures;
};
std::optional<VSRegionContext> vs_region_context(const LLHost&);
std::optional<VSRegionContext> vs_region_context(const LLVector3d&);
std::optional<VSRegionContext> vs_region_context(const LLUUID&);
std::optional<VSRegionContext> vs_current_region_context();
std::vector<VSRegionContext> vs_region_contexts();

bool vs_publish_region_owner(const VSRegionContext&,const LLUUID&);

// No invented fallback: callers retain their own reference missing-region policy.
std::optional<F32> vs_region_height(const LLVector3d&);

LLVector3d vs_clip_visible_regions(const LLVector3d& start,const LLVector3d& end);
