bool ReferenceManager::enableLinkObjects()
{
    bool new_value = false;
    // check if there are at least 2 objects selected, and that the
    // user can modify at least one of the selected objects.

    // in component mode, can't link
    if (!gSavedSettings.getBOOL("EditLinkedParts"))
    {
        if(ReferenceManager::getInstance()->selectGetAllRootsValid() && ReferenceManager::getInstance()->getSelection()->getRootObjectCount() >= 2)
        {
            struct f : public LLSelectedObjectFunctor
            {
                virtual bool apply(LLViewerObject* object)
                {
                    LLViewerObject *root_object = (object == NULL) ? NULL : object->getRootEdit();
                    return object->permModify() && !object->isPermanentEnforced() &&
                        ((root_object == NULL) || !root_object->isPermanentEnforced());
                }
            } func;
            const bool firstonly = true;
            new_value = ReferenceManager::getInstance()->getSelection()->applyToRootObjects(&func, firstonly);
        }
    }
    if (!ReferenceManager::getInstance()->getSelection()->checkAnimatedObjectLinkable())
    {
        new_value = false;
    }
// [RLVa:KB] - Checked: 2011-03-19 (RLVa-1.3.0f) | Modified: RLVa-0.2.0g
    if ( (new_value) && ((rlv_handler_t::isEnabled()) && (!RlvActions::canStand())) )
    {
        // Allow only if the avie isn't sitting on any of the selected objects
        LLObjectSelectionHandle hSel = ReferenceManager::getInstance()->getSelection();
        RlvSelectIsSittingOn f(gAgentAvatarp);
        if (hSel->getFirstRootNode(&f, true) != NULL)
            new_value = false;
    }
// [/RLVa:KB]
    return new_value;
}
bool ReferenceManager::enableUnlinkObjects()
{
    LLViewerObject* first_editable_object = ReferenceManager::getInstance()->getSelection()->getFirstEditableObject();
    LLViewerObject *root_object = (first_editable_object == NULL) ? NULL : first_editable_object->getRootEdit();

    bool new_value = ReferenceManager::getInstance()->selectGetAllRootsValid() &&
        first_editable_object &&
        !first_editable_object->isAttachment() && !first_editable_object->isPermanentEnforced() &&
        ((root_object == NULL) || !root_object->isPermanentEnforced());
// [RLVa:KB] - Checked: 2011-03-19 (RLVa-1.3.0f) | Modified: RLVa-0.2.0g
    if ( (new_value) && ((rlv_handler_t::isEnabled()) && (!RlvActions::canStand())) )
    {
        // Allow only if the avie isn't sitting on any of the selected objects
        LLObjectSelectionHandle hSel = ReferenceManager::getInstance()->getSelection();
        RlvSelectIsSittingOn f(gAgentAvatarp);
        if (hSel->getFirstRootNode(&f, true) != NULL)
            new_value = false;
    }
// [/RLVa:KB]
    return new_value;
}