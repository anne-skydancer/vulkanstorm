// Frozen actual task controller, LGPL-2.1.
EAcceptance LLToolDragAndDrop::dad3dUpdateInventoryCategory(
    LLViewerObject* obj, S32 face, MASK mask, bool drop)
{
    LL_DEBUGS() << "LLToolDragAndDrop::dad3dUpdateInventoryCategory()" << LL_ENDL;
    if (obj == NULL)
    {
        LL_WARNS() << "obj is NULL; aborting func with ACCEPT_NO" << LL_ENDL;
        return ACCEPT_NO;
    }

    if ((mSource != SOURCE_AGENT) && (mSource != SOURCE_LIBRARY))
    {
        return ACCEPT_NO;
    }
    if (obj->isAttachment())
    {
        return ACCEPT_NO_LOCKED;
    }

    LLViewerInventoryItem* item = NULL;
    LLViewerInventoryCategory* cat = NULL;
    locateInventory(item, cat);
    if (!cat)
    {
        return ACCEPT_NO;
    }

    // Find all the items in the category
    LLDroppableItem droppable(!obj->permYouOwner());
    LLInventoryModel::cat_array_t cats;
    LLInventoryModel::item_array_t items;
    gInventory.collectDescendentsIf(cat->getUUID(),
                    cats,
                    items,
                    LLInventoryModel::EXCLUDE_TRASH,
                    droppable);
    cats.push_back(cat);
    if (droppable.countNoCopy() > 0)
    {
        LL_WARNS() << "*** Need to confirm this step" << LL_ENDL;
    }
    LLViewerObject* root_object = obj;
    if (obj->getParent())
    {
        LLViewerObject* parent_obj = (LLViewerObject*)obj->getParent();
        if (!parent_obj->isAvatar())
        {
            root_object = parent_obj;
        }
    }

    EAcceptance rv = ACCEPT_NO;

    // Check for accept
    for (LLInventoryModel::cat_array_t::const_iterator cat_iter = cats.begin();
         cat_iter != cats.end();
         ++cat_iter)
    {
        const LLViewerInventoryCategory *cat = (*cat_iter);
        rv = gInventory.isCategoryComplete(cat->getUUID()) ? ACCEPT_YES_MULTI : ACCEPT_NO;
        if(rv < ACCEPT_YES_SINGLE)
        {
            LL_DEBUGS() << "Category " << cat->getUUID() << "is not complete." << LL_ENDL;
            break;
        }
    }
    if (ACCEPT_YES_COPY_SINGLE <= rv)
    {
        for (LLInventoryModel::item_array_t::const_iterator item_iter = items.begin();
             item_iter != items.end();
             ++item_iter)
        {
            LLViewerInventoryItem *item = (*item_iter);
            /*
            // Pass the base objects, not the links.
            if (item && item->getIsLinkType())
            {
                item = item->getLinkedItem();
                (*item_iter) = item;
            }
            */
            rv = willObjectAcceptInventory(root_object, item, DAD_CATEGORY);
            if (rv < ACCEPT_YES_COPY_SINGLE)
            {
                LL_DEBUGS() << "Object will not accept " << item->getUUID() << LL_ENDL;
                break;
            }
        }
    }

    // If every item is accepted, send it on
    if (drop && (ACCEPT_YES_COPY_SINGLE <= rv))
    {
        uuid_vec_t ids;
        for (LLInventoryModel::item_array_t::const_iterator item_iter = items.begin();
             item_iter != items.end();
             ++item_iter)
        {
            const LLViewerInventoryItem *item = (*item_iter);
            ids.push_back(item->getUUID());
        }
        LLCategoryDropObserver* dropper = new LLCategoryDropObserver(ids, obj->getID(), mSource);
        dropper->startFetch();
        if (dropper->isFinished())
        {
            dropper->done();
        }
        else
        {
            gInventory.addObserver(dropper);
        }
    }
    return rv;
}