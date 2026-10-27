// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::moveInventory(const LLUUID& folder_id,
                                   const LLUUID& item_id)
{
    LL_DEBUGS() << "LLViewerObject::moveInventory " << item_id << LL_ENDL;
    if (!mRegionp)
    {
        LL_WARNS() << "Object " << mID << " has no region, item " << item_id << " not moved" << LL_ENDL;
        return;
    }
    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_MoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->addUUIDFast(_PREHASH_FolderID, folder_id);
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());

    LLInventoryObject* inv_obj = getInventoryObject(item_id);
    if(inv_obj)
    {
        LLViewerInventoryItem* item = (LLViewerInventoryItem*)inv_obj;
        if(!item->getPermissions().allowCopyBy(gAgent.getID()))
        {
            deleteInventoryItem(item_id);
            ++mExpectedInventorySerialNum;
        }
    }
}