bool LLToolPie::useClickAction(MASK mask,
                               LLViewerObject* object,
                               LLViewerObject* parent)
{
    return  mask == MASK_NONE
            && object
            && !object->isAttachment()
            && LLPrimitive::isPrimitive(object->getPCode())
                // useClickAction does not handle Touch (0) or Disabled action
            && ((object->getClickAction() && object->getClickAction() != CLICK_ACTION_DISABLED)
                || (parent && parent->getClickAction() && parent->getClickAction() != CLICK_ACTION_DISABLED));

}