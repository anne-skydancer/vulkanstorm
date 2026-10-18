void LLSelectMgr::overrideAvatarUpdates()
{
    if (mAvatarOverridesMap.size() == 0)
    {
        return;
    }

    if (!mAllowSelectAvatar || !gFloaterTools)
    {
        resetAvatarOverrides();
        return;
    }

    if (!gFloaterTools->getVisible() && getSelection()->isEmpty())
    {
        // when user switches selection, floater is invisible and selection is empty
        LLToolset *toolset = LLToolMgr::getInstance()->getCurrentToolset();
        if (toolset->isShowFloaterTools()
            && toolset->isToolSelected(0)) // Pie tool
        {
            resetAvatarOverrides();
            return;
        }
    }

    // remove selected avatars from this list,
    // but set object overrides to make sure avatar won't snap back
    struct f : public LLSelectedNodeFunctor
    {
        f(LLSelectMgr* p) : mManager(p) {}
        LLSelectMgr* mManager;
        virtual bool apply(LLSelectNode* selectNode)
        {
            LLViewerObject* object = selectNode->getObject();
            if (object && !object->getParent())
            {
                LLVOAvatar* avatar = object->asAvatar();
                if (avatar)
                {
                    uuid_av_override_map_t::iterator iter = mManager->mAvatarOverridesMap.find(avatar->getID());
                    if (iter != mManager->mAvatarOverridesMap.end())
                    {
                        if (selectNode->mLastPositionLocal.isExactlyZero())
                        {
                            selectNode->mLastPositionLocal = iter->second.mLastPositionLocal;
                        }
                        if (selectNode->mLastRotation == LLQuaternion())
                        {
                            selectNode->mLastRotation = iter->second.mLastRotation;
                        }
                        mManager->mAvatarOverridesMap.erase(iter);
                    }
                }
            }
            return true;
        }
    } func(this);
    getSelection()->applyToNodes(&func);

    // Override avatar positions
    uuid_av_override_map_t::iterator it = mAvatarOverridesMap.begin();
    while (it != mAvatarOverridesMap.end())
    {
        if (it->second.mObject->isDead())
        {
            it = mAvatarOverridesMap.erase(it);
        }
        else
        {
            if (!it->second.mLastPositionLocal.isExactlyZero())
            {
                it->second.mObject->setPosition(it->second.mLastPositionLocal);
            }
            if (it->second.mLastRotation != LLQuaternion())
            {
                it->second.mObject->setRotation(it->second.mLastRotation);
            }
            it++;
        }
    }
}