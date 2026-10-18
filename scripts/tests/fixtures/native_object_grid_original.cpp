void ReferenceManager::getGrid(LLVector3& origin, LLQuaternion &rotation, LLVector3 &scale, bool for_snap_guides)
{
    mGridObjects.cleanupNodes();

    LLViewerObject* first_grid_object = mGridObjects.getFirstObject();

    if (mGridMode == GRID_MODE_LOCAL && mSelectedObjects->getObjectCount())
    {
        //LLViewerObject* root = getSelectedParentObject(mSelectedObjects->getFirstObject());
        mGridOrigin = mSavedSelectionBBox.getCenterAgent();
        mGridScale = mSavedSelectionBBox.getExtentLocal() * 0.5f;

        // DEV-12570 Just taking the saved selection box rotation prevents
        // wild rotations of linked sets while in local grid mode
        //if(mSelectedObjects->getObjectCount() < 2 || !root || root->mDrawable.isNull())
        {
            mGridRotation = mSavedSelectionBBox.getRotation();
        }
        /*else //set to the root object
        {
            mGridRotation = root->getRenderRotation();
        }*/
    }
    else if (mGridMode == GRID_MODE_REF_OBJECT && first_grid_object && first_grid_object->mDrawable.notNull())
    {
        LLSelectNode *node = mSelectedObjects->findNode(first_grid_object);
        if (!for_snap_guides && node)
        {
            mGridRotation = node->mSavedRotation;
        }
        else
        {
            mGridRotation = first_grid_object->getRenderRotation();
        }

        LLVector4a min_extents(F32_MAX);
        LLVector4a max_extents(-F32_MAX);
        bool grid_changed = false;
        for (LLObjectSelection::iterator iter = mGridObjects.begin();
             iter != mGridObjects.end(); ++iter)
        {
            LLViewerObject* object = (*iter)->getObject();
            LLDrawable* drawable = object->mDrawable;
            if (drawable)
            {
                const LLVector4a* ext = drawable->getSpatialExtents();
                update_min_max(min_extents, max_extents, ext[0]);
                update_min_max(min_extents, max_extents, ext[1]);
                grid_changed = true;
            }
        }
        if (grid_changed)
        {
            LLVector4a center, size;
            center.setAdd(min_extents, max_extents);
            center.mul(0.5f);
            size.setSub(max_extents, min_extents);
            size.mul(0.5f);

            mGridOrigin.set(center.getF32ptr());
            LLDrawable* drawable = first_grid_object->mDrawable;
            if (drawable && drawable->isActive())
            {
                mGridOrigin = mGridOrigin * first_grid_object->getRenderMatrix();
            }
            mGridScale.set(size.getF32ptr());
        }
    }
    else // GRID_MODE_WORLD or just plain default
    {
        const bool non_root_ok = true;
        LLViewerObject* first_object = mSelectedObjects->getFirstRootObject(non_root_ok);

        mGridOrigin.clearVec();
        mGridRotation.loadIdentity();

        mSelectedObjects->mSelectType = getSelectTypeForObject( first_object );

        switch (mSelectedObjects->mSelectType)
        {
        case SELECT_TYPE_ATTACHMENT:
            if (first_object && first_object->getRootEdit()->mDrawable.notNull())
            {
                // this means this object *has* to be an attachment
                LLXform* attachment_point_xform = first_object->getRootEdit()->mDrawable->mXform.getParent();
                // <FS:Ansariel> Crash fix for FIRE-15206
                if (!attachment_point_xform) break;
                mGridOrigin = attachment_point_xform->getWorldPosition();
                mGridRotation = attachment_point_xform->getWorldRotation();
                mGridScale = LLVector3(1.f, 1.f, 1.f) * gSavedSettings.getF32("GridResolution");
            }
            break;
        case SELECT_TYPE_HUD:
            mGridScale = LLVector3(1.f, 1.f, 1.f) * llmin(gSavedSettings.getF32("GridResolution"), 0.5f);
            break;
        case SELECT_TYPE_WORLD:
            mGridScale = LLVector3(1.f, 1.f, 1.f) * gSavedSettings.getF32("GridResolution");
            break;
        }
    }
    llassert(mGridOrigin.isFinite());

    origin = mGridOrigin;
    rotation = mGridRotation;
    scale = mGridScale;
}