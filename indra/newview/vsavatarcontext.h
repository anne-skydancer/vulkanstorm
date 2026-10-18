// Actual peer avatar location/region values for shared CPU UI policies. LGPL-2.1.
#pragma once
#include "vsregioncontext.h"
#include "lluuid.h"
#include "v3dmath.h"
#include <functional>
#include <optional>
class LLViewerRegion;
class VSAvatarContext
{
public:
    static std::optional<VSAvatarContext> resolve(const LLUUID&);
    bool current() const{return mCurrent && mCurrent();}
    const LLUUID& id()const{return mID;}
    const LLVector3d& position()const{return mPosition;}
    bool altitudeKnown()const{return mAltitudeKnown;}
    const LLHost& host()const{return mHost;}
    LLUUID getOwner()const;
    bool isEstateManager()const;
    bool isOwnedSelf()const;
    bool isOwnedGroup()const;
private:
    LLUUID mID;
    LLHost mHost;
    LLVector3d mPosition;
    bool mAltitudeKnown=true;
    const LLViewerRegion* mLegacy=nullptr;
    std::optional<VSRegionContext> mNative;
    std::function<bool()> mCurrent;
};
