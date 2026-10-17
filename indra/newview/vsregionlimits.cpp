// Shared CPU simulator region limits, transposed from LLWorld. LGPL-2.1.
#include "llviewerprecompiledheaders.h"
#include "vsregionlimits.h"
#include "llviewernetwork.h"
#include "llviewercontrol.h"
#include "llprimitive.h"
#include "llselectmgr.h"
#include "xform.h"
#include "llworld.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "llviewerwindow.h"
#include "llappviewer.h"
#endif
extern std::string SYSTEM_FROM;
VSRegionLimits::VSRegionLimits()
{
    applyGridDefaults(false,false);
    refreshLimits();
}
void VSRegionLimits::refreshLimits()
{
    if(!LLGridManager::getInstance())return;
#ifdef OPENSIM
    applyGridDefaults(LLGridManager::getInstance()->isInOpenSim(),true);
#else
    applyGridDefaults(false,true);
#endif
}
void VSRegionLimits::applyGridDefaults(bool opensim,bool publishGridLabel)
{
#ifndef OPENSIM
    (void)opensim;
#endif
#ifdef OPENSIM // <FS:AW optional opensim support>
    if(opensim)
    {
        //llmath/xform.h
        mRegionMaxHeight = OS_MAX_OBJECT_Z; //llmath/xform.h
        mRegionMinPrimScale = OS_MIN_PRIM_SCALE;
        mRegionMaxPrimScale = OS_DEFAULT_MAX_PRIM_SCALE;
        mRegionMaxPrimScaleNoMesh = OS_DEFAULT_MAX_PRIM_SCALE;// no restrictions here
        mRegionMaxHollowSize = OS_OBJECT_MAX_HOLLOW_SIZE;
        mRegionMinHoleSize = OS_OBJECT_MIN_HOLE_SIZE;
// <FS:CR> Aurora Sim
        mMaxPhysPrimScale = OS_DEFAULT_MAX_PRIM_SCALE;
        mMaxLinkedPrims = 10000;
        mMaxPhysLinkedPrims = 10000;
        mMaxInventoryItemsTransfer = 42;
        mAllowRenderName = 2;
        mAllowMinimap = true;
        mAllowPhysicalPrims = true;
        mAllowRenderWater = true;

        mMaxPrimXPos = F32_MAX;
        mMaxPrimYPos = F32_MAX;
        mMaxPrimZPos = OS_MAX_OBJECT_Z;
        mMinPrimXPos = 0.f;
        mMinPrimYPos = 0.f;
        mMinPrimZPos = OS_MIN_OBJECT_Z;
        mMaxDragDistance = 10000.f;
        mAllowParcelWindLight = true;
        mEnableTeenMode = false; //get saved settings?
        mEnforceMaxBuild = false;
        mLockedDrawDistance = false;

        mDrawDistance = -1.f;
        mTerrainDetailScale = -1.f;

        if(publishGridLabel)SYSTEM_FROM = "Grid";
// </FS:CR> Aurora Sim
    }
    else
#endif // OPENSIM // <FS:AW optional opensim support>
    {
        //llmath/xform.h
        mRegionMaxHeight = SL_MAX_OBJECT_Z;
        mRegionMinPrimScale = SL_MIN_PRIM_SCALE;
        mRegionMaxPrimScale = SL_DEFAULT_MAX_PRIM_SCALE;
        mRegionMaxPrimScaleNoMesh = SL_DEFAULT_MAX_PRIM_SCALE_NO_MESH;
        //llprimitive/llprimitive.*
        mRegionMaxHollowSize = SL_OBJECT_MAX_HOLLOW_SIZE;
        mRegionMinHoleSize = SL_OBJECT_MIN_HOLE_SIZE;
// <FS:CR> Aurora Sim
        mMaxPhysPrimScale = SL_DEFAULT_MAX_PRIM_SCALE;
        mMaxLinkedPrims = MAX_CHILDREN_PER_TASK;
        mMaxPhysLinkedPrims = MAX_CHILDREN_PER_PHYSICAL_TASK;
        mMaxInventoryItemsTransfer = 42;
        mAllowRenderName = 2;
        mAllowMinimap = true;
        mAllowPhysicalPrims = true;
        mAllowRenderWater = true;

        mMaxPrimXPos = 256.f;
        mMaxPrimYPos = 256.f;
        mMaxPrimZPos = SL_MAX_OBJECT_Z;
        mMinPrimXPos = 0.f;
        mMinPrimYPos = 0.f;
        mMinPrimZPos = SL_MIN_OBJECT_Z;
        mMaxDragDistance = 10000.f;
        mAllowParcelWindLight = false;
        mEnableTeenMode = false; //get saved settings?
        mEnforceMaxBuild = false;
        mLockedDrawDistance = false;

        mDrawDistance = -1.f;
        mTerrainDetailScale = -1.f;

        if(publishGridLabel)SYSTEM_FROM = "Second Life";
// </FS:CR> Aurora Sim
    }
    LL_DEBUGS("OS_SETTINGS") << "RegionMaxHeight    " << mRegionMaxHeight << LL_ENDL;
    LL_DEBUGS("OS_SETTINGS") << "RegionMinPrimScale " << mRegionMinPrimScale << LL_ENDL;
    LL_DEBUGS("OS_SETTINGS") << "RegionMaxPrimScale " << mRegionMaxPrimScale << LL_ENDL;
    LL_DEBUGS("OS_SETTINGS") << "RegionMaxHollowSize    " << mRegionMaxHollowSize << LL_ENDL;
    LL_DEBUGS("OS_SETTINGS") << "RegionMinHoleSize  " << mRegionMinHoleSize << LL_ENDL;
}
void VSRegionLimits::setRegionMaxHeight(F32 val)
{
    if(val <= 0.0f)
        mRegionMaxHeight = OS_MAX_OBJECT_Z;
    else
        mRegionMaxHeight = val;

    setMaxPrimZPos(mRegionMaxHeight);
}

void VSRegionLimits::setRegionMinPrimScale(F32 val)
{
    if(val <= 0.0f)
        mRegionMinPrimScale = OS_MIN_PRIM_SCALE;
    else
        mRegionMinPrimScale = val;
}

void VSRegionLimits::setRegionMaxPrimScale(F32 val)
{
    if(val <= 0.0f)
        mRegionMaxPrimScale = OS_DEFAULT_MAX_PRIM_SCALE;
    else
        mRegionMaxPrimScale = val;
}

void VSRegionLimits::setRegionMaxPrimScaleNoMesh(F32 val)
{
    if(val <= 0.0f)
        mRegionMaxPrimScaleNoMesh = OS_DEFAULT_MAX_PRIM_SCALE;
    else
        mRegionMaxPrimScaleNoMesh = val;
}

void VSRegionLimits::setRegionMaxHollowSize(F32 val)
{
    if(val <= 0.0f)
        mRegionMaxHollowSize = OS_OBJECT_MAX_HOLLOW_SIZE;
    else
        mRegionMaxHollowSize = val;
}

void VSRegionLimits::setRegionMinHoleSize(F32 val)
{
    if(val <= 0.0f)
        mRegionMinHoleSize = OS_OBJECT_MIN_HOLE_SIZE;
    else
        mRegionMinHoleSize = val;
}

void VSRegionLimits::setMaxPhysPrimScale(F32 val)
{
    if(val <= 0.0f)
        mMaxPhysPrimScale = mRegionMaxPrimScale;
    else
        mMaxPhysPrimScale = val;
}

void VSRegionLimits::setMaxDragDistance(F32 val)
{
    if(val <= 0.0f)
        mMaxDragDistance = gSavedSettings.getF32("MaxDragDistance");
    else
        mMaxDragDistance = val;
}

void VSRegionLimits::setMaxLinkedPrims(S32 val)
{
    if(val < 0)
        mMaxLinkedPrims = 10000;
    else
        mMaxLinkedPrims = val;
}

void VSRegionLimits::setMaxPhysLinkedPrims(S32 val)
{
    if(val < 0)
        mMaxPhysLinkedPrims = 10000;
    else
        mMaxPhysLinkedPrims = val;
}

void VSRegionLimits::setMaxInventoryItemsTransfer(S32 val)
{
    if(val < 0)
        mMaxInventoryItemsTransfer = 42;
    else
        mMaxInventoryItemsTransfer = val;
}

void VSRegionLimits::setMaxPrimXPos(F32 val)
{
    if(val <= 0.0f)
        mMaxPrimXPos = F32_MAX;
    else
        mMaxPrimXPos = val;
}

void VSRegionLimits::setMaxPrimYPos(F32 val)
{
    if(val <= 0.0f)
        mMaxPrimYPos = F32_MAX;
    else
        mMaxPrimYPos = val;
}

void VSRegionLimits::setMaxPrimZPos(F32 val)
{
    if(val <= 0.0f)
        mMaxPrimZPos = F32_MAX;
    else
        mMaxPrimZPos = val;
}

void VSRegionLimits::setMinPrimXPos(F32 val)
{
    if(val < 0.0f)
        mMinPrimXPos = 0.0f;
    else
        mMinPrimXPos = val;
}

void VSRegionLimits::setMinPrimYPos(F32 val)
{
    if(val < 0.0f)
        mMinPrimYPos = 0.0f;
    else
        mMinPrimYPos = val;
}

void VSRegionLimits::setMinPrimZPos(F32 val)
{
    // <FS:Ansariel> OpenSim limits
    //if(val < 0.0f)
    //  mMinPrimZPos = 0.0f;
    F32 min_limit = SL_MIN_OBJECT_Z;
#if OPENSIM
    if (LLGridManager::instance().isInOpenSim())
    {
        min_limit = OS_MIN_OBJECT_Z;
    }
#endif
    if(val < min_limit)
        mMinPrimZPos = min_limit;
    // <FS:Ansariel>
    else
        mMinPrimZPos = val;
}

void VSRegionLimits::setDrawDistance(F32 val)
{
    if(val < 0.0f)
        mDrawDistance = -1.f;
    else
        mDrawDistance = val;
}

void VSRegionLimits::setTerrainDetailScale(F32 val)
{
    if(val < 0.0f)
        mTerrainDetailScale = -1.f;
    else
        mTerrainDetailScale = val;
}

void VSRegionLimits::setAllowMinimap(bool val)         { mAllowMinimap = val; }
void VSRegionLimits::setAllowPhysicalPrims(bool val)   { mAllowPhysicalPrims = val; }
void VSRegionLimits::setAllowRenderWater(bool val)     { mAllowRenderWater = val; }
void VSRegionLimits::setAllowParcelWindLight(bool val) { mAllowParcelWindLight = val; }
void VSRegionLimits::setEnableTeenMode(bool val)       { mEnableTeenMode = val; }
void VSRegionLimits::setEnforceMaxBuild(bool val)      { mEnforceMaxBuild = val; }
void VSRegionLimits::setLockedDrawDistance(bool val)   { mLockedDrawDistance = val; }
void VSRegionLimits::setAllowRenderName(S32 val)       { mAllowRenderName = val; }


VSRegionLimits& vs_region_limits()
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow ? gViewerWindow->isNativeVulkan() :
        (LLAppViewer::instance() && LLAppViewer::instance()->isNativeVulkanLogin()))
    {
        if(auto owner=VSNativeSession::active())return owner->regionLimits();
        // The disconnected native UI has grid defaults, not a fabricated world.
        static VSRegionLimits defaults;
        defaults.refreshLimits();
        return defaults;
    }
#endif
    return LLWorld::instance();
}
