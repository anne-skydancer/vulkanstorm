// Frozen actual task controller, LGPL-2.1.
EAcceptance LLToolDragAndDrop::willObjectAcceptInventory(LLViewerObject* obj, LLInventoryItem* item, EDragAndDropType type)
{
    // check the basics
    if (!item || !obj)
        return ACCEPT_NO;

    // HACK: downcast
    LLViewerInventoryItem* vitem = (LLViewerInventoryItem*)item;
    if (!vitem->isFinished() && (type != DAD_CATEGORY))
    {
        // Note: for DAD_CATEGORY we assume that folder version check passed and folder
        // is complete, meaning that items inside are up to date.
        // (isFinished() == false) at the moment shows that item was loaded from cache.
        // Library or agent inventory only.
        return ACCEPT_NO;
    }
    if (vitem->getIsLinkType())
        return ACCEPT_NO; // No giving away links

    // deny attempts to drop from an object onto itself. This is to
    // help make sure that drops that are from an object to an object
    // don't have to worry about order of evaluation. Think of this
    // like check for self in assignment.
    if (obj->getID() == item->getParentUUID())
    {
        return ACCEPT_NO;
    }

    //bool copy = (perm.allowCopyBy(gAgent.getID(),
    //                            gAgent.getGroupID())
    //           && (obj->mPermModify || obj->mFlagAllowInventoryAdd));
    bool worn = false;
    LLVOAvatarSelf* my_avatar = NULL;
    switch (item->getType())
    {
    case LLAssetType::AT_OBJECT:
        my_avatar = gAgentAvatarp;
        if(my_avatar && my_avatar->isWearingAttachment(item->getUUID()))
        {
                worn = true;
        }
        break;
    case LLAssetType::AT_BODYPART:
    case LLAssetType::AT_CLOTHING:
        if (gAgentWearables.isWearingItem(item->getUUID()))
        {
            worn = true;
        }
        break;
    case LLAssetType::AT_CALLINGCARD:
        // Calling Cards in object are disabled for now
        // because of incomplete LSL support. See STORM-1117.
        return ACCEPT_NO;
    default:
        break;
    }
    const LLPermissions& perm = item->getPermissions();
    bool modify = (obj->permModify() || obj->flagAllowInventoryAdd());
    bool transfer = false;
    if ((obj->permYouOwner() && (perm.getOwner() == gAgent.getID()))
       || perm.allowOperationBy(PERM_TRANSFER, gAgent.getID()))
    {
        transfer = true;
    }
    bool volume = (LL_PCODE_VOLUME == obj->getPCode());
    bool attached = obj->isAttachment();
    bool unrestricted = (perm.getMaskBase() & PERM_ITEM_UNRESTRICTED) == PERM_ITEM_UNRESTRICTED;

// [RLVa:KB] - Checked: 2010-03-31 (RLVa-1.2.0c) | Modified: RLVa-1.0.0c
    if (rlv_handler_t::isEnabled())
    {
        const LLViewerObject* pObjRoot = obj->getRootEdit();
        if (gRlvAttachmentLocks.isLockedAttachment(pObjRoot))
        {
            return ACCEPT_NO_LOCKED;        // Disallow inventory drops on a locked attachment
        }
        else if ( (gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) || (gRlvHandler.hasBehaviour(RLV_BHVR_SITTP)) )
        {
            if ( (isAgentAvatarValid()) && (gAgentAvatarp->isSitting()) && (gAgentAvatarp->getRoot() == pObjRoot) )
                return ACCEPT_NO_LOCKED;    // ... or on a linkset the avie is sitting on under @unsit=n/@sittp=n
        }
    }
// [/RLVa:KB]

    if(attached && !unrestricted)
    {
        // Attachments are in world and in inventory simultaneously,
        // at the moment server doesn't support such a situation.
        return ACCEPT_NO_LOCKED;
    }
    else if(modify && transfer && volume && !worn)
    {
        return ACCEPT_YES_MULTI;
    }
    else if(!modify)
    {
        return ACCEPT_NO_LOCKED;
    }
    return ACCEPT_NO;
}