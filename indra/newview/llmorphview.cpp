/**
 * @file llmorphview.cpp
 * @brief Container for Morph functionality
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

#include "llmorphview.h"

#include "lljoint.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "lldrawable.h"
#include "lldrawpoolavatar.h"
#include "llface.h"
//#include "llfirstuse.h"
#include "llfloatertools.h"
#include "llresmgr.h"
#include "lltoolmgr.h"
#include "lltoolmorph.h"
#include "llviewercamera.h"
#include "llvoavatarself.h"
#include "llviewercontrol.h"        // ## Zi: Optional Edit Appearance Lighting
#include "llviewerwindow.h"
#include "vsavatarservices.h"
#include "vsnativeim.h"
#include "llcharacter.h"
#include "llavatarappearance.h"
#include "lluictrlfactory.h"
#include "llrootview.h"
#if VS_NATIVE_VULKAN
#include "vsnativesession.h"
#endif
#include "pipeline.h"

LLMorphView *gMorphView = NULL;

constexpr F32 MORPH_NEAR_CLIP = 0.1f;

//-----------------------------------------------------------------------------
// LLMorphView()
//-----------------------------------------------------------------------------
LLMorphView::LLMorphView(const LLMorphView::Params& p)
:   LLView(p),
    mCameraTargetJoint( NULL ),
    mCameraOffset(-0.5f, 0.05f, 0.07f ),
    mCameraTargetOffset(0.f, 0.f, 0.05f ),
    mOldCameraNearClip( 0.f ),
    mCameraPitch( 0.f ),
    mCameraYaw( 0.f ),
    mCameraDrivenByKeys( false )
{}

//-----------------------------------------------------------------------------
// initialize()
//-----------------------------------------------------------------------------
void    LLMorphView::initialize()
{
    mCameraPitch = 0.f;
    mCameraYaw = 0.f;

    auto* avatar = vs_avatar_character();
    if (!avatar)
    {
        gAgentCamera.changeCameraToDefault();
        return;
    }

    avatar->stopMotion( ANIM_AGENT_BODY_NOISE );

    //gAgentAvatarp->mSpecialRenderMode = 3; // <FS:Ansariel> Removed by SL-13522 09/20/2020

    // set up camera for close look at avatar
    mOldCameraNearClip = LLViewerCamera::getInstance()->getNear();
    LLViewerCamera::getInstance()->setNear(MORPH_NEAR_CLIP);
}

//-----------------------------------------------------------------------------
// shutdown()
//-----------------------------------------------------------------------------
void    LLMorphView::shutdown()
{
    if (auto* avatar = vs_avatar_character())
    {
        avatar->startMotion( ANIM_AGENT_BODY_NOISE );
        //gAgentAvatarp->mSpecialRenderMode = 0; // <FS:Ansariel> Removed by SL-13522 09/20/2020
        // reset camera
        LLViewerCamera::getInstance()->setNear(mOldCameraNearClip);
    }
}


//-----------------------------------------------------------------------------
// setVisible()
//-----------------------------------------------------------------------------
void LLMorphView::setVisible(bool visible)
{
    if( visible != getVisible() )
    {
        LLView::setVisible(visible);

        if (visible)
        {
            // TODO: verify some user action has already opened outfit editor? - Nyx
            initialize();

            // First run dialog
            //LLFirstUse::useAppearance();
        }
        else
        {
            // TODO: verify some user action has already closed outfit editor ? - Nyx
            shutdown();
        }
    }
}

void LLMorphView::updateCamera()
{
    auto* avatar = vs_avatar_character();
    if (!avatar) return;
#if VS_NATIVE_VULKAN
    if (VSNativeSession::active())
        mCameraTargetJoint = avatar->getJoint(mNativeTargetJointName.empty() ? "mHead" : mNativeTargetJointName);
#endif

    if (!mCameraTargetJoint)
    {
        setCameraTargetJoint( avatar->getJoint( "mHead" ) );
    }


    if (!mCameraTargetJoint) return;
    LLJoint* root_joint = avatar->getRootJoint();
    if( !root_joint )
    {
        return;
    }

    const LLQuaternion& avatar_rot = root_joint->getWorldRotation();

    LLVector3d joint_pos = gAgent.getPosGlobalFromAgent(mCameraTargetJoint->getWorldPosition());
    LLVector3d target_pos = joint_pos + mCameraTargetOffset * avatar_rot;

    LLQuaternion camera_rot_yaw(mCameraYaw, LLVector3::z_axis);
    LLQuaternion camera_rot_pitch(mCameraPitch, LLVector3::y_axis);

    LLVector3d camera_pos = joint_pos + mCameraOffset * camera_rot_pitch * camera_rot_yaw * avatar_rot;

    gAgentCamera.setCameraPosAndFocusGlobal( camera_pos, target_pos, gAgent.getID() );
}

void LLMorphView::setCameraDrivenByKeys(bool b)
{
    if( mCameraDrivenByKeys != b )
    {
        if( b )
        {
            // Reset to the default camera position specified by mCameraPitch, mCameraYaw, etc.
            updateCamera();
        }
        mCameraDrivenByKeys = b;
    }
}

LLMorphView* LLMorphView::ensureNativeOwner()
{
#if VS_NATIVE_VULKAN
    if (!VSNativeSession::active() || !gViewerWindow || !gViewerWindow->getRootView()) return nullptr;
    if (!gMorphView)
    {
        LLMorphView::Params p;
        p.name = "morph_view";
        p.rect = gViewerWindow->getRootView()->getLocalRect();
        p.follows.flags = FOLLOWS_ALL;
        p.visible = false;
        p.mouse_opaque = false;
        gMorphView = LLUICtrlFactory::create<LLMorphView>(p, gViewerWindow->getRootView());
    }
    return gMorphView;
#else
    return nullptr;
#endif
}
void LLMorphView::retireNativeAccount()
{
#if VS_NATIVE_VULKAN
    if (gViewerWindow && gViewerWindow->isNativeVulkan() && gMorphView)
    {
        auto* view = gMorphView;
        view->setVisible(false);
        gMorphView = nullptr;
        delete view;
    }
#endif
}

void LLMorphView::setCameraTargetJoint(LLJoint* joint)
{
    mCameraTargetJoint = joint;
    mNativeTargetJointName = joint ? joint->getName() : std::string();
}
