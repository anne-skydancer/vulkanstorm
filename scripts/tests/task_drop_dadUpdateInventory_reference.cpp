// Frozen actual task controller, LGPL-2.1.
EAcceptance LLToolDragAndDrop::dad3dUpdateInventory(
    LLViewerObject* obj, S32 face, MASK mask, bool drop)
{
    LL_DEBUGS() << "LLToolDragAndDrop::dadUpdateInventory()" << LL_ENDL;

    // *HACK: In order to resolve SL-22177, we need to block drags
    // from notecards and objects onto other objects.
    if((SOURCE_WORLD == mSource) || (SOURCE_NOTECARD == mSource))
    {
        return ACCEPT_NO;
    }

    LLViewerInventoryItem* item;
    LLViewerInventoryCategory* cat;
    locateInventory(item, cat);
    if (!item || !item->isFinished())
        return ACCEPT_NO;

    LLViewerObject* root_object = obj;
    if (obj && obj->getParent())
    {
        LLViewerObject* parent_obj = (LLViewerObject*)obj->getParent();
        if (!parent_obj->isAvatar())
        {
            root_object = parent_obj;
        }
    }

    EAcceptance rv = willObjectAcceptInventory(root_object, item);
    if (root_object && drop && (ACCEPT_YES_COPY_SINGLE <= rv))
    {
        dropInventory(root_object, item, mSource, mSourceID);
    }

    return rv;
}