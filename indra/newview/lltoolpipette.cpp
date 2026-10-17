/**
 * @file lltoolpipette.cpp
 * @brief LLToolPipette class implementation
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

/**
 * A tool to pick texture entry infro from objects in world (color/texture)
 */

#include "llviewerprecompiledheaders.h"

// File includes
#include "lltoolpipette.h"

// Library includes
#include "lltooltip.h"

// Viewer includes
#include "llviewerobjectlist.h"
#include "llviewerwindow.h"
#include "llselectmgr.h"
#include "lltoolmgr.h"
#include "vsnativesession.h"
#include "vsvulkancontext.h"
#include "vsworldscene.h"

//
// Member functions
//

LLToolPipette::LLToolPipette()
:   LLTool(std::string("Pipette"))
,   mSuccess(true)
{
}


LLToolPipette::~LLToolPipette()
{ }


bool LLToolPipette::nativeActive() const
{
    return mNativeActive && mNativeAccount && mNativeAccount();
}

void LLToolPipette::setNativeActive(bool active)
{
    mNativeActive = false;
    mNativeAccount = {};
    if (!active) { setMouseCapture(false); return; }
#if VS_NATIVE_VULKAN
    if (const auto owner = VSNativeSession::active(); owner && owner->regionReady())
    {
        mNativeAccount = owner->accountGuard();
        mNativeActive = true;
        mSuccess = true;
        mTooltipMsg.clear();
    }
#endif
}

bool LLToolPipette::nativeSample(S32 x, S32 y, MASK mask)
{
#if VS_NATIVE_VULKAN
    if (!nativeActive() || !gViewerWindow || !gViewerWindow->nativeContext()) return false;
    LLPickInfo pick(LLCoordGL(x, y), mask, true, true, false, false, true, false, nullptr);
    if (!gViewerWindow->nativePick(pick) || pick.mObjectFace < 0) return false;
    auto& world = gViewerWindow->nativeContext()->world();
    const auto object = world.object(pick.mObjectID);
    if (!object) return false;
    const auto face = world.face(*object, U32(pick.mObjectFace));
    LLTextureEntry entry;
    if (!face || !world.copyTextureEntry(*face, entry)) return false;
    // Reuse the same signal and picker callbacks. Texture picking still
    // resolves inventory and enforces its ordinary asset permission rules.
    mSuccess = true;
    setTextureEntry(&entry);
    return true;
#else
    return false;
#endif
}

bool LLToolPipette::handleMouseDown(S32 x, S32 y, MASK mask)
{
    mSuccess = true;
    mTooltipMsg.clear();
    setMouseCapture(true);
    if (gViewerWindow->isNativeVulkan())
    {
        if (!nativeSample(x, y, mask)) mSuccess = false;
        return true;
    }
    gViewerWindow->pickAsync(x, y, mask, pickCallback);
    return true;
}

bool LLToolPipette::handleMouseUp(S32 x, S32 y, MASK mask)
{
    mSuccess = true;
    if (gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        setNativeActive(false);
        return true;
    }
    LLSelectMgr::getInstance()->unhighlightAll();
    // *NOTE: This assumes the pipette tool is a transient tool.
    LLToolMgr::getInstance()->clearTransientTool();
    setMouseCapture(false);
    return true;
}

void LLToolPipette::onMouseCaptureLost()
{
    // Focus manager already transferred capture. Do not release its new owner.
    mNativeActive = false;
    mNativeAccount = {};
    LLTool::onMouseCaptureLost();
}

bool LLToolPipette::handleHover(S32 x, S32 y, MASK mask)
{
    gViewerWindow->setCursor(mSuccess ? UI_CURSOR_PIPETTE : UI_CURSOR_NO);
    if (hasMouseCapture()) // mouse button is down
    {
        if (gViewerWindow->isNativeVulkan()) { if (!nativeSample(x, y, mask)) mSuccess = false; }
        else gViewerWindow->pickAsync(x, y, mask, pickCallback);
        return true;
    }
    return false;
}

bool LLToolPipette::handleToolTip(S32 x, S32 y, MASK mask)
{
    if (mTooltipMsg.empty())
    {
        return false;
    }

    LLRect sticky_rect;
    sticky_rect.setCenterAndSize(x, y, 20, 20);
    LLToolTipMgr::instance().show(LLToolTip::Params()
        .message(mTooltipMsg)
        .sticky_rect(sticky_rect));

    return true;
}

void LLToolPipette::pickCallback(const LLPickInfo& pick_info)
{
    LLViewerObject* hit_obj = pick_info.getObject();
    LLSelectMgr::getInstance()->unhighlightAll();

    // if we clicked on a face of a valid prim, save off texture entry data
    if (hit_obj &&
        hit_obj->getPCode() == LL_PCODE_VOLUME &&
        pick_info.mObjectFace != -1 &&
        hit_obj->getNumTEs() > pick_info.mObjectFace)
    {
        //TODO: this should highlight the selected face only
        LLSelectMgr::getInstance()->highlightObjectOnly(hit_obj);

        LLPointer<LLViewerObject> hit_obj_ptr = hit_obj;
        LLToolPipette::getInstance()->mSignal(hit_obj_ptr, pick_info.mObjectFace);
    }
}

void LLToolPipette::setResult(bool success, const std::string& msg)
{
    mTooltipMsg = msg;
    mSuccess = success;
}
