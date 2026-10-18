/**
 * @file lltoolfocus.cpp
 * @brief A tool to set the build focus point.
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
#include "lltoolfocus.h"
#include "vsavatarservices.h"
#if VS_NATIVE_VULKAN
#include "vsobjectcontext.h"
#include "vsvulkancontext.h"
#include <set>
#endif
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#include "vsobjectselection.h"
#endif

// Library includes
#include "v3math.h"
#include "llfontgl.h"
#include "llui.h"

// Viewer includes
#include "llagent.h"
#include "llagentcamera.h"
#include "llbutton.h"
#include "llviewercontrol.h"
#include "llviewerinput.h"//<FS:JL> Mouse movement by Singularity
#include "lldrawable.h"
#include "lltooltip.h"
#include "llhudmanager.h"
#include "llfloatertools.h"
#include "llselectmgr.h"
#include "llstatusbar.h"
#include "lltoolmgr.h"
#include "llviewercamera.h"
#include "llviewerobject.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llmorphview.h"
#include "llfloaterreg.h"
#include "llfloatercamera.h"
#include "llmenugl.h"

// Globals
bool gCameraBtnZoom = true;
bool gCameraBtnOrbit = false;
bool gCameraBtnPan = false;

const S32 SLOP_RANGE = 4;

//
// Camera - shared functionality
//

LLToolCamera::LLToolCamera()
:   LLTool(std::string("Camera")),
    mAccumX(0),
    mAccumY(0),
    mMouseDownX(0),
    mMouseDownY(0),
    mOutsideSlopX(false),
    mOutsideSlopY(false),
    mValidClickPoint(false),
    mClickPickPending(false),
    mValidSelection(false),
    mMouseSteering(false),
    mMouseUpX(0),
    mMouseUpY(0),
    mMouseUpMask(MASK_NONE)
{ }


LLToolCamera::~LLToolCamera()
{ }

// virtual
void LLToolCamera::handleSelect()
{
    if (gFloaterTools)
    {
        gFloaterTools->setStatusText("camera");
        // in case we start from tools floater, we count any selection as valid
        mValidSelection = gFloaterTools->getVisible();
    }
}

// virtual
void LLToolCamera::handleDeselect()
{
//  gAgent.setLookingAtAvatar(false);

    // Make sure that temporary selection won't pass anywhere except pie tool.
    MASK override_mask = gKeyboard ? gKeyboard->currentMask(true) : 0;
    if (!mValidSelection && (override_mask != MASK_NONE || (gFloaterTools && gFloaterTools->getVisible())))
    {
        LLMenuGL::sMenuContainer->hideMenus();
        #if VS_NATIVE_VULKAN
        const auto native=VSNativeSession::active();
        if(native && gViewerWindow->isNativeVulkan())
        {
            if(auto* selection=native->existingObjectSelection())selection->validateSelection();
        }
        else
        #endif
        LLSelectMgr::getInstance()->validateSelection();
    }
}

bool LLToolCamera::handleMouseDown(S32 x, S32 y, MASK mask)
{
    // Ensure a mouseup
    setMouseCapture(true);

    // call the base class to propogate info to sim
    LLTool::handleMouseDown(x, y, mask);

    mAccumX = 0;
    mAccumY = 0;

    mOutsideSlopX = false;
    mOutsideSlopY = false;

    mValidClickPoint = false;

    // Sometimes Windows issues down and up events near simultaneously
    // without giving async pick a chance to trigged
    // Ex: mouse from numlock emulation
    mClickPickPending = true;

    // If mouse capture gets ripped away, claim we moused up
    // at the point we moused down. JC
    mMouseUpX = x;
    mMouseUpY = y;
    mMouseUpMask = mask;

    gViewerWindow->hideCursor();

    gViewerWindow->pickAsync(x, y, mask, pickCallback, /*bool pick_transparent*/ false, /*bool pick_rigged*/ false, /*bool pick_unselectable*/ true);

    return true;
}

namespace {
struct ReferenceCameraPickPeer
{
    bool current()const{return true;}
    auto object(const LLPickInfo& pick)const{return pick.getObject();}
    bool hud(LLViewerObject* object)const{return object->isHUDAttachment();}
    bool hudSelection()const{auto selected=LLSelectMgr::instance().getSelection();return selected->getObjectCount() && selected->getSelectType()==SELECT_TYPE_HUD;}
    bool self(LLViewerObject* object)const{return isAgentAvatarValid() && object==gAgentAvatarp;}
    bool isSelfPointer(LLViewerObject* object)const{return object==gAgentAvatarp;}
    bool attachment(LLViewerObject* object)const{return object->isAttachment();}
    bool youOwner(LLViewerObject* object)const{return object->permYouOwner();}
    bool attachmentSelf(LLViewerObject* object)const{return LLVOAvatar::findAvatarFromAttachment(object)->isSelf();}
};
#if VS_NATIVE_VULKAN
struct NativeCameraPickPeer
{
    using Object=std::optional<VSWorldScene::ObjectHandle>;
    std::shared_ptr<VSNativeSession> session;
    std::function<bool()> account;
    VSWorldScene& world;
    bool current()const{return account && account() && session==VSNativeSession::active() && session->phase()==VSNativeSession::Phase::Connected;}
    Object object(const LLPickInfo& pick)const{return current()?world.object(pick.mObjectID):Object();}
    bool hud(const Object& object)const{const auto row=current()?world.objectAdmission(*object):std::nullopt;return row && row->hud;}
    bool hudSelection()const{auto* owner=session->existingObjectSelection();if(!current()||!owner)return false;const auto type=owner->selectionType();return type.selected && type.type==VSObjectSelection::SelectionType::HUD;}
    bool self(const Object& object)const{return object && current() && world.current(*object) && object->id==gAgent.getID() && vs_avatar_character();}
    bool isSelfPointer(const Object& object)const{return self(object);}
    bool attachment(const Object& object)const{const auto row=current()?world.objectAdmission(*object):std::nullopt;return row && row->attachment;}
    bool youOwner(const Object& object)const{const auto row=current()?world.objectAdmission(*object):std::nullopt;return row && VSObjectContext::ownedByAgent(row->rootUpdateFlags);}
    bool attachmentSelf(const Object& object)const
    {
        if(!current() || !object)return false;
        auto cursor=object;std::set<LLUUID> visited;
        while(cursor && current() && visited.insert(cursor->id).second)
        {
            const auto row=world.objectAdmission(*cursor);if(!row)return false;
            if(row->pcode==LL_PCODE_LEGACY_AVATAR)return self(cursor);
            cursor=world.parentObject(*cursor);
        }
        return false;
    }
};
#endif
}
template<class Peer>
void LLToolCamera::applyPick(const LLPickInfo& pick_info,Peer& peer)
{
    LLToolCamera* camera=this;
    if(!peer.current())return;
    if (!camera->mClickPickPending)
    {
        return;
    }
    camera->mClickPickPending = false;

    camera->mMouseDownX = pick_info.mMousePt.mX;
    camera->mMouseDownY = pick_info.mMousePt.mY;

    gViewerWindow->moveCursorToCenter();

    // Potentially recenter if click outside rectangle
    auto hit_obj=peer.object(pick_info);

    // Check for hit the sky, or some other invalid point
    if (!hit_obj && pick_info.mPosGlobal.isExactlyZero())
    {
        camera->mValidClickPoint = false;
        return;
    }

    // check for hud attachments
    if (hit_obj && peer.hud(hit_obj))
    {
        if (!peer.hudSelection())
        {
            camera->mValidClickPoint = false;
            return;
        }
    }

    if( CAMERA_MODE_CUSTOMIZE_AVATAR == gAgentCamera.getCameraMode() )
    {
        bool good_customize_avatar_hit = false;
        if( hit_obj )
        {
            if (peer.self(hit_obj))
            {
                // It's you
                good_customize_avatar_hit = true;
            }
            else if (peer.attachment(hit_obj) && peer.youOwner(hit_obj))
            {
                // It's an attachment that you're wearing
                good_customize_avatar_hit = true;
            }
        }

        if( !good_customize_avatar_hit )
        {
            camera->mValidClickPoint = false;
            return;
        }

        if( gMorphView )
        {
            gMorphView->setCameraDrivenByKeys( false );
        }
    }
    //RN: check to see if this is mouse-driving as opposed to ALT-zoom or Focus tool
    else if (pick_info.mKeyMask & MASK_ALT ||
            (LLToolMgr::getInstance()->getCurrentTool()->getName() == "Camera"))
    {
        auto hit_obj=peer.object(pick_info);
        if (hit_obj)
        {
            // ...clicked on a world object, so focus at its position
            if (!peer.hud(hit_obj))
            {
                gAgentCamera.setFocusOnAvatar(false, ANIMATE);
                gAgentCamera.setFocusGlobal(pick_info);
            }
        }
        else if (!pick_info.mPosGlobal.isExactlyZero())
        {
            // Hit the ground
            gAgentCamera.setFocusOnAvatar(false, ANIMATE);
            gAgentCamera.setFocusGlobal(pick_info);
        }

        bool zoom_tool = gCameraBtnZoom && (LLToolMgr::getInstance()->getBaseTool() == LLToolCamera::getInstance());
        // <FS:Ansariel> Replace frequently called gSavedSettings
        static LLCachedControl<bool> sFreezeTime(gSavedSettings, "FreezeTime");
        // </FS:Ansariel>
        if (!(pick_info.mKeyMask & MASK_ALT) &&
            !LLFloaterCamera::inFreeCameraMode() &&
            !zoom_tool &&
            gAgentCamera.cameraThirdPerson() &&
            gViewerWindow->getLeftMouseDown() &&
            // <FS:Ansariel> Replace frequently called gSavedSettings
            //!gSavedSettings.getBOOL("FreezeTime") &&
            !sFreezeTime &&
            // </FS:Ansariel>
            (peer.isSelfPointer(hit_obj) ||
             (hit_obj && peer.attachment(hit_obj) && peer.attachmentSelf(hit_obj))))
        {
            camera->mMouseSteering = true;
        }

    }

    camera->mValidClickPoint = true;

    if( CAMERA_MODE_CUSTOMIZE_AVATAR == gAgentCamera.getCameraMode() )
    {
        gAgentCamera.setFocusOnAvatar(false, false);

        LLVector3d cam_pos = gAgentCamera.getCameraPositionGlobal();

        gAgentCamera.setCameraPosAndFocusGlobal( cam_pos, pick_info.mPosGlobal, pick_info.mObjectID);
    }
}
void LLToolCamera::pickCallback(const LLPickInfo& pick_info)
{
#if VS_NATIVE_VULKAN
    if(gViewerWindow && gViewerWindow->isNativeVulkan())
    {
        const auto owner=VSNativeSession::active();
        if(!owner || !gViewerWindow->nativeContext())return;
        NativeCameraPickPeer peer{owner,owner->accountGuard(),gViewerWindow->nativeContext()->world()};
        getInstance()->applyPick(pick_info,peer);return;
    }
#endif
    ReferenceCameraPickPeer peer;getInstance()->applyPick(pick_info,peer);
}


// "Let go" of the mouse, for example on mouse up or when
// we lose mouse capture.  This ensures that cursor becomes visible
// if a modal dialog pops up during Alt-Zoom. JC
void LLToolCamera::releaseMouse()
{
    // Need to tell the sim that the mouse button is up, since this
    // tool is no longer working and cursor is visible (despite actual
    // mouse button status).
    LLTool::handleMouseUp(mMouseUpX, mMouseUpY, mMouseUpMask);

    gViewerWindow->showCursor();

    //for the situation when left click was performed on the Agent
    if (!LLFloaterCamera::inFreeCameraMode())
    {
        LLToolMgr::getInstance()->clearTransientTool();
    }

    mMouseSteering = false;
    mValidClickPoint = false;
    mOutsideSlopX = false;
    mOutsideSlopY = false;
}


bool LLToolCamera::handleMouseUp(S32 x, S32 y, MASK mask)
{
    // Claim that we're mousing up somewhere
    mMouseUpX = x;
    mMouseUpY = y;
    mMouseUpMask = mask;

    if (hasMouseCapture())
    {
        // Do not move camera if we haven't gotten a pick
        if (!mClickPickPending)
        {
            if (mValidClickPoint)
            {
                if (CAMERA_MODE_CUSTOMIZE_AVATAR == gAgentCamera.getCameraMode())
                {
                    LLCoordGL mouse_pos;
                    LLVector3 focus_pos = gAgent.getPosAgentFromGlobal(gAgentCamera.getFocusGlobal());
                    bool success = LLViewerCamera::getInstance()->projectPosAgentToScreen(focus_pos, mouse_pos);
                    if (success)
                    {
                        LLUI::getInstance()->setMousePositionScreen(mouse_pos.mX, mouse_pos.mY);
                    }
                }
                else if (mMouseSteering)
                {
                    LLUI::getInstance()->setMousePositionScreen(mMouseDownX, mMouseDownY);
                }
                else
                {
                    gViewerWindow->moveCursorToCenter();
                }
            }
            else
            {
                // not a valid zoomable object
                LLUI::getInstance()->setMousePositionScreen(mMouseDownX, mMouseDownY);
            }
        }

        // calls releaseMouse() internally
        setMouseCapture(false);
    }
    else
    {
        releaseMouse();
    }

    return true;
}

static bool right_hold_mouse_walk = false;//<FS:JL> Mouse movement by Singularity

bool LLToolCamera::handleHover(S32 x, S32 y, MASK mask)
{
    //<FS:JL> Mouse movement by Singularity
    if (right_hold_mouse_walk)
    {
        agent_push_forward(KEYSTATE_LEVEL);
    }
    //</FS:JL>

    S32 dx = gViewerWindow->getCurrentMouseDX();
    S32 dy = gViewerWindow->getCurrentMouseDY();

    if (hasMouseCapture() && mValidClickPoint)
    {
        mAccumX += llabs(dx);
        mAccumY += llabs(dy);

        if (mAccumX >= SLOP_RANGE)
        {
            mOutsideSlopX = true;
        }

        if (mAccumY >= SLOP_RANGE)
        {
            mOutsideSlopY = true;
        }
    }

    if (mOutsideSlopX || mOutsideSlopY)
    {
        if (!mValidClickPoint)
        {
            LL_DEBUGS("UserInput") << "hover handled by LLToolFocus [invalid point]" << LL_ENDL;
            gViewerWindow->setCursor(UI_CURSOR_NO);
            gViewerWindow->showCursor();
            return true;
        }

        if (gCameraBtnOrbit ||
            mask == MASK_ORBIT ||
            mask == (MASK_ALT | MASK_ORBIT))
        {
            // Orbit tool
            if (hasMouseCapture())
            {
                const F32 RADIANS_PER_PIXEL = 360.f * DEG_TO_RAD / gViewerWindow->getWorldViewWidthScaled();

                if (dx != 0)
                {
                    gAgentCamera.cameraOrbitAround( -dx * RADIANS_PER_PIXEL );
                }

                if (dy != 0)
                {
                    gAgentCamera.cameraOrbitOver( -dy * RADIANS_PER_PIXEL );
                }

                gViewerWindow->moveCursorToCenter();
            }
            LL_DEBUGS("UserInput") << "hover handled by LLToolFocus [active]" << LL_ENDL;
        }
        else if (   gCameraBtnPan ||
                    mask == MASK_PAN ||
                    mask == (MASK_PAN | MASK_ALT) )
        {
            // Pan tool
            if (hasMouseCapture())
            {
                LLVector3d camera_to_focus = gAgentCamera.getCameraPositionGlobal();
                camera_to_focus -= gAgentCamera.getFocusGlobal();
                F32 dist = (F32) camera_to_focus.normVec();

                // Fudge factor for pan
                F32 meters_per_pixel = 3.f * dist / gViewerWindow->getWorldViewWidthScaled();

                if (dx != 0)
                {
                    gAgentCamera.cameraPanLeft( dx * meters_per_pixel );
                }

                if (dy != 0)
                {
                    gAgentCamera.cameraPanUp( -dy * meters_per_pixel );
                }

                gViewerWindow->moveCursorToCenter();
            }
            LL_DEBUGS("UserInput") << "hover handled by LLToolPan" << LL_ENDL;
        }
        else if (gCameraBtnZoom)
        {
            // Zoom tool
            if (hasMouseCapture())
            {

                const F32 RADIANS_PER_PIXEL = 360.f * DEG_TO_RAD / gViewerWindow->getWorldViewWidthScaled();

                if (dx != 0)
                {
                    gAgentCamera.cameraOrbitAround( -dx * RADIANS_PER_PIXEL );
                }

                const F32 IN_FACTOR = 0.99f;

                if (dy != 0 && mOutsideSlopY )
                {
                    if (mMouseSteering)
                    {
                        gAgentCamera.cameraOrbitOver( -dy * RADIANS_PER_PIXEL );
                    }
                    else
                    {
                        gAgentCamera.cameraZoomIn((F32)pow( IN_FACTOR, dy ) );
                    }
                }

                gViewerWindow->moveCursorToCenter();
            }

            LL_DEBUGS("UserInput") << "hover handled by LLToolZoom" << LL_ENDL;
        }
    }

    if (gCameraBtnOrbit ||
        mask == MASK_ORBIT ||
        mask == (MASK_ALT | MASK_ORBIT))
    {
        gViewerWindow->setCursor(UI_CURSOR_TOOLCAMERA);
    }
    else if (   gCameraBtnPan ||
                mask == MASK_PAN ||
                mask == (MASK_PAN | MASK_ALT) )
    {
        gViewerWindow->setCursor(UI_CURSOR_TOOLPAN);
    }
    else
    {
        gViewerWindow->setCursor(UI_CURSOR_TOOLZOOMIN);
    }

    return true;
}

//<FS:JL> Mouse movement by Singularity
bool LLToolCamera::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
    if (mMouseSteering)
    {
        agent_push_forward(KEYSTATE_DOWN);
        right_hold_mouse_walk = true;
        return true;
    }
    else
    {
        return false;
    }
}

bool LLToolCamera::handleRightMouseUp(S32 x, S32 y, MASK mask)
{
    if (mMouseSteering || right_hold_mouse_walk)
    {
        agent_push_forward(KEYSTATE_UP);
        right_hold_mouse_walk = false;
        return true;
    }
    else
    {
        return false;
    }
}
//</FS:JL>

void LLToolCamera::onMouseCaptureLost()
{
    releaseMouse();
    // <FS:Ansariel> Mouse movement by Singularity
    handleRightMouseUp(0,0,0);
}
