void LLSelectMgr::resetObjectOverrides(LLObjectSelectionHandle selected_handle)
{
    struct f : public LLSelectedNodeFunctor
    {
        f(bool a, LLSelectMgr* p) : mAvatarOverridesPersist(a), mManager(p) {}
        bool mAvatarOverridesPersist;
        LLSelectMgr* mManager;
        virtual bool apply(LLSelectNode* node)
        {
            if (mAvatarOverridesPersist)
            {
                LLViewerObject* object = node->getObject();
                if (object && !object->getParent())
                {
                    LLVOAvatar* avatar = object->asAvatar();
                    if (avatar)
                    {
                        mManager->mAvatarOverridesMap.emplace(avatar->getID(), AvatarPositionOverride(node->mLastPositionLocal, node->mLastRotation, object));
                    }
                }
            }
            node->mLastPositionLocal.setVec(0, 0, 0);
            node->mLastRotation = LLQuaternion();
            node->mLastScale.setVec(0, 0, 0);
            return true;
        }
    } func(mAllowSelectAvatar, this);

    selected_handle->applyToNodes(&func);
}