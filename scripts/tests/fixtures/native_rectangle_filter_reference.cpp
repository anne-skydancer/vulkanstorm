void Reference::highlightObjectOnly(LLViewerObject* objectp)
{
    if (!objectp)
    {
        return;
    }

    if (objectp->getPCode() != LL_PCODE_VOLUME)
    {
        return;
    }

    if ((gSavedSettings.getBOOL("SelectOwnedOnly") && !objectp->permYouOwner())
        || (gSavedSettings.getBOOL("SelectMovableOnly") && (!objectp->permMove() || objectp->isPermanentEnforced())))
    {
        // only select my own objects
        return;
    }

    // <FS:Ansariel> FIRE-14593: Option to select only copyable objects
    if (!objectp->permCopy() && gSavedSettings.getBOOL("FSSelectCopyableOnly"))
    {
        return;
    }
    // </FS:Ansariel>

    // <FS:Ansariel> FIRE-304: Option to exclude group owned objects
    if (objectp->permGroupOwner() && !gSavedSettings.getBOOL("FSSelectIncludeGroupOwned"))
    {
        return;
    }
    // </FS:Ansariel>

    mRectSelectedObjects.insert(objectp);
}