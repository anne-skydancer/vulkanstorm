// Frozen actual task controller, LGPL-2.1.
void LLToolDragAndDrop::dropInventory(LLViewerObject* hit_obj,
                                      LLInventoryItem* item,
                                      LLToolDragAndDrop::ESource source,
                                      const LLUUID& src_id)
{
    // *HACK: In order to resolve SL-22177, we need to block drags
    // from notecards and objects onto other objects.
    if ((SOURCE_WORLD == LLToolDragAndDrop::getInstance()->mSource)
       || (SOURCE_NOTECARD == LLToolDragAndDrop::getInstance()->mSource))
    {
        LL_WARNS() << "Call to LLToolDragAndDrop::dropInventory() from world"
            << " or notecard." << LL_ENDL;
        return;
    }

    LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
    time_t creation_date = time_corrected();
    new_item->setCreationDate(creation_date);

    if (!item->getPermissions().allowCopyBy(gAgent.getID()))
    {
        if (SOURCE_AGENT == source)
        {
            // Remove the inventory item from local inventory. The
            // server will actually remove the item from agent
            // inventory.
            gInventory.deleteObject(item->getUUID());
            gInventory.notifyObservers();
        }
        else if (SOURCE_WORLD == source)
        {
            // *FIX: if the objects are in different regions, and the
            // source region has crashed, you can bypass these
            // permissions.
            LLViewerObject* src_obj = gObjectList.findObject(src_id);
            if (src_obj)
            {
                src_obj->removeInventory(item->getUUID());
            }
            else
            {
                LL_WARNS() << "Unable to find source object." << LL_ENDL;
                return;
            }
        }
    }
    hit_obj->updateInventory(new_item, TASK_INVENTORY_ITEM_KEY, true);
    if (LLFloaterReg::instanceVisible("build"))
    {
        // *FIX: only show this if panel not expanded?
        LLFloaterReg::showInstance("build", "Contents");
    }

    // VEFFECT: AddToInventory
    LLHUDEffectSpiral *effectp = (LLHUDEffectSpiral *)LLHUDManager::getInstance()->createViewerEffect(LLHUDObject::LL_HUD_EFFECT_BEAM, true);
    effectp->setSourceObject(gAgentAvatarp);
    effectp->setTargetObject(hit_obj);
    effectp->setDuration(LL_HUD_DUR_SHORT);
    effectp->setColor(LLColor4U(gAgent.getEffectColor()));
    if (gFloaterTools)
    {
        gFloaterTools->dirty();
    }
}