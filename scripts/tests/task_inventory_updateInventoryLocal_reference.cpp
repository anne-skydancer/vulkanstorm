// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::updateInventoryLocal(LLInventoryItem* item, U8 key)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item->getUUID(), mID, item->getPermissions(),
                                  item->getAssetUUID(), item->getType(),
                                  item->getInventoryType(),
                                  item->getName(), item->getDescription(),
                                  item->getSaleInfo(), item->getFlags(),
                                  item->getCreationDate());

    // do the internal logic
    const bool is_new = false;
    doUpdateInventory(task_item, key, is_new);
}