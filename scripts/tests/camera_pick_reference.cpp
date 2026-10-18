void LLToolCamera::referenceCallback(const LLPickInfo& pick_info)
{
    LLToolCamera* camera = LLToolCamera::getInstance();
    if (!camera->mClickPickPending)
    {
        return;
    }
    camera->mClickPickPending = false;

    camera->mMouseDownX = pick_info.mMousePt.mX;
    camera->mMouseDownY = pick_info.mMousePt.mY;

    gViewerWindow->moveCursorToCenter();

    // Potentially recenter if click outside rectangle
    LLViewerObject* hit_obj = pick_info.getObject();

    // Check for hit the sky, or some other invalid point
    if (!hit_obj && pick_info.mPosGlobal.isExactlyZero())
    {
        camera->mValidClickPoint = false;
        return;
    }

    // check for hud attachments
    if (hit_obj && hit_obj->isHUDAttachment())
    {
        LLObjectSelectionHandle selection = LLSelectMgr::getInstance()->getSelection();
        if (!selection->getObjectCount() || selection->getSelectType() != SELECT_TYPE_HUD)
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
            if (isAgentAvatarValid() && (hit_obj == gAgentAvatarp))
            {
                // It's you
                good_customize_avatar_hit = true;
            }
            else if (hit_obj->isAttachment() && hit_obj->permYouOwner())
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
        LLViewerObject* hit_obj = pick_info.getObject();
        if (hit_obj)
        {
            // ...clicked on a world object, so focus at its position
            if (!hit_obj->isHUDAttachment())
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
            (hit_obj == gAgentAvatarp ||
             (hit_obj && hit_obj->isAttachment() && LLVOAvatar::findAvatarFromAttachment(hit_obj)->isSelf())))
        {
            LLToolCamera::getInstance()->mMouseSteering = true;
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