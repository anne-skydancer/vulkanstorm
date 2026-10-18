void LLManip::rebuild(LLViewerObject* vobj)
{
    LLDrawable* drawablep = vobj->mDrawable;
    if (drawablep && drawablep->getVOVolume())
    {
        gPipeline.markRebuild(drawablep,LLDrawable::REBUILD_VOLUME);
        drawablep->setState(LLDrawable::MOVE_UNDAMPED); // force to UNDAMPED
        drawablep->updateMove();
        LLSpatialGroup* group = drawablep->getSpatialGroup();
        if (group)
        {
            group->dirtyGeom();
            gPipeline.markRebuild(group);
        }

        LLViewerObject::const_child_list_t& child_list = vobj->getChildren();
        for (LLViewerObject::child_list_t::const_iterator iter = child_list.begin(), endIter = child_list.end();
             iter != endIter; ++iter)
        {
            LLViewerObject* child = *iter;
            rebuild(child);
        }
    }
}