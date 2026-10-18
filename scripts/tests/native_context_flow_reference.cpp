bool LLToolPie::handleRightClickPick()
{
    S32 x = mPick.mMousePt.mX;
    S32 y = mPick.mMousePt.mY;
    MASK mask = mPick.mKeyMask;

    if (mPick.mPickType != LLPickInfo::PICK_LAND)
    {
        LLViewerParcelMgr::getInstance()->deselectLand();
    }

    // didn't click in any UI object, so must have clicked in the world
    LLViewerObject *object = mPick.getObject();

    // Can't ignore children here.
    LLToolSelect::handleObjectSelection(mPick, false, true);

    // Spawn pie menu
    if (mPick.mPickType == LLPickInfo::PICK_LAND)
    {
        LLParcelSelectionHandle selection = LLViewerParcelMgr::getInstance()->selectParcelAt( mPick.mPosGlobal );
        gMenuHolder->setParcelSelection(selection);
        // <FS:Zi> Pie menu
        if (gSavedSettings.getBOOL("UsePieMenu"))
        {
            gPieMenuLand->show(x, y);
        }
        // </FS:Zi> Pie menu
        else
        gMenuLand->show(x, y);

        showVisualContextMenuEffect();

    }
    else if (mPick.mObjectID == gAgent.getID() )
    {
        // <FS:Zi> Pie menu
        if (gSavedSettings.getBOOL("UsePieMenu"))
        {
            if (!gPieMenuAvatarSelf)
            {
                //either at very early startup stage or at late quitting stage,
                //this event is ignored.
                return true;
            }

            gPieMenuAvatarSelf->show(x, y);
        }
        // </FS:Zi> Pie menu
        else
        {
            if(!gMenuAvatarSelf)
            {
                //either at very early startup stage or at late quitting stage,
                //this event is ignored.
                return true ;
            }

            gMenuAvatarSelf->show(x, y);
        }
    }
    else if (object)
    {
        gMenuHolder->setObjectSelection(LLSelectMgr::getInstance()->getSelection());

        bool is_other_attachment = (object->isAttachment() && !object->isHUDAttachment() && !object->permYouOwner());
        if (object->isAvatar() || is_other_attachment)
        {
            // Find the attachment's avatar
            while( object && object->isAttachment())
            {
                object = (LLViewerObject*)object->getParent();
                llassert(object);
            }

            if (!object)
            {
                return true; // unexpected, but escape
            }

            // Object is an avatar, so check for mute by id.
            LLVOAvatar* avatar = (LLVOAvatar*)object;
            std::string name = avatar->getFullname();
            std::string mute_msg;
            if (LLMuteList::getInstance()->isMuted(avatar->getID(), avatar->getFullname()))
            {
                mute_msg = LLTrans::getString("UnmuteAvatar");
            }
            else
            {
                mute_msg = LLTrans::getString("MuteAvatar");
            }

// [RLVa:KB] - Checked: 2010-04-11 (RLVa-1.2.0e) | Modified: RLVa-1.1.0l
            // Don't show the context menu on empty selection when fartouch restricted [see LLToolSelect::handleObjectSelection()]
            if ( (!rlv_handler_t::isEnabled()) || (!LLSelectMgr::getInstance()->getSelection()->isEmpty()) ||
                 (!gRlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH)) )
            {
// [/RLVa:KB]
                if (is_other_attachment)
                {
                    // <FS:Zi> Pie menu
                    // gMenuAttachmentOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);
                    // gMenuAttachmentOther->show(x, y);
                    if(gSavedSettings.getBOOL("UsePieMenu"))
                    {
                        gPieMenuAttachmentOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);

                        static std::string checkmark("\xE2\x9C\x94");
                        gPieMenuAttachmentOther->getChild<LLUICtrl>("RenderNormally")->setValue((check_avatar_render_mode(0) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_normally"));
                        gPieMenuAttachmentOther->getChild<LLUICtrl>("DoNotRender")->setValue((check_avatar_render_mode(1) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_never"));
                        gPieMenuAttachmentOther->getChild<LLUICtrl>("AlwaysRenderFully")->setValue((check_avatar_render_mode(2) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_fully"));

                        gPieMenuAttachmentOther->show(x, y);
                    }
                    else
                    {
                        // getChild() seems to fail for LLMenuItemCallGL items, so we changed the XML instead
                        // gMenuAttachmentOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);
                        gMenuAttachmentOther->show(x, y);
                    }
                    // </FS:Zi>
                }
                else
                {
                    // <FS:Zi> Pie menu
                    // gMenuAvatarOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);
                    // gMenuAvatarOther->show(x, y);
                    if(gSavedSettings.getBOOL("UsePieMenu"))
                    {
                        gPieMenuAvatarOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);

                        static std::string checkmark("\xE2\x9C\x94");
                        gPieMenuAvatarOther->getChild<LLUICtrl>("RenderNormally")->setValue((check_avatar_render_mode(0) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_normally"));
                        gPieMenuAvatarOther->getChild<LLUICtrl>("DoNotRender")->setValue((check_avatar_render_mode(1) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_never"));
                        gPieMenuAvatarOther->getChild<LLUICtrl>("AlwaysRenderFully")->setValue((check_avatar_render_mode(2) ? (checkmark + " ") : "") + LLTrans::getString("Pie_av_render_fully"));

                        gPieMenuAvatarOther->show(x, y);
                    }
                    else
                    {
                        // getChild() seems to fail for LLMenuItemCallGL items, so we changed the XML instead
                        // gMenuAvatarOther->getChild<LLUICtrl>("Avatar Mute")->setValue(mute_msg);
                        gMenuAvatarOther->show(x, y);
                    }
                    // </FS:Zi>
                }
// [RLVa:KB] - Checked: 2010-04-11 (RLVa-1.2.0e) | Modified: RLVa-1.1.0l
            }
            else
            {
                make_ui_sound("UISndInvalidOp");
            }
// [/RLVa:KB]
        }
        else if (object->isAttachment())
        {
            // <FS:Zi> Pie menu
            if (gSavedSettings.getBOOL("UsePieMenu"))
            {
                gPieMenuAttachmentSelf->show(x, y);
            }
            // </FS:Zi> Pie menu
            else
            gMenuAttachmentSelf->show(x, y);
        }
        else
        {
            // BUG: What about chatting child objects?
            std::string name;
            LLSelectNode* node = LLSelectMgr::getInstance()->getSelection()->getFirstRootNode();
            if (node)
            {
                name = node->mName;
            }

// [RLVa:KB] - Checked: 2010-04-11 (RLVa-1.2.el) | Modified: RLVa-1.1.0l
            // Don't show the pie menu on empty selection when fartouch/interaction restricted
            // (not entirely accurate in case of Tools / Select Only XXX [see LLToolSelect::handleObjectSelection()]
            if ( (!rlv_handler_t::isEnabled()) || (!LLSelectMgr::getInstance()->getSelection()->isEmpty()) ||
                 (!gRlvHandler.hasBehaviour(RLV_BHVR_FARTOUCH)) )
            {
// [/RLVa:KB]
                // <FS:Zi> Pie menu
                // gMenuHolder->getChild<LLUICtrl>("Object Mute")->setValue(mute_msg);
                // gMenuObject->show(x, y);
                if (gSavedSettings.getBOOL("UsePieMenu"))
                {
                    std::string mute_msg;
                    if (LLMuteList::getInstance()->isMuted(object->getID(), name))
                    {
                        mute_msg = LLTrans::getString("UnmuteObject");
                    }
                    else
                    {
                        mute_msg = LLTrans::getString("MuteObject2");
                    }
                    gPieMenuObject->getChild<LLUICtrl>("Object Mute")->setValue(mute_msg);
                    gPieMenuObject->show(x, y);
                }
                else
                {
                    gMenuObject->setItemVisible("Take Multiple", (LLSelectMgr::getInstance()->getSelection()->getRootObjectCount() > 1));
                    gMenuObject->show(x, y);
                }
                // </FS:Zi>

                showVisualContextMenuEffect();
// [RLVa:KB] - Checked: 2010-04-11 (RLVa-1.2.el) | Modified: RLVa-1.1.0l
            }
            else
            {
                make_ui_sound("UISndInvalidOp");
            }
// [/RLVa:KB]
        }
    }
    else if (mPick.mParticleOwnerID.notNull())
    {
        // <FS:Ansariel> FIRE-12355: Pie menu for mute particle menu
        if (gSavedSettings.getBOOL("UsePieMenu"))
        {
            if (gPieMenuMuteParticle && mPick.mParticleOwnerID != gAgent.getID())
            {
                gPieMenuMuteParticle->show(x, y);
            }
        }
        else
        // </FS:Ansariel>
        if (gMenuMuteParticle && mPick.mParticleOwnerID != gAgent.getID())
        {
            gMenuMuteParticle->show(x,y);
        }
    }

    // non UI object - put focus back "in world"
    if (gFocusMgr.getKeyboardFocus())
    {
        gFocusMgr.setKeyboardFocus(NULL);
    }

    LLTool::handleRightMouseDown(x, y, mask);
    // We handled the event.
    return true;
}