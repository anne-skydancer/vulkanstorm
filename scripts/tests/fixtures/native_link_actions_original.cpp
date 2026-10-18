bool ReferenceManager::linkObjects()
{
    if (!ReferenceManager::getInstance()->selectGetAllRootsValid())
    {
        LLNotificationsUtil::add("UnableToLinkWhileDownloading");
        return true;
    }

    S32 object_count = ReferenceManager::getInstance()->getSelection()->getObjectCount();
// <FS:CR> Aurora Sim
    //if (object_count > MAX_CHILDREN_PER_TASK + 1)
    S32 object_max = vs_region_limits().getMaxLinkedPrims();

    if (object_count > object_max + 1)
// </FS:CR> Aurora Sim
    {
        LLSD args;
        args["COUNT"] = llformat("%d", object_count);
// <FS:CR> Aurora Sim
        //int max = MAX_CHILDREN_PER_TASK+1;
        int max = object_max+1;
// </FS:CR> Aurora Sim
        args["MAX"] = llformat("%d", max);
        LLNotificationsUtil::add("UnableToLinkObjects", args);
        return true;
    }

    if (ReferenceManager::getInstance()->getSelection()->getRootObjectCount() < 2)
    {
        LLNotificationsUtil::add("CannotLinkIncompleteSet");
        return true;
    }

    if (!ReferenceManager::getInstance()->selectGetRootsModify())
    {
        LLNotificationsUtil::add("CannotLinkModify");
        return true;
    }

    if (!ReferenceManager::getInstance()->selectGetRootsNonPermanentEnforced())
    {
        LLNotificationsUtil::add("CannotLinkPermanent");
        return true;
    }

    LLUUID owner_id;
    std::string owner_name;
    if (!ReferenceManager::getInstance()->selectGetOwner(owner_id, owner_name))
    {
        // we don't actually care if you're the owner, but novices are
        // the most likely to be stumped by this one, so offer the
        // easiest and most likely solution.
        LLNotificationsUtil::add("CannotLinkDifferentOwners");
        return true;
    }

    if (!ReferenceManager::getInstance()->selectGetSameRegion())
    {
        LLNotificationsUtil::add("CannotLinkAcrossRegions");
        return true;
    }

    ReferenceManager::getInstance()->sendLink();

    return true;
}
bool ReferenceManager::unlinkObjects()
{

    // <FS:PP>
    if (gSavedSettings.getBOOL("FSUnlinkConfirmEnabled"))
    {
    // </FS:PP>

        S32 min_objects_for_confirm = gSavedSettings.getS32("MinObjectsForUnlinkConfirm");
        S32 unlink_object_count = mSelectedObjects->getObjectCount(); // clears out nodes with NULL objects
        if (unlink_object_count >= min_objects_for_confirm
            && unlink_object_count > mSelectedObjects->getRootObjectCount())
        {
            // total count > root count means that there are childer inside and that there are linksets that will be unlinked
            LLNotificationsUtil::add("ConfirmUnlink", LLSD(), LLSD(), []{});
            return true;
        }

    // <FS:PP>
    }
    // </FS:PP>

    ReferenceManager::getInstance()->sendDelink();
    return true;
}