bool LLVOVolume::hasMediaPermission(const LLMediaEntry* media_entry, MediaPermType perm_type)
{
    // NOTE: This logic ALMOST duplicates the logic in the server (in particular, in llmediaservice.cpp).
    if (NULL == media_entry ) return false; // XXX should we assert here?

    // The agent has permissions if:
    // - world permissions are on, or
    // - group permissions are on, and agent_id is in the group, or
    // - agent permissions are on, and agent_id is the owner

    // *NOTE: We *used* to check for modify permissions here (i.e. permissions were
    // granted if permModify() was true).  However, this doesn't make sense in the
    // viewer: we don't want to show controls or allow interaction if the author
    // has deemed it so.  See DEV-42115.

    U8 media_perms = (perm_type == MEDIA_PERM_INTERACT) ? media_entry->getPermsInteract() : media_entry->getPermsControl();

    // World permissions
    if (0 != (media_perms & LLMediaEntry::PERM_ANYONE))
    {
        return true;
    }

    // Group permissions
    else if (0 != (media_perms & LLMediaEntry::PERM_GROUP))
    {
        LLPermissions* obj_perm = LLSelectMgr::getInstance()->findObjectPermissions(this);
        if (obj_perm && gAgent.isInGroup(obj_perm->getGroup()))
        {
            return true;
        }
    }

    // Owner permissions
    else if (0 != (media_perms & LLMediaEntry::PERM_OWNER) && permYouOwner())
    {
        return true;
    }

    return false;

}