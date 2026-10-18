// Frozen original OpenGL/reference picking implementation; LGPL-2.1.
bool OriginalVolume::canPick(bool pick_rigged,bool pick_unselectable)
{
    if (!mbCanSelect
        || mDrawable->isDead()
        || !gPipeline.hasRenderType(mDrawable->getRenderType()))
    {
        return false;
    }

    if (!pick_unselectable)
    {
        if (!LLSelectMgr::instance().canSelectObject(this, true))
        {
            return false;
        }
    }

    if (getClickAction() == CLICK_ACTION_IGNORE && !LLFloater::isVisible(gFloaterTools))
    {
        return false;
    }

LLVolume*volume=getVolume();bool transform=true;
    if (mDrawable->isState(LLDrawable::RIGGED))
    {
        if ((pick_rigged) || (getAvatar() && (getAvatar()->isSelf()) && (LLFloater::isVisible(gFloaterTools))))
        {
            updateRiggedVolume(true, LLRiggedVolume::DO_NOT_UPDATE_FACES);
            volume = mRiggedVolume;
            transform = false;
        }
        else
        { //cannot pick rigged attachments on other avatars or when not in build mode
            return false;
        }
    }

return true;
}
