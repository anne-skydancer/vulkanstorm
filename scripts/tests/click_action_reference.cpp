// Reference LLToolPie inheritance policy at 6635dd3c63, before extraction. LGPL-2.1.
U8 reference_click_action(LLViewerObject* obj)
{
    if (!obj) return CLICK_ACTION_NONE;
    if (obj->isAttachment()) return CLICK_ACTION_NONE;

    U8 click_action = CLICK_ACTION_TOUCH;
    LLViewerObject* parent = obj->getRootEdit();
    U8 object_action = obj->getClickAction();
    U8 parent_action = parent ? parent->getClickAction() : CLICK_ACTION_TOUCH;
    if (parent_action == CLICK_ACTION_DISABLED || object_action)
    {
        // CLICK_ACTION_DISABLED ("None" in UI) is intended for child action to
        // override parent's action when assigned to parent or to child
        click_action = object_action;
    }
    else if (parent_action)
    {
        click_action = parent_action;
    }
    return click_action;
}
