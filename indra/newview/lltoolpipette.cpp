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
#include "vsobjectselection.h"
#include "llview.h"
#include <utility>

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
#if VS_NATIVE_VULKAN
    if(LLToolMgr::instance().nativeCurrentTool()!=this)return false;
    const auto* consumer=mNativeConsumer.get();
    return mNativeAccount && mNativeAccount() && consumer && consumer->isInVisibleChain() &&
        (!mNativeOperation || mNativeOperation());
#else
    return false;
#endif
}
bool LLToolPipette::nativeConsumer(const LLView* view) const
{return nativeActive() && mNativeConsumer.get()==view;}


void LLToolPipette::setNativeActive(bool active,LLView* consumer,std::function<bool()> operation)
{
#if VS_NATIVE_VULKAN
    const bool highlighted=std::exchange(mNativeHighlightOwned,false);
    mNativeAccount={};mNativeOperation={};mNativeConsumer=LLHandle<LLView>();
    if(highlighted)
        if(const auto owner=VSNativeSession::active();owner && owner->existingObjectSelection())owner->existingObjectSelection()->clearHighlights();
    if(hasMouseCapture())setMouseCapture(false);
    auto& tools=LLToolMgr::instance();
    if(tools.nativeCurrentTool()==this)tools.clearTransientTool();
    if(!active || !consumer || !consumer->isInVisibleChain() || !tools.nativeToolsReady())return;
    const auto owner=VSNativeSession::active();if(!owner || !owner->regionReady())return;
    const auto account=owner->accountGuard();const auto view=consumer->getHandle();
    if(!account() || view.isDead() || (operation && !operation()))return;
    mNativeAccount=account;mNativeConsumer=view;mNativeOperation=std::move(operation);
    mSuccess=true;mTooltipMsg.clear();
    tools.setTransientTool(this);
    if(!nativeActive())setNativeActive(false);
#else
    (void)active;(void)consumer;(void)operation;
#endif
}
void LLToolPipette::handleDeselect()
{
    if(gViewerWindow && gViewerWindow->isNativeVulkan())setNativeActive(false);
    LLTool::handleDeselect();
}


bool LLToolPipette::nativeSample(S32 x,S32 y,MASK mask)
{
#if VS_NATIVE_VULKAN
    if(!nativeActive() || !gViewerWindow || !gViewerWindow->nativeContext())return false;
    const auto owner=VSNativeSession::active();if(!owner)return false;
    auto* selection=owner->objectSelection();if(!selection)return false;
    selection->clearHighlights();mNativeHighlightOwned=false;
    if(!nativeActive())return false;
    LLPickInfo pick(LLCoordGL(x,y),mask,true,true,false,false,true,false,nullptr);
    if(!gViewerWindow->nativePick(pick) || !pick.mNativeObjectCurrent || !pick.mNativeObjectCurrent() || pick.mObjectFace<0)return false;
    auto& world=gViewerWindow->nativeContext()->world();
    const auto object=world.object(pick.mObjectID);if(!object)return false;
    const auto source=world.objectAdmission(*object);if(!source || source->pcode!=LL_PCODE_VOLUME)return false;
    const auto face=world.face(*object,U32(pick.mObjectFace));LLTextureEntry entry;
    if(!face || !world.copyTextureEntry(*face,entry) || !nativeActive() || !pick.mNativeObjectCurrent())return false;
    const auto account=mNativeAccount,operation=mNativeOperation;const auto consumer=mNativeConsumer;
    if(!selection->beginHighlightRequest([account,operation,consumer]
        {return account && account() && !consumer.isDead() && (!operation || operation());}))return false;
    mNativeHighlightOwned=true;
    if(!selection->highlight({*object},true) || !nativeActive() || !pick.mNativeObjectCurrent())return false;
    mSuccess=true;setTextureEntry(&entry);return true;
#else
    return false;
#endif
}


bool LLToolPipette::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if(gViewerWindow && gViewerWindow->isNativeVulkan() && !nativeActive())
    {setNativeActive(false);return false;}
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
    if(gViewerWindow && gViewerWindow->isNativeVulkan())setNativeActive(false);
    LLTool::onMouseCaptureLost();
}


bool LLToolPipette::handleHover(S32 x, S32 y, MASK mask)
{
    if(gViewerWindow && gViewerWindow->isNativeVulkan() && !nativeActive())
    {setNativeActive(false);return false;}
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
