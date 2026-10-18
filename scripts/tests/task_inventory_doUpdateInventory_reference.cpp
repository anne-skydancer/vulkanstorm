// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::doUpdateInventory(
    LLPointer<LLViewerInventoryItem>& item,
    U8 key,
    bool is_new)
{
    LLViewerInventoryItem* old_item = NULL;
    if(TASK_INVENTORY_ITEM_KEY == key)
    {
        // <FS:ND> Do not use C-Style cast for polymorphic upcasting
//      old_item = (LLViewerInventoryItem*)getInventoryObject(item->getUUID());
        old_item = dynamic_cast<LLViewerInventoryItem*>(getInventoryObject(item->getUUID()));
        // </FS:ND>
    }
    else if(TASK_INVENTORY_ASSET_KEY == key)
    {
        old_item = getInventoryItemByAsset(item->getAssetUUID());
    }
    LLUUID item_id;
    LLUUID new_owner;
    LLUUID new_group;
    bool group_owned = false;
    if(old_item)
    {
        item_id = old_item->getUUID();
        new_owner = old_item->getPermissions().getOwner();
        new_group = old_item->getPermissions().getGroup();
        group_owned = old_item->getPermissions().isGroupOwned();
        old_item = NULL;
    }
    else
    {
        item_id = item->getUUID();
    }
    if(!is_new && mInventory)
    {
        // Attempt to update the local inventory. If we can get the
        // object perm, we have perfect visibility, so we want the
        // serial number to match. Otherwise, take our best guess and
        // make sure that the serial number does not match.
        deleteInventoryItem(item_id);
        LLPermissions perm(item->getPermissions());
        LLPermissions* obj_perm = LLSelectMgr::getInstance()->findObjectPermissions(this);
        bool is_atomic = (S32)LLAssetType::AT_OBJECT != item->getType();
        if(obj_perm)
        {
            perm.setOwnerAndGroup(LLUUID::null, obj_perm->getOwner(), obj_perm->getGroup(), is_atomic);
        }
        else
        {
            if(group_owned)
            {
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            else if(!new_owner.isNull())
            {
                // The object used to be in inventory, so we can
                // assume the owner and group will match what they are
                // there.
                perm.setOwnerAndGroup(LLUUID::null, new_owner, new_group, is_atomic);
            }
            // *FIX: can make an even better guess by using the mPermGroup flags
            else if(permYouOwner())
            {
                // best guess.
                perm.setOwnerAndGroup(LLUUID::null, gAgent.getID(), item->getPermissions().getGroup(), is_atomic);
                --mExpectedInventorySerialNum;
            }
            else
            {
                // dummy it up.
                perm.setOwnerAndGroup(LLUUID::null, LLUUID::null, LLUUID::null, is_atomic);
                --mExpectedInventorySerialNum;
            }
        }
        LLViewerInventoryItem* oldItem = item;
        LLViewerInventoryItem* new_item = new LLViewerInventoryItem(oldItem);
        new_item->setPermissions(perm);
        mInventory->push_front(new_item);
        doInventoryCallback();
        ++mExpectedInventorySerialNum;
    }
    else if (is_new)
    {
        ++mExpectedInventorySerialNum;
    }
}