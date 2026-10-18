bool ReferenceInput::handleMouse(LLWindow *window_impl, LLCoordGL pos, MASK mask, EMouseClickType clicktype, bool down)
{
    bool is_toolmgr_action = false;
    bool handled = gViewerWindow->handleAnyMouseClick(window_impl, pos, mask, clicktype, down, is_toolmgr_action);
    // Native input is owned directly by the shared UI and CPU camera controller.
    // Do not enqueue legacy mouse binding callbacks against scene/tool owners.
    if (gViewerWindow->isNativeVulkan()) return handled;

    if (clicktype != CLICK_NONE)
    {
        // Special case
        // If UI doesn't handle double click, LMB click is issued, so supres LMB 'down' when doubleclick is set
        // handle !down as if we are handling doubleclick

        bool double_click_sp = (clicktype == CLICK_LEFT
            && (mMouseLevel[CLICK_DOUBLELEFT] != MOUSE_STATE_SILENT)
            && mMouseLevel[CLICK_LEFT] == MOUSE_STATE_SILENT);
        if (double_click_sp && !down)
        {
            // Process doubleclick instead
            clicktype = CLICK_DOUBLELEFT;
        }

        // If the first LMB click is handled by the menu, skip the following double click
        static bool skip_double_click = false;
        if (clicktype == CLICK_LEFT && down)
        {
            skip_double_click = is_toolmgr_action ? false : handled;
        }

        if (double_click_sp && down)
        {
            // Consume click.
            // Due to handling, double click that is not handled will be immediately followed by LMB click
        }
        else if (clicktype == CLICK_DOUBLELEFT && skip_double_click)
        {
            handled = true;
        }
        // If UI handled 'down', it should handle 'up' as well
        // If we handle 'down' not by UI, then we should handle 'up'/'level' regardless of UI
        else if (handled)
        {
            // UI handled new 'down' so iterupt whatever state we were in.
            if (mMouseLevel[clicktype] != MOUSE_STATE_SILENT)
            {
                if (mMouseLevel[clicktype] == MOUSE_STATE_DOWN)
                {
                    mMouseLevel[clicktype] = MOUSE_STATE_CLICK;
                }
                else
                {
                    mMouseLevel[clicktype] = MOUSE_STATE_UP;
                }
            }
        }
        else if (down)
        {
            if (mMouseLevel[clicktype] == MOUSE_STATE_DOWN)
            {
                // this is repeated hit (mouse does not repeat event until release)
                // for now treat rapid clicking like mouse being held
                mMouseLevel[clicktype] = MOUSE_STATE_LEVEL;
            }
            else
            {
                mMouseLevel[clicktype] = MOUSE_STATE_DOWN;
            }
        }
        else if (mMouseLevel[clicktype] != MOUSE_STATE_SILENT)
        {
            // Released mouse key
            if (mMouseLevel[clicktype] == MOUSE_STATE_DOWN)
            {
                mMouseLevel[clicktype] = MOUSE_STATE_CLICK;
            }
            else
            {
                mMouseLevel[clicktype] = MOUSE_STATE_UP;
            }
        }
    }

    return handled;
}