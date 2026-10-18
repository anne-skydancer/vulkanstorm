// Frozen actual shared object-inventory controller. LGPL-2.1.
void LLViewerObject::updateViewerInventoryAsset(
                    const LLViewerInventoryItem* item,
                    const LLUUID& new_asset)
{
    LLPointer<LLViewerInventoryItem> task_item =
        new LLViewerInventoryItem(item);
    task_item->setAssetUUID(new_asset);

    // do the internal logic
    doUpdateInventory(task_item, TASK_INVENTORY_ITEM_KEY, false);
}