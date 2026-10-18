// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::updateInventory(
    LLViewerInventoryItem* item,
    U8 key,
    bool is_new)
{
    // This slices the object into what we're concerned about on the
    // viewer. The simulator will take the permissions and transfer
    // ownership.
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(),
                                  item->getFlags(),
                                  item->getCreationDate());
    task_item->setTransactionID(item->getTransactionID());
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_UpdateTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_UpdateData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addU8Fast(_PREHASH_Key, key);
    msg->nextBlockFast(_PREHASH_InventoryData);
    task_item->packMessage(msg);
    msg->sendReliable(mRegionp->getHost());

    // do the internal logic
    doUpdateInventory(task_item, key, is_new);
}