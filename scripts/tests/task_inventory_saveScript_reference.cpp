// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::saveScript(
    const LLViewerInventoryItem* item,
    bool active,
    bool is_new)
{
    /*
     * XXXPAM Investigate not making this copy.  Seems unecessary, but I'm unsure about the
     * interaction with doUpdateInventory() called below.
     */
    LL_DEBUGS() << "LLViewerObject::saveScript() " << item->getUUID() << " " << item->getAssetUUID() << LL_ENDL;

    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());

    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, script " << item->getUUID() << " not saved" << LL_ENDL;
        return;
    }

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RezScript);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_GroupID, gAgent.getGroupID());
    msg->nextBlockFast(_PREHASH_UpdateBlock);
    msg->addU32Fast(_PREHASH_ObjectLocalID, (mLocalID));
    U8 enabled = active;
    msg->addBOOLFast(_PREHASH_Enabled, enabled);
    msg->nextBlockFast(_PREHASH_InventoryBlock);
    task_item->packMessage(msg);
    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, is_new);
}