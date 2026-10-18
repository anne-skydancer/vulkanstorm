bool LLToolPie::handleMouseDown(S32 x, S32 y, MASK mask)
{
    if (mDoubleClickTimer.getStarted())
    {
        mDoubleClickTimer.stop();
    }

    mMouseOutsideSlop = false;
    mMouseDownX = x;
    mMouseDownY = y;
    LLTimer pick_timer;
    bool pick_rigged = false; //gSavedSettings.getBOOL("AnimatedObjectsAllowLeftClick");
    LLPickInfo transparent_pick = gViewerWindow->pickImmediate(x, y, true /*includes transparent*/, pick_rigged, false, true, false);
    LLPickInfo visible_pick = gViewerWindow->pickImmediate(x, y, false, pick_rigged);
    LLViewerObject *transp_object = transparent_pick.getObject();
    LLViewerObject *visible_object = visible_pick.getObject();

    // Current set of priorities
    // 1. Transparent attachment pick
    // 2. Transparent actionable pick
    // 3. Visible attachment pick (e.x we click on attachment under invisible floor)
    // 4. Visible actionable pick
    // 5. Transparent pick (e.x. movement on transparent object/floor, our default pick)
    // left mouse down always picks transparent (but see handleMouseUp).
    // Also see LLToolPie::handleHover() - priorities are a bit different there.
    // Todo: we need a more consistent set of rules to work with
    if (transp_object == visible_object || !visible_object ||
        !transp_object) // avoid potential for null dereference below, don't make assumptions about behavior of pickImmediate
    {
        mPick = transparent_pick;
    }
    // <FS:ND> FIRE-29031; Handle transparent being nullptr (due to RLVa?). visible_object cannot be invalid here or the if above had been entered
    else if( !transp_object)
    {
        mPick = visible_pick;
    }
    // </FS:ND>
    else
    {
        // Select between two non-null picks
        LLViewerObject *transp_parent = transp_object->getRootEdit();
        LLViewerObject *visible_parent = visible_object->getRootEdit();
        if (transp_object->isAttachment())
        {
            // 1. Transparent attachment
            mPick = transparent_pick;
        }
        else if (transp_object->getClickAction() != CLICK_ACTION_DISABLED
                 && (useClickAction(mask, transp_object, transp_parent) || transp_object->flagHandleTouch() || (transp_parent && transp_parent->flagHandleTouch())))
        {
            // 2. Transparent actionable pick
            mPick = transparent_pick;
        }
        else if (visible_object->isAttachment())
        {
            // 3. Visible attachment pick
            mPick = visible_pick;
        }
        else if (visible_object->getClickAction() != CLICK_ACTION_DISABLED
                 && (useClickAction(mask, visible_object, visible_parent) || visible_object->flagHandleTouch() || (visible_parent && visible_parent->flagHandleTouch())))
        {
            // 4. Visible actionable pick
            mPick = visible_pick;
        }
        else
        {
            // 5. Default: transparent
            mPick = transparent_pick;
        }
    }
    LL_INFOS() << "pick_rigged is " << (S32) pick_rigged << " pick time elapsed " << pick_timer.getElapsedTimeF32() << LL_ENDL;

    mPick.mKeyMask = mask;

    mMouseButtonDown = true;

    // If nothing clickable is picked, needs to return
    // false for click-to-walk or click-to-teleport to work.
    return handleLeftClickPick();
}