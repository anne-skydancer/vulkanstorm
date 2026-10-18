/**
 * @file lltoolselectrect.cpp
 * @brief A tool to select multiple objects with a screen-space rectangle.
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
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

#include "llviewerprecompiledheaders.h"

// File includes
#include "lltoolselectrect.h"

// Library includes
#include "llgl.h"
#include "llrender.h"

// Viewer includes
#include "llviewercontrol.h"
#include "llui.h"
#include "llselectmgr.h"
#include "lltoolmgr.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include "llviewerwindow.h"
#include "llviewercamera.h"

#include "llglheaders.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsobjectselection.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "rlvactions.h"
#include "rlvhandler.h"
#endif

// Globals
const S32 SLOP_RADIUS = 5;


//
// Member functions
//

LLToolSelectRect::LLToolSelectRect( LLToolComposite* composite )
    :
    LLToolSelect( composite ),
    mDragStartX(0),
    mDragStartY(0),
    mDragEndX(0),
    mDragEndY(0),
    mDragLastWidth(0),
    mDragLastHeight(0),
    mMouseOutsideSlop(false)

{ }


void dialog_refresh_all(void);

bool LLToolSelectRect::handleMouseDown(S32 x, S32 y, MASK mask)
{
    bool pick_rigged = false; //gSavedSettings.getBOOL("AnimatedObjectsAllowLeftClick");
    handlePick(gViewerWindow->pickImmediate(x, y, true /* pick_transparent */, pick_rigged));

    LLTool::handleMouseDown(x, y, mask);

#if VS_NATIVE_VULKAN
    if(gViewerWindow->isNativeVulkan())return mPick.mObjectID.notNull() &&
        (!mPick.mNativeObjectCurrent || mPick.mNativeObjectCurrent());
#endif
    return mPick.getObject().notNull();
}

void LLToolSelectRect::handlePick(const LLPickInfo& pick)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow->isNativeVulkan())
    {
        const auto session=VSNativeSession::active();if(!session)return;
        if(pick.mNativeRequestCurrent && !pick.mNativeRequestCurrent())return;
        const auto account=session->accountGuard();
        mNativeRectangleCurrent=[account,request=pick.mNativeRequestCurrent]
            {return account() && (!request || request());};
    }
#endif
    mPick = pick;

    // start dragging rectangle
    setMouseCapture( true );

    mDragStartX = pick.mMousePt.mX;
    mDragStartY = pick.mMousePt.mY;
    mDragEndX = pick.mMousePt.mX;
    mDragEndY = pick.mMousePt.mY;

    mMouseOutsideSlop = false;
}


bool LLToolSelectRect::handleMouseUp(S32 x, S32 y, MASK mask)
{
#if VS_NATIVE_VULKAN
    mNativeRectangleReleasing=gViewerWindow->isNativeVulkan();
#endif
    setMouseCapture( false );
#if VS_NATIVE_VULKAN
    mNativeRectangleReleasing=false;
#endif

    if( mMouseOutsideSlop )
    {
        mDragLastWidth = 0;
        mDragLastHeight = 0;

        mMouseOutsideSlop = false;

#if VS_NATIVE_VULKAN
        if(gViewerWindow->isNativeVulkan())
        {
            const auto session=VSNativeSession::active();
            const auto selection=session?session->retainObjectSelection():nullptr;
            if(selection)selection->commitRectangle(mask==MASK_CONTROL,mNativeRectangleCurrent);
            return true;
        }
#endif
        if (mask == MASK_CONTROL)
        {
            LLSelectMgr::getInstance()->deselectHighlightedObjects();
        }
        else
        {
            LLSelectMgr::getInstance()->selectHighlightedObjects();
        }
        return true;
    }
    else
    {
        return LLToolSelect::handleMouseUp(x, y, mask);
    }
}


bool LLToolSelectRect::handleHover(S32 x, S32 y, MASK mask)
{
    if( hasMouseCapture() )
    {
        if (mMouseOutsideSlop || outsideSlop(x, y, mDragStartX, mDragStartY))
        {
            if (!mMouseOutsideSlop && !(mask & MASK_SHIFT) && !(mask & MASK_CONTROL))
            {
                // just started rect select, and not adding to current selection
                LLSelectMgr::getInstance()->deselectAll();
            }
            mMouseOutsideSlop = true;
            mDragEndX = x;
            mDragEndY = y;

            handleRectangleSelection(x, y, mask);
        }
        else
        {
            return LLToolSelect::handleHover(x, y, mask);
        }

        LL_DEBUGS("UserInput") << "hover handled by LLToolSelectRect (active)" << LL_ENDL;
    }
    else
    {
        LL_DEBUGS("UserInput") << "hover handled by LLToolSelectRect (inactive)" << LL_ENDL;
    }

    gViewerWindow->setCursor(UI_CURSOR_ARROW);
    return true;
}


void LLToolSelectRect::draw()
{
    if( hasMouseCapture() && mMouseOutsideSlop)
    {
        if (gKeyboard->currentMask(true) == MASK_CONTROL)
        {
            gGL.color4f(1.f, 0.f, 0.f, 1.f);
        }
        else
        {
            gGL.color4f(1.f, 1.f, 0.f, 1.f);
        }
        if(!gViewerWindow->isNativeVulkan())gGL.getTexUnit(0)->unbind(LLTexUnit::TT_TEXTURE);
        gl_rect_2d(
            llmin(mDragStartX, mDragEndX),
            llmax(mDragStartY, mDragEndY),
            llmax(mDragStartX, mDragEndX),
            llmin(mDragStartY, mDragEndY),
            false);
        if (gKeyboard->currentMask(true) == MASK_CONTROL)
        {
            gGL.color4f(1.f, 0.f, 0.f, 0.1f);
        }
        else
        {
            gGL.color4f(1.f, 1.f, 0.f, 0.1f);
        }
        gl_rect_2d(
            llmin(mDragStartX, mDragEndX),
            llmax(mDragStartY, mDragEndY),
            llmax(mDragStartX, mDragEndX),
            llmin(mDragStartY, mDragEndY));
    }
}

// true if x,y outside small box around start_x,start_y
bool LLToolSelectRect::outsideSlop(S32 x, S32 y, S32 start_x, S32 start_y)
{
    S32 dx = x - start_x;
    S32 dy = y - start_y;

    return (dx <= -SLOP_RADIUS || SLOP_RADIUS <= dx || dy <= -SLOP_RADIUS || SLOP_RADIUS <= dy);
}


//
// Static functions
//

void LLToolSelectRect::handleDeselect()
{
    LLToolSelect::handleDeselect();
#if VS_NATIVE_VULKAN
    if(mNativeRectangleReleasing)return;
    mNativeRectangleCurrent={};
    if(const auto owner=VSNativeSession::active())
        if(auto* selection=owner->existingObjectSelection())selection->clearRectangle();
#endif
}
#if VS_NATIVE_VULKAN
void LLToolSelectRect::handleNativeRectangleSelection(S32 x,S32 y,MASK mask)
{
    if(!mNativeRectangleCurrent || !mNativeRectangleCurrent())return;
    // Same RLVa policy as the reference rectangle controller.
    if(RlvActions::isRlvEnabled() && (RlvActions::canEdit(ERlvCheckType::Nothing) ||
        RlvActions::hasBehaviour(RLV_BHVR_INTERACT)))return;
    const auto session=VSNativeSession::active();if(!session)return;
    const auto selection=session->retainObjectSelection();if(!selection)return;
    const F32 maximum=gSavedSettings.getF32("MaxSelectDistance");
    bool limit=gSavedSettings.getBOOL("LimitSelectDistance");
    F32 distanceSquared=maximum*maximum;
    const auto& scale=LLUI::getScaleFactor();
    const S32 left=ll_round(F32(llmin(x,mDragStartX))*scale.mV[VX]);
    const S32 right=ll_round(F32(llmax(x,mDragStartX))*scale.mV[VX]);
    const S32 top=ll_round(F32(llmax(y,mDragStartY))*scale.mV[VY]);
    const S32 bottom=ll_round(F32(llmin(y,mDragStartY))*scale.mV[VY]);
    const S32 width=right-left+1,height=top-bottom+1;
    const bool grow=height>mDragLastHeight || width>mDragLastWidth;
    const bool shrink=height<mDragLastHeight || width<mDragLastWidth;
    if(!grow && !shrink)return;
    auto& camera=LLViewerCamera::instance();
    F32 nearPlane=camera.getNear(),farPlane=camera.getFar();
    const auto avatar=gAgent.getPositionAgent();
    const auto relative=avatar-camera.getOrigin();
    if(limit)
    {
        farPlane=relative*camera.getAtAxis()+maximum;
        nearPlane=llmax(relative*camera.getAtAxis()-maximum,.1f);
    }
    F32 projectionFar=limit?maximum:gAgentCamera.mDrawDistance;
    if(gRlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH))
    {
        const F32 range=RlvActions::getModifierValue<float>(RLV_MODIFIER_FARTOUCHDIST);
        farPlane=relative*camera.getAtAxis()+range;
        nearPlane=llmax(relative*camera.getAtAxis()-range,.1f);
        limit=true;distanceSquared=range*range;projectionFar=range;
    }
    const auto frustum=camera.selectionFrustum((left+right)/2-width/2,(top+bottom)/2-height/2,
        width,height,nearPlane,farPlane,projectionFar);
    if(!frustum)return;
    if(selection->updateRectangle(*frustum,grow,shrink,mask==MASK_CONTROL,limit,distanceSquared,avatar,mNativeRectangleCurrent))
    {
        // A resource refusal retains both accepted highlights and prior size,
        // so the same unchanged pointer retries after ownership is available.
        mDragLastWidth=width;mDragLastHeight=height;
    }
}
#endif
