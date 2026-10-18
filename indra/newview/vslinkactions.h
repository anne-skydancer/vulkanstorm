// Original link/unlink controllers, shared by real selection peers. LGPL-2.1.
#pragma once
#include "llnotificationsutil.h"
#include "llviewercontrol.h"
#include "vsregionlimits.h"
template<class Manager> bool vs_link_objects(Manager& manager)
{
    if (!manager.selectGetAllRootsValid())
    {
        LLNotificationsUtil::add("UnableToLinkWhileDownloading");
        return true;
    }

    S32 object_count = manager.getSelection()->getObjectCount();
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

    if (manager.getSelection()->getRootObjectCount() < 2)
    {
        LLNotificationsUtil::add("CannotLinkIncompleteSet");
        return true;
    }

    if (!manager.selectGetRootsModify())
    {
        LLNotificationsUtil::add("CannotLinkModify");
        return true;
    }

    if (!manager.selectGetRootsNonPermanentEnforced())
    {
        LLNotificationsUtil::add("CannotLinkPermanent");
        return true;
    }

    LLUUID owner_id;
    std::string owner_name;
    if (!manager.selectGetOwner(owner_id, owner_name))
    {
        // we don't actually care if you're the owner, but novices are
        // the most likely to be stumped by this one, so offer the
        // easiest and most likely solution.
        LLNotificationsUtil::add("CannotLinkDifferentOwners");
        return true;
    }

    if (!manager.selectGetSameRegion())
    {
        LLNotificationsUtil::add("CannotLinkAcrossRegions");
        return true;
    }

    manager.sendLink();

    return true;
}
template<class Manager,class Confirm> bool vs_unlink_objects(Manager& manager,Confirm confirm)
{

    // <FS:PP>
    if (gSavedSettings.getBOOL("FSUnlinkConfirmEnabled"))
    {
    // </FS:PP>

        S32 min_objects_for_confirm = gSavedSettings.getS32("MinObjectsForUnlinkConfirm");
        S32 unlink_object_count = manager.getSelection()->getObjectCount(); // clears out nodes with NULL objects
        if (unlink_object_count >= min_objects_for_confirm
            && unlink_object_count > manager.getSelection()->getRootObjectCount())
        {
            // total count > root count means that there are childer inside and that there are linksets that will be unlinked
            confirm();
            return true;
        }

    // <FS:PP>
    }
    // </FS:PP>

    manager.sendDelink();
    return true;
}
