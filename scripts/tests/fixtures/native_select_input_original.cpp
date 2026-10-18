void LLToolCompInspect::referenceCallback(const LLPickInfo& pick_info)
{
    LLViewerObject* hit_obj = pick_info.getObject();
    LLToolCompInspect * tool_inspectp = LLToolCompInspect::getInstance();

    if (!tool_inspectp->mMouseDown)
    {
        // fast click on object, but mouse is already up...just do select
        tool_inspectp->mSelectRect->handleObjectSelection(pick_info, gSavedSettings.getBOOL("EditLinkedParts"), false);
        return;
    }

    LLSelectMgr * mgr_selectp = LLSelectMgr::getInstance();
    if( hit_obj && mgr_selectp->getSelection()->getObjectCount()) {
        LLEditMenuHandler::gEditMenuHandler = mgr_selectp;
    }

    tool_inspectp->setCurrentTool( tool_inspectp->mSelectRect );
    tool_inspectp->mIsToolCameraActive = false;
    tool_inspectp->mSelectRect->handlePick( pick_info );
}