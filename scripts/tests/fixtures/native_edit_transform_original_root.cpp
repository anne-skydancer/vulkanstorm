bool LLVOVolume::isRootEdit() const
{
    if (mParent && !((LLViewerObject*)mParent)->isAvatar())
    {
        return false;
    }
    return true;
}