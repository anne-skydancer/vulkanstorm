// Shared CPU simulator region limits, transposed from LLWorld. LGPL-2.1.
#pragma once
#include "llmath.h"
class VSRegionLimits
{
public:
    VSRegionLimits();
    virtual ~VSRegionLimits()=default;
    virtual void refreshLimits();
    F32 getRegionMaxHeight() const      { return mRegionMaxHeight; }
    F32 getRegionMinPrimScale() const   { return mRegionMinPrimScale; }
    F32 getRegionMaxPrimScale() const   { return mRegionMaxPrimScale; }
    F32 getRegionMaxPrimScaleNoMesh() const { return mRegionMaxPrimScaleNoMesh; }
    F32 getRegionMaxHollowSize() const  { return mRegionMaxHollowSize; }
    F32 getRegionMinHoleSize() const    { return mRegionMinHoleSize; }
// <FS:CR> Aurora Sim
    int getMaxLinkedPrims() const         { return mMaxLinkedPrims; }
    int getMaxPhysLinkedPrims() const     { return mMaxPhysLinkedPrims; }
    int getMaxInventoryItemsTransfer() const { return mMaxInventoryItemsTransfer; }
    int getAllowRenderName() const           { return mAllowRenderName; }
    bool getAllowMinimap() const             { return mAllowMinimap; }
    bool getAllowPhysicalPrims() const       { return mAllowPhysicalPrims; }
    bool getAllowRenderWater() const         { return mAllowRenderWater; }

    F32 getMaxPrimXPos() const          { return mMaxPrimXPos; }
    F32 getMaxPrimYPos() const          { return mMaxPrimYPos; }
    F32 getMaxPrimZPos() const          { return mMaxPrimZPos; }
    F32 getMinPrimXPos() const          { return mMinPrimXPos; }
    F32 getMinPrimYPos() const          { return mMinPrimYPos; }
    F32 getMinPrimZPos() const          { return mMinPrimZPos; }
    F32 getMaxDragDistance() const      { return mMaxDragDistance; }
    F32 getMaxPhysPrimScale() const     { return mMaxPhysPrimScale; }
    bool getAllowParcelWindLight() const{ return mAllowParcelWindLight; }
    bool getEnableTeenMode() const      { return mEnableTeenMode; }
    bool getEnforceMaxBuild() const     { return mEnforceMaxBuild; }
    bool getLockedDrawDistance() const  { return mLockedDrawDistance; }

    F32 getDrawDistance() const         { return mDrawDistance; }
    F32 getTerrainDetailScale() const   { return mTerrainDetailScale; }

    //setters
    void setRegionMaxHeight(F32 val);
    void setRegionMinPrimScale(F32 val);
    void setRegionMaxPrimScale(F32 val);
    void setRegionMaxPrimScaleNoMesh(F32 val);
    void setRegionMaxHollowSize(F32 val);
    void setRegionMinHoleSize(F32 val);

    void setMaxLinkedPrims(S32 val);
    void setMaxPhysLinkedPrims(S32 val);
    void setMaxInventoryItemsTransfer(S32 val);
    void setAllowRenderName(S32 val);
    void setAllowMinimap(bool val);
    void setAllowPhysicalPrims(bool val);
    void setAllowRenderWater(bool val);

    void setMaxPrimXPos(F32 val);
    void setMaxPrimYPos(F32 val);
    void setMaxPrimZPos(F32 val);
    void setMinPrimXPos(F32 val);
    void setMinPrimYPos(F32 val);
    void setMinPrimZPos(F32 val);
    void setMaxDragDistance(F32 val);
    void setMaxPhysPrimScale(F32 val);
    void setAllowParcelWindLight(bool val);
    void setEnableTeenMode(bool val);
    void setEnforceMaxBuild(bool val);
    void setLockedDrawDistance(bool val);

    void setDrawDistance(F32 val);
    void setTerrainDetailScale(F32 val);
// <FS:CR> Aurora Sim
// </AW: opensim-limits>

protected:
    void applyGridDefaults(bool opensim,bool publishGridLabel);
    F32 mRegionMaxHeight;
    F32 mRegionMinPrimScale;
    F32 mRegionMaxPrimScale;
    F32 mRegionMaxPrimScaleNoMesh;
    F32 mRegionMaxHollowSize;
    F32 mRegionMinHoleSize;
// <FS:CR> Aurora Sim
    S32 mMaxLinkedPrims;
    S32 mMaxPhysLinkedPrims;
    S32 mMaxInventoryItemsTransfer;
    S32 mAllowRenderName;
    bool mAllowMinimap;
    bool mAllowPhysicalPrims;
    bool mAllowRenderWater;

    F32     mMaxPrimXPos;
    F32     mMaxPrimYPos;
    F32     mMaxPrimZPos;
    F32     mMinPrimXPos;
    F32     mMinPrimYPos;
    F32     mMinPrimZPos;
    F32     mMaxDragDistance;
    F32     mMaxPhysPrimScale;
    bool    mAllowParcelWindLight;
    bool    mEnableTeenMode;
    bool    mEnforceMaxBuild;
    bool    mLockedDrawDistance;

    F32 mDrawDistance;
    F32 mTerrainDetailScale;
// <FS:CR> Aurora Sim
};
VSRegionLimits& vs_region_limits();

void vs_apply_region_limits();
