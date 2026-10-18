const LLVector3 &LLViewerObject::getPositionRegion() const
{
    if (!isRoot())
    {
        LLViewerObject *parent = (LLViewerObject *)getParent();
        mPositionRegion = parent->getPositionRegion() + (getPosition() * parent->getRotation());
    }
    else
    {
        mPositionRegion = getPosition();
    }

    return mPositionRegion;
}