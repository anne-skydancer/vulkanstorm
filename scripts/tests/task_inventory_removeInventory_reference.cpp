// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::removeInventory(const LLUUID& item_id)
{
    // close associated floater properties
    LLSD params;
    params["id"] = item_id;
    params["object"] = mID;
    LLFloaterReg::hideInstance("item_properties", params);

    // <FS:Ansariel> Keep legacy properties floater
    // close any associated floater properties
    LLFloaterReg::hideInstance("properties", item_id);
    // </FS:Ansariel>

    LLMessageSystem* msg = gMessageSystem;
    msg->newMessageFast(_PREHASH_RemoveTaskInventory);
    msg->nextBlockFast(_PREHASH_AgentData);
    msg->addUUIDFast(_PREHASH_AgentID, gAgent.getID());
    msg->addUUIDFast(_PREHASH_SessionID, gAgent.getSessionID());
    msg->nextBlockFast(_PREHASH_InventoryData);
    msg->addU32Fast(_PREHASH_LocalID, mLocalID);
    msg->addUUIDFast(_PREHASH_ItemID, item_id);
    msg->sendReliable(mRegionp->getHost());
    deleteInventoryItem(item_id);
    ++mExpectedInventorySerialNum;
}