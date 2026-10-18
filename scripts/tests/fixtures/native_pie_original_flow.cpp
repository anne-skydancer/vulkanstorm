bool LLToolPie::handleLeftClickPick()
{
    S32 x = mPick.mMousePt.mX;
    S32 y = mPick.mMousePt.mY;
    MASK mask = mPick.mKeyMask;
    if (mPick.mPickType == LLPickInfo::PICK_PARCEL_WALL)
    {
        LLParcel* parcel = LLViewerParcelMgr::getInstance()->getCollisionParcel();
        if (parcel)
        {
            LLViewerParcelMgr::getInstance()->selectCollisionParcel();
            if (parcel->getParcelFlag(PF_USE_PASS_LIST)
                && !LLViewerParcelMgr::getInstance()->isCollisionBanned())
            {
                // if selling passes, just buy one
                void* deselect_when_done = (void*)true;
                LLPanelLandGeneral::onClickBuyPass(deselect_when_done);
            }
            else
            {
                // not selling passes, get info
                LLFloaterReg::showInstance("about_land");
            }
        }

        gFocusMgr.setKeyboardFocus(NULL);
        return LLTool::handleMouseDown(x, y, mask);
    }

    // didn't click in any UI object, so must have clicked in the world
    LLViewerObject *object = mPick.getObject();
    LLViewerObject *parent = NULL;

    if (mPick.mPickType != LLPickInfo::PICK_LAND)
    {
        LLViewerParcelMgr::getInstance()->deselectLand();
    }

    if (object)
    {
        parent = object->getRootEdit();
    }

    if (handleMediaClick(mPick))
    {
        return true;
    }

    // If it's a left-click, and we have a special action, do it.
    if (useClickAction(mask, object, parent))
    {
// [RLVa:KB] - Checked: RLVa-1.1.0
        // Blanket block all left-click special actions on objects the user can't interact with
        if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canInteract(object, mPick.mObjectOffset)) )
        {
            return true;
        }
// [/RLVa:KB]

// [RLVa:KB] - @buy
        const std::function<bool(const LLUUID&, U8)> fnRlvCheck = [](const LLUUID& idObj, U8 clickAction) {
            switch (clickAction)
            {
                case CLICK_ACTION_BUY:
                    return RlvActions::canBuyObject(idObj);
                case CLICK_ACTION_PAY:
                    return RlvActions::canPayObject(idObj);
                default:
                    return true;
            }
        };
// [/RLVa:KB]
        mClickAction = 0;
        if (object && object->getClickAction())
        {
            mClickAction = object->getClickAction();
// [RLVa:KB] - @buy
            if ( (RlvActions::isRlvEnabled()) && (!fnRlvCheck(object->getID(), mClickAction)) )
            {
                mClickAction = CLICK_ACTION_NONE;
            }
// [/RLVa:KB]
        }
        else if (parent && parent->getClickAction())
        {
            mClickAction = parent->getClickAction();
// [RLVa:KB] - @buy
            if ((RlvActions::isRlvEnabled()) && (!fnRlvCheck(parent->getID(), mClickAction)))
            {
                mClickAction = CLICK_ACTION_NONE;
            }
// [/RLVa:KB]
        }

        switch(mClickAction)
        {
        case CLICK_ACTION_TOUCH:
            // touch behavior down below...
            break;
        case CLICK_ACTION_SIT:
            {
                if (isAgentAvatarValid() && !gAgentAvatarp->isSitting() && !gSavedSettings.getBOOL("FSBlockClickSit")) // agent not already sitting
                {
                    handle_object_sit_or_stand();
                    // put focus in world when sitting on an object
                    gFocusMgr.setKeyboardFocus(NULL);
                    return true;
                } // else nothing (fall through to touch)
            }
        case CLICK_ACTION_PAY:
            if ( mClickActionPayEnabled )
            {
                if ((object && object->flagTakesMoney())
                    || (parent && parent->flagTakesMoney()))
                {
                    // pay event goes to object actually clicked on
                    mClickActionObject = object;
                    mLeftClickSelection = LLToolSelect::handleObjectSelection(mPick, false, true);
                    if (LLSelectMgr::getInstance()->selectGetAllValid())
                    {
                        // call this right away, since we have all the info we need to continue the action
                        selectionPropertiesReceived();
                    }
                    return true;
                }
            }
            break;
        case CLICK_ACTION_BUY:
            if ( mClickActionBuyEnabled )
            {
                mClickActionObject = parent;
                mLeftClickSelection = LLToolSelect::handleObjectSelection(mPick, false, true, true);
                if (LLSelectMgr::getInstance()->selectGetAllValid())
                {
                    // call this right away, since we have all the info we need to continue the action
                    selectionPropertiesReceived();
                }
                return true;
            }
            break;
        case CLICK_ACTION_OPEN:
            if (parent && parent->allowOpen())
            {
                mClickActionObject = parent;
                mLeftClickSelection = LLToolSelect::handleObjectSelection(mPick, false, true, true);
                if (LLSelectMgr::getInstance()->selectGetAllValid())
                {
                    // call this right away, since we have all the info we need to continue the action
                    selectionPropertiesReceived();
                }
            }
            return true;
        case CLICK_ACTION_PLAY:
            handle_click_action_play();
            return true;
        case CLICK_ACTION_OPEN_MEDIA:
            // mClickActionObject = object;
            handle_click_action_open_media(object);
            return true;
        case CLICK_ACTION_ZOOM:
            {
                const F32 PADDING_FACTOR = 2.f;
                LLViewerObject* object = gObjectList.findObject(mPick.mObjectID);

                if (object)
                {
                    gAgentCamera.setFocusOnAvatar(false, ANIMATE);

                    LLBBox bbox = object->getBoundingBoxAgent() ;
                    F32 angle_of_view = llmax(0.1f, LLViewerCamera::getInstance()->getAspect() > 1.f ? LLViewerCamera::getInstance()->getView() * LLViewerCamera::getInstance()->getAspect() : LLViewerCamera::getInstance()->getView());
                    F32 distance = bbox.getExtentLocal().magVec() * PADDING_FACTOR / atan(angle_of_view);

                    LLVector3 obj_to_cam = LLViewerCamera::getInstance()->getOrigin() - bbox.getCenterAgent();
                    obj_to_cam.normVec();

                    LLVector3d object_center_global = gAgent.getPosGlobalFromAgent(bbox.getCenterAgent());
                    gAgentCamera.setCameraPosAndFocusGlobal(object_center_global + LLVector3d(obj_to_cam * distance),
                                                      object_center_global,
                                                      mPick.mObjectID );
                }
            }
            return true;
        case CLICK_ACTION_DISABLED:
            return true;
        default:
            // nothing
            break;
        }
    }

    // put focus back "in world"
    if (gFocusMgr.getKeyboardFocus())
    {
        gFocusMgr.setKeyboardFocus(NULL);
    }

    bool touchable = object
                     && (object->getClickAction() != CLICK_ACTION_DISABLED)
                     && (object->flagHandleTouch() || (parent && parent->flagHandleTouch()));

    // Switch to grab tool if physical or triggerable
    if (object &&
        !object->isAvatar() &&
        ((object->flagUsePhysics() || (parent && !parent->isAvatar() && parent->flagUsePhysics())) || touchable)
        )
    {
// [RLVa:KB] - Checked: RLVa-1.1.0
        // Triggered by left-clicking on a touchable object
        if ( (RlvActions::isRlvEnabled()) && (!RlvActions::canTouch(object, mPick.mObjectOffset)) )
        {
            return LLTool::handleMouseDown(x, y, mask);
        }
// [/RLVa:KB]

        gGrabTransientTool = this;
        mMouseButtonDown = false;
        // <FS:Ansariel> FIRE-15578: After exiting mouselook, left clicking on a touchable object opens build floater; We have it fixed within LLToolGrab
        //LLToolGrab::getInstance()->setClickedInMouselook(gAgentCamera.cameraMouselook());
        // </FS:Ansariel>
        LLToolMgr::getInstance()->getCurrentToolset()->selectTool( LLToolGrab::getInstance() );
        return LLToolGrab::getInstance()->handleObjectHit( mPick );
    }

    if(!object)show_script_error_icon(mPick.mHUDIcon);

    // If left-click never selects or spawns a menu
    // Eat the event.

    // mouse already released
    if (!mMouseButtonDown)
    {
        return true;
    }

    while (object && object->isAttachment() && !object->flagHandleTouch())
    {
        // don't pick avatar through hud attachment
        if (object->isHUDAttachment())
        {
            break;
        }
        object = (LLViewerObject*)object->getParent();
    }
    if (object && object == gAgentAvatarp)
    {
        // we left clicked on avatar, switch to focus mode
        mMouseButtonDown = false;
        LLToolMgr::getInstance()->setTransientTool(LLToolCamera::getInstance());
        gViewerWindow->hideCursor();
        LLToolCamera::getInstance()->setMouseCapture(true);
        LLToolCamera::getInstance()->setClickPickPending();
        LLToolCamera::getInstance()->pickCallback(mPick);
        if(!gSavedSettings.getBOOL("ClickOnAvatarKeepsCamera")) // <FS:Zi> keep camera in place when clicking on ourselves
            gAgentCamera.setFocusOnAvatar(true, true);

        return true;
    }
    //////////
    //  // Could be first left-click on nothing
    //  LLFirstUse::useLeftClickNoHit();
    /////////

    return LLTool::handleMouseDown(x, y, mask);
}