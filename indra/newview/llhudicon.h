/**
 * @file llhudicon.h
 * @brief LLHUDIcon class definition
 *
 * $LicenseInfo:firstyear=2006&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#ifndef LL_LLHUDICON_H
#define LL_LLHUDICON_H

#include "llpointer.h"

#include "llhudobject.h"
#include "v4color.h"
#include "v4coloru.h"
#include "v2math.h"
#include "llrect.h"
#include "llframetimer.h"
#include "llfontgl.h"
#include <set>
#include <vector>
#include <optional>
#include <functional>
#include "vsworldrenderer.h"
#include "llquaternion.h"

// Renders a 2D icon billboard floating at the location specified.
class LLViewerTexture;

class LLHUDIcon : public LLHUDObject
{
friend class LLHUDObject;

public:
    /*virtual*/ void render();
    /*virtual*/ void markDead();
    /*virtual*/ F32 getDistance() const { return mDistance; }

    void setImage(LLViewerTexture* imagep);
    struct NativeSource
    {
        LLUUID id;
        LLVector3 position,scale;
        LLVector3d globalPosition;
        LLQuaternion rotation;
        F32 radius=0;
    };
    using NativeSourceQuery=std::function<std::optional<NativeSource>()>;
    void setNativeSource(NativeSourceQuery,const VSUIRenderer::Image&,std::shared_ptr<const void> residency = {},std::function<void()> retire = {},std::function<VSUIRenderer::Image()> imageQuery = {});
    LLUUID getSourceID() const;
    std::optional<LLVector3> nativePickPoint() const;
    std::optional<VSWorldRenderer::Packet> nativePacket(const LLVector3d& regionOrigin) const;
    static LLPointer<LLHUDIcon> findNative(const LLUUID&);
    static void appendNative(VSWorldRenderer&,const LLVector3d& regionOrigin,std::vector<VSWorldRenderer::Packet>&);
    static LLHUDIcon* intersectNative(const LLVector4a&,const LLVector4a&,LLVector4a*);

    void setScale(F32 fraction_of_fov);

    void restartLifeTimer() { mLifeTimer.reset(); }

    static LLHUDIcon* lineSegmentIntersectAll(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection);

    static void updateAll();
    static void cleanupDeadIcons();
    static S32 getNumInstances();

    static bool iconsNearby();

    bool getHidden() const { return mHidden; }
    void setHidden( bool hide ) { mHidden = hide; }

    bool lineSegmentIntersect(const LLVector4a& start, const LLVector4a& end, LLVector4a* intersection);

    // <FS:Ansariel> Mark script error icons
    void setScriptError();
    bool getScriptError() const { return mScriptError; }
    static bool scriptIconsNearby();
    // </FS:Ansariel> Mark script error icons

protected:
    LLHUDIcon(const U8 type);
    ~LLHUDIcon();

private:
    LLPointer<LLViewerTexture> mImagep;
    NativeSourceQuery mNativeSource;
    std::function<VSUIRenderer::Image()> mNativeImageQuery;
    std::function<void()> mNativeRetire;
    std::shared_ptr<const void> mNativeResidency;
    LLVector3d mNativeRegionOrigin,mNativeAgentOrigin;
    VSUIRenderer::Image mNativeImage;
    VSWorldRenderer::Mesh mNativeMesh;
    std::array<LLVector3,4> mNativeQuad{};
    bool mNativeQuadKnown=false;
    F32 mNativeAlpha=0;
    bool nativeQuad(std::array<LLVector3,4>&,F32&);

    LLFrameTimer    mAnimTimer;
    LLFrameTimer    mLifeTimer;
    F32             mDistance;
    F32             mScale;
    bool            mHidden;

    typedef std::vector<LLPointer<LLHUDIcon> > icon_instance_t;
    static icon_instance_t sIconInstances;

    // <FS:Ansariel> Mark script error icons
    bool                    mScriptError;
    static icon_instance_t  sScriptErrorIconInstances;
    // </FS:Ansariel> Mark script error icons
};

#endif // LL_LLHUDICON_H
